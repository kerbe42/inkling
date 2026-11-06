#!/usr/bin/env python3
"""A tiny SMTP server for the email tests (python stdlib only). Not for real mail.

  smtp_test_server.py PORT MODE CERT KEY OUTDIR [--mechs "PLAIN LOGIN"]

MODE: smtps (TLS from the first byte, like port 465), starttls (plain until STARTTLS, like 587; AUTH only after it),
plain (never offers STARTTLS). The login is user test@example.com, password from $SMTP_TEST_PASSWORD. Each accepted
message is saved to OUTDIR/N.eml (dot-stuffing undone) with OUTDIR/N.json (envelope, AUTH mechanism, TLS version).
RCPT TO reject@example.com gets 550. Prints "ready" once listening.
"""
import base64
import json
import os
import socketserver
import ssl
import sys

USER = "test@example.com"
PASSWORD = os.environ.get("SMTP_TEST_PASSWORD", "app-password-123")


class Handler(socketserver.StreamRequestHandler):
    def setup(self):
        self.tls = None
        if MODE == "smtps":
            self.request = CTX.wrap_socket(self.request, server_side=True)
            self.tls = self.request.version()
        super().setup()

    def send(self, line):
        self.wfile.write((line + "\r\n").encode())
        self.wfile.flush()

    def readline(self):
        line = self.rfile.readline(4096)
        if not line:
            raise ConnectionError("closed")
        return line.decode("utf-8", "replace").rstrip("\r\n")

    def handle(self):
        self.authed, self.mech, self.mail_from, self.rcpts = False, None, None, []
        self.send("220 test.invalid ESMTP test server")
        while True:
            try:
                line = self.readline()
            except (ConnectionError, ssl.SSLError, OSError):
                return
            cmd = line[:4].upper()
            arg = line[5:] if len(line) > 5 else ""
            if cmd in ("EHLO", "HELO"):
                ext = ["test.invalid hello " + arg, "SIZE 20000000", "8BITMIME"]
                if MODE == "starttls" and not self.tls:
                    ext.append("STARTTLS")
                if self.tls:
                    ext.append("AUTH " + MECHS)
                for e in ext[:-1]:
                    self.send("250-" + e)
                self.send("250 " + ext[-1])
            elif line.upper() == "STARTTLS" and MODE == "starttls" and not self.tls:
                self.send("220 go ahead")
                self.connection = self.request = CTX.wrap_socket(self.request, server_side=True)
                self.tls = self.request.version()
                self.rfile = self.request.makefile("rb")
                self.wfile = self.request.makefile("wb")
                self.authed = False
            elif cmd == "AUTH":
                self.auth(arg)
            elif cmd == "MAIL":
                if not self.authed:
                    self.send("530 5.7.0 Authentication required")
                    continue
                self.mail_from, self.rcpts = arg.split(":", 1)[1].strip(), []
                self.send("250 2.1.0 ok")
            elif cmd == "RCPT":
                to = arg.split(":", 1)[1].strip()
                if "reject@example.com" in to:
                    self.send("550 5.1.1 no such user")
                    continue
                self.rcpts.append(to)
                self.send("250 2.1.5 ok")
            elif cmd == "DATA":
                self.send("354 end with <CRLF>.<CRLF>")
                data = bytearray()
                while True:
                    raw = self.rfile.readline(1 << 20)
                    if not raw:
                        return
                    if raw == b".\r\n":
                        break
                    if raw.startswith(b"."):
                        raw = raw[1:]
                    data += raw
                n = len([f for f in os.listdir(OUT) if f.endswith(".eml")]) + 1
                with open(os.path.join(OUT, "%d.eml" % n), "wb") as f:
                    f.write(data)
                meta = {"mail_from": self.mail_from, "rcpts": self.rcpts, "mech": self.mech, "tls": self.tls}
                with open(os.path.join(OUT, "%d.json" % n), "w") as f:
                    json.dump(meta, f)
                self.send("250 2.0.0 queued as %d" % n)
            elif cmd == "QUIT":
                self.send("221 bye")
                return
            elif cmd in ("RSET", "NOOP"):
                self.send("250 ok")
            else:
                self.send("502 unknown command")

    def auth(self, arg):
        parts = arg.split()
        mech = parts[0].upper() if parts else ""
        if not self.tls or mech not in MECHS.split():
            self.send("504 mechanism not available")
            return
        try:
            if mech == "PLAIN":
                if len(parts) > 1:
                    blob = parts[1]
                else:
                    self.send("334 ")
                    blob = self.readline()
                _, user, pw = base64.b64decode(blob).decode().split("\0")
            else:  # LOGIN
                if len(parts) > 1:
                    user = base64.b64decode(parts[1]).decode()
                else:
                    self.send("334 VXNlcm5hbWU6")
                    user = base64.b64decode(self.readline()).decode()
                self.send("334 UGFzc3dvcmQ6")
                pw = base64.b64decode(self.readline()).decode()
        except Exception:
            self.send("501 bad auth data")
            return
        if user == USER and pw == PASSWORD:
            self.authed, self.mech = True, mech
            self.send("235 2.7.0 accepted")
        else:
            self.send("535 5.7.8 Username and Password not accepted")


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    port, MODE, cert, key, OUT = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
    MECHS = sys.argv[sys.argv.index("--mechs") + 1] if "--mechs" in sys.argv else "PLAIN LOGIN"
    CTX = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    if "--tls12" in sys.argv:
        CTX.maximum_version = ssl.TLSVersion.TLSv1_2
    CTX.load_cert_chain(cert, key)
    os.makedirs(OUT, exist_ok=True)
    with Server(("127.0.0.1", port), Handler) as srv:
        print("ready", flush=True)
        srv.serve_forever()

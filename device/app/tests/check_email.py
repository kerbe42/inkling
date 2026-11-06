#!/usr/bin/env python3
"""Checks a message the test SMTP server received against what test_email wrote (run_email_tests.sh).

  check_email.py ORIGDIR RECEIVED.eml RECEIVED.json EXPECT_MECH EXPECT_TLS_PREFIX

ORIGDIR holds sent.eml, subject.txt, body.txt and page.png. Exits non-zero with a reason on any difference.
"""
import email
import email.policy
import email.utils
import json
import os
import sys


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


orig, received, meta_path, want_mech, want_tls = sys.argv[1:6]
sent = open(os.path.join(orig, "sent.eml"), "rb").read()
got = open(received, "rb").read()
meta = json.load(open(meta_path))
if got != sent:
    fail("the received bytes differ from the message written (%d vs %d bytes)" % (len(got), len(sent)))
if meta["mech"] != want_mech:
    fail("AUTH mechanism %s, expected %s" % (meta["mech"], want_mech))
if not (meta["tls"] or "").startswith(want_tls):
    fail("TLS version %s, expected %s" % (meta["tls"], want_tls))
if meta["mail_from"].split()[0] != "<test@example.com>" or meta["rcpts"] != ["<test@example.com>"]:
    fail("envelope %r" % meta)

msg = email.message_from_bytes(got, policy=email.policy.default)
defects = list(msg.defects)
for part in msg.walk():
    defects += part.defects
if defects:
    fail("parser defects: %r" % defects)
subject = open(os.path.join(orig, "subject.txt"), encoding="utf-8").read()
if str(msg["subject"]) != subject:
    fail("subject %r != %r" % (str(msg["subject"]), subject))
if email.utils.parsedate_to_datetime(msg["date"]) is None:
    fail("bad Date")
if not msg["message-id"] or "@" not in msg["message-id"]:
    fail("bad Message-ID")
if msg.get_content_type() != "multipart/mixed":
    fail("content type " + msg.get_content_type())
parts = list(msg.iter_parts())
if len(parts) != 2:
    fail("%d parts" % len(parts))
text, att = parts
body = open(os.path.join(orig, "body.txt"), encoding="utf-8").read()
if text.get_content_type() != "text/plain" or text.get_content_charset() != "utf-8":
    fail("text part %s %s" % (text.get_content_type(), text.get_content_charset()))
got_body = text.get_content().replace("\r\n", "\n")
if got_body.rstrip("\n") != body:
    fail("body differs:\n%r\n%r" % (got_body, body))
png = open(os.path.join(orig, "page.png"), "rb").read()
if att.get_content_type() != "image/png" or att.get_filename() != "page-3.png" or att.get_content_disposition() != "attachment":
    fail("attachment headers %s %s %s" % (att.get_content_type(), att.get_filename(), att.get_content_disposition()))
if att.get_content() != png:
    fail("attachment bytes differ")
for n, line in enumerate(got.split(b"\r\n")):
    if len(line) > 78:
        fail("line %d is %d characters" % (n + 1, len(line)))
print("ok: %s, %s, %d bytes, subject/body/attachment intact" % (meta["tls"], meta["mech"], len(got)))

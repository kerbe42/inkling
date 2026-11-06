/* Send > Email this page through the user's own mail account: SMTP presets, the MIME message, and one run of
 * BIN_DIR/curl-smtp (a small static curl with SMTP over mbedTLS, built by device/tools/build_curl_smtp.sh; the
 * Kindle's /usr/bin/curl has no SMTP). The login reaches curl through a 0600 config file (-K), never the command
 * line. curl's exit code and error line become a short message for the status line. Nothing here knows about the App
 * or the UI, so device/app/tests/test_email.c drives it on the laptop (qemu-arm + a local SMTP server). */
#include "email.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const EmailPreset PRESETS[] = {
	{ "gmail", "Gmail", "smtp.gmail.com", 465 },
	{ "outlook", "Outlook", "smtp-mail.outlook.com", 587 },
	{ "icloud", "iCloud", "smtp.mail.me.com", 587 },
	{ "yahoo", "Yahoo", "smtp.mail.yahoo.com", 465 },
	{ "fastmail", "Fastmail", "smtp.fastmail.com", 465 },
};

const EmailPreset *email_preset(const char *id) {
	if (!id) return NULL;
	if (!strcasecmp(id, "hotmail") || !strcasecmp(id, "live")) id = "outlook";
	for (size_t i = 0; i < sizeof PRESETS / sizeof PRESETS[0]; i++)
		if (!strcasecmp(id, PRESETS[i].id)) return &PRESETS[i];
	return NULL;
}

/* A host name, IPv4 address or [IPv6] literal: nothing that could change the meaning of the URL. */
static int host_ok(const char *h) {
	size_t n = strlen(h);
	if (!n || n > 90) return 0;
	if (h[0] == '[') {
		if (h[n - 1] != ']') return 0;
		for (size_t i = 1; i + 1 < n; i++)
			if (!strchr("0123456789abcdefABCDEF:.", h[i])) return 0;
		return 1;
	}
	if (h[0] == '-' || h[0] == '.') return 0;
	for (size_t i = 0; i < n; i++) {
		char c = h[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0;
	}
	return 1;
}

int email_target(const char *provider, const char *server, int port, char *url, size_t urln, int *starttls, char *name,
		 size_t namen, char *err, size_t errn) {
	const EmailPreset *p = email_preset(provider);
	const char *host;
	int prt;
	if (p) {
		host = p->host;
		prt = p->port;
		snprintf(name, namen, "%s", p->name);
	} else if (provider && !strcasecmp(provider, "custom")) {
		if (!server || !server[0]) { snprintf(err, errn, "set the mail server first (smtp_server)"); return -1; }
		if (!host_ok(server)) { snprintf(err, errn, "the mail server name looks wrong: %.60s", server); return -1; }
		host = server;
		prt = port > 0 ? port : 587;
		if (prt > 65535) { snprintf(err, errn, "the mail server port looks wrong: %d", prt); return -1; }
		snprintf(name, namen, "the mail server");
	} else {
		snprintf(err, errn, "unknown email provider \"%.20s\"", provider ? provider : "");
		return -1;
	}
	snprintf(url, urln, "%s://%s:%d", prt == 465 ? "smtps" : "smtp", host, prt);
	*starttls = prt != 465;
	return 0;
}

/* ---- the message ---- */
static void rand_hex(char *out, int nbytes) {
	unsigned char r[16] = { 0 };
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got = fd >= 0 ? read(fd, r, (size_t)nbytes) : -1;
	if (fd >= 0) close(fd);
	if (got != nbytes) {
		unsigned long long x = (unsigned long long)time(NULL) ^ ((unsigned long long)getpid() << 16) ^ (unsigned long long)clock();
		for (int i = 0; i < nbytes; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; r[i] = (unsigned char)(x >> 33); }
	}
	for (int i = 0; i < nbytes; i++) sprintf(out + 2 * i, "%02x", r[i]);
}

/* A header value on one line: control characters (CR and LF above all) become spaces. */
static void header_clean(char *dst, size_t n, const char *s) {
	size_t j = 0;
	for (; s && *s && j + 1 < n; s++) dst[j++] = (unsigned char)*s < 32 || *s == 127 ? ' ' : *s;
	dst[j] = 0;
}

static void b64_chunk(FILE *f, const unsigned char *d, size_t n) {
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		fputc(T[v >> 18 & 63], f);
		fputc(T[v >> 12 & 63], f);
		fputc(i + 1 < n ? T[v >> 6 & 63] : '=', f);
		fputc(i + 2 < n ? T[v & 63] : '=', f);
	}
}

/* Subject: as is when it is plain ASCII, else RFC 2047 encoded words (UTF-8, base64) that never split a character. */
static void write_subject(FILE *f, const char *raw) {
	char s[600];
	header_clean(s, sizeof s, raw);
	int ascii = 1;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++)
		if (*p > 126) ascii = 0;
	if (ascii && !strstr(s, "=?")) { fprintf(f, "Subject: %s\r\n", s); return; }
	fputs("Subject:", f);
	const unsigned char *p = (const unsigned char *)s;
	while (*p) {
		size_t n = 0;
		while (p[n]) { /* whole characters, at most 42 bytes (56 base64 chars) per word */
			size_t c = 1;
			while (p[n + c] && (p[n + c] & 0xC0) == 0x80) c++;
			if (n + c > 42 && n) break;
			n += c;
		}
		fputs(" =?UTF-8?B?", f);
		b64_chunk(f, p, n);
		fputs("?=", f);
		p += n;
		if (*p) fputs("\r\n", f); /* folded: the next word starts with the space */
	}
	fputs("\r\n", f);
}

/* The body as quoted-printable: lines of at most 76 characters, CRLF line ends, trailing blanks and a leading dot
 * encoded. */
static void write_qp(FILE *f, const char *s) {
	int col = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		unsigned c = *p;
		if (c == '\r' && p[1] == '\n') continue;
		if (c == '\n') { fputs("\r\n", f); col = 0; continue; }
		int eol = !p[1] || p[1] == '\n' || (p[1] == '\r' && p[2] == '\n');
		for (int pass = 0; pass < 2; pass++) {
			int lit = (c >= 33 && c <= 126 && c != '=' && !(c == '.' && col == 0)) || ((c == ' ' || c == '\t') && !eol);
			int len = lit ? 1 : 3;
			if (col + len > 75 && pass == 0) { fputs("=\r\n", f); col = 0; continue; }
			if (lit) fputc((int)c, f);
			else fprintf(f, "=%02X", c);
			col += len;
			break;
		}
	}
	if (col) fputs("\r\n", f);
}

int email_write_mime(const char *path, const char *from, const char *to, const char *subject, const char *text,
		     const char *png64, const char *png_name, const char *msgid_domain) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return -1;
	FILE *f = fdopen(fd, "w");
	if (!f) { close(fd); return -1; }
	char bnd[48], id[40], date[64], h[400], dom[100], fname[80];
	rand_hex(id, 12);
	snprintf(bnd, sizeof bnd, "=_scribe_");
	rand_hex(bnd + strlen(bnd), 12);
	time_t now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);
	strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S +0000", &tm);
	header_clean(dom, sizeof dom, msgid_domain && msgid_domain[0] ? msgid_domain : "inkling.invalid");
	for (char *c = dom; *c; c++)
		if (*c == ' ' || *c == '>' || *c == '<' || *c == '@') *c = '-';
	fprintf(f, "Date: %s\r\n", date);
	header_clean(h, sizeof h, from);
	fprintf(f, "From: %s\r\n", h);
	header_clean(h, sizeof h, to);
	fprintf(f, "To: %s\r\n", h);
	write_subject(f, subject && subject[0] ? subject : "Notebook page");
	fprintf(f, "Message-ID: <%lld.%s@%s>\r\n", (long long)now, id, dom);
	fputs("MIME-Version: 1.0\r\n", f);
	fputs("X-Mailer: Inkling for Kindle\r\n", f);
	fprintf(f, "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n", bnd);
	fputs("This is a multi-part message in MIME format.\r\n\r\n", f);
	fprintf(f, "--%s\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n", bnd);
	write_qp(f, text ? text : "");
	if (png64 && png64[0]) {
		header_clean(fname, sizeof fname, png_name && png_name[0] ? png_name : "page.png");
		for (char *c = fname; *c; c++)
			if (*c == '"' || *c == '\\') *c = '_';
		fprintf(f, "\r\n--%s\r\nContent-Type: image/png; name=\"%s\"\r\nContent-Transfer-Encoding: base64\r\n"
			   "Content-Disposition: attachment; filename=\"%s\"\r\n\r\n", bnd, fname, fname);
		size_t n = strlen(png64);
		for (size_t i = 0; i < n; i += 76) {
			fwrite(png64 + i, 1, n - i < 76 ? n - i : 76, f);
			fputs("\r\n", f);
		}
	}
	fprintf(f, "\r\n--%s--\r\n", bnd);
	int bad = ferror(f);
	if (fflush(f) != 0) bad = 1;
	fsync(fd);
	if (fclose(f) != 0) bad = 1;
	return bad ? -1 : 0;
}

/* ---- sending ---- */
static int addr_ok(const char *a) {
	if (!a || !a[0] || a[0] == '-' || !strchr(a, '@') || strlen(a) > 150) return 0;
	for (const unsigned char *p = (const unsigned char *)a; *p; p++)
		if (*p <= ' ' || *p == 127 || *p == '<' || *p == '>' || *p == '"' || *p == ',') return 0;
	return 1;
}

/* curl config syntax: inside double quotes a backslash escapes \ and ", and \t \r \n stand for tab, CR and LF (curl
 * turns them back), so no value can end the line and add an option of its own. Other control characters are left
 * out. */
static size_t cfg_quote(char *dst, size_t n, const char *s) {
	size_t j = 0;
	for (; *s && j + 2 < n; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
		else if (c == '\n' || c == '\r' || c == '\t') { dst[j++] = '\\'; dst[j++] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't'; }
		else if (c >= 32 && c != 127) dst[j++] = (char)c;
	}
	dst[j] = 0;
	return j;
}

void email_error_message(int code, const char *curl_err, const char *name, int starttls, char *err, size_t errn) {
	const char *e = curl_err ? curl_err : "";
	int smtp = 0;
	const char *fp = strstr(e, "failed: ");
	if (fp) smtp = atoi(fp + 8);
	switch (code) {
	case 6: snprintf(err, errn, "no connection (couldn't find the mail server)"); break;
	case 7: snprintf(err, errn, "no connection to %s", name); break;
	case 28: snprintf(err, errn, "%s didn't answer in time", name); break;
	case 35: snprintf(err, errn, "the secure connection to %s failed", name); break;
	case 60: /* mbedTLS: "...validity has expired", "...starts in the future", "...CN does not match...", "...not correctly signed..." */
		if (strstr(e, "expired") || strstr(e, "future")) snprintf(err, errn, "%s's certificate looks out of date: is the Kindle's clock right?", name);
		else if (strstr(e, "does not match")) snprintf(err, errn, "%s's certificate is for another server name", name);
		else snprintf(err, errn, "couldn't verify %s's certificate", name);
		break;
	case 64: snprintf(err, errn, starttls ? "%s doesn't offer STARTTLS: try port 465" : "%s refused TLS", name); break;
	case 67: snprintf(err, errn, "%s refused the password: use an app password (see README)", name); break;
	case 9: snprintf(err, errn, "%s turned the connection away", name); break;
	case 55:
		if (strstr(e, "MAIL failed") && smtp == 530) snprintf(err, errn, "%s wants a login: check the address and app password", name);
		else if (strstr(e, "MAIL failed")) snprintf(err, errn, "%s refused the sender: use your account's own address (%d)", name, smtp);
		else if (strstr(e, "RCPT failed")) snprintf(err, errn, "%s refused the recipient address (%d)", name, smtp);
		else if (strstr(e, "DATA failed")) snprintf(err, errn, "%s refused the message (%d)", name, smtp);
		else snprintf(err, errn, "the connection dropped while sending");
		break;
	case 8: snprintf(err, errn, "%s rejected the message", name); break;
	case 52:
	case 56: snprintf(err, errn, "the connection to %s dropped", name); break;
	case 26: snprintf(err, errn, "couldn't read the email file"); break;
	case 127: snprintf(err, errn, "the email helper is missing: reinstall Inkling"); break;
	default:
		if (code > 128) snprintf(err, errn, "sending was stopped");
		else snprintf(err, errn, "sending failed (curl error %d)", code);
	}
}

int email_smtp_send(const SmtpSend *s, char *err, size_t errn, char *detail, size_t detailn) {
	if (detail && detailn) detail[0] = 0;
	if (access(s->curl, X_OK) != 0) { email_error_message(127, "", s->name, s->starttls, err, errn); return 127; }
	if (!addr_ok(s->mail_from) || !addr_ok(s->rcpt) || !addr_ok(s->user)) {
		snprintf(err, errn, "the email address looks wrong");
		return 3;
	}
	char cfg[1024], qu[330], qp[660]; /* every character escaped: user (< 160) and password (< 320) still fit */
	cfg_quote(qu, sizeof qu, s->user);
	cfg_quote(qp, sizeof qp, s->pass ? s->pass : "");
	int cn = snprintf(cfg, sizeof cfg, "user = \"%s:%s\"\n", qu, qp);
	memset(qp, 0, sizeof qp);
	if (cn < 0 || cn >= (int)sizeof cfg) { memset(cfg, 0, sizeof cfg); snprintf(err, errn, "the login is too long"); return 3; }
	unlink(s->cfg);
	int fd = open(s->cfg, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	ssize_t w = fd >= 0 ? write(fd, cfg, (size_t)cn) : -1;
	memset(cfg, 0, sizeof cfg);
	if (fd >= 0) {
		fchmod(fd, 0600);
		close(fd);
	}
	if (w != cn) { unlink(s->cfg); snprintf(err, errn, "couldn't write the login file"); return 23; }

	int pfd[2];
	if (pipe(pfd) != 0) { unlink(s->cfg); snprintf(err, errn, "couldn't start the email helper"); return 2; }
	char maxt[16];
	snprintf(maxt, sizeof maxt, "%d", s->max_time > 0 ? s->max_time : 120);
	pid_t pid = fork();
	if (pid == 0) {
		int nul = open("/dev/null", O_RDWR);
		if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); }
		dup2(pfd[1], 2);
		close(pfd[0]);
		close(pfd[1]);
		const char *av[24];
		int n = 0;
		av[n++] = "curl-smtp";
		av[n++] = "-q"; /* no ~/.curlrc */
		av[n++] = "-sS";
		av[n++] = "--max-time";
		av[n++] = maxt;
		av[n++] = "--connect-timeout";
		av[n++] = "20";
		if (s->starttls) av[n++] = "--ssl-reqd";
		av[n++] = "--cacert";
		av[n++] = s->cacert;
		av[n++] = "-K";
		av[n++] = s->cfg;
		av[n++] = "--mail-from";
		av[n++] = s->mail_from;
		av[n++] = "--mail-rcpt";
		av[n++] = s->rcpt;
		av[n++] = "--upload-file";
		av[n++] = s->eml;
		av[n++] = "--url";
		av[n++] = s->url;
		av[n] = NULL;
		execv(s->curl, (char *const *)av);
		_exit(127);
	}
	close(pfd[1]);
	char e[600] = "";
	size_t en = 0;
	if (pid > 0) {
		ssize_t r;
		while ((r = read(pfd[0], e + en, sizeof e - 1 - en)) > 0 || (r < 0 && errno == EINTR))
			if (r > 0) en += (size_t)r;
		e[en] = 0;
	}
	close(pfd[0]);
	int st = 0, code = 2;
	if (pid > 0 && waitpid(pid, &st, 0) == pid) code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
	unlink(s->cfg);
	if (code == 0) return 0;
	email_error_message(code, e, s->name, s->starttls, err, errn);
	if (detail && detailn) { /* curl's error line ("curl: (67) Login denied"), for the log */
		char *c = strstr(e, "curl: (");
		if (!c) c = e;
		char *nl = strchr(c, '\n');
		if (nl) *nl = 0;
		header_clean(detail, detailn, c);
	}
	return code;
}

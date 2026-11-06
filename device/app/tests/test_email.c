/* Host test for device/app/email.c (run by run_email_tests.sh).
 *   test_email unit              presets, custom servers, error messages, MIME encoding rules
 *   test_email mime DIR          writes DIR/sent.eml and the originals (subject.txt, body.txt, page.png) for
 *                                check_email.py
 *   test_email send URL STARTTLS CACERT EML CURL CFG RCPT
 *                                one email_smtp_send() as user test@example.com, password $SMTP_TEST_PASSWORD;
 *                                prints "code=N err=... detail=..." */
#include "../email.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

static int fails;
#define CHECK(c)                                                                  \
	do {                                                                      \
		if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
	} while (0)

static const char SUBJECT[] = "Café notes – 日本語のメモ ✓ and a fairly long subject line that has to be folded";
static const char BODY[] =
	"Shopping list:\n"
	"- 2 × lait (½ litre)\n"
	"- crème fraîche = 1 pot\n"
	".a line that starts with a dot\n"
	"..two dots\n"
	"trailing spaces here   \n"
	"tab at the end\t\n"
	"A long line without any breaks at all: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
	"tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco.\n"
	"\n"
	"Emoji: 📝🖊️ and Greek: Καλημέρα\n"
	"From the start of a line\n"
	"=3D is not an escape here, and neither is =\n"
	"last line without a newline";

typedef struct {
	unsigned char *p;
	size_t n, cap;
} Buf;
static void png_cb(void *ctx, void *data, int size) {
	Buf *b = ctx;
	if (b->n + (size_t)size > b->cap) { b->cap = (b->n + (size_t)size) * 2; b->p = realloc(b->p, b->cap); }
	memcpy(b->p + b->n, data, (size_t)size);
	b->n += (size_t)size;
}
static char *b64(const unsigned char *d, size_t n) {
	static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *o = malloc(4 * ((n + 2) / 3) + 1), *q = o;
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		*q++ = T[v >> 18 & 63];
		*q++ = T[v >> 12 & 63];
		*q++ = i + 1 < n ? T[v >> 6 & 63] : '=';
		*q++ = i + 2 < n ? T[v & 63] : '=';
	}
	*q = 0;
	return o;
}
static void put(const char *dir, const char *name, const void *d, size_t n) {
	char p[512];
	snprintf(p, sizeof p, "%s/%s", dir, name);
	FILE *f = fopen(p, "wb");
	if (!f || fwrite(d, 1, n, f) != n) { perror(p); exit(2); }
	fclose(f);
}

/* A page-like PNG: 600x400 grey with some "ink". */
static Buf make_png(void) {
	int w = 600, h = 400;
	unsigned char *px = malloc((size_t)w * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) px[y * w + x] = (unsigned char)(((x / 7 + y / 5) % 23 == 0 || (x * x + y * 3) % 97 == 0) ? 0 : 255);
	Buf png = { 0 };
	stbi_write_png_to_func(png_cb, &png, w, h, 1, px, w);
	free(px);
	return png;
}

static char *slurp(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *b = malloc((size_t)n + 1);
	b[fread(b, 1, (size_t)n, f)] = 0;
	fclose(f);
	return b;
}

static void unit(void) {
	char url[200], name[40], err[200];
	int st = -1;
	CHECK(email_target("gmail", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtps://smtp.gmail.com:465") && st == 0 && !strcmp(name, "Gmail"));
	CHECK(email_target("outlook", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtp://smtp-mail.outlook.com:587") && st == 1);
	CHECK(email_target("Hotmail", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0 && !strcmp(name, "Outlook"));
	CHECK(email_target("icloud", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtp://smtp.mail.me.com:587") && st == 1);
	CHECK(email_target("yahoo", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtps://smtp.mail.yahoo.com:465") && st == 0);
	CHECK(email_target("fastmail", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtps://smtp.fastmail.com:465") && st == 0);
	CHECK(email_target("custom", "mail.example.org", 465, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtps://mail.example.org:465") && st == 0);
	CHECK(email_target("custom", "mail.example.org", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtp://mail.example.org:587") && st == 1);
	CHECK(email_target("custom", "[2001:db8::1]", 25, url, sizeof url, &st, name, sizeof name, err, sizeof err) == 0);
	CHECK(!strcmp(url, "smtp://[2001:db8::1]:25") && st == 1);
	CHECK(email_target("custom", "", 587, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target("custom", "evil.example/x", 587, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target("custom", "user@evil.example", 587, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target("custom", "-K/etc/passwd", 587, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target("custom", "mail.example.org", 70000, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target("resend", "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);
	CHECK(email_target(NULL, "", 0, url, sizeof url, &st, name, sizeof name, err, sizeof err) != 0);

	email_error_message(67, "curl: (67) Login denied", "Gmail", 0, err, sizeof err);
	CHECK(!strcmp(err, "Gmail refused the password: use an app password (see README)"));
	email_error_message(6, "curl: (6) Could not resolve host: smtp.gmail.com", "Gmail", 0, err, sizeof err);
	CHECK(!strncmp(err, "no connection", 13));
	email_error_message(55, "curl: (55) RCPT failed: 550", "Gmail", 0, err, sizeof err);
	CHECK(strstr(err, "recipient") && strstr(err, "550"));
	email_error_message(55, "curl: (55) MAIL failed: 530", "Gmail", 0, err, sizeof err);
	CHECK(strstr(err, "wants a login"));
	email_error_message(64, "curl: (64) STARTTLS not supported.", "the mail server", 1, err, sizeof err);
	CHECK(strstr(err, "STARTTLS") && strstr(err, "465"));
	email_error_message(60, "curl: (60) mbedTLS: The certificate validity has expired", "iCloud", 1, err, sizeof err);
	CHECK(strstr(err, "certificate") && strstr(err, "clock"));

	/* encoding rules on a written message */
	const char *p = "/tmp/scribe-test-email-unit.eml";
	CHECK(email_write_mime(p, "Inkling <test@example.com>\r\nBcc: x@example.com", "test@example.com", SUBJECT, BODY, "QUJD",
			       "page-1.png", "example.com") == 0);
	char *m = slurp(p);
	CHECK(m != NULL);
	if (m) {
		CHECK(!strstr(m, "\r\nBcc:")); /* header injection neutralised */
		CHECK(strstr(m, "Subject: =?UTF-8?B?") != NULL);
		CHECK(strstr(m, "Message-ID: <") && strstr(m, "@example.com>\r\n"));
		CHECK(strstr(m, "Content-Transfer-Encoding: quoted-printable"));
		CHECK(strstr(m, "trailing spaces here  =20\r\n") != NULL);
		CHECK(strstr(m, "tab at the end=09\r\n") != NULL);
		CHECK(strstr(m, "\r\n=2Ea line that starts") != NULL);
		CHECK(strstr(m, "=3D3D is not an escape") != NULL);
		/* CRLF only, and no line longer than 78 characters */
		int maxl = 0, bare_lf = 0;
		for (char *s = m, *nl; *s; s = nl + 1) {
			nl = strchr(s, '\n');
			if (!nl) break;
			if (nl == s || nl[-1] != '\r') bare_lf++;
			int l = (int)(nl - s) - 1;
			if (l > maxl) maxl = l;
		}
		CHECK(bare_lf == 0);
		CHECK(maxl <= 78);
		free(m);
	}
	remove(p);
}

/* The login reaches curl in a -K config file: a password with quotes, backslashes, CR/LF or other control characters
 * must stay inside its one quoted line (a raw newline would start a new curl option). A stand-in curl copies the
 * file while it exists. */
static void login_file(void) {
	char dir[256], sh[300], cfg[300], copy[320];
	snprintf(dir, sizeof dir, "%s/inkling-email-unit-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	if (!mkdtemp(dir)) { CHECK(0); return; }
	snprintf(sh, sizeof sh, "%s/curl.sh", dir);
	snprintf(cfg, sizeof cfg, "%s/smtp.cfg", dir);
	snprintf(copy, sizeof copy, "%s/curl.sh.cfg", dir);
	FILE *f = fopen(sh, "w");
	if (!f) { CHECK(0); return; }
	fputs("#!/bin/sh\nwhile [ $# -gt 0 ]; do [ \"$1\" = -K ] && cp \"$2\" \"$0.cfg\"; shift; done\nexit 0\n", f);
	fclose(f);
	chmod(sh, 0700);
	SmtpSend s = { .curl = sh, .cacert = "/dev/null", .cfg = cfg, .url = "smtps://localhost:465", .starttls = 0,
		       .user = "test@example.com", .pass = "a\"b\\c\nurl = \"file:///etc/passwd\"\r\x01\td",
		       .mail_from = "test@example.com", .rcpt = "test@example.com", .eml = "/dev/null", .name = "x", .max_time = 5 };
	char err[200], detail[200];
	CHECK(email_smtp_send(&s, err, sizeof err, detail, sizeof detail) == 0);
	CHECK(access(cfg, F_OK) != 0); /* deleted after the send */
	char got[512] = "";
	f = fopen(copy, "r");
	size_t n = f ? fread(got, 1, sizeof got - 1, f) : 0;
	if (f) fclose(f);
	got[n] = 0;
	CHECK(!strcmp(got, "user = \"test@example.com:a\\\"b\\\\c\\nurl = \\\"file:///etc/passwd\\\"\\r\\td\"\n"));
	CHECK(strchr(got, '\n') == got + n - 1); /* one line */
	/* the longest login (a 150-character address, a 319-character password of quotes) still fits whole */
	char user[151], pass[320];
	memset(user, 'u', sizeof user - 1);
	user[sizeof user - 1] = 0;
	user[5] = '@';
	memset(pass, '"', sizeof pass - 1);
	pass[sizeof pass - 1] = 0;
	s.user = s.mail_from = user;
	s.pass = pass;
	unlink(copy);
	CHECK(email_smtp_send(&s, err, sizeof err, detail, sizeof detail) == 0);
	f = fopen(copy, "r");
	n = 0;
	char big[1200];
	if (f) { n = fread(big, 1, sizeof big, f); fclose(f); }
	CHECK(n == 8 + 150 + 1 + 2 * 319 + 2);
	unlink(copy);
	unlink(sh);
	rmdir(dir);
}

int main(int argc, char **argv) {
	if (argc >= 2 && !strcmp(argv[1], "unit")) {
		unit();
		login_file();
		printf("email unit tests: %s\n", fails ? "FAILED" : "ok");
		return fails != 0;
	}
	if (argc >= 3 && !strcmp(argv[1], "mime")) {
		Buf png = make_png();
		char *p64 = b64(png.p, png.n);
		char path[512];
		snprintf(path, sizeof path, "%s/sent.eml", argv[2]);
		if (email_write_mime(path, "Inkling <test@example.com>", "test@example.com", SUBJECT, BODY, p64, "page-3.png",
				     "example.com") != 0) { fprintf(stderr, "write failed\n"); return 1; }
		put(argv[2], "subject.txt", SUBJECT, strlen(SUBJECT));
		put(argv[2], "body.txt", BODY, strlen(BODY));
		put(argv[2], "page.png", png.p, png.n);
		free(p64);
		free(png.p);
		printf("wrote %s\n", path);
		return 0;
	}
	if (argc >= 9 && !strcmp(argv[1], "send")) {
		const char *pw = getenv("SMTP_TEST_PASSWORD");
		SmtpSend s = { .curl = argv[6], .cacert = argv[4], .cfg = argv[7], .url = argv[2], .starttls = atoi(argv[3]),
			       .user = "test@example.com", .pass = pw ? pw : "", .mail_from = "test@example.com", .rcpt = argv[8],
			       .eml = argv[5], .name = "the mail server", .max_time = 60 };
		char err[200], detail[200];
		int code = email_smtp_send(&s, err, sizeof err, detail, sizeof detail);
		printf("code=%d err=%s detail=%s\n", code, code ? err : "", code ? detail : "");
		FILE *f = fopen(argv[7], "r");
		if (f) { printf("LEAK: the login file is still there\n"); fclose(f); return 3; }
		return 0;
	}
	fprintf(stderr, "usage: see the top of test_email.c\n");
	return 2;
}

/* Host tests for the self-update (update_core.c): versions, SHA-256, the manifest, Ed25519 signatures (a key pair
 * made here; no real key), the bundle reader against good and hostile archives, the installer against a temp dir, and
 * the whole check + install through tests/fake_curl.sh standing in for curl and GitHub.
 * Usage: test_update <fake_curl.sh>     or: test_update verify <pubhex> <file> <sigfile>  (exit 0 = valid) */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "monocypher-ed25519.h"
#include "update.h"
#include "update_pubkey.h"

static int fails, checks;
#define CHECK(c) ((void)(checks++, (c) ? 0 : (fails++, fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c))))

void logf_(const char *fmt, ...) {
	if (!getenv("UPDATE_TEST_LOG")) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static char root[256];
static const char *fake_curl;

static void put(const char *path, const void *d, size_t n) {
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(2); }
	fwrite(d, 1, n, f);
	fclose(f);
}
static void puts_(const char *path, const char *s) { put(path, s, strlen(s)); }
static char *get(const char *path, size_t *n) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	char *b = malloc(1 << 22);
	size_t k = fread(b, 1, (1 << 22) - 1, f);
	fclose(f);
	b[k] = 0;
	if (n) *n = k;
	return b;
}
static int same(const char *path, const char *want) {
	char *b = get(path, NULL);
	int ok = b && !strcmp(b, want);
	free(b);
	return ok;
}
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int mode_of(const char *p) { struct stat st; return stat(p, &st) == 0 ? (int)(st.st_mode & 0777) : -1; }
static void path(char *out, const char *fmt, ...) {
	char rel[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(rel, sizeof rel, fmt, ap);
	va_end(ap);
	snprintf(out, 600, "%s/%s", root, rel);
}
static void sh(const char *fmt, ...) {
	char cmd[2000];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	if (system(cmd) != 0) { fprintf(stderr, "command failed: %s\n", cmd); exit(2); }
}
static void hex(const uint8_t *d, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]); }

/* ---- versions, hex, sha ---- */
static void test_versions(void) {
	int v[4];
	CHECK(upd_version_parse("0.2.0", v) == 0 && v[0] == 0 && v[1] == 2 && v[2] == 0);
	CHECK(upd_version_parse("v1.2.3.4", v) == 0 && v[3] == 4);
	CHECK(upd_version_parse("1", v) == 0 && v[0] == 1);
	const char *bad[] = { "", "v", "1.", ".1", "1..2", "1.x", "1.2.3.4.5", "1234567.0", "1.2-beta", " 1.2", "1.2 ", NULL };
	for (int i = 0; bad[i]; i++) CHECK(upd_version_parse(bad[i], v) == -1);
	CHECK(upd_version_parse(NULL, v) == -1);
	CHECK(upd_version_cmp("0.2.0", "0.10.0") < 0);
	CHECK(upd_version_cmp("0.10.0", "0.9.9") > 0);
	CHECK(upd_version_cmp("v1.2", "1.2.0") == 0);
	CHECK(upd_version_cmp("1.2.3.1", "1.2.3") > 0);
	CHECK(upd_version_cmp("1.0.0", "0.99.99") > 0);
	CHECK(upd_version_cmp("junk", "0.0.1") < 0);
	CHECK(upd_version_cmp("0.0.1", "junk") > 0);
	uint8_t k[32];
	CHECK(upd_hex_decode(UPDATE_PUBKEY_HEX, k, 32) == 0); /* the key built into the app is well-formed */
	CHECK(upd_hex_decode("abc", k, 2) == -1);
	CHECK(upd_hex_decode("zz", k, 1) == -1);
	CHECK(upd_hex_decode("A0f1", k, 2) == 0 && k[0] == 0xa0 && k[1] == 0xf1);
}

static void test_sha(void) {
	uint8_t d[32];
	char h[65];
	upd_sha256("", 0, d), hex(d, 32, h);
	CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	upd_sha256("abc", 3, d), hex(d, 32, h);
	CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	upd_sha256(two, strlen(two), d), hex(d, 32, h);
	CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	char *m = malloc(1000000);
	memset(m, 'a', 1000000);
	upd_sha256(m, 1000000, d), hex(d, 32, h);
	CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
	/* a file with odd sizes against sha256sum */
	for (int i = 0; i < 1000000; i++) m[i] = (char)(i * 7 + i / 300);
	char p[600], out[600];
	path(p, "sha.bin");
	put(p, m, 777777);
	free(m);
	long size = 0;
	CHECK(upd_sha256_file(p, h, &size) == 0 && size == 777777);
	path(out, "sha.txt");
	sh("sha256sum '%s' | cut -c1-64 > '%s'", p, out);
	char *want = get(out, NULL);
	CHECK(want && !strncmp(want, h, 64));
	free(want);
	CHECK(upd_sha256_file("/nonexistent/x", h, NULL) == -1);
}

/* ---- manifest + signature ---- */
static const char *GOOD = "format=scribe-update-1\nversion=0.3.0\nmin_version=0.2.0\nbundle=scribe-update-0.3.0.tar\n"
			  "size=1234\nsha256=" "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n";

static int parse(const char *s) {
	UpdManifest m;
	char e[100];
	return upd_manifest_parse(s, strlen(s), &m, e, sizeof e);
}

static void test_manifest(void) {
	UpdManifest m;
	char e[100];
	CHECK(upd_manifest_parse(GOOD, strlen(GOOD), &m, e, sizeof e) == 0);
	CHECK(!strcmp(m.version, "0.3.0") && !strcmp(m.min_version, "0.2.0") && m.size == 1234 && m.format == 1);
	CHECK(!strcmp(m.bundle, "scribe-update-0.3.0.tar") && m.sha256[0] == 'e' && strlen(m.sha256) == 64);
	/* CRLF, comments, unknown keys and an upper-case hash are fine */
	CHECK(parse("# made by make_release.sh\r\nformat=scribe-update-1\r\nversion=v0.3.0\r\nmin_version=0.2.0\r\nnew_key=x\r\n"
		    "bundle=b.tar\r\nsize=5\r\nsha256=E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855\r\n") == 0);
	char buf[1000];
	const char *swaps[][2] = {
		{ "format=scribe-update-1", "format=scribe-update-2" }, { "version=0.3.0", "version=0.3.x" },
		{ "min_version=0.2.0", "min_version=" }, { "bundle=scribe-update-0.3.0.tar", "bundle=../x.tar" },
		{ "bundle=scribe-update-0.3.0.tar", "bundle=.hidden" }, { "bundle=scribe-update-0.3.0.tar", "bundle=a/b.tar" },
		{ "size=1234", "size=0" }, { "size=1234", "size=99999999999" }, { "size=1234", "size=-5" },
		{ "size=1234", "size=12a" }, { "sha256=e3b0", "sha256=z3b0" }, { "version=0.3.0\n", "" },
		{ "size=1234\n", "size=1234\nsize=1234\n" }, { "version=0.3.0", "version 0.3.0" },
	};
	for (size_t i = 0; i < sizeof swaps / sizeof *swaps; i++) {
		const char *at = strstr(GOOD, swaps[i][0]);
		snprintf(buf, sizeof buf, "%.*s%s%s", (int)(at - GOOD), GOOD, swaps[i][1], at + strlen(swaps[i][0]));
		int r = parse(buf);
		if (r == 0) fprintf(stderr, "accepted bad manifest #%zu\n", i);
		CHECK(r == -1);
	}
	CHECK(upd_manifest_parse("", 0, &m, e, sizeof e) == -1);
	CHECK(upd_manifest_parse("format=scribe-update-1\0x", 24, &m, e, sizeof e) == -1);
}

static uint8_t SK[64], PK[32], SK2[64], PK2[32];

static void keypair(uint8_t sk[64], uint8_t pk[32]) {
	uint8_t seed[32];
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, seed, 32) != 32) exit(2);
	close(fd);
	crypto_ed25519_key_pair(sk, pk, seed);
}

static void test_signature(void) {
	uint8_t sig[64];
	size_t n = strlen(GOOD);
	crypto_ed25519_sign(sig, SK, (const uint8_t *)GOOD, n);
	CHECK(upd_sig_check(PK, GOOD, n, sig) == 0);
	CHECK(upd_sig_check(PK2, GOOD, n, sig) == -1); /* another key */
	char t[1000];
	memcpy(t, GOOD, n);
	for (size_t i = 0; i < n; i += 13) { /* any changed byte */
		t[i] ^= 1;
		CHECK(upd_sig_check(PK, t, n, sig) == -1);
		t[i] ^= 1;
	}
	CHECK(upd_sig_check(PK, GOOD, n - 1, sig) == -1);
	sig[10] ^= 0x40;
	CHECK(upd_sig_check(PK, GOOD, n, sig) == -1);
	uint8_t pub[32];
	CHECK(upd_hex_decode(UPDATE_PUBKEY_HEX, pub, 32) == 0);
	sig[10] ^= 0x40;
	CHECK(upd_sig_check(pub, GOOD, n, sig) == -1); /* the test key isn't the release key */
}

/* ---- bundles ---- */
#define NEW_ENTRY "#!/bin/sh\n# Name: Inkling\nexec /bin/sh /var/local/scribe/app/launch.sh\n"
#define OLD_ENTRY "#!/bin/sh\n# Name: Scribe\nexec /bin/sh /var/local/scribe/app/launch.sh\n"
static void make_payload(const char *dir, const char *tag) {
	char p[700];
	sh("rm -rf '%s' && mkdir -p '%s/licences'", dir, dir);
	snprintf(p, sizeof p, "%s/scribe", dir);
	char elf[64];
	int n = snprintf(elf, sizeof elf, "\177ELF app %s\n", tag);
	put(p, elf, (size_t)n);
	snprintf(p, sizeof p, "%s/launch.sh", dir), puts_(p, "#!/bin/sh\n# new launcher\n");
	/* the entry twice, as make_release.sh packs it: scribe.sh is for the updaters up to 0.2.x */
	snprintf(p, sizeof p, "%s/inkling.sh", dir), puts_(p, NEW_ENTRY);
	snprintf(p, sizeof p, "%s/scribe.sh", dir), puts_(p, NEW_ENTRY);
	snprintf(p, sizeof p, "%s/VERSION", dir), puts_(p, "0.3.0\n");
	snprintf(p, sizeof p, "%s/notes.txt", dir), puts_(p, "a file this version doesn't know\n");
	snprintf(p, sizeof p, "%s/licences/OFL.txt", dir), puts_(p, "font licence\n");
	snprintf(p, sizeof p, "%s/licences/Monocypher-LICENCE.md", dir), puts_(p, "crypto licence\n");
}

/* the same tar command as make_release.sh */
static void make_tar(const char *dir, const char *tar) {
	sh("cd '%s' && tar --format=ustar --owner=0 --group=0 --numeric-owner --mtime=@0 --sort=name -cf '%s' *", dir, tar);
}

/* a hand-made ustar member (for hostile archives) */
static size_t member(unsigned char *out, const char *name, char type, const char *data, const char *link) {
	unsigned char *h = out;
	memset(h, 0, 512);
	size_t n = data ? strlen(data) : 0;
	snprintf((char *)h, 100, "%s", name);
	memcpy(h + 100, "0000644", 7);
	memcpy(h + 108, "0000000", 7);
	memcpy(h + 116, "0000000", 7);
	snprintf((char *)h + 124, 12, "%011o", (unsigned)n);
	memcpy(h + 136, "00000000000", 11);
	h[156] = (unsigned char)type;
	if (link) snprintf((char *)h + 157, 100, "%s", link);
	memcpy(h + 257, "ustar", 6);
	memcpy(h + 263, "00", 2);
	memset(h + 148, ' ', 8);
	unsigned sum = 0;
	for (int i = 0; i < 512; i++) sum += h[i];
	snprintf((char *)h + 148, 8, "%06o", sum);
	size_t pad = (n + 511) / 512 * 512;
	memset(out + 512, 0, pad);
	if (n) memcpy(out + 512, data, n);
	return 512 + pad;
}

static int extract_raw(const unsigned char *t, size_t n, const char *dest) {
	char p[600], e[160];
	path(p, "hostile.tar");
	put(p, t, n);
	int r = upd_extract(p, dest, e, sizeof e);
	if (getenv("UPDATE_TEST_LOG")) fprintf(stderr, "extract: %d %s\n", r, r ? e : "");
	return r;
}

static void test_extract(void) {
	char dir[600], tar[600], dest[600], p[700], e[160];
	path(dir, "payload");
	path(tar, "bundle.tar");
	path(dest, "out");
	make_payload(dir, "v3");
	make_tar(dir, tar);
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	snprintf(p, sizeof p, "%s/launch.sh", dest), CHECK(same(p, "#!/bin/sh\n# new launcher\n"));
	snprintf(p, sizeof p, "%s/licences/OFL.txt", dest), CHECK(same(p, "font licence\n"));
	snprintf(p, sizeof p, "%s/scribe", dest), CHECK(is_file(p));
	snprintf(p, sizeof p, "%s/notes.txt", dest), CHECK(!is_file(p)); /* unknown: not unpacked */
	/* hostile archives */
	static unsigned char t[64 * 1024];
	size_t n;
	char outside[600];
	path(outside, "evil");
	unlink(outside);
	n = member(t, "scribe", '2', NULL, "/etc/passwd"); /* a symlink */
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1);
	n = member(t, "scribe", '1', NULL, "x"); /* a hard link */
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1);
	n = member(t, "scribe", '0', "\177ELF", NULL);
	n += member(t + n, "../evil", '0', "x", NULL);  /* outside the dir: not a known name, skipped */
	n += member(t + n, "licences/../../evil", '0', "x", NULL);
	n += member(t + n, "/tmp/evil", '0', "x", NULL);
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == 0);
	CHECK(!is_file(outside));
	snprintf(p, sizeof p, "%s/scribe", dest), CHECK(same(p, "\177ELF"));
	n = member(t, "scribe", '0', "a", NULL);
	n += member(t + n, "scribe", '0', "b", NULL); /* twice */
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1);
	n = member(t, "scribe", '0', "abc", NULL);
	t[0] = 'S'; /* checksum no longer matches */
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1);
	n = member(t, "scribe", '0', "abc", NULL);
	CHECK(extract_raw(t, n, dest) == -1); /* no end blocks: cut short */
	n = member(t, "scribe", '0', "abcdef", NULL);
	CHECK(extract_raw(t, 600, dest) == -1); /* data cut short */
	n = member(t, "scribe", 'x', "path=evil", NULL); /* pax headers aren't made by make_release.sh */
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1);
	n = member(t, "scribe", '0', "abc", NULL);
	memcpy(t + 257, "nope!", 5);
	memset(t + n, 0, 1024), n += 1024;
	CHECK(extract_raw(t, n, dest) == -1); /* not ustar (and the checksum is off too) */
	memset(t, 0, 1024);
	CHECK(extract_raw(t, 1024, dest) == -1); /* empty */
	CHECK(upd_extract("/nonexistent.tar", dest, e, sizeof e) == -1);
}

/* ---- installing ---- */
static UpdTargets T;
static char tb[600], ta[600], td[600], tu[600];

static void make_targets(void) {
	path(tb, "kindle/bin"), path(ta, "kindle/app"), path(td, "kindle/documents"), path(tu, "kindle/usb");
	sh("rm -rf '%s/kindle' && mkdir -p '%s' '%s' '%s' '%s'", root, tb, ta, td, tu);
	char p[700];
	snprintf(p, sizeof p, "%s/scribe", tb), puts_(p, "\177ELF old app\n");
	snprintf(p, sizeof p, "%s/launch.sh", ta), puts_(p, "old launcher\n");
	/* an install from before the rename: the entry and the installer under their old names, and a file of the same
	 * name that isn't this app's (stays) */
	snprintf(p, sizeof p, "%s/scribe.sh", td), puts_(p, OLD_ENTRY);
	snprintf(p, sizeof p, "%s/Install Scribe.sh", td), puts_(p, "#!/bin/sh\nmkdir -p /var/local/scribe/bin\n");
	snprintf(p, sizeof p, "%s/Uninstall Scribe.sh", td), puts_(p, "someone else's\n");
	T = (UpdTargets){ tb, ta, td, tu };
}

static int untouched(void) {
	char p[700];
	int ok = 1;
	snprintf(p, sizeof p, "%s/scribe", tb), ok &= same(p, "\177ELF old app\n");
	snprintf(p, sizeof p, "%s/launch.sh", ta), ok &= same(p, "old launcher\n");
	snprintf(p, sizeof p, "%s/scribe.sh", td), ok &= same(p, OLD_ENTRY);
	snprintf(p, sizeof p, "%s/Install Scribe.sh", td), ok &= is_file(p);
	snprintf(p, sizeof p, "%s/inkling.sh", td), ok &= !is_file(p);
	snprintf(p, sizeof p, "%s/scribe.new", tb), ok &= !is_file(p);
	snprintf(p, sizeof p, "%s/launch.sh.new", ta), ok &= !is_file(p);
	snprintf(p, sizeof p, "%s/inkling.sh.new", td), ok &= !is_file(p);
	return ok;
}

static int installed(const char *tag) {
	char p[700], want[64];
	int ok = 1;
	snprintf(want, sizeof want, "\177ELF app %s\n", tag);
	snprintf(p, sizeof p, "%s/scribe", tb), ok &= same(p, want) && mode_of(p) == 0755;
	snprintf(p, sizeof p, "%s/scribe.prev", tb), ok &= same(p, "\177ELF old app\n");
	snprintf(p, sizeof p, "%s/launch.sh", ta), ok &= same(p, "#!/bin/sh\n# new launcher\n") && mode_of(p) == 0755;
	snprintf(p, sizeof p, "%s/launch.sh.prev", ta), ok &= same(p, "old launcher\n");
	snprintf(p, sizeof p, "%s/inkling.sh", td), ok &= same(p, NEW_ENTRY);
	snprintf(p, sizeof p, "%s/scribe.sh", td), ok &= !is_file(p);
	snprintf(p, sizeof p, "%s/Install Scribe.sh", td), ok &= !is_file(p);
	snprintf(p, sizeof p, "%s/licences/OFL.txt", tu), ok &= same(p, "font licence\n");
	snprintf(p, sizeof p, "%s/scribe.new", tb), ok &= !is_file(p);
	return ok;
}

static void test_install(void) {
	char dir[600], tar[600], dest[600], p[700], e[160];
	path(dir, "payload");
	path(tar, "bundle.tar");
	path(dest, "new");
	make_payload(dir, "v3");
	make_tar(dir, tar);
	/* good */
	make_targets();
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	CHECK(upd_install(dest, &T, e, sizeof e) == 0);
	CHECK(installed("v3"));
	snprintf(p, sizeof p, "%s/curl-smtp", tb), CHECK(!is_file(p)); /* not in this bundle */
	/* with curl-smtp, over a fresh Kindle (no old files: no .prev) */
	sh("rm -rf '%s/kindle' && mkdir -p '%s' '%s' '%s' '%s'", root, tb, ta, td, tu);
	snprintf(p, sizeof p, "%s/curl-smtp", dir), puts_(p, "\177ELF curl\n");
	make_tar(dir, tar);
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	CHECK(upd_install(dest, &T, e, sizeof e) == 0);
	snprintf(p, sizeof p, "%s/curl-smtp", tb), CHECK(same(p, "\177ELF curl\n") && mode_of(p) == 0755);
	snprintf(p, sizeof p, "%s/scribe.prev", tb), CHECK(!is_file(p));
	unlink(p), snprintf(p, sizeof p, "%s/curl-smtp", dir), unlink(p);
	/* a bundle without the app, or with something that isn't one: nothing changes */
	make_targets();
	make_tar(dir, tar);
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	snprintf(p, sizeof p, "%s/scribe", dest), puts_(p, "#!/bin/sh\necho not an app\n");
	CHECK(upd_install(dest, &T, e, sizeof e) == -1);
	CHECK(untouched());
	unlink(p);
	CHECK(upd_install(dest, &T, e, sizeof e) == -1);
	CHECK(untouched());
	/* the entry only under its old name scribe.sh: installed as inkling.sh; with no entry at all: nothing changes */
	make_targets();
	snprintf(p, sizeof p, "%s/inkling.sh", dir), unlink(p);
	make_tar(dir, tar);
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	CHECK(upd_install(dest, &T, e, sizeof e) == 0);
	CHECK(installed("v3"));
	make_targets();
	snprintf(p, sizeof p, "%s/scribe.sh", dir), unlink(p);
	make_tar(dir, tar);
	CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
	CHECK(upd_install(dest, &T, e, sizeof e) == -1 && strstr(e, "inkling.sh"));
	CHECK(untouched());
	make_payload(dir, "v3");
	make_tar(dir, tar);
	/* a target that can't be written (the Library folder read-only): nothing changes, no .new left behind */
	if (geteuid() != 0) {
		make_targets();
		CHECK(upd_extract(tar, dest, e, sizeof e) == 0);
		chmod(td, 0555);
		CHECK(upd_install(dest, &T, e, sizeof e) == -1);
		chmod(td, 0755);
		CHECK(untouched());
		snprintf(p, sizeof p, "%s/licences/OFL.txt.new", tu), CHECK(!is_file(p));
	}
	upd_rm_tree(dest);
}

/* ---- the rename (Scribe -> Inkling): the USB folder and the Library entries ---- */
static void test_migrate(void) {
	char o[600], n[600], d[600], p[700];
	path(o, "mig/scribe-claude"), path(n, "mig/inkling"), path(d, "mig/documents");
	/* nothing there: nothing to do */
	sh("rm -rf '%s/mig' && mkdir -p '%s'", root, d);
	CHECK(upd_migrate_usb_dir(o, n) == 0 && !is_file(n));
	/* only the old folder (an in-app update): renamed with everything in it */
	sh("mkdir -p '%s/notebooks/default' '%s/install' && echo cfg > '%s/config.txt' && echo png > '%s/notebooks/default/page-0001.png'",
	   o, o, o, o);
	CHECK(upd_migrate_usb_dir(o, n) == 1);
	snprintf(p, sizeof p, "%s/config.txt", n), CHECK(same(p, "cfg\n"));
	snprintf(p, sizeof p, "%s/notebooks/default/page-0001.png", n), CHECK(same(p, "png\n"));
	CHECK(!is_file(o));
	CHECK(upd_migrate_usb_dir(o, n) == 0); /* once */
	/* both, and the new one is in use: the old one is left alone */
	sh("mkdir -p '%s' && echo old > '%s/config.txt'", o, o);
	CHECK(upd_migrate_usb_dir(o, n) == 2);
	snprintf(p, sizeof p, "%s/config.txt", n), CHECK(same(p, "cfg\n"));
	snprintf(p, sizeof p, "%s/config.txt", o), CHECK(same(p, "old\n"));
	/* both, the new one only the install files of a new zip (the installer's case): the settings, notebooks and key
	 * files move over, the old install files go, the new ones stay */
	sh("rm -rf '%s' '%s' && mkdir -p '%s/install' '%s/licences' '%s/notebooks/default' '%s/install' '%s/licences'", o, n, o, o, o, n, n);
	sh("echo cfg > '%s/config.txt' && echo png > '%s/notebooks/default/page-0001.png' && echo key > '%s/anthropic-key.txt'", o, o, o);
	sh("echo old > '%s/install/scribe' && echo old > '%s/licences/OFL.txt' && echo new > '%s/install/scribe' && echo new > '%s/licences/OFL.txt'",
	   o, o, n, n);
	CHECK(upd_migrate_usb_dir(o, n) == 1);
	snprintf(p, sizeof p, "%s/config.txt", n), CHECK(same(p, "cfg\n"));
	snprintf(p, sizeof p, "%s/notebooks/default/page-0001.png", n), CHECK(same(p, "png\n"));
	snprintf(p, sizeof p, "%s/anthropic-key.txt", n), CHECK(same(p, "key\n"));
	snprintf(p, sizeof p, "%s/install/scribe", n), CHECK(same(p, "new\n"));
	snprintf(p, sizeof p, "%s/licences/OFL.txt", n), CHECK(same(p, "new\n"));
	CHECK(!is_file(o));
	/* ... and a file both have (never overwritten): it stays in the old folder, which is kept */
	sh("rm -rf '%s' '%s' && mkdir -p '%s/install' '%s/install' && echo cfg > '%s/config.txt' && echo a > '%s/notes.txt' && echo b > '%s/notes.txt'",
	   o, n, o, n, o, o, n);
	CHECK(upd_migrate_usb_dir(o, n) == 2);
	snprintf(p, sizeof p, "%s/config.txt", n), CHECK(same(p, "cfg\n"));
	snprintf(p, sizeof p, "%s/notes.txt", n), CHECK(same(p, "b\n"));
	snprintf(p, sizeof p, "%s/notes.txt", o), CHECK(same(p, "a\n"));
	snprintf(p, sizeof p, "%s/install", o), CHECK(!is_file(p));

	/* Library entries: the only entry, under its old name, becomes inkling.sh */
	snprintf(p, sizeof p, "%s/scribe.sh", d), puts_(p, NEW_ENTRY);
	CHECK(upd_migrate_entries(d) == 1);
	snprintf(p, sizeof p, "%s/inkling.sh", d), CHECK(same(p, NEW_ENTRY));
	snprintf(p, sizeof p, "%s/scribe.sh", d), CHECK(!is_file(p));
	CHECK(upd_migrate_entries(d) == 0);
	/* with inkling.sh there, the old names of this app go; a file that isn't this app's stays */
	snprintf(p, sizeof p, "%s/scribe.sh", d), puts_(p, OLD_ENTRY);
	snprintf(p, sizeof p, "%s/Scribe Claude.sh", d), puts_(p, OLD_ENTRY);
	snprintf(p, sizeof p, "%s/Install Scribe.sh", d), puts_(p, "#!/bin/sh\nmkdir -p /var/local/scribe/bin\n");
	snprintf(p, sizeof p, "%s/Uninstall Scribe.sh", d), puts_(p, "someone else's\n");
	CHECK(upd_migrate_entries(d) == 3);
	snprintf(p, sizeof p, "%s/inkling.sh", d), CHECK(same(p, NEW_ENTRY));
	snprintf(p, sizeof p, "%s/scribe.sh", d), CHECK(!is_file(p));
	snprintf(p, sizeof p, "%s/Scribe Claude.sh", d), CHECK(!is_file(p));
	snprintf(p, sizeof p, "%s/Install Scribe.sh", d), CHECK(!is_file(p));
	snprintf(p, sizeof p, "%s/Uninstall Scribe.sh", d), CHECK(same(p, "someone else's\n"));
	sh("rm -rf '%s/mig'", root);
}

/* ---- the whole thing through the fake curl ---- */
static char fake[600];
static UpdCtx C;

static void ctx_reset(const char *token) {
	memset(&C, 0, sizeof C);
	snprintf(C.curl, sizeof C.curl, "%s", fake_curl);
	snprintf(C.api, sizeof C.api, "https://api.test");
	snprintf(C.repo, sizeof C.repo, "kerbe42/inkling");
	snprintf(C.token, sizeof C.token, "%s", token);
	snprintf(C.current, sizeof C.current, "0.2.0");
	char st[600];
	path(st, "stage");
	snprintf(C.stage, sizeof C.stage, "%s", st);
	memcpy(C.pubkey, PK, 32);
	C.targets = T;
}

/* a release: tag, the USB zip, the bundle, the manifest (signed with sk) */
static void make_release(const char *tag, const char *min, const uint8_t *sk, int with_manifest) {
	char dir[600], p[700], files[600], h[65];
	snprintf(fake, sizeof fake, "%s/fake", root);
	sh("rm -rf '%s' && mkdir -p '%s/files'", fake, fake);
	snprintf(files, sizeof files, "%s/files", fake);
	path(dir, "payload");
	make_payload(dir, "v3");
	char bundle[700];
	snprintf(bundle, sizeof bundle, "%s/inkling-update-%s.tar", files, tag);
	make_tar(dir, bundle);
	long size = 0;
	upd_sha256_file(bundle, h, &size);
	char man[600];
	int mn = snprintf(man, sizeof man, "format=scribe-update-1\nversion=%s\nmin_version=%s\nbundle=inkling-update-%s.tar\nsize=%ld\nsha256=%s\n",
			  tag, min, tag, size, h);
	snprintf(p, sizeof p, "%s/inkling-update-%s.manifest", files, tag), put(p, man, (size_t)mn);
	uint8_t sig[64];
	crypto_ed25519_sign(sig, sk, (const uint8_t *)man, (size_t)mn);
	snprintf(p, sizeof p, "%s/inkling-update-%s.manifest.sig", files, tag), put(p, sig, 64);
	snprintf(p, sizeof p, "%s/inkling-%s.zip", files, tag), puts_(p, "zip");
	char json[4000];
	int jn = snprintf(json, sizeof json, "{\"tag_name\":\"v%s\",\"name\":\"Scribe %s\",\"body\":\"notes\",\"assets\":[", tag, tag);
	const char *names[4] = { "inkling-%s.zip", "inkling-update-%s.tar", "inkling-update-%s.manifest", "inkling-update-%s.manifest.sig" };
	for (int i = 0; i < (with_manifest ? 4 : 2); i++) {
		char nm[100];
		snprintf(nm, sizeof nm, names[i], tag);
		snprintf(p, sizeof p, "%s/%s", files, nm);
		struct stat st;
		stat(p, &st);
		jn += snprintf(json + jn, sizeof json - (size_t)jn,
			       "%s{\"name\":\"%s\",\"size\":%ld,\"url\":\"https://api.test/repos/kerbe42/inkling/releases/assets/%s\","
			       "\"browser_download_url\":\"https://dl.test/%s\"}",
			       i ? "," : "", nm, (long)st.st_size, nm, nm);
	}
	snprintf(json + jn, sizeof json - (size_t)jn, "]}");
	snprintf(p, sizeof p, "%s/release.json", fake), puts_(p, json);
	setenv("FAKE_DIR", fake, 1);
}

static char *fake_log(void) {
	char p[700];
	snprintf(p, sizeof p, "%s/log", fake);
	char *l = get(p, NULL);
	return l ? l : strdup("");
}

static void test_flow(void) {
	char p[700];
	/* public repo: check, then install */
	make_targets();
	make_release("0.3.0", "0.2.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_AVAILABLE);
	CHECK(!strcmp(C.version, "0.3.0") && !strcmp(C.latest, "0.3.0"));
	CHECK(upd_install_run(&C) == UPD_INSTALLED);
	CHECK(installed("v3"));
	char *log = fake_log();
	CHECK(!strstr(log, "LEAK") && !strstr(log, "NOFOLLOW") && !strstr(log, "UNEXPECTED"));
	CHECK(strstr(log, "GET https://dl.test/inkling-update-0.3.0.tar auth=no follow=1") != NULL);
	free(log);
	snprintf(p, sizeof p, "%s/bundle.tar", C.stage), CHECK(!is_file(p)); /* staging cleaned up */
	snprintf(p, sizeof p, "%s/hdr.txt", C.stage), CHECK(!is_file(p));

	/* private repo: the token goes to the API only, never to the storage host or a command line */
	make_targets();
	make_release("0.3.0", "0.2.0", SK, 1);
	const char *tok = "github_pat_TESTONLY0123456789";
	ctx_reset(tok);
	CHECK(upd_check(&C) == UPD_AVAILABLE);
	CHECK(upd_install_run(&C) == UPD_INSTALLED);
	CHECK(installed("v3"));
	log = fake_log();
	CHECK(!strstr(log, "LEAK") && !strstr(log, tok) && !strstr(log, "UNEXPECTED"));
	CHECK(strstr(log, "GET https://api.test/repos/kerbe42/inkling/releases/latest auth=yes") != NULL);
	CHECK(strstr(log, "GET https://api.test/repos/kerbe42/inkling/releases/assets/inkling-update-0.3.0.tar auth=yes follow=0") != NULL);
	CHECK(strstr(log, "GET https://objects.test/inkling-update-0.3.0.tar auth=no follow=1") != NULL);
	free(log);
	snprintf(p, sizeof p, "%s/hdr.txt", C.stage), CHECK(!is_file(p)); /* the header file with the token is gone */

	/* the bundle on GitHub swapped after the check: not installed */
	make_targets();
	make_release("0.3.0", "0.2.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_AVAILABLE);
	snprintf(p, sizeof p, "%s/files/inkling-update-0.3.0.tar", fake);
	char d2[600], *orig = get(p, NULL);
	path(d2, "payload2");
	make_payload(d2, "EVIL");
	make_tar(d2, p);
	CHECK(upd_install_run(&C) == UPD_ERR && strstr(C.msg, "doesn't match"));
	CHECK(untouched());
	free(orig);

	/* signed with another key: refused at the check, and the install refuses too */
	make_targets();
	make_release("0.3.0", "0.2.0", SK2, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_ERR && strstr(C.msg, "signature"));
	CHECK(upd_install_run(&C) == UPD_ERR);
	CHECK(untouched());

	/* the manifest swapped after the check (the signature no longer matches) */
	make_targets();
	make_release("0.3.0", "0.2.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_AVAILABLE);
	snprintf(p, sizeof p, "%s/manifest", C.stage);
	char *mt = get(p, NULL);
	char *at = strstr(mt, "min_version=0.2.0");
	at[12] = '1';
	puts_(p, mt);
	free(mt);
	CHECK(upd_install_run(&C) == UPD_ERR && strstr(C.msg, "signature"));
	CHECK(untouched());

	/* already the latest; a newer one that needs a USB install; a release without update files */
	make_release("0.2.0", "0.2.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_LATEST);
	make_release("0.1.9", "0.1.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_LATEST);
	make_release("1.0.0", "0.9.0", SK, 1);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_USB_ONLY && strstr(C.msg, "USB"));
	make_release("0.3.0", "0.2.0", SK, 0);
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_USB_ONLY && strstr(C.msg, "USB"));

	/* GitHub errors */
	make_release("0.3.0", "0.2.0", SK, 1);
	snprintf(p, sizeof p, "%s/latest.status", fake), puts_(p, "404");
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_ERR && strstr(C.msg, "private repo needs a GitHub token"));
	ctx_reset("github_pat_TESTONLY0123456789");
	CHECK(upd_check(&C) == UPD_ERR && strstr(C.msg, "token can't read it"));
	puts_(p, "403");
	ctx_reset("");
	CHECK(upd_check(&C) == UPD_ERR && strstr(C.msg, "limiting"));
	unlink(p);
	ctx_reset("");
	snprintf(C.api, sizeof C.api, "https://unknown.test");
	CHECK(upd_check(&C) == UPD_ERR && strstr(C.msg, "no connection"));
	ctx_reset("");
	snprintf(C.current, sizeof C.current, "dev");
	CHECK(upd_check(&C) == UPD_ERR);
	ctx_reset("");
	snprintf(C.api, sizeof C.api, "http://api.test"); /* never plain http */
	CHECK(upd_check(&C) == UPD_ERR);

	/* the release JSON: odd and missing fields */
	ctx_reset("");
	CHECK(upd_release_parse(&C, "{\"tag_name\":\"v0.4.1\",\"assets\":[{\"name\":\"x.manifest\",\"url\":\"http://bad\"},"
				    "{\"name\":\"x.manifest.sig\",\"browser_download_url\":\"https://dl.test/x.manifest.sig\"},{\"size\":3}]}") == 0);
	CHECK(!strcmp(C.latest, "0.4.1") && C.nassets == 2 && C.manifest_i == 0 && C.sig_i == 1 && C.assets[0].url[0] == 0);
	CHECK(upd_release_parse(&C, "not json") == -1);
	CHECK(upd_release_parse(&C, "{\"assets\":[]}") == -1);
}

int main(int argc, char **argv) {
	if (argc == 5 && !strcmp(argv[1], "verify")) { /* openssl interop, from run_update_tests.sh */
		uint8_t pub[32];
		size_t mn = 0, sn = 0;
		char *m = get(argv[3], &mn), *s = get(argv[4], &sn);
		int ok = m && s && sn == 64 && upd_hex_decode(argv[2], pub, 32) == 0 && upd_sig_check(pub, m, mn, (uint8_t *)s) == 0;
		UpdManifest man;
		char e[100];
		if (ok && upd_manifest_parse(m, mn, &man, e, sizeof e) != 0) { fprintf(stderr, "%s\n", e); ok = 0; }
		free(m);
		free(s);
		return ok ? 0 : 1;
	}
	if (argc != 2) { fprintf(stderr, "usage: test_update <fake_curl.sh>\n"); return 2; }
	fake_curl = argv[1];
	const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
	snprintf(root, sizeof root, "%s/scribe-update-XXXXXX", tmp);
	if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
	keypair(SK, PK);
	keypair(SK2, PK2);
	test_versions();
	test_sha();
	test_manifest();
	test_signature();
	test_extract();
	test_install();
	test_migrate();
	test_flow();
	if (!fails) sh("rm -rf '%s'", root);
	else fprintf(stderr, "files kept in %s\n", root);
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

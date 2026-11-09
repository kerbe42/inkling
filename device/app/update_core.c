#define _GNU_SOURCE
/* Self-update, the parts without the screen: versions, the signed manifest, SHA-256, the bundle (ustar) reader, the
 * installer and the GitHub requests (run in the network worker). See update.h for the release format. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON.h"
#include "monocypher-ed25519.h"
#include "update.h"

#define SETERR(...) (err && en ? (void)snprintf(err, en, __VA_ARGS__) : (void)0)

/* ---- files ---- */

/* Whole file, NUL-terminated; NULL if missing, unreadable or bigger than max. */
static char *slurp(const char *path, long max, size_t *len) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return NULL;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > max) { close(fd); return NULL; }
	char *b = malloc((size_t)st.st_size + 1);
	size_t n = 0;
	while (b && n < (size_t)st.st_size) {
		ssize_t r = read(fd, b + n, (size_t)st.st_size - n);
		if (r <= 0) { free(b); b = NULL; break; }
		n += (size_t)r;
	}
	close(fd);
	if (!b) return NULL;
	b[n] = 0;
	if (len) *len = n;
	return b;
}

static int put_file(const char *path, const char *data, size_t n) {
	unlink(path);
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0) return -1;
	int ok = write(fd, data, n) == (ssize_t)n;
	return close(fd) == 0 && ok ? 0 : -1;
}

static int write_all(int fd, const void *p, size_t n) {
	const char *c = p;
	while (n) {
		ssize_t w = write(fd, c, n);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return -1;
		c += w;
		n -= (size_t)w;
	}
	return 0;
}

/* Copies src to dst (created fresh with mode), flushed to disk. */
static int copy_file(const char *src, const char *dst, mode_t mode) {
	int in = open(src, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (in < 0) return -1;
	unlink(dst);
	int out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
	if (out < 0) { close(in); return -1; }
	char buf[16384];
	ssize_t r;
	int ok = 1;
	while (ok && (r = read(in, buf, sizeof buf)) != 0) {
		if (r < 0) { if (errno == EINTR) continue; ok = 0; break; }
		ok = write_all(out, buf, (size_t)r) == 0;
	}
	close(in);
	if (ok) ok = fsync(out) == 0;
	if (close(out) != 0) ok = 0;
	if (!ok) unlink(dst);
	return ok ? 0 : -1;
}

void upd_rm_tree(const char *dir) {
	DIR *d = opendir(dir);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char p[512];
			snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
			struct stat st;
			if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
				DIR *d2 = opendir(p);
				struct dirent *e2;
				while (d2 && (e2 = readdir(d2))) {
					if (!strcmp(e2->d_name, ".") || !strcmp(e2->d_name, "..")) continue;
					char p2[800];
					snprintf(p2, sizeof p2, "%s/%s", p, e2->d_name);
					unlink(p2);
				}
				if (d2) closedir(d2);
				rmdir(p);
			} else unlink(p);
		}
		closedir(d);
	}
	rmdir(dir);
}

/* ---- versions ---- */

int upd_version_parse(const char *s, int v[4]) {
	memset(v, 0, 4 * sizeof *v);
	if (!s) return -1;
	if (*s == 'v' || *s == 'V') s++;
	int n = 0;
	for (;;) {
		if (*s < '0' || *s > '9' || n >= 4) return -1;
		int x = 0, digits = 0;
		while (*s >= '0' && *s <= '9') {
			if (++digits > 6) return -1;
			x = x * 10 + (*s++ - '0');
		}
		v[n++] = x;
		if (!*s) return 0;
		if (*s++ != '.') return -1;
	}
}

int upd_version_cmp(const char *a, const char *b) {
	int va[4], vb[4];
	int ea = upd_version_parse(a, va), eb = upd_version_parse(b, vb);
	if (ea || eb) return ea && eb ? 0 : ea ? -1 : 1;
	for (int i = 0; i < 4; i++)
		if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
	return 0;
}

static void strip_v(char *out, size_t n, const char *tag) {
	if (tag[0] == 'v' || tag[0] == 'V') tag++;
	snprintf(out, n, "%s", tag);
}

int upd_hex_decode(const char *hex, uint8_t *out, size_t n) {
	if (!hex || strlen(hex) != 2 * n) return -1;
	for (size_t i = 0; i < 2 * n; i++) {
		char c = hex[i];
		int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (d < 0) return -1;
		if (i & 1) out[i / 2] = (uint8_t)(out[i / 2] | d);
		else out[i / 2] = (uint8_t)(d << 4);
	}
	return 0;
}

/* ---- the manifest ---- */

/* a plain file name: letters, digits, . _ + -, not starting with a dot */
static int safe_name(const char *s, size_t max) {
	size_t n = strlen(s);
	if (!n || n >= max || s[0] == '.') return 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
		      c == '+' || c == '-'))
			return 0;
	}
	return 1;
}

int upd_manifest_parse(const char *text, size_t len, UpdManifest *m, char *err, size_t en) {
	memset(m, 0, sizeof *m);
	if (!len || len > 4096 || memchr(text, 0, len)) { SETERR("manifest: bad size"); return -1; }
	char buf[4097];
	memcpy(buf, text, len);
	buf[len] = 0;
	int seen = 0; /* bit per key, against duplicates */
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		size_t l = strlen(line);
		if (l && line[l - 1] == '\r') line[--l] = 0;
		if (!l || line[0] == '#') continue;
		char *eq = strchr(line, '=');
		if (!eq) { SETERR("manifest: bad line"); return -1; }
		*eq = 0;
		const char *k = line, *v = eq + 1;
		static const char *keys[] = { "format", "version", "min_version", "bundle", "size", "sha256" };
		int ki = -1;
		for (int i = 0; i < 6; i++)
			if (!strcmp(k, keys[i])) ki = i;
		if (ki < 0) continue; /* a newer manifest may carry more */
		if (seen & (1 << ki)) { SETERR("manifest: %s twice", k); return -1; }
		seen |= 1 << ki;
		int vv[4];
		switch (ki) {
		case 0:
			if (strcmp(v, "scribe-update-1")) { SETERR("manifest: unknown format %.40s", v); return -1; }
			m->format = 1;
			break;
		case 1:
		case 2:
			if (upd_version_parse(v, vv) || strlen(v) >= sizeof m->version) { SETERR("manifest: bad %s", k); return -1; }
			strip_v(ki == 1 ? m->version : m->min_version, sizeof m->version, v);
			break;
		case 3:
			if (!safe_name(v, sizeof m->bundle)) { SETERR("manifest: bad bundle name"); return -1; }
			snprintf(m->bundle, sizeof m->bundle, "%s", v);
			break;
		case 4: {
			long s = 0;
			if (!*v || strlen(v) > 9) { SETERR("manifest: bad size"); return -1; }
			for (const char *p = v; *p; p++) {
				if (*p < '0' || *p > '9') { SETERR("manifest: bad size"); return -1; }
				s = s * 10 + (*p - '0');
			}
			if (s <= 0 || s > UPD_MAX_BUNDLE) { SETERR("manifest: bad size"); return -1; }
			m->size = s;
			break;
		}
		case 5: {
			uint8_t h[32];
			if (upd_hex_decode(v, h, 32)) { SETERR("manifest: bad sha256"); return -1; }
			for (int i = 0; i < 64; i++) m->sha256[i] = (char)(v[i] >= 'A' && v[i] <= 'F' ? v[i] + 32 : v[i]);
			m->sha256[64] = 0;
			break;
		}
		}
	}
	if (seen != 63) { SETERR("manifest: incomplete"); return -1; }
	return 0;
}

int upd_sig_check(const uint8_t pub[32], const void *msg, size_t len, const uint8_t sig[64]) {
	return crypto_ed25519_check(sig, pub, msg, len) == 0 ? 0 : -1;
}

/* ---- SHA-256 (FIPS 180-4) ---- */

typedef struct {
	uint32_t h[8];
	uint64_t n;
	uint8_t b[64];
	size_t bl;
} Sha;

static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
	0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
	0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(Sha *s, const uint8_t *p) {
	uint32_t w[64];
	for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 64; i++) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (int i = 0; i < 64; i++) {
		uint32_t t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(Sha *s) {
	static const uint32_t h0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(s->h, h0, sizeof h0);
	s->n = 0;
	s->bl = 0;
}

static void sha_update(Sha *s, const void *data, size_t n) {
	const uint8_t *p = data;
	s->n += n;
	while (n) {
		size_t k = 64 - s->bl < n ? 64 - s->bl : n;
		memcpy(s->b + s->bl, p, k);
		s->bl += k;
		p += k;
		n -= k;
		if (s->bl == 64) { sha_block(s, s->b); s->bl = 0; }
	}
}

static void sha_final(Sha *s, uint8_t out[32]) {
	uint64_t bits = s->n * 8;
	uint8_t pad = 0x80, z = 0;
	sha_update(s, &pad, 1);
	while (s->bl != 56) sha_update(s, &z, 1);
	uint8_t l[8];
	for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8 * i));
	sha_update(s, l, 8);
	for (int i = 0; i < 8; i++) {
		out[4 * i] = (uint8_t)(s->h[i] >> 24);
		out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
		out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
		out[4 * i + 3] = (uint8_t)s->h[i];
	}
}

void upd_sha256(const void *data, size_t n, uint8_t out[32]) {
	Sha s;
	sha_init(&s);
	sha_update(&s, data, n);
	sha_final(&s, out);
}

int upd_sha256_file(const char *path, char hex[65], long *size) {
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) return -1;
	Sha s;
	sha_init(&s);
	char buf[16384];
	ssize_t r;
	long total = 0;
	while ((r = read(fd, buf, sizeof buf)) != 0) {
		if (r < 0) { if (errno == EINTR) continue; close(fd); return -1; }
		sha_update(&s, buf, (size_t)r);
		total += r;
	}
	close(fd);
	uint8_t d[32];
	sha_final(&s, d);
	for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
	if (size) *size = total;
	return 0;
}

/* ---- the bundle: a ustar archive ---- */

static long octal(const unsigned char *p, int n) {
	long v = 0;
	int i = 0, digits = 0;
	while (i < n && p[i] == ' ') i++;
	for (; i < n && p[i] >= '0' && p[i] <= '7'; i++, digits++) {
		if (v > 0x0FFFFFFFL) return -1; /* no overflow with a 32-bit long */
		v = v * 8 + (p[i] - '0');
	}
	for (; i < n; i++)
		if (p[i] != ' ' && p[i] != 0) return -1;
	return digits ? v : -1;
}

/* the files the installer knows */
static int known_file(const char *name) {
	/* scribe.sh: the Library entry under its name up to 0.2.x (bundles carry it for those updaters) */
	static const char *top[] = { "scribe", "launch.sh", "inkling.sh", "scribe.sh", "VERSION", "curl-smtp" };
	for (size_t i = 0; i < sizeof top / sizeof *top; i++)
		if (!strcmp(name, top[i])) return 1;
	return !strncmp(name, "licences/", 9) && safe_name(name + 9, 80);
}

int upd_extract(const char *tar_path, const char *dest, char *err, size_t en) {
	FILE *f = fopen(tar_path, "rbe");
	if (!f) { SETERR("can't open the bundle"); return -1; }
	upd_rm_tree(dest);
	if (mkdir(dest, 0700) != 0) { fclose(f); SETERR("can't make %s", dest); return -1; }
	char seen[64][96];
	int nseen = 0, zeros = 0, rc = -1, nfiles = 0;
	unsigned char h[512];
	for (;;) {
		if (fread(h, 1, 512, f) != 512) { SETERR("bundle: cut short"); goto out; }
		int empty = 1;
		for (int i = 0; i < 512 && empty; i++) empty = !h[i];
		if (empty) {
			if (++zeros == 2) break;
			continue;
		}
		if (zeros) { SETERR("bundle: data after the end"); goto out; }
		long sum = 0;
		for (int i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : h[i];
		if (octal(h + 148, 8) != sum) { SETERR("bundle: bad header checksum"); goto out; }
		if (memcmp(h + 257, "ustar", 5)) { SETERR("bundle: not a ustar archive"); goto out; }
		char name[260];
		char nm[101], px[156];
		memcpy(nm, h, 100); nm[100] = 0;
		memcpy(px, h + 345, 155); px[155] = 0;
		snprintf(name, sizeof name, "%s%s%s", px, px[0] ? "/" : "", nm);
		char *nmp = name;
		while (!strncmp(nmp, "./", 2)) nmp += 2;
		long size = octal(h + 124, 12);
		if (size < 0 || size > UPD_MAX_BUNDLE) { SETERR("bundle: bad size for %.60s", nmp); goto out; }
		long padded = (size + 511) / 512 * 512;
		int type = h[156];
		if (type == '5') { /* a directory: nothing to unpack, the installer makes licences/ itself */
			if (size) { SETERR("bundle: bad directory entry"); goto out; }
			continue;
		}
		if (type != '0' && type != 0) { SETERR("bundle: unexpected entry %.60s (type %c)", nmp, type); goto out; }
		if (!known_file(nmp)) { /* a newer bundle may carry more; this version doesn't install it */
			logf_("update: skipping %.60s from the bundle", nmp);
			if (fseek(f, padded, SEEK_CUR) != 0) { SETERR("bundle: cut short"); goto out; }
			continue;
		}
		for (int i = 0; i < nseen; i++)
			if (!strcmp(seen[i], nmp)) { SETERR("bundle: %.60s twice", nmp); goto out; }
		if (nseen < 64) snprintf(seen[nseen++], sizeof seen[0], "%.90s", nmp); /* known names are short */
		else { SETERR("bundle: too many files"); goto out; }
		char path[512];
		if (!strncmp(nmp, "licences/", 9)) {
			snprintf(path, sizeof path, "%s/licences", dest);
			mkdir(path, 0700);
		}
		snprintf(path, sizeof path, "%s/%s", dest, nmp);
		int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd < 0) { SETERR("can't write %.60s", nmp); goto out; }
		long left = size;
		char buf[16384];
		int ok = 1;
		while (ok && left > 0) {
			size_t want = left < (long)sizeof buf ? (size_t)left : sizeof buf;
			ok = fread(buf, 1, want, f) == want && write_all(fd, buf, want) == 0;
			left -= (long)want;
		}
		if (ok) ok = fsync(fd) == 0;
		if (close(fd) != 0) ok = 0;
		if (!ok) { SETERR("bundle: can't unpack %.60s", nmp); goto out; }
		if (padded > size && fseek(f, padded - size, SEEK_CUR) != 0) { SETERR("bundle: cut short"); goto out; }
		nfiles++;
	}
	rc = nfiles ? 0 : -1;
	if (rc) SETERR("bundle: empty");
out:
	fclose(f);
	if (rc) upd_rm_tree(dest);
	return rc;
}

/* ---- installing ---- */

typedef struct {
	char src[400], dst[400];
	mode_t mode;
	int keep_prev, staged, done;
} Step;

static int exists(const char *p) {
	struct stat st;
	return lstat(p, &st) == 0;
}

/* src -> dst.new beside the target: a rename when it's the same file system, else a copy */
static int stage_step(Step *s) {
	char tmp[420];
	snprintf(tmp, sizeof tmp, "%s.new", s->dst);
	unlink(tmp);
	if (rename(s->src, tmp) != 0) {
		if (errno != EXDEV || copy_file(s->src, tmp, s->mode) != 0) return -1;
	}
	s->staged = 1;
	if (chmod(tmp, s->mode) != 0 && (s->mode & 0111)) return -1; /* the USB storage (vfat) may refuse; fine for text */
	return 0;
}

int upd_install(const char *new_dir, const UpdTargets *t, char *err, size_t en) {
	Step st[40];
	int n = 0;
	memset(st, 0, sizeof st);
	char p[400];
	/* the binary must be there and look like one; launch.sh and the Library entry too */
	snprintf(p, sizeof p, "%s/scribe", new_dir);
	char magic[4] = { 0 };
	int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0 || read(fd, magic, 4) != 4 || memcmp(magic, "\177ELF", 4)) {
		if (fd >= 0) close(fd);
		SETERR("the bundle has no app");
		return -1;
	}
	close(fd);
	snprintf(p, sizeof p, "%s/launch.sh", new_dir);
	if (!exists(p)) { SETERR("the bundle has no launch.sh"); return -1; }
	char entry[400]; /* the Library entry: inkling.sh, or the same file under its old name scribe.sh */
	snprintf(entry, sizeof entry, "%s/inkling.sh", new_dir);
	if (!exists(entry)) snprintf(entry, sizeof entry, "%s/scribe.sh", new_dir);
	if (!exists(entry)) { SETERR("the bundle has no inkling.sh"); return -1; }
	/* the plan, in install order: the binary last */
	snprintf(p, sizeof p, "%s/licences", new_dir);
	DIR *d = opendir(p);
	if (d) {
		char ld[400];
		snprintf(ld, sizeof ld, "%s/licences", t->usb_dir);
		mkdir(ld, 0755);
		struct dirent *e;
		while ((e = readdir(d)) && n < 30) {
			if (!safe_name(e->d_name, 80)) continue;
			snprintf(st[n].src, sizeof st[n].src, "%s/licences/%s", new_dir, e->d_name);
			snprintf(st[n].dst, sizeof st[n].dst, "%s/licences/%s", t->usb_dir, e->d_name);
			st[n++].mode = 0644;
		}
		closedir(d);
	}
	snprintf(st[n].src, sizeof st[n].src, "%s", entry);
	snprintf(st[n].dst, sizeof st[n].dst, "%s/inkling.sh", t->docs_dir);
	st[n++].mode = 0644;
	snprintf(p, sizeof p, "%s/curl-smtp", new_dir);
	if (exists(p)) {
		snprintf(st[n].src, sizeof st[n].src, "%s", p);
		snprintf(st[n].dst, sizeof st[n].dst, "%s/curl-smtp", t->bin_dir);
		st[n++].mode = 0755;
	}
	snprintf(st[n].src, sizeof st[n].src, "%s/launch.sh", new_dir);
	snprintf(st[n].dst, sizeof st[n].dst, "%s/launch.sh", t->app_dir);
	st[n].keep_prev = 1;
	st[n++].mode = 0755;
	snprintf(st[n].src, sizeof st[n].src, "%s/scribe", new_dir);
	snprintf(st[n].dst, sizeof st[n].dst, "%s/scribe", t->bin_dir);
	st[n].keep_prev = 1;
	st[n++].mode = 0755;

	char tmp[420], prev[420];
	int i;
	/* 1: every new file beside its target (a full disk fails here, before anything changed) */
	for (i = 0; i < n; i++)
		if (stage_step(&st[i]) != 0) { SETERR("can't write %.200s.new (disk full?)", st[i].dst); goto undo; }
	/* 2: the old app and launcher kept for going back (a hard link: no copy needed) */
	for (i = 0; i < n; i++) {
		if (!st[i].keep_prev || !exists(st[i].dst)) continue;
		snprintf(prev, sizeof prev, "%s.prev", st[i].dst);
		unlink(prev);
		if (link(st[i].dst, prev) != 0 && copy_file(st[i].dst, prev, st[i].mode) != 0) {
			SETERR("can't keep %.200s.prev", st[i].dst);
			goto undo;
		}
	}
	sync();
	/* 3: rename into place, the binary last */
	for (i = 0; i < n; i++) {
		snprintf(tmp, sizeof tmp, "%s.new", st[i].dst);
		if (rename(tmp, st[i].dst) != 0) { SETERR("can't replace %.200s", st[i].dst); goto undo; }
		st[i].done = 1;
	}
	upd_migrate_entries(t->docs_dir); /* the entry's old names go now that inkling.sh is there */
	sync();
	return 0;
undo:
	for (int k = 0; k < n; k++) {
		snprintf(tmp, sizeof tmp, "%s.new", st[k].dst);
		if (st[k].staged && !st[k].done) unlink(tmp);
		if (st[k].done && st[k].keep_prev) { /* put the old launcher back: it goes with the old app */
			snprintf(prev, sizeof prev, "%s.prev", st[k].dst);
			if (exists(prev)) rename(prev, st[k].dst);
		}
	}
	sync();
	return -1;
}

/* ---- the rename: the app was called Scribe up to 0.2.x ---- */

/* a file of this app (an entry, installer or uninstaller runs something in /var/local/scribe), not someone else's */
static int ours(const char *p) {
	size_t n = 0;
	char *s = slurp(p, 1L << 20, &n);
	int r = s && memmem(s, n, "/var/local/scribe", 17) != NULL;
	free(s);
	return r;
}

int upd_migrate_entries(const char *docs_dir) {
	static const char *entries[] = { "scribe.sh", "Scribe Claude.sh" };
	static const char *tools[] = { "Install Scribe.sh", "Uninstall Scribe.sh" };
	char p[400], q[400];
	int n = 0;
	snprintf(q, sizeof q, "%s/inkling.sh", docs_dir);
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) {
		snprintf(p, sizeof p, "%s/%s", docs_dir, entries[i]);
		if (!ours(p)) continue;
		if (!exists(q)) { /* the only entry there is: renamed, never just removed */
			if (rename(p, q) == 0) {
				logf_("rename: the Library entry %s is now inkling.sh", entries[i]);
				n++;
			} else logf_("rename: can't rename %s to inkling.sh (%s)", p, strerror(errno));
		} else if (unlink(p) == 0) {
			logf_("rename: removed the old Library entry %s", entries[i]);
			n++;
		}
	}
	for (size_t i = 0; i < sizeof tools / sizeof *tools; i++) {
		snprintf(p, sizeof p, "%s/%s", docs_dir, tools[i]);
		if (ours(p) && unlink(p) == 0) {
			logf_("rename: removed %s from the Library", tools[i]);
			n++;
		}
	}
	return n;
}

int upd_migrate_usb_dir(const char *old_dir, const char *new_dir) {
	struct stat st;
	char a[600], b[600];
	if (lstat(old_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
	if (lstat(new_dir, &st) != 0) {
		if (rename(old_dir, new_dir) != 0) {
			logf_("rename: can't move %s to %s (%s)", old_dir, new_dir, strerror(errno));
			return -1;
		}
		logf_("rename: moved %s to %s", old_dir, new_dir);
		return 1;
	}
	snprintf(a, sizeof a, "%s/config.txt", new_dir);
	snprintf(b, sizeof b, "%s/notebooks", new_dir);
	if (!S_ISDIR(st.st_mode) || exists(a) || exists(b)) {
		logf_("rename: %s and %s are both there: using %s, the old one is left alone", old_dir, new_dir, new_dir);
		return 2;
	}
	/* new holds only the install files of a new zip (no settings or notebooks yet): what it lacks moves over. The
	 * names are read first, then moved: the folder isn't read while it changes. */
	static char names[64][256];
	int n = 0, rc = 1;
	DIR *d = opendir(old_dir);
	if (!d) return -1;
	struct dirent *e;
	while ((e = readdir(d)) && n < 64) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || strlen(e->d_name) >= sizeof names[0]) continue;
		snprintf(names[n++], sizeof names[0], "%s", e->d_name);
	}
	closedir(d);
	for (int i = 0; i < n; i++) {
		snprintf(a, sizeof a, "%s/%s", old_dir, names[i]);
		snprintf(b, sizeof b, "%s/%s", new_dir, names[i]);
		if (exists(b)) continue;
		if (rename(a, b) != 0) {
			logf_("rename: can't move %s to %s (%s)", a, b, strerror(errno));
			rc = -1;
		}
	}
	/* what's left of the old install files: the new zip brought its own */
	static const char *superseded[] = { "install", "licences" };
	for (size_t i = 0; i < sizeof superseded / sizeof *superseded; i++) {
		snprintf(a, sizeof a, "%s/%s", old_dir, superseded[i]);
		if (lstat(a, &st) == 0 && S_ISDIR(st.st_mode)) upd_rm_tree(a);
	}
	if (rmdir(old_dir) != 0) {
		logf_("rename: %s is left: it still holds files that %s has too", old_dir, new_dir);
		return rc < 0 ? -1 : 2;
	}
	logf_("rename: moved the files of %s into %s", old_dir, new_dir);
	return rc;
}

/* ---- GitHub ---- */

static int https_url(const char *u) { return u && !strncmp(u, "https://", 8) && !strpbrk(u, "\"\\\r\n \t"); }

int upd_release_parse(UpdCtx *c, const char *json) {
	c->nassets = 0;
	c->manifest_i = c->sig_i = -1;
	c->latest[0] = 0;
	cJSON *r = cJSON_Parse(json);
	const char *tag = cJSON_GetStringValue(cJSON_GetObjectItem(r, "tag_name"));
	if (!tag || strlen(tag) >= sizeof c->latest) { cJSON_Delete(r); return -1; }
	strip_v(c->latest, sizeof c->latest, tag);
	cJSON *as, *list = cJSON_GetObjectItem(r, "assets");
	cJSON_ArrayForEach(as, list) {
		if (c->nassets >= UPD_MAX_ASSETS) break;
		const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(as, "name"));
		const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(as, "url"));
		const char *dl = cJSON_GetStringValue(cJSON_GetObjectItem(as, "browser_download_url"));
		cJSON *sz = cJSON_GetObjectItem(as, "size");
		if (!name || strlen(name) >= sizeof c->assets[0].name) continue;
		UpdAsset *a = &c->assets[c->nassets++];
		memset(a, 0, sizeof *a);
		snprintf(a->name, sizeof a->name, "%s", name);
		if (https_url(url) && strlen(url) < sizeof a->url) snprintf(a->url, sizeof a->url, "%s", url);
		if (https_url(dl) && strlen(dl) < sizeof a->dl) snprintf(a->dl, sizeof a->dl, "%s", dl);
		a->size = cJSON_IsNumber(sz) ? (long)sz->valuedouble : 0;
	}
	cJSON_Delete(r);
	/* the manifest: <anything>.manifest (the one named after the tag if there are several), then <it>.sig */
	for (int i = 0; i < c->nassets; i++) {
		size_t l = strlen(c->assets[i].name);
		if (l < 9 || strcmp(c->assets[i].name + l - 9, ".manifest")) continue;
		if (c->manifest_i < 0 || strstr(c->assets[i].name, c->latest)) c->manifest_i = i;
	}
	if (c->manifest_i >= 0) {
		char want[140];
		snprintf(want, sizeof want, "%s.sig", c->assets[c->manifest_i].name);
		for (int i = 0; i < c->nassets; i++)
			if (!strcmp(c->assets[i].name, want)) c->sig_i = i;
	}
	return 0;
}

static void progress(UpdCtx *c, int stage, long done, long total) {
	__atomic_store_n(&c->total, total, __ATOMIC_RELAXED);
	__atomic_store_n(&c->done, done, __ATOMIC_RELAXED);
	__atomic_store_n(&c->stage_no, stage, __ATOMIC_RELEASE);
}

/* GET url into out with /usr/bin/curl. The URL goes in through a config file and the headers (the token) through a
 * root-only header file, so neither shows on a command line. follow: follow redirects; otherwise the redirect target
 * comes back in redir. Returns the HTTP status, or -(1000 + curl's exit code). */
static int curl_get(UpdCtx *c, const char *url, const char *hdr, const char *out, long max, int follow, char *redir, size_t rn) {
	if (redir && rn) redir[0] = 0;
	if (!https_url(url)) return -2001;
	char cfg[200], hf[200], ef[200], ua[64], maxs[24], line[1200];
	snprintf(cfg, sizeof cfg, "%s/curl.cfg", c->stage);
	snprintf(hf, sizeof hf, "%s/hdr.txt", c->stage);
	snprintf(ef, sizeof ef, "%s/curl.err", c->stage);
	snprintf(ua, sizeof ua, "Inkling/%s", c->current);
	snprintf(maxs, sizeof maxs, "%ld", max);
	int ln = snprintf(line, sizeof line, "url = \"%s\"\n", url);
	if (ln < 0 || (size_t)ln >= sizeof line || put_file(cfg, line, (size_t)ln) != 0) return -2002;
	if (hdr[0] && put_file(hf, hdr, strlen(hdr)) != 0) { unlink(cfg); return -2002; }
	unlink(out);
	int pfd[2];
	if (pipe(pfd) != 0) { unlink(cfg); unlink(hf); return -1001; }
	const char *argv[40];
	int k = 0;
	argv[k++] = "curl";
	argv[k++] = "-q"; /* no ~/.curlrc */
	argv[k++] = "-sS";
	argv[k++] = "--proto"; argv[k++] = "=https";
	argv[k++] = "--proto-redir"; argv[k++] = "=https";
	argv[k++] = "--max-redirs"; argv[k++] = "5";
	argv[k++] = "--connect-timeout"; argv[k++] = "20";
	argv[k++] = "--max-time"; argv[k++] = "900";
	argv[k++] = "--max-filesize"; argv[k++] = maxs;
	argv[k++] = "-A"; argv[k++] = ua;
	argv[k++] = "-o"; argv[k++] = out;
	argv[k++] = "-w"; argv[k++] = "%{http_code} %{redirect_url}";
	if (follow) argv[k++] = "-L";
	char hat[210];
	snprintf(hat, sizeof hat, "@%s", hf);
	if (hdr[0]) { argv[k++] = "-H"; argv[k++] = hat; }
	argv[k++] = "-K"; argv[k++] = cfg;
	argv[k] = NULL;
	pid_t pid = fork();
	if (pid == 0) {
		dup2(pfd[1], 1);
		close(pfd[0]);
		close(pfd[1]);
		int e = open(ef, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (e >= 0) dup2(e, 2);
		execv(c->curl, (char *const *)argv);
		_exit(127);
	}
	close(pfd[1]);
	int st = 0;
	if (pid < 0) st = -1;
	else
		for (;;) {
			pid_t w = waitpid(pid, &st, WNOHANG);
			if (w == pid || (w < 0 && errno != EINTR)) break;
			struct stat sb;
			if (stat(out, &sb) == 0) __atomic_store_n(&c->done, (long)sb.st_size, __ATOMIC_RELAXED);
			usleep(200000);
		}
	char res[1100] = { 0 };
	size_t got = 0;
	ssize_t r;
	while (got < sizeof res - 1 && (r = read(pfd[0], res + got, sizeof res - 1 - got)) > 0) got += (size_t)r;
	close(pfd[0]);
	unlink(hf);
	unlink(cfg);
	if (pid < 0) return -1001;
	int ex = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
	int http = atoi(res);
	char *sp = strchr(res, ' ');
	if (redir && rn && sp) {
		sp++;
		sp[strcspn(sp, "\r\n")] = 0;
		snprintf(redir, rn, "%s", sp);
	}
	memset(res, 0, sizeof res);
	if (ex != 0 && http == 0) return -(1000 + ex);
	if (ex != 0 && follow) return -(1000 + ex); /* e.g. too big, cut off */
	return http;
}

static void stage_path(UpdCtx *c, char *p, size_t n, const char *name) { snprintf(p, n, "%s/%s", c->stage, name); }

static int prepare_stage(UpdCtx *c) {
	struct stat st;
	if (mkdir(c->stage, 0700) != 0 && errno != EEXIST) return -1;
	if (lstat(c->stage, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
	chmod(c->stage, 0700);
	return 0;
}

static void clean_stage(UpdCtx *c, int all) {
	char p[300];
	const char *names[] = { "bundle.tar", "curl.cfg", "hdr.txt", "release.json", "manifest", "manifest.sig" };
	for (int i = 0; i < (all ? 6 : 3); i++) {
		stage_path(c, p, sizeof p, names[i]);
		unlink(p);
	}
	stage_path(c, p, sizeof p, "new");
	upd_rm_tree(p);
}

/* Downloads a release asset: with a token through the API (needed for a private repo; the token goes to GitHub only,
 * never to the storage host it redirects to), else through its public download link. */
static int fetch_asset(UpdCtx *c, const UpdAsset *as, const char *out, long max) {
	int st;
	if (c->token[0] && as->url[0]) {
		char hdr[400], redir[1024];
		snprintf(hdr, sizeof hdr, "Accept: application/octet-stream\nX-GitHub-Api-Version: 2022-11-28\nAuthorization: Bearer %s\n",
			 c->token);
		st = curl_get(c, as->url, hdr, out, max, 0, redir, sizeof redir);
		memset(hdr, 0, sizeof hdr);
		if (st >= 300 && st < 400) st = https_url(redir) ? curl_get(c, redir, "", out, max, 1, NULL, 0) : -2003;
		memset(redir, 0, sizeof redir);
	} else if (as->dl[0]) st = curl_get(c, as->dl, "", out, max, 1, NULL, 0);
	else st = -2004;
	return st;
}

static UpdAsset *find_asset(UpdCtx *c, const char *name) {
	for (int i = 0; i < c->nassets; i++)
		if (!strcmp(c->assets[i].name, name)) return &c->assets[i];
	return NULL;
}

/* the downloaded manifest, only if its signature checks out */
static int verified_manifest(UpdCtx *c, UpdManifest *m) {
	char mp[300], sp[300], e[100];
	stage_path(c, mp, sizeof mp, "manifest");
	stage_path(c, sp, sizeof sp, "manifest.sig");
	size_t ml = 0, sl = 0;
	char *mt = slurp(mp, 4096, &ml), *sig = slurp(sp, 64, &sl);
	int rc = -1;
	if (mt && sig && sl == 64 && upd_sig_check(c->pubkey, mt, ml, (const uint8_t *)sig) == 0) {
		rc = upd_manifest_parse(mt, ml, m, e, sizeof e);
		if (rc) logf_("update: %s", e);
	}
	free(mt);
	free(sig);
	return rc;
}

__attribute__((format(printf, 2, 3))) static int fail(UpdCtx *c, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(c->msg, sizeof c->msg, fmt, ap);
	va_end(ap);
	logf_("update: %s", c->msg);
	progress(c, UPS_IDLE, 0, 0);
	return UPD_ERR;
}

int upd_check(UpdCtx *c) {
	char p[300], url[300], hdr[400];
	int v[4];
	c->msg[0] = c->version[0] = 0;
	c->nassets = 0;
	c->manifest_i = c->sig_i = -1;
	progress(c, UPS_ASK, 0, 0);
	if (upd_version_parse(c->current, v)) return fail(c, "this build has no version number (%s)", c->current);
	if (prepare_stage(c)) return fail(c, "can't write to %s", c->stage);
	clean_stage(c, 1);
	snprintf(url, sizeof url, "%s/repos/%s/releases/latest", c->api, c->repo);
	int hn = snprintf(hdr, sizeof hdr, "Accept: application/vnd.github+json\nX-GitHub-Api-Version: 2022-11-28\n");
	if (c->token[0]) snprintf(hdr + hn, sizeof hdr - (size_t)hn, "Authorization: Bearer %s\n", c->token);
	stage_path(c, p, sizeof p, "release.json");
	int st = curl_get(c, url, hdr, p, 2L << 20, 1, NULL, 0);
	memset(hdr, 0, sizeof hdr);
	logf_("update: %s -> %d", url, st);
	if (st < 0) return fail(c, "no connection to GitHub - is Wi-Fi on? (%d)", st);
	if (st == 404 && c->token[0]) return fail(c, "no release found in %s (or the token can't read it)", c->repo);
	if (st == 404) return fail(c, "no release found in %s", c->repo);
	if (st == 401) return fail(c, "GitHub rejected the token - check github_token");
	if (st == 403 || st == 429) return fail(c, "GitHub is limiting requests (%d) - try again later", st);
	if (st != 200) return fail(c, "GitHub answered %d", st);
	char *json = slurp(p, 2L << 20, NULL);
	int bad = !json || upd_release_parse(c, json) != 0;
	free(json);
	unlink(p);
	if (bad) return fail(c, "unexpected answer from GitHub");
	if (upd_version_parse(c->latest, v)) return fail(c, "the latest release (%s) has no version number", c->latest);
	if (upd_version_cmp(c->latest, c->current) <= 0) { progress(c, UPS_IDLE, 0, 0); return UPD_LATEST; }
	if (c->manifest_i < 0 || c->sig_i < 0) {
		snprintf(c->msg, sizeof c->msg, "Version %s is out, but only as a USB install (see README)", c->latest);
		progress(c, UPS_IDLE, 0, 0);
		return UPD_USB_ONLY;
	}
	progress(c, UPS_FETCH_SMALL, 0, 0);
	stage_path(c, p, sizeof p, "manifest");
	st = fetch_asset(c, &c->assets[c->manifest_i], p, 4096);
	if (st == 200) {
		stage_path(c, p, sizeof p, "manifest.sig");
		st = fetch_asset(c, &c->assets[c->sig_i], p, 1024);
	}
	if (st != 200) return fail(c, "couldn't download the update's manifest (%d)", st);
	UpdManifest m;
	if (verified_manifest(c, &m)) return fail(c, "the update's signature is not valid - not installing it");
	if (upd_version_cmp(m.version, c->current) <= 0) return fail(c, "the update file is for an older version (%s)", m.version);
	if (upd_version_cmp(m.min_version, c->current) > 0) {
		snprintf(c->msg, sizeof c->msg, "Version %s can't update this one - install it from USB (see README)", m.version);
		progress(c, UPS_IDLE, 0, 0);
		return UPD_USB_ONLY;
	}
	UpdAsset *b = find_asset(c, m.bundle);
	if (!b) return fail(c, "the release has no %s", m.bundle);
	if (b->size && b->size != m.size) return fail(c, "%s on GitHub isn't the signed size", m.bundle);
	snprintf(c->version, sizeof c->version, "%s", m.version);
	progress(c, UPS_IDLE, 0, 0);
	return UPD_AVAILABLE;
}

int upd_install_run(UpdCtx *c) {
	UpdManifest m;
	char bp[300], np[300], hex[65], e[160];
	if (verified_manifest(c, &m)) return fail(c, "the update's signature is not valid - not installing it");
	if (upd_version_cmp(m.version, c->current) <= 0 || upd_version_cmp(m.min_version, c->current) > 0)
		return fail(c, "the update doesn't fit this version");
	UpdAsset *b = find_asset(c, m.bundle);
	if (!b) return fail(c, "the release has no %s", m.bundle);
	stage_path(c, bp, sizeof bp, "bundle.tar");
	stage_path(c, np, sizeof np, "new");
	progress(c, UPS_DOWNLOAD, 0, m.size);
	int st = fetch_asset(c, b, bp, m.size);
	if (st != 200) { clean_stage(c, 0); return fail(c, "download failed (%d)", st); }
	progress(c, UPS_VERIFY, m.size, m.size);
	long size = -1;
	if (upd_sha256_file(bp, hex, &size) || size != m.size || strcmp(hex, m.sha256)) {
		clean_stage(c, 0);
		return fail(c, "the download doesn't match the signed update - not installing it");
	}
	progress(c, UPS_INSTALL, 0, 0);
	if (upd_extract(bp, np, e, sizeof e)) { clean_stage(c, 0); return fail(c, "%s", e); }
	unlink(bp);
	if (upd_install(np, &c->targets, e, sizeof e)) { clean_stage(c, 0); return fail(c, "%s", e); }
	clean_stage(c, 0);
	snprintf(c->version, sizeof c->version, "%s", m.version);
	logf_("update: installed %s (was %s)", m.version, c->current);
	progress(c, UPS_IDLE, 0, 0);
	return UPD_INSTALLED;
}

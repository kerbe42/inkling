/* Self-update from GitHub releases: the parts that don't touch the screen (update_core.c), so the host tests can run
 * them. The app glue (Menu > Check for updates) is in update.c.
 *
 * A release carries, next to the USB zip: a bundle (an uncompressed ustar archive of what the installer installs), a
 * manifest (key=value lines: format, version, min_version, bundle, size, sha256) and the manifest's detached Ed25519
 * signature (64 raw bytes, asset name = manifest name + ".sig"). Nothing is unpacked before the signature checks out
 * against the public key built into the app and the bundle's size and SHA-256 match the signed manifest. */
#ifndef UPDATE_H
#define UPDATE_H

#include <stddef.h>
#include <stdint.h>

void logf_(const char *fmt, ...);

#define UPD_MAX_BUNDLE (64L << 20)
#define UPD_MAX_ASSETS 40

enum { UPD_ERR = -1, UPD_LATEST = 1, UPD_AVAILABLE, UPD_USB_ONLY, UPD_INSTALLED };
enum { UPS_IDLE = 0, UPS_ASK, UPS_FETCH_SMALL, UPS_DOWNLOAD, UPS_VERIFY, UPS_INSTALL }; /* progress stages */

typedef struct {
	int format;
	char version[32], min_version[32], bundle[96], sha256[65];
	long size;
} UpdManifest;

typedef struct {
	char name[128], url[320], dl[480]; /* url: the API asset URL, dl: browser_download_url */
	long size;
} UpdAsset;

typedef struct {
	const char *bin_dir;  /* scribe, curl-smtp */
	const char *app_dir;  /* launch.sh */
	const char *docs_dir; /* inkling.sh, the Library entry */
	const char *usb_dir;  /* licences/ */
} UpdTargets;

typedef struct {
	/* set by the caller before a job */
	char curl[128], api[96], repo[100], token[256], current[32], stage[128];
	uint8_t pubkey[32];
	UpdTargets targets;
	/* results */
	char latest[32];  /* the release's tag, without the v */
	char version[32]; /* the signed manifest's version */
	UpdAsset assets[UPD_MAX_ASSETS];
	int nassets, manifest_i, sig_i;
	char msg[160]; /* why it failed / why USB only, for the status line */
	/* progress: written by the worker, read by the main thread */
	int stage_no;
	long done, total;
} UpdCtx;

/* versions "1.2.3" (up to 4 numbers, an optional leading v) */
int upd_version_parse(const char *s, int v[4]);   /* 0 ok, -1 not a version */
int upd_version_cmp(const char *a, const char *b); /* -1, 0, 1 (an invalid version sorts lowest) */

int upd_hex_decode(const char *hex, uint8_t *out, size_t n); /* exactly 2n hex digits; 0 ok */

int upd_manifest_parse(const char *text, size_t len, UpdManifest *m, char *err, size_t en); /* 0 ok */
int upd_sig_check(const uint8_t pub[32], const void *msg, size_t len, const uint8_t sig[64]); /* 0 = valid */

void upd_sha256(const void *data, size_t n, uint8_t out[32]);
int upd_sha256_file(const char *path, char hex[65], long *size); /* 0 ok */

/* Unpacks the files the installer knows (scribe, launch.sh, inkling.sh, scribe.sh, VERSION, curl-smtp,
 * licences/<name>) from a ustar archive into a new empty dir; refuses links, devices, unsafe names and bad headers.
 * scribe.sh is the Library entry under its name up to 0.2.x: bundles carry it too, for those versions' updaters. 0 ok. */
int upd_extract(const char *tar_path, const char *dest_dir, char *err, size_t en);
/* Installs an unpacked bundle: every file is first written next to its target as <name>.new, then renamed into
 * place, the binary last; the old scribe and launch.sh stay as scribe.prev / launch.sh.prev. The Library entry goes
 * in as inkling.sh (from the bundle's inkling.sh, else its scribe.sh), then upd_migrate_entries. 0 ok. */
int upd_install(const char *new_dir, const UpdTargets *t, char *err, size_t en);
void upd_rm_tree(const char *dir); /* removes dir and what's in it (two levels deep at most) */

/* The rename: the app was called Scribe up to 0.2.x. Both run at every start (main.c), cheap when there's nothing to do.
 * upd_migrate_usb_dir: the USB folder moves once (same file system: a rename). new missing: old is renamed to new.
 * Both there and new not set up yet (no config.txt or notebooks: only the install files of a new zip were copied):
 * what new lacks moves over and old's superseded install/ and licences/ go. Otherwise old is left alone (logged).
 * Returns 0 nothing to do, 1 moved, 2 old left (both there), -1 something couldn't be moved. */
int upd_migrate_usb_dir(const char *old_dir, const char *new_dir);
/* upd_migrate_entries: the old Library entry (scribe.sh, before that "Scribe Claude.sh") becomes inkling.sh if that's
 * missing (an updater up to 0.2.x installs the new entry under the old name), else it's removed; "Install Scribe.sh"
 * and "Uninstall Scribe.sh" are removed. Only files of this app (they run something in /var/local/scribe). Returns
 * how many files it changed. */
int upd_migrate_entries(const char *docs_dir);

/* the GitHub release JSON: tag + assets; finds the manifest (name ends in .manifest) and its .sig */
int upd_release_parse(UpdCtx *c, const char *json); /* 0 ok */

/* worker side (each returns an UPD_* code; c->msg says why on UPD_ERR / UPD_USB_ONLY) */
int upd_check(UpdCtx *c);
int upd_install_run(UpdCtx *c);

#endif

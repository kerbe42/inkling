/* Menu > Check for updates. The first tap asks GitHub for the latest release (in the network worker, so the page stays
 * usable); if it is newer and its signed manifest checks out, the second tap downloads the bundle, verifies it,
 * installs it (update_core.c) and the app exits with UPDATE_EXIT_CODE so launch.sh starts the new version. Any failure
 * leaves the running install as it was. The repo is config.txt's update_repo (default below); a private repo needs a
 * read-only token in the key store (github_token, or a file github-token.txt in the inkling folder). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "scribe.h"
#include "update.h"
#include "update_pubkey.h"

#ifndef SCRIBE_VERSION
#error "SCRIBE_VERSION is missing: build with device/app/build.sh (it passes device/release/VERSION)"
#endif

#define UPDATE_STAGE "/var/local/scribe/update"
#define UPDATE_DEFAULT_REPO "kerbe42/inkling"
#define TOKEN_DROP USB_DIR "/github-token.txt"

enum { U_IDLE, U_CHECKING, U_LATEST, U_AVAILABLE, U_USB, U_INSTALLING, U_FAILED, U_DONE };
static UpdCtx ctx; /* the worker's while a job runs (net_busy) */
static int ustate, restart;
static char avail[32], lastmsg[160], desc[200]; /* main thread copies */
static int shown_stage = -1;
static long shown_tenth = -1;

static int job_check(void *arg, char **body, size_t *len) {
	*body = NULL;
	*len = 0;
	return upd_check(arg);
}

static int job_install(void *arg, char **body, size_t *len) {
	*body = NULL;
	*len = 0;
	return upd_install_run(arg);
}

static int name_chars(const char *s, size_t n, const char *extra) {
	if (!n) return 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(extra, c))) return 0;
	}
	return 1;
}

/* "owner/name" */
static int valid_repo(const char *s) {
	const char *sl = strchr(s, '/');
	if (!sl || strchr(sl + 1, '/') || s[0] == '.' || sl[1] == '.' || strlen(s) >= sizeof ctx.repo) return 0;
	return name_chars(s, (size_t)(sl - s), "-_.") && name_chars(sl + 1, strlen(sl + 1), "-_.");
}

static int valid_token(const char *t) {
	size_t n = strlen(t);
	return n >= 10 && n < sizeof ctx.token && name_chars(t, n, "-_.");
}

static void trim(char *s) {
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
	size_t i = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	if (i) memmove(s, s + i, strlen(s + i) + 1);
}

/* update_repo from config.txt, else the default */
static void read_repo(char *out, size_t n) {
	snprintf(out, n, "%s", UPDATE_DEFAULT_REPO);
	FILE *f = fopen(USB_DIR "/config.txt", "r");
	if (!f) return;
	char line[300];
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		if (line[0] == '#' || !eq) continue;
		*eq = 0;
		trim(line);
		if (strcmp(line, "update_repo")) continue;
		char *v = eq + 1;
		trim(v);
		if (valid_repo(v)) snprintf(out, n, "%s", v);
		else if (v[0]) logf_("update: config.txt update_repo \"%.80s\" isn't owner/name, using %s", v, UPDATE_DEFAULT_REPO);
	}
	fclose(f);
}

/* A token copied over USB as github-token.txt goes into the key store, and the file is deleted ("delete" removes it). */
static void import_token_file(App *a) {
	int fd = open(TOKEN_DROP, O_RDONLY | O_NOFOLLOW);
	if (fd < 0) return;
	char t[400] = { 0 };
	ssize_t r = read(fd, t, sizeof t - 1);
	close(fd);
	unlink(TOKEN_DROP);
	t[r > 0 ? r : 0] = 0;
	trim(t);
	if (!strcmp(t, "delete")) { key_set("github_token", ""); ui_set_status(a, "GitHub token removed"); }
	else if (valid_token(t)) { key_set("github_token", t); logf_("update: GitHub token saved from github-token.txt"); }
	else ui_set_status(a, "github-token.txt didn't hold a token - not saved");
	memset(t, 0, sizeof t);
}

static void fill_ctx(void) {
	snprintf(ctx.curl, sizeof ctx.curl, "/usr/bin/curl");
	snprintf(ctx.api, sizeof ctx.api, "https://api.github.com");
	read_repo(ctx.repo, sizeof ctx.repo);
	const char *t = key_get("github_token");
	if (t[0] && !valid_token(t)) logf_("update: the stored github_token has unexpected characters, not using it");
	snprintf(ctx.token, sizeof ctx.token, "%s", valid_token(t) ? t : "");
	snprintf(ctx.current, sizeof ctx.current, "%s", SCRIBE_VERSION);
	snprintf(ctx.stage, sizeof ctx.stage, "%s", UPDATE_STAGE);
	upd_hex_decode(UPDATE_PUBKEY_HEX, ctx.pubkey, 32);
	ctx.targets = (UpdTargets){ BIN_DIR, APP_DIR, DOCS_DIR, USB_DIR };
}

void update_tap(App *a) {
	if (ustate == U_CHECKING || ustate == U_INSTALLING) {
		ui_set_status(a, ustate == U_CHECKING ? "Still checking for updates..." : "Still installing the update...");
		return;
	}
	if (ustate == U_DONE) return;
	if (net_busy()) { ui_set_status(a, "Busy with another request - tap again in a moment"); return; }
	import_token_file(a);
	fill_ctx();
	int install = ustate == U_AVAILABLE;
	if (net_submit_fn(install ? job_install : job_check, &ctx, UPDATE_NET_KIND) != 0) {
		memset(ctx.token, 0, sizeof ctx.token);
		ui_set_status(a, "Busy with another request - tap again in a moment");
		return;
	}
	ustate = install ? U_INSTALLING : U_CHECKING;
	shown_stage = -1;
	if (install) ui_set_status(a, "Downloading Inkling %s...", avail);
	else ui_set_status(a, "Checking for updates...");
}

void update_result(App *a, int status, char *body) {
	(void)body;
	memset(ctx.token, 0, sizeof ctx.token);
	snprintf(lastmsg, sizeof lastmsg, "%s", ctx.msg);
	if (ustate == U_CHECKING) {
		if (status == UPD_LATEST) {
			ustate = U_LATEST;
			ui_set_status(a, "Inkling %s is the latest", SCRIBE_VERSION);
		} else if (status == UPD_AVAILABLE) {
			ustate = U_AVAILABLE;
			snprintf(avail, sizeof avail, "%s", ctx.version);
			ui_set_status(a, "Version %s is available - tap again to install", avail);
		} else if (status == UPD_USB_ONLY) {
			ustate = U_USB;
			ui_set_status(a, "%s", lastmsg);
		} else {
			ustate = U_FAILED;
			ui_set_status(a, "Update check failed: %s", lastmsg);
		}
	} else if (ustate == U_INSTALLING) {
		if (status == UPD_INSTALLED) {
			ustate = U_DONE;
			restart = 1;
			ui_set_status(a, "Inkling %s installed - restarting...", avail);
			a->quit = 1; /* main() returns UPDATE_EXIT_CODE; launch.sh starts the new version */
		} else {
			ustate = U_FAILED;
			ui_set_status(a, "Update not installed: %s", lastmsg);
		}
	}
}

void update_tick(App *a) {
	if (ustate != U_CHECKING && ustate != U_INSTALLING) return;
	int sn = __atomic_load_n(&ctx.stage_no, __ATOMIC_ACQUIRE);
	long done = __atomic_load_n(&ctx.done, __ATOMIC_RELAXED), total = __atomic_load_n(&ctx.total, __ATOMIC_RELAXED);
	long tenth = done / 104858; /* 0.1 MB steps */
	if (sn == shown_stage && tenth == shown_tenth) return;
	shown_stage = sn;
	shown_tenth = tenth;
	if (sn == UPS_FETCH_SMALL) ui_set_status(a, "Checking the update's signature...");
	else if (sn == UPS_DOWNLOAD && total > 0)
		ui_set_status(a, "Downloading Inkling %s: %.1f of %.1f MB", avail, done / 1048576.0, total / 1048576.0);
	else if (sn == UPS_VERIFY) ui_set_status(a, "Verifying the download...");
	else if (sn == UPS_INSTALL) ui_set_status(a, "Installing Inkling %s...", avail);
}

const char *update_row_desc(App *a) {
	(void)a;
	switch (ustate) {
	case U_CHECKING: return "Checking GitHub for a newer version...";
	case U_LATEST: snprintf(desc, sizeof desc, "Inkling %s is the latest version.", SCRIBE_VERSION); break;
	case U_AVAILABLE: snprintf(desc, sizeof desc, "Version %s is available - tap to install it.", avail); break;
	case U_INSTALLING: snprintf(desc, sizeof desc, "Installing version %s...", avail); break;
	case U_DONE: snprintf(desc, sizeof desc, "Version %s installed - restarting.", avail); break;
	case U_USB: snprintf(desc, sizeof desc, "%s", lastmsg); break;
	case U_FAILED: snprintf(desc, sizeof desc, "Last try failed: %s", lastmsg); break;
	default: snprintf(desc, sizeof desc, "You have Inkling %s. Tap to look for a newer version.", SCRIBE_VERSION); break;
	}
	return desc;
}

int update_restart_pending(void) { return restart; }

/* scribe (the Inkling app): takes over the screen and pen, draws ink live, sends each turn's page to the AI service
 * you chose (brain.c), and plays back the reply. Exit gives the screen back.
 * Build: device/app/build.sh. Launch: device/app/launch.sh (from the library via the shell integration). */
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "scribe.h"
#include "update.h"

enum { K_AI = 1, K_EMAIL, K_KEYCHECK, K_DRAW };

static App app;
static volatile sig_atomic_t g_quit;
static int send_requested, sent_count, turn_inflight;
static double turn_started, last_tick;
static AiJob *ai_job, *email_job, *draw_job;
/* the drawing pass: what it draws, for which page, and a finished drawing waiting for you to be back on the page */
static char draw_desc[480];
static int draw_page, have_drawn;
static double draw_started;
static Reply drawn;
static KeyCheck *keycheck_job;
static int want_turn; /* page turn waiting for the current answer: +1 next, -1 previous, 2 new */
/* Early sending: a turn goes out 0.8 s after the pen lifts (the network time hides inside your pause); writing
 * again before its answer is on the page cancels it, and an answer that comes back before your pause is over
 * waits until then. */
#define EARLY_MS 800
static int turn_early, turn_stale, have_held;
static double answer_not_before, pen_up_t;
static Reply held;
static AiJob *held_job;
static void idle_timer_reset(void);
static void schedule_send(App *a);
#ifdef INKLING_DEV
static int capturing(void);
#endif
static void cancel_early(App *a);

double mono_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

void logf_(const char *fmt, ...) {
	char ts[32];
	struct timespec rt;
	clock_gettime(CLOCK_REALTIME, &rt);
	time_t now = rt.tv_sec;
	strftime(ts, sizeof ts, "%H:%M:%S", localtime(&now));
	fprintf(stderr, "%s.%03ld ", ts, rt.tv_nsec / 1000000);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

static void on_signal(int s) { (void)s; g_quit = 1; }
static volatile sig_atomic_t g_force_send; /* send a turn even with no new strokes (also SIGUSR1 in dev builds) */
#ifdef INKLING_DEV /* developer hooks: dev builds only (device/app/build.sh --dev), never in a release */
static void on_usr1(int s) { (void)s; g_force_send = 1; }
static volatile sig_atomic_t g_toggle_settings; /* SIGUSR2: developer hook, open/close the settings page */
static void on_usr2(int s) { (void)s; g_toggle_settings = 1; }
#endif

void state_save(App *a) {
	FILE *f = fopen(APP_DIR "/state.tmp", "w");
	if (!f) return;
	fprintf(f, "page=%d\nlast_page=%d\nmode=%s\n", a->page, a->last_page, a->mode_id[a->mode]);
	fclose(f);
	rename(APP_DIR "/state.tmp", APP_DIR "/state");
}

static void state_load(App *a) {
	FILE *f = fopen(APP_DIR "/state", "r");
	if (!f) return;
	char line[128], m[24];
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "page=%d", &a->page) == 1) continue;
		if (sscanf(line, "last_page=%d", &a->last_page) == 1) continue;
		if (sscanf(line, "mode=%23s", m) == 1)
			for (int i = 0; i < a->nmodes; i++)
				if (!strcmp(a->mode_id[i], m)) a->mode = i;
	}
	fclose(f);
}

static void default_modes(App *a) {
	const char *ids[] = { "talk", "notes", "math", "translate", "auto" };
	const char *labels[] = { "Chat", "Type up", "Math", "Translate", "Auto" };
	const int as[] = { a->pause_ms, a->pause_ms, a->pause_ms, a->pause_ms, a->pause_ms };
	a->nmodes = 5;
	for (int i = 0; i < 5; i++) {
		snprintf(a->mode_id[i], sizeof a->mode_id[i], "%s", ids[i]);
		snprintf(a->mode_label[i], sizeof a->mode_label[i], "%s", labels[i]);
		a->autosend_ms[i] = as[i];
	}
}

static int open_input(const char *dev, int grab) {
	int fd = open(dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) { logf_("open %s: %s", dev, strerror(errno)); return -1; }
	if (grab && ioctl(fd, EVIOCGRAB, 1) < 0) logf_("grab %s: %s", dev, strerror(errno));
	return fd;
}

static void send_turn(App *a) {
	if (a->provider == PROV_NONE) {
		ui_set_status(a, "Add your API key to inkling/config.txt (USB storage), then reopen the app");
		send_requested = 0;
		a->autosend_at = 0;
		return;
	}
	AiJob *j = brain_prepare(a);
	int early = !send_requested && mono_ms() < answer_not_before;
	if (net_submit_fn(brain_run, j, K_AI) == 0) {
		ai_job = j;
		sent_count = a->npending;
		turn_inflight = 1;
		turn_early = early;
		turn_stale = 0;
		turn_started = mono_ms();
		last_tick = turn_started;
		a->autosend_at = 0;
		send_requested = 0;
		if (!early) ui_set_status(a, "thinking..."); /* early: quiet until your pause is over */
	} else brain_job_free(j);
}

/* After the pen lifts: send early (API providers), the answer may show once the pause is over. */
static void schedule_send(App *a) {
	int ms = a->autosend_ms[a->mode];
	double now = mono_ms();
	pen_up_t = now;
#ifdef INKLING_DEV
	if (capturing()) { a->autosend_at = 0; answer_not_before = 0; return; } /* recording handwriting: no AI */
#endif
	if (ms <= 0 || !a->npending) { a->autosend_at = 0; answer_not_before = 0; return; }
	answer_not_before = now + ms;
	a->autosend_at = now + (ms < EARLY_MS ? ms : EARLY_MS);
}

/* You're writing again (or opening a menu): an early turn whose answer isn't on the page yet is dropped. */
static void cancel_early(App *a) {
	if (turn_inflight && turn_early && !turn_stale) { turn_stale = 1; brain_cancel(); logf_("early turn cancelled: you kept writing"); }
	if (have_held) {
		free(held.reply); free(held.slen); free(held.dpts);
		memset(&held, 0, sizeof held);
		brain_job_free(held_job);
		held_job = NULL;
		have_held = 0;
		logf_("early answer dropped: you kept writing");
	}
	(void)a;
}

static void goto_page(App *a, int n) {
	int is_new = n > a->last_page;
	page_save(a);
	pending_clear(a);
	a->autosend_at = 0;
	a->page = n;
	if (n > a->last_page) a->last_page = n;
	page_load(a, n);
	state_save(a);
	if (is_new) brain_new_page(a);
	ui_set_status(a, "page %d of %d%s", a->page, a->last_page, is_new ? " (new)" : "");
	logf_("page %d of %d", a->page, a->last_page);
}

/* dir: +1 next (or a new page after the last), -1 previous, 2 new page at the end */
static void request_turn(App *a, int dir) {
	if (dir == -1 && a->page <= 1) { ui_set_status(a, "this is the first page"); return; }
	if (dir == 1 && a->page >= a->last_page && page_is_blank(a)) { ui_set_status(a, "this is a fresh page"); return; }
	want_turn = dir;
	if (a->npending) send_requested = 1; /* answer what's on this page first */
	if (net_busy() || a->nops || a->npending) ui_set_status(a, "turning the page after the answer...");
}

static void do_turn(App *a) {
	int dir = want_turn;
	want_turn = 0;
	if (dir == 2) goto_page(a, a->last_page + 1);
	else if (dir == -1 && a->page > 1) goto_page(a, a->page - 1);
	else if (dir == 1) goto_page(a, a->page + 1);
}

#ifdef INKLING_DEV /* (SIGUSR2 without a devtap file) */
static void toggle_settings(App *a) {
	if (a->in_settings) { settings_close(a); ui_layout(a); ui_draw_bar(a); return; }
	if (net_busy() || a->nops) { ui_set_status(a, "wait for the answer to finish"); return; }
	page_save(a);
	settings_open(a);
	ui_layout(a);
	ui_draw_bar(a);
}
#endif

void app_request_turn(App *a, int dir) { request_turn(a, dir); }

/* One line in the status bar saying what the current mode does. */
void app_explain_mode(App *a) {
	const char *m = a->mode_id[a->mode];
	if (!strcmp(m, "talk")) ui_set_status(a, "Chat: write anything - the AI writes back to you in handwriting");
	else if (!strcmp(m, "notes")) ui_set_status(a, "Type up: your handwriting becomes neat typed notes (lists, tables, headings)");
	else if (!strcmp(m, "math")) ui_set_status(a, "Math: write a problem or your working - it checks it step by step");
	else if (!strcmp(m, "translate")) ui_set_status(a, "Translate: write anything - it writes it back in %s (tap a mode to stop)", a->translate_to);
	else if (!strcmp(m, "auto")) ui_set_status(a, "Auto: the AI decides what your writing needs");
}

static void close_any(App *a) {
	if (a->in_settings) settings_close(a);
	else if (a->in_pagemenu) pagemenu_close(a);
	else if (a->in_games) games_close(a);
	else if (a->in_menu) menu_close(a);
	ui_layout(a);
	ui_draw_bar(a);
}

/* Actions reached from the Menu panel: 2 page menu, 3 exit, 4 settings, 5 games. */
void app_action(App *a, int kind) {
	switch (kind) {
	case 2: pagemenu_open(a); break;
	case 3: a->quit = 1; break;
	case 4: page_save(a); settings_open(a); break;
	case 5: games_open_menu(a); break;
	}
}

/* Games, from the mode picker: the games list (from a game too). */
void app_open_games(App *a) {
	if (a->in_games == 2) games_open_menu(a); /* leaving a game for the list (it is saved if it can resume) */
	else if (!a->in_games) { cancel_early(a); reply_finish(a); games_open_menu(a); }
	ui_layout(a);
	ui_draw_bar(a);
}

/* Chosen in the mode picker. A mode change ends an AI game; picked from the games, it goes back to the page. */
void app_set_mode(App *a, int k) {
	if (a->in_games) { games_close(a); ui_layout(a); }
	a->game_prompt = NULL;
	if (a->mode != k) {
		a->mode = k;
		state_save(a);
		logf_("mode %s", a->mode_id[a->mode]);
	}
	ui_draw_bar(a);
	app_explain_mode(a);
	if (a->npending && !turn_inflight) schedule_send(a); /* what you wrote is answered in the new mode */
}

/* Send now: answer what's new, or read the whole page again. */
static void send_now(App *a) {
	if (a->npending) send_requested = 1;
	else if (!page_is_blank(a)) g_force_send = 1; /* re-read the whole page */
	else ui_set_status(a, "nothing new to send");
}

/* Email this page: the AI types it up, Resend sends it to you with a picture of the page. */
static void email_page(App *a) {
	if (a->provider == PROV_NONE) { ui_set_status(a, "email needs an AI key (see README)"); return; }
	if (page_is_blank(a)) { ui_set_status(a, "nothing on this page to email"); return; }
	const char *missing = brain_email_missing(a);
	if (missing) { ui_set_status(a, "%s", missing); return; }
	AiJob *j = brain_prepare_email(a);
	if (!j) { ui_set_status(a, "set up email first: Menu > Settings > Email"); return; }
	if (net_submit_fn(brain_email_run, j, K_EMAIL) != 0) { brain_job_free(j); ui_set_status(a, "busy - try again in a moment"); return; }
	email_job = j;
	logf_("email: page %d to %s", a->page, brain_email_to(a));
	ui_set_status(a, "writing the email for page %d...", a->page);
}

/* AI & keys > Test key (and after a key is saved): a free authenticated call, in the network worker. */
void app_key_check(App *a, const char *provider) {
	const Provider *p = provider_find(provider);
	if (!p) return;
	if (net_busy()) { ui_set_status(a, "busy - test the key again in a moment"); return; }
	KeyCheck *j = brain_key_check_prepare(a, provider, key_get(p->key_name));
	if (!j) { ui_set_status(a, "no %s key to test", p->short_name); return; }
	if (net_submit_fn(brain_key_check_run, j, K_KEYCHECK) != 0) { brain_key_check_free(j); ui_set_status(a, "busy - try again"); return; }
	keycheck_job = j;
	ui_set_status(a, "checking the %s key...", p->short_name);
}

void app_dropdown_action(App *a, int kind, const char *id) {
	logf_("%s: %s", kind == 2 ? "clear" : kind == 3 ? "menu" : "send", id);
	if (kind == 2) {
		if (!strcmp(id, "page")) { reply_free(a); page_clear(a); ui_set_status(a, "page %d cleared - the conversation is kept", a->page); }
		else if (!strcmp(id, "forget")) conversation_forget(a);
		else if (!strcmp(id, "both")) { reply_free(a); page_clear(a); conversation_forget(a); ui_set_status(a, "a blank page and a fresh conversation"); }
		else if (!strcmp(id, "delete")) { reply_free(a); page_delete(a); }
		else if (!strcmp(id, "startover")) { reply_free(a); notebook_start_over(a); }
	} else if (kind == 3) {
		if (!strcmp(id, "newpage")) request_turn(a, 2);
		else if (!strcmp(id, "settings")) { page_save(a); settings_open(a); ui_layout(a); ui_draw_bar(a); }
		else if (!strcmp(id, "exit")) a->quit = 1;
		else if (!strcmp(id, "update")) update_tap(a);
	} else if (kind == 4) {
		if (!strcmp(id, "now")) send_now(a);
		else if (!strcmp(id, "email")) email_page(a);
	}
}

/* A top-bar button was tapped (pen or finger). */
static void activate(App *a, int i) {
	if (i < 0 || i >= a->nbuttons) return;
	Button *b = &a->buttons[i];
	if (a->in_modes) { /* any bar tap closes an open drop-down; its own button only closes it */
		int was = a->in_modes;
		modes_close(a);
		if ((b->kind == 9 && was == 1) || (b->kind == 8 && was == 2) || (b->kind == 6 && was == 3) || (b->kind == 1 && was == 4)) return;
	}
	if (b->kind == 9) { /* the mode picker */
		if (a->in_menu || a->in_settings || a->in_pagemenu) { ui_set_status(a, "tap Close to go back to your page"); return; }
		if (turn_early) cancel_early(a);
		if (net_busy() && !turn_stale) { ui_set_status(a, "wait for the answer to finish"); return; }
		reply_finish(a);
		modes_open(a);
		ui_draw_button(a, i);
		return;
	}
	int sub = a->in_menu || a->in_settings || a->in_games || a->in_pagemenu;
	if (b->kind == 6) { /* Menu / Close */
		if (!sub && turn_early) cancel_early(a); /* asked again when you come back to the page */
		if (sub) { close_any(a); if (a->npending && !turn_inflight) schedule_send(a); }
		else if (net_busy() && !turn_stale) ui_set_status(a, "wait for the answer to finish");
		else { reply_finish(a); menupick_open(a, b->x + b->w); ui_draw_button(a, i); } /* New page, Settings, Exit */
		return;
	}
	if (b->kind == 5) { /* Games */
		if (a->in_games == 2) games_open_menu(a);
		else if (a->in_games == 1) games_close(a);
		else if (sub) { ui_set_status(a, "tap Close first"); return; }
		else if (net_busy() && !(turn_early && (cancel_early(a), 1))) { ui_set_status(a, "wait for the answer to finish"); return; }
		else { reply_finish(a); games_open_menu(a); }
		ui_layout(a);
		ui_draw_bar(a);
		return;
	}
	if (sub) { ui_set_status(a, "tap Close to go back to your page"); return; }
	switch (b->kind) {
	case 0:
		app_set_mode(a, b->arg);
		break;
	case 8: /* Clear: a drop-down (clear the page, forget the conversation, both, delete the page, start over) */
		if (turn_early) cancel_early(a);
		if (net_busy() && !turn_stale) { ui_set_status(a, "wait for the answer to finish"); break; }
		reply_finish(a);
		clearpick_open(a, b->x + b->w);
		ui_draw_button(a, i);
		break;
	case 7:
		a->erase_mode = !a->erase_mode;
		ui_draw_button(a, i);
		ui_set_status(a, a->erase_mode ? "Eraser on: the pen rubs out. Tap Erase again to write." : "Eraser off: the pen writes again");
		break;
	case 1: /* Send: Send now, Email this page */
		if (turn_early) cancel_early(a);
		if (net_busy() && !turn_stale) { ui_set_status(a, "wait for the answer to finish"); break; }
		reply_finish(a);
		sendpick_open(a, b->x + b->w);
		ui_draw_button(a, i);
		break;
	}
}

#ifdef INKLING_DEV
/* Developer taps (devtap) go straight to the button under the point. */
static void tap(App *a, int x, int y) { activate(a, ui_hit(a, x, y)); }
#endif

/* Top bar, pen or finger: the press highlights the button under it and the release acts on that same button, so
 * where the tip or finger happens to lift doesn't matter. A second activation of the same button within 350 ms is
 * a bouncing pen tip (it would open the menu and close it again at once) and is ignored. */
static void bar_press(App *a, int x, int y) {
	if (a->pressed >= 0) ui_draw_button(a, a->pressed); /* a press that never got its release */
	a->pressed = ui_hit(a, x, y);
	ui_press(a, a->pressed);
}

static void bar_release(App *a, int moved, const char *by) {
	int i = a->pressed;
	a->pressed = -1;
	if (i < 0) { logf_("bar: %s tap between buttons", by); return; }
	if (moved) { ui_draw_button(a, i); logf_("bar: %s slid off %s, ignored", by, a->buttons[i].text); return; }
	if (i == a->last_act_btn && a->ev_t - a->last_act_t < 350) { ui_draw_button(a, i); logf_("bar: %s double tap on %s, ignored", by, a->buttons[i].text); return; }
	a->last_act_btn = i;
	a->last_act_t = a->ev_t;
	logf_("bar: %s %s", by, a->buttons[i].text);
	/* the button stays dark from the press until its action redraws it (Clear armed stays dark, a menu redraws the
	 * bar); only if nothing did is it put back to normal - no light flash in between */
	int before = ui_redraws;
	activate(a, i);
	if (ui_redraws == before && i < a->nbuttons) ui_draw_button(a, i);
}

static void pen_report(App *a, int prev_touch);

#ifdef INKLING_DEV
/* Developer hook for scripted demos: SIGUSR2 with SECURE_DIR/devink present replays its strokes as pen input
 * ("x y" per line in screen px, a blank line ends a stroke), at handwriting speed. Lines "x y t p" (t = ms from
 * the start, p = pressure) replay recorded handwriting with its own timing and pressure (see the capture hook). */
static struct {
	int active, n, i, down, timed;
	short *x, *y;
	int *t;
	unsigned short *p;
	unsigned char *end; /* 1 = last point of a stroke */
	double next, start;
} rp;

/* Capture hook: while SECURE_DIR/capture exists, the strokes written on the page are logged to SECURE_DIR/captured.txt
 * as "x y t p" lines (t = ms since the first point), a blank line after each stroke, "# page N" when the page
 * changes; nothing is sent to the AI meanwhile. For recording a real person's handwriting for the demo. */
static FILE *cap_f;
static double cap_t0;
static int cap_page = -1;
static int capturing(void) { return access(SECURE_DIR "/capture", F_OK) == 0; }

static void replay_load(void) {
	FILE *f = fopen(SECURE_DIR "/devink", "r");
	if (!f) return;
	int cap = 4096;
	rp.x = realloc(rp.x, (size_t)cap * sizeof *rp.x);
	rp.y = realloc(rp.y, (size_t)cap * sizeof *rp.y);
	rp.end = realloc(rp.end, (size_t)cap);
	rp.t = realloc(rp.t, (size_t)cap * sizeof *rp.t);
	rp.p = realloc(rp.p, (size_t)cap * sizeof *rp.p);
	rp.n = 0;
	rp.timed = 0;
	char line[96];
	while (fgets(line, sizeof line, f)) {
		int x, y, t = 0, pr = 1900;
		if (line[0] == '#') continue;
		int k = sscanf(line, "%d %d %d %d", &x, &y, &t, &pr);
		if (k >= 2) {
			if (rp.n == cap) {
				cap *= 2;
				rp.x = realloc(rp.x, (size_t)cap * sizeof *rp.x);
				rp.y = realloc(rp.y, (size_t)cap * sizeof *rp.y);
				rp.t = realloc(rp.t, (size_t)cap * sizeof *rp.t);
				rp.p = realloc(rp.p, (size_t)cap * sizeof *rp.p);
				rp.end = realloc(rp.end, (size_t)cap);
			}
			if (k >= 3) rp.timed = 1;
			rp.x[rp.n] = (short)x; rp.y[rp.n] = (short)y; rp.t[rp.n] = t; rp.p[rp.n] = (unsigned short)(pr > 0 && pr < 4096 ? pr : 1900);
			rp.end[rp.n] = 0; rp.n++;
		} else if (rp.n) rp.end[rp.n - 1] = 1;
	}
	fclose(f);
	unlink(SECURE_DIR "/devink");
	if (rp.n) {
		rp.end[rp.n - 1] = 1; rp.active = 1; rp.i = 0; rp.down = 0;
		rp.start = mono_ms() - (rp.timed ? rp.t[0] : 0);
		rp.next = rp.timed ? rp.start + rp.t[0] : mono_ms();
	}
}

static void replay_step(App *a, double now) {
	while (rp.active && now >= rp.next) {
		if (rp.i >= rp.n) { rp.active = 0; a->pen_near = 0; break; }
		int i = rp.i++;
		a->px = (int)((long)rp.x[i] * PEN_MAX_X / (a->W - 1));
		a->py = (int)((long)rp.y[i] * PEN_MAX_Y / (a->H - 1));
		a->pp = rp.p[i]; a->rubber = 0; a->pen_near = 1;
		a->pen_t = a->ev_t = now;
		int prev = a->touching;
		a->touching = 1;
		pen_report(a, prev);
		if (rp.timed) { /* recorded handwriting: its own timing */
			if (rp.end[i]) { a->touching = 0; pen_report(a, 1); }
			if (rp.i < rp.n) rp.next = rp.start + rp.t[rp.i];
			continue;
		}
		if (rp.end[i]) {
			a->touching = 0;
			pen_report(a, 1);
			rp.next += 160;
		} else {
			int dx = rp.x[i + 1] - rp.x[i], dy = rp.y[i + 1] - rp.y[i];
			rp.next += sqrt((double)(dx * dx + dy * dy)) / 1.3 + 2;
		}
	}
	flush_dirty(a, &a->dirty, &a->fast);
}
#endif /* INKLING_DEV */

/* Scratch-out: a stroke that goes back and forth over existing ink erases it. To tell ink that was already there
 * from the ink the stroke lays down itself, each 16x16-px cell is looked at once, while the pen is still at least
 * one cell away (the 5x5 cells around the pen's cell). Reading framebuffer memory is slow (a whole-page scan took
 * 263 ms on the Kindle, at every pen-down), so only the cells the pen comes near are read. */
#define SCELL 16
static unsigned char *occ; /* per cell: 0 not looked at yet, 1 blank, 2 had ink before this stroke */
static int occ_w, occ_h, occ_lcx, occ_lcy, over_ink;

static void occ_snapshot(App *a) {
	occ_w = (a->cw + SCELL - 1) / SCELL; occ_h = (a->ch + SCELL - 1) / SCELL;
	if (!occ) occ = malloc((size_t)occ_w * (size_t)occ_h);
	if (occ) memset(occ, 0, (size_t)occ_w * (size_t)occ_h);
	occ_lcx = occ_lcy = -1;
	over_ink = 0;
}

static void occ_look(App *a, int ccx, int ccy) {
	for (int j = ccy - 2; j <= ccy + 2; j++)
		for (int i = ccx - 2; i <= ccx + 2; i++) {
			if (i < 0 || j < 0 || i >= occ_w || j >= occ_h) continue;
			unsigned char *o = &occ[(size_t)j * (size_t)occ_w + (size_t)i];
			if (*o) continue;
			*o = 1;
			int x0 = a->cx + i * SCELL, y0 = a->cy + j * SCELL;
			for (int y = y0; y < y0 + SCELL && y < a->cy + a->ch && *o == 1; y += 2) {
				const unsigned char *row = a->fb + (size_t)(a->pan_y + y) * (size_t)a->stride;
				for (int x = x0; x < x0 + SCELL && x < a->cx + a->cw; x += 2)
					if (row[x] < 128) { *o = 2; break; }
			}
		}
}

/* Call before the point is drawn. */
static int occ_at(App *a, int x, int y) {
	if (!occ) return 0;
	int cx = (x - a->cx) / SCELL, cy = (y - a->cy) / SCELL;
	if (cx < 0 || cy < 0 || cx >= occ_w || cy >= occ_h) return 0;
	if (cx != occ_lcx || cy != occ_lcy) { occ_look(a, cx, cy); occ_lcx = cx; occ_lcy = cy; }
	return occ[(size_t)cy * (size_t)occ_w + (size_t)cx] == 2;
}

/* Reversals of a stroke along one axis, counting only legs at least `need` px long. */
static int reversals(const Stroke *s, int axis, int need) {
	int dir = 0, rev = 0, start = axis ? s->pts[0].y : s->pts[0].x, ext = start;
	for (int i = 1; i < s->n; i++) {
		int v = axis ? s->pts[i].y : s->pts[i].x;
		if (!dir) { if (abs(v - start) >= need) { dir = v > start ? 1 : -1; ext = v; } continue; }
		if ((dir > 0 && v > ext) || (dir < 0 && v < ext)) ext = v;
		else if (abs(v - ext) >= need) { rev++; dir = -dir; ext = v; }
	}
	return rev;
}

/* Back and forth at least 4 times across most of its own width (or height), mostly over ink that was already there. */
static int is_scratch(const Stroke *s, int *bx, int *by, int *bw, int *bh) {
	if (s->n < 16 || over_ink * 10 < s->n * 3) return 0;
	int x0 = s->pts[0].x, x1 = x0, y0 = s->pts[0].y, y1 = y0;
	double len = 0;
	for (int i = 1; i < s->n; i++) {
		const Pt *p = &s->pts[i], *q = &s->pts[i - 1];
		if (p->x < x0) x0 = p->x; if (p->x > x1) x1 = p->x;
		if (p->y < y0) y0 = p->y; if (p->y > y1) y1 = p->y;
		len += sqrt((double)((p->x - q->x) * (p->x - q->x) + (p->y - q->y) * (p->y - q->y)));
	}
	int w = x1 - x0, h = y1 - y0, hit = 0;
	for (int axis = 0; axis < 2 && !hit; axis++) {
		int span = axis ? h : w;
		if (span < 40) continue;
		int need = span * 2 / 5 > 20 ? span * 2 / 5 : 20;
		hit = reversals(s, axis, need) >= 3 && len >= 2.5 * span;
	}
	if (!hit) return 0;
	*bx = x0 - 10; *by = y0 - 10; *bw = w + 20; *bh = h + 20;
	return 1;
}

/* Rubs out a rectangle of the page and forgets unsent strokes that were inside it. */
static void erase_box(App *a, int x, int y, int w, int h) {
	if (x < a->cx) { w -= a->cx - x; x = a->cx; }
	if (y < a->cy) { h -= a->cy - y; y = a->cy; }
	if (x + w > a->cx + a->cw) w = a->cx + a->cw - x;
	if (y + h > a->cy + a->ch) h = a->cy + a->ch - y;
	if (w <= 0 || h <= 0) return;
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, 0xFF);
	disp_refresh(&a->disp, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &a->gc16);
	int k = 0;
	for (int i = 0; i < a->npending; i++) {
		Stroke *st = &a->pending[i];
		int in = 0;
		for (int j = 0; j < st->n; j++) in += st->pts[j].x >= x && st->pts[j].x < x + w && st->pts[j].y >= y && st->pts[j].y < y + h;
		if (st->n && in * 10 >= st->n * 7) free(st->pts);
		else a->pending[k++] = *st;
	}
	a->npending = k;
	a->ink_bottom = page_ink_bottom(a);
	a->page_dirty = 1;
}

static void pen_report(App *a, int prev_touch) {
	int sx = (int)((long)a->px * (a->W - 1) / PEN_MAX_X);
	int sy = (int)((long)a->py * (a->H - 1) / PEN_MAX_Y);
	int down = a->touching && !prev_touch, up = !a->touching && prev_touch;
	if (down) {
		a->pen_x0 = sx;
		a->pen_y0 = sy;
		a->pen_in_bar = sy < a->cy;
		if (a->pen_in_bar) bar_press(a, sx, sy);
	}
	if (a->pen_in_bar) { /* a tap on the top bar, whatever is open: never ink */
		if (up) { a->pen_in_bar = 0; bar_release(a, abs(sx - a->pen_x0) + abs(sy - a->pen_y0) > 150, "pen"); }
		return;
	}
	int x0 = a->pen_x0, y0 = a->pen_y0;
	if (a->in_modes) { if (up && !modes_tap(a, x0, y0)) modes_close(a); return; }
	if (a->in_menu) { if (up) menu_tap(a, x0, y0); return; }
	if (a->in_pagemenu) {
		if (up) { pagemenu_tap(a, x0, y0); if (!a->in_pagemenu) { ui_layout(a); ui_draw_bar(a); } }
		return;
	}
	if (a->in_games == 1) { if (up) games_menu_tap(a, x0, y0); return; }
	if (a->in_games == 2) { /* playing: record the stroke (ink if the game wants it), hand it over on pen-up */
		if (down) { a->cur.n = 0; a->have_last = 0; }
		if (a->touching) {
			if (games_wants_ink(a)) {
				if (a->have_last) stamp_line(a, a->lx, a->ly, sx, sy, 3, 0x00, &a->dirty);
				else stamp(a, sx, sy, 3, 0x00, &a->dirty);
			}
			stroke_add(&a->cur, sx, sy, a->pp);
			a->lx = sx; a->ly = sy; a->have_last = 1;
		}
		if (up) { games_stroke(a, &a->cur, 0); a->cur.n = 0; }
		return;
	}
	if (a->in_settings) { if (up) settings_tap(a, x0, y0); return; }
	if (down) {
		a->cur.tool = a->rubber || a->erase_mode ? 1 : 0;
		a->stroke_lag = 0;
		cancel_early(a);
#ifdef INKLING_DEV
		if (!a->cur.tool && capturing()) {
			if (!cap_f) cap_f = fopen(SECURE_DIR "/captured.txt", "a");
			if (cap_f && cap_page != a->page) { fprintf(cap_f, "# page %d\n", a->page); cap_page = a->page; }
		}
#endif
		if (!a->cur.tool && a->scratch_erase) occ_snapshot(a);
		a->cur_active = 1;
		a->have_last = 0;
		a->autosend_at = 0;
	}
	if (a->touching && a->cur_active) {
		int thin = !strcmp(a->ink, "thin"), thick = !strcmp(a->ink, "thick");
		int r = a->cur.tool ? ERASER_R : thin ? 1 + a->pp * 2 / 4095 : thick ? 2 + a->pp * 5 / 4095 : 1 + a->pp * 3 / 4095;
		uint8_t g = a->cur.tool ? 0xFF : 0x00;
		if (!a->cur.tool && occ_at(a, sx, sy)) over_ink++;
#ifdef INKLING_DEV
		if (cap_f) {
			if (cap_t0 == 0) cap_t0 = a->ev_t;
			fprintf(cap_f, "%d %d %.0f %d\n", sx, sy, a->ev_t - cap_t0, a->pp);
		}
#endif
		a->page_dirty = 1;
		if (!a->have_last) {
			stamp(a, sx, sy, r, g, &a->dirty);
			stroke_add(&a->cur, sx, sy, a->pp);
		} else {
			stamp_line(a, a->lx, a->ly, sx, sy, r, g, &a->dirty);
			Pt *q = &a->cur.pts[a->cur.n - 1];
			if (abs(sx - q->x) + abs(sy - q->y) >= 2) stroke_add(&a->cur, sx, sy, a->pp);
		}
		a->lx = sx; a->ly = sy; a->have_last = 1;
		double lag = mono_ms() - a->ev_t; /* from the pen report arriving to its ink being drawn */
		if (lag > a->stroke_lag) a->stroke_lag = lag;
	}
#ifdef INKLING_DEV
	if (up && cap_f) { fputs("\n", cap_f); fflush(cap_f); }
#endif
	if (up) {
		if (a->cur_active && a->stroke_lag > 60) logf_("pen: drawing fell up to %.0f ms behind during this stroke", a->stroke_lag);
		if (a->cur_active) {
			a->cur_active = 0;
			int bx, by, bw, bh, ms = a->autosend_ms[a->mode];
			if (a->cur.n > 0 && !a->cur.tool && a->scratch_erase && is_scratch(&a->cur, &bx, &by, &bw, &bh)) {
				flush_dirty(a, &a->dirty, &a->fast);
				erase_box(a, bx, by, bw, bh);
				logf_("scratch-out: erased %dx%d at %d,%d", bw, bh, bx, by);
				a->cur.n = 0;
				schedule_send(a);
			} else if (a->cur.n > 0 && a->cur.tool) {
				/* rubbing out isn't something to answer: the AI sees the page as it is at the next turn */
				a->cur.n = 0;
				schedule_send(a);
			} else if (a->cur.n > 0) {
				pending_push(a, &a->cur);
				schedule_send(a);
			}
			(void)ms;
		}
	}
}

static void pen_event(App *a, const InEv *k) {
	static int touch_now;
	a->pen_t = k->t;
	if (k->type == EV_ABS) {
		if (k->code == ABS_X) a->px = k->value;
		else if (k->code == ABS_Y) a->py = k->value;
		else if (k->code == ABS_PRESSURE) a->pp = k->value;
	} else if (k->type == EV_KEY) {
		if (k->code == BTN_TOUCH) touch_now = k->value;
		else if (k->code == BTN_TOOL_RUBBER) { a->rubber = k->value; a->pen_near = k->value; }
		else if (k->code == BTN_TOOL_PEN) a->pen_near = k->value;
	} else if (k->type == EV_SYN && k->code == SYN_REPORT) {
		int prev = a->touching;
		a->touching = touch_now;
		pen_report(a, prev);
	}
}

/* The pen counts as nearby only while it keeps reporting: a pen lying still stops sending events. */
static int pen_in_range(App *a) { return (a->pen_near || a->touching) && a->ev_t - a->pen_t < 600; }

/* Touch screen, two contacts tracked separately. A contact that starts on the top bar is a button press, accepted
 * even with the pen nearby (a resting palm is below the pen, never up there); one that starts on the page is
 * ignored while the pen is in range (palm rejection), otherwise it's a tap or, alone on the screen, a page flick. */
static struct {
	int slot, id[MT_SLOTS], x[MT_SLOTS], y[MT_SLOTS], began[MT_SLOTS], ended[MT_SLOTS];
	int x0[MT_SLOTS], y0[MT_SLOTS], maxd[MT_SLOTS], multi[MT_SLOTS], bar[MT_SLOTS], palm[MT_SLOTS], owns[MT_SLOTS];
} T = { .id = { -1, -1 } };

static void touch_gesture(App *a, int s) {
	int x = T.x0[s], y = T.y0[s], dx = T.x[s] - x, dy = T.y[s] - y;
	if (T.bar[s]) {
		if (T.owns[s]) bar_release(a, T.maxd[s] > 150, "finger");
		else logf_("bar: finger tap ignored (pen on the screen)");
		return;
	}
	int tapped = T.maxd[s] < 60, flick = !T.multi[s] && abs(dx) > 250 && abs(dx) > 2 * abs(dy);
	if (T.palm[s]) { if (tapped || flick) logf_("touch at %d,%d ignored (pen in range)", x, y); return; }
	if (a->in_modes) { if (tapped && !modes_tap(a, x, y)) modes_close(a); return; }
	if (a->in_menu) { if (tapped) menu_tap(a, x, y); return; }
	if (a->in_pagemenu) {
		if (tapped) { pagemenu_tap(a, x, y); if (!a->in_pagemenu) { ui_layout(a); ui_draw_bar(a); } }
		return;
	}
	if (a->in_games) {
		if (!tapped) return;
		if (a->in_games == 1) games_menu_tap(a, x, y);
		else {
			Pt pt = { (int16_t)x, (int16_t)y, 0 };
			Stroke one = { 0, 1, 1, &pt };
			games_stroke(a, &one, 1);
		}
		return;
	}
	if (a->in_settings) {
		if (flick) settings_flip(a, dx < 0 ? 1 : -1);
		else if (tapped) settings_tap(a, x, y);
		return;
	}
	if (flick) {
		logf_("flick %s", dx < 0 ? "left (next page)" : "right (previous page)");
		request_turn(a, dx < 0 ? 1 : -1);
	}
}

static void touch_event(App *a, const InEv *e) {
	if (e->type == EV_ABS) {
		int s = T.slot;
		if (e->code == ABS_MT_SLOT) T.slot = e->value >= 0 && e->value < MT_SLOTS ? e->value : 0;
		else if (e->code == ABS_MT_TRACKING_ID) {
			if (e->value >= 0 && T.id[s] < 0) T.began[s] = 1;
			else if (e->value < 0 && T.id[s] >= 0) T.ended[s] = 1;
			T.id[s] = e->value;
		} else if (e->code == ABS_MT_POSITION_X) T.x[s] = e->value;
		else if (e->code == ABS_MT_POSITION_Y) T.y[s] = e->value;
		return;
	}
	if (e->type != EV_SYN || e->code != SYN_REPORT) return;
	int active = 0;
	for (int s = 0; s < MT_SLOTS; s++) active += T.id[s] >= 0 || T.ended[s];
	for (int s = 0; s < MT_SLOTS; s++) {
		if (T.began[s]) {
			T.began[s] = 0;
			T.x0[s] = T.x[s]; T.y0[s] = T.y[s];
			T.maxd[s] = 0;
			T.multi[s] = active > 1;
			T.bar[s] = T.y0[s] < a->cy;
			T.palm[s] = !T.bar[s] && pen_in_range(a);
			T.owns[s] = T.bar[s] && !a->touching && a->pressed < 0;
			if (T.owns[s]) bar_press(a, T.x0[s], T.y0[s]);
		}
		if (T.id[s] >= 0 || T.ended[s]) {
			int d = abs(T.x[s] - T.x0[s]) + abs(T.y[s] - T.y0[s]);
			if (d > T.maxd[s]) T.maxd[s] = d;
			if (active > 1) T.multi[s] = 1;
			if (!T.bar[s] && pen_in_range(a)) T.palm[s] = 1; /* the pen came near during the contact */
		}
		if (T.ended[s]) { T.ended[s] = 0; touch_gesture(a, s); }
	}
}

static void power_event(App *a, const InEv *e) {
	if (e->type == EV_KEY && e->code == KEY_POWER && e->value == 1) {
		logf_("power key: leaving so the Kindle can sleep/lock normally");
		a->quit = 1;
		a->quit_power = 1;
	}
}

/* Everything the input thread queued, in order. */
static void read_input(App *a) {
	input_drain_note();
	InEv e;
	double waited = 0;
	int n = 0;
	while (input_next(&e)) {
		a->ev_t = e.t;
		double age = mono_ms() - e.t;
		if (age > waited) waited = age;
		if (e.src == SRC_PEN) pen_event(a, &e);
		else if (e.src == SRC_TOUCH) touch_event(a, &e);
		else power_event(a, &e);
		if (++n % 64 == 0) flush_dirty(a, &a->dirty, &a->fast);
	}
	flush_dirty(a, &a->dirty, &a->fast);
	if (n) idle_timer_reset();
	int kd, qd;
	input_take_drops(&kd, &qd);
	if (kd || qd) logf_("input: %d kernel drop(s) resynced, %d queued event(s) lost", kd, qd);
	if (waited > 1500) logf_("input: events waited %.0f ms for the app (busy drawing)", waited);
}

/* The Kindle going to sleep - cover closed, power key or the screensaver timeout - is announced by the power
 * daemon. The app steps aside at once: while it runs the Kindle's own UI is paused and the app holds the touch
 * screen, so the sleep screen and the passcode lock couldn't work. launch.sh reopens the app once the Kindle has
 * woken up and been unlocked (exit code 3). */
static pid_t pw_pid;
static int pw_fd = -1;

static void power_events_start(void) {
	int p[2];
	if (pipe2(p, O_CLOEXEC) < 0) return;
	pw_pid = fork();
	if (pw_pid == 0) {
		dup2(p[1], 1);
		int dn = open("/dev/null", O_WRONLY);
		if (dn >= 0) dup2(dn, 2);
		execl("/usr/bin/lipc-wait-event", "lipc-wait-event", "-m", "com.lab126.powerd",
		      "goingToScreenSaver,readyToSuspend,suspending", (char *)NULL);
		_exit(127);
	}
	close(p[1]);
	if (pw_pid < 0) { close(p[0]); pw_pid = 0; logf_("power events: could not start the listener"); return; }
	pw_fd = p[0];
	fcntl(pw_fd, F_SETFL, O_NONBLOCK);
}

static void power_events_read(App *a) {
	char buf[256];
	ssize_t n = read(pw_fd, buf, sizeof buf - 1);
	if (n <= 0) {
		if (n == 0) { close(pw_fd); pw_fd = -1; logf_("power events: listener ended"); }
		return;
	}
	buf[n] = 0;
	char *nl = strchr(buf, '\n');
	if (nl) *nl = 0;
	if (!a->quit) logf_("power: %s - the Kindle is going to sleep, leaving so it can sleep and lock normally", buf);
	a->quit = 1;
	a->quit_sleep = 1;
}

/* You're using the Kindle: restart its idle countdown (the screensaver timeout), at most every 20 s. Runs in a
 * short-lived thread so the pen never waits for it. */
static void *idle_reset_thread(void *arg) {
	(void)arg;
	if (system("lipc-set-prop -i com.lab126.powerd touchScreenSaverTimeout 1 >/dev/null 2>&1") != 0) {}
	return NULL;
}

static double idle_reset_at = -1e9;

static void idle_timer_reset(void) {
	double now = mono_ms();
	if (now - idle_reset_at < 20000) return;
	idle_reset_at = now;
	pthread_t t;
	if (pthread_create(&t, NULL, idle_reset_thread, NULL) == 0) pthread_detach(t);
}

static void power_events_stop(void) {
	if (pw_pid > 0) { kill(pw_pid, SIGTERM); waitpid(pw_pid, NULL, 0); pw_pid = 0; }
}

/* Type up turns the handwriting into the text: wipe the strokes that were just read, and start the typed text where
 * they began - unless other ink is still below that point, then it goes under everything as usual. */
static void typeup_wipe(App *a, int n) {
	int top = a->cy + a->ch, any = 0;
	Dirty d = { 0, 0, -1, -1 };
	for (int i = 0; i < n && i < a->npending; i++) {
		const Stroke *s = &a->pending[i];
		if (s->tool) continue;
		for (int k = 0; k < s->n; k++) {
			const Pt *p = &s->pts[k];
			if (k == 0) stamp(a, p->x, p->y, 9, 0xFF, &d);
			else stamp_line(a, s->pts[k - 1].x, s->pts[k - 1].y, p->x, p->y, 9, 0xFF, &d);
			if (p->y - 10 < top) top = p->y - 10;
			any = 1;
		}
	}
	if (!any) return;
	flush_dirty(a, &d, &a->gc16);
	int lowest = page_ink_bottom(a);
	a->ink_bottom = lowest < top ? (top - 40 > a->cy ? top - 40 : a->cy) : lowest; /* layout adds its 40 px gap */
	logf_("type up: handwriting wiped, text starts at y %d", a->ink_bottom + 40);
}

/* An AI answer goes on the page. */
/* A reply asked for a picture: a stronger model draws it in net lane 1 while you carry on. */
static void start_draw(App *a, const char *desc, int own) {
	if (net_lane_busy(1)) { ui_set_status(a, "still drawing the last picture..."); return; }
	AiJob *j = brain_prepare_draw(a, desc, own);
	if (!j) return;
	if (net_submit_lane(1, brain_draw_run, j, K_DRAW) != 0) { brain_job_free(j); return; }
	draw_job = j;
	draw_page = a->page;
	if (!own) draw_started = mono_ms();
	if (desc != draw_desc) snprintf(draw_desc, sizeof draw_desc, "%s", desc);
	ui_set_status(a, "drawing it...");
}

static void show_drawing(App *a) {
	have_drawn = 0;
	if (a->page == draw_page && !page_is_blank(a)) {
		layout_picture(a, &drawn);
		ui_set_status(a, "drew it in %.1f s", (mono_ms() - draw_started) / 1000);
		logf_("drawing on the page %.1f s after it was asked for", (mono_ms() - draw_started) / 1000);
	} else logf_("drawing dropped: the page changed");
	free(drawn.slen);
	free(drawn.dpts);
	memset(&drawn, 0, sizeof drawn);
}

static void apply_answer(App *a, AiJob *job, Reply *r) {
	brain_commit(a, job, r);
	if (!a->typeup_below && !strcmp(a->mode_id[a->mode], "notes") && !strcmp(r->mode, "notes")) typeup_wipe(a, sent_count);
	for (int i = 0; i < sent_count && i < a->npending; i++) free(a->pending[i].pts);
	int left = a->npending - sent_count;
	if (left > 0) memmove(a->pending, a->pending + sent_count, (size_t)left * sizeof *a->pending);
	a->npending = left > 0 ? left : 0;
	reply_free(a);
	if (strcmp(r->mode, a->mode_id[a->mode]))
		for (int i = 0; i < a->nmodes; i++)
			if (!strcmp(a->mode_id[i], r->mode) && strcmp(a->mode_id[a->mode], "auto")) { a->mode = i; ui_draw_bar(a); }
	layout_reply(a, r);
	double now = mono_ms();
	ui_set_status(a, "answered in %.1f s (%s)", (now - (pen_up_t > 0 && pen_up_t < turn_started ? pen_up_t : turn_started)) / 1000, r->mode);
	logf_("turn ok in %.1f s, mode %s, style %s, %d ops; on the page %.1f s after the pen lifted", (now - turn_started) / 1000,
	      r->mode, r->style, a->nops, pen_up_t > 0 ? (now - pen_up_t) / 1000 : 0);
	if (r->illustrate[0]) start_draw(a, r->illustrate, 0);
	free(r->reply);
	free(r->slen);
	free(r->dpts);
	memset(r, 0, sizeof *r);
}

static void handle_result(App *a) {
	int kind, st;
	char *body;
	size_t len;
	if (!net_take_result(&kind, &st, &body, &len)) return;
	if (kind == K_AI) {
		turn_inflight = 0;
		if (turn_stale) { /* you kept writing: dropped, asked again at your next pause */
			turn_stale = 0;
			brain_job_free(ai_job);
			ai_job = NULL;
			return;
		}
		Reply r = { 0 };
		if (ai_job && brain_parse(a, ai_job, st, body, len, &r)) {
			if (turn_early && mono_ms() < answer_not_before) { /* ready before your pause is over: wait for it */
				held = r;
				held_job = ai_job;
				have_held = 1;
				ai_job = NULL;
				logf_("answer ready in %.1f s, shown when your pause is over", (mono_ms() - turn_started) / 1000);
				return;
			}
			apply_answer(a, ai_job, &r);
		}
		else if (want_turn) { want_turn = 0; logf_("page turn dropped: the answer failed"); }
		brain_job_free(ai_job);
		ai_job = NULL;
	} else if (kind == K_KEYCHECK) {
		char msg[200] = "";
		int ok = keycheck_job ? brain_key_check_result(keycheck_job, st, body, len, msg, sizeof msg) : 0;
		ui_set_status(a, "%s", msg[0] ? msg : ok ? "the key works" : "the key didn't work");
		logf_("key check: %s", ok ? "works" : "failed");
		brain_key_check_free(keycheck_job);
		keycheck_job = NULL;
	} else if (kind == UPDATE_NET_KIND) {
		update_result(a, st, body);
	} else if (kind == K_DRAW) {
		Reply r = { 0 };
		int ok = draw_job && brain_parse_draw(draw_job, st, body, len, &r);
		int own = brain_draw_is_own(draw_job);
		brain_job_free(draw_job);
		draw_job = NULL;
		if (ok) {
			if (have_drawn) { free(drawn.slen); free(drawn.dpts); }
			drawn = r;
			have_drawn = 1; /* shown from the main loop once you're on the page */
		} else if (!own && a->page == draw_page) start_draw(a, draw_desc, 1); /* once more with your own model */
		else ui_set_status(a, "couldn't draw it this time - ask again?");
	} else if (kind == K_EMAIL) {
		cJSON *r = body ? cJSON_Parse(body) : NULL;
		const char *subj = cJSON_GetStringValue(cJSON_GetObjectItem(r, "subject"));
		const char *err = cJSON_GetStringValue(cJSON_GetObjectItem(r, "error"));
		if (st == 200 && subj) { ui_set_status(a, "emailed \"%s\" to %s", subj, brain_email_to(a)); logf_("email sent: %s", subj); }
		else { ui_set_status(a, "email not sent: %s", err ? err : "something went wrong"); logf_("email failed (%d): %s", st, err ? err : ""); }
		cJSON_Delete(r);
		brain_job_free(email_job);
		email_job = NULL;
	}
	free(body);
}

int main(void) {
	App *a = &app;
	setvbuf(stderr, NULL, _IOLBF, 0);
	int lock = open("/var/run/scribe-claude.lock", O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) < 0) { logf_("already running"); return 1; }
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGPIPE, SIG_IGN);
#ifdef INKLING_DEV
	signal(SIGUSR1, on_usr1);
	signal(SIGUSR2, on_usr2);
	logf_("dev build: developer hooks on");
#endif

	/* the app was called Scribe up to 0.2.x: its USB folder and Library entries take the new names once (before
	 * anything reads USB_DIR) */
	upd_migrate_usb_dir(OLD_USB_DIR, USB_DIR);
	upd_migrate_entries(DOCS_DIR);

	brain_load_config(a);
	default_modes(a);
	state_load(a);
	if (!strcmp(a->mode_id[a->mode], "auto")) a->mode = 0; /* Auto was taken off the bar: Chat */
	if (a->page < 1) a->page = 1;
	if (a->last_page < a->page) a->last_page = a->page;

	if (disp_open(&a->disp, "/dev/fb0") < 0) { logf_("display: cannot open /dev/fb0"); return 1; }
	a->fast = (DispRefresh){ .waveform = DISP_WFM_DU };
	a->gc16 = (DispRefresh){ .waveform = DISP_WFM_GC16 };
	a->gl16 = (DispRefresh){ .waveform = DISP_WFM_GL16 }; /* grays without the black flash */
	a->flash = (DispRefresh){ .waveform = DISP_WFM_GC16, .flash = true };
	a->W = a->disp.width;
	a->H = a->disp.height;
	a->stride = a->disp.stride;
	a->pan_y = a->disp.yoffset;
	a->cx = 0; a->cy = BAR_H; a->cw = a->W; a->ch = a->H - BAR_H;
	a->ink_bottom = a->cy;
	const char *fonts[4] = { "Amazon-Ember-Regular.ttf", "Amazon-Ember-Bold.ttf", "Amazon-Ember-RegularItalic.ttf",
				 "Amazon-Ember-BoldItalic.ttf" };
	const int styles[4] = { TXT_REGULAR, TXT_BOLD, TXT_ITALIC, TXT_BOLD_ITALIC };
	for (int i = 0; i < 4; i++) {
		char fp[160];
		snprintf(fp, sizeof fp, FONT_DIR "%s", fonts[i]);
		if (disp_font_add(&a->font_ui, fp, styles[i]) < 0) logf_("font %s not loaded", fp);
	}
	reply_fonts_load(a);
	a->fb = a->disp.fb;
	a->fbsize = a->disp.fbsize;
	if (a->fb && a->fbsize && (a->saved = malloc(a->fbsize))) memcpy(a->saved, a->fb, a->fbsize);
	logf_("start: screen %dx%d stride %d pan %d, page %d, mode %s, provider %s", a->W, a->H, a->stride, a->pan_y, a->page,
	      a->mode_id[a->mode], brain_provider_name(a));

	/* The Kindle may sleep as usual (cover, power key, its idle timeout): the app holds the pen and touch screen, so
	 * the power daemon doesn't see you writing - idle_timer_reset() tells it, and the sleep listener steps aside. */
	idle_timer_reset();

	a->pen_fd = open_input(PEN_DEV, 1);
	a->touch_fd = open_input(TOUCH_DEV, 1);
	a->power_fd = open_input(POWER_DEV, 0);
	a->dirty.x1 = -1;
	a->pressed = -1;
	a->last_act_btn = -1;
	if (input_start(a->pen_fd, a->touch_fd, a->power_fd) < 0) { logf_("input thread failed to start"); return 1; }
	power_events_start();

	DispRect all = { 0, 0, a->W, a->H };
	disp_fill_rect(&a->disp, &all, 0xFF);
	disp_refresh(&a->disp, 0, 0, 0, 0, &a->flash);
	if (a->provider == PROV_NONE)
		snprintf(a->status, sizeof a->status, "Add your API key to inkling/config.txt (USB storage), then reopen");
	else snprintf(a->status, sizeof a->status, "ready - %s - page %d of %d - flick left/right to turn pages",
		      brain_provider_name(a), a->page, a->last_page);
	ui_layout(a);
	ui_draw_bar(a);
	state_save(a);
	page_load(a, a->page);
	net_start();

	while (!g_quit && !a->quit) {
		double now = mono_ms();
		int animating = a->nops > 0;
		if (have_drawn && !a->cur_active && !a->in_settings && !a->in_games && !a->in_pagemenu && !a->in_menu && !a->in_modes)
			show_drawing(a);
		if (!net_busy()) {
			if (!animating && a->npending && !a->cur_active && !a->in_settings && !a->in_games && !a->in_pagemenu && !a->in_menu && !a->in_modes &&
				   (send_requested || (a->autosend_at > 0 && now >= a->autosend_at))) {
				send_turn(a);
			}
		}
#ifdef INKLING_DEV
		if (g_toggle_settings) { /* developer hook: SIGUSR2 taps at SECURE_DIR/devtap "x y" if present, else toggles settings */
			g_toggle_settings = 0;
			if (access(SECURE_DIR "/devdump", R_OK) == 0) { unlink(SECURE_DIR "/devdump"); hits_dump(a, SECURE_DIR "/hits.txt"); goto devdone; }
			if (access(SECURE_DIR "/devinput", R_OK) == 0) { input_script_start(SECURE_DIR "/devinput", a->W, a->H); goto devdone; }
			if (access(SECURE_DIR "/devink", R_OK) == 0) { replay_load(); goto devdone; }
			FILE *dt = fopen(SECURE_DIR "/devtap", "r");
			int dx = 0, dy = 0;
			if (dt && fscanf(dt, "%d %d", &dx, &dy) == 2) {
				fclose(dt);
				unlink(SECURE_DIR "/devtap");
				a->ev_t = mono_ms();
				if (dy < a->cy) tap(a, dx, dy);
				else if (a->in_menu) menu_tap(a, dx, dy);
				else if (a->in_settings) settings_tap(a, dx, dy);
				else if (a->in_pagemenu) { pagemenu_tap(a, dx, dy); if (!a->in_pagemenu) { ui_layout(a); ui_draw_bar(a); } }
				else if (a->in_games == 1) games_menu_tap(a, dx, dy);
				else if (a->in_games == 2) {
					Pt pt = { (int16_t)dx, (int16_t)dy, 0 };
					Stroke one = { 0, 1, 1, &pt };
					games_stroke(a, &one, 1);
				}
			} else {
				if (dt) fclose(dt);
				toggle_settings(a);
			}
		devdone:;
		}
		int stall = input_take_stall(); /* developer hook: pretend to be busy drawing */
		if (stall > 0) { logf_("dev: stalling %d ms", stall); usleep((useconds_t)stall * 1000); }
#endif
		if (g_force_send && !net_busy() && !animating) { g_force_send = 0; send_turn(a); }
		if (have_held && now >= answer_not_before && !a->cur_active && !a->in_menu && !a->in_settings && !a->in_games &&
		    !a->in_pagemenu && !a->in_modes && !animating) {
			have_held = 0;
			apply_answer(a, held_job, &held);
			brain_job_free(held_job);
			held_job = NULL;
		}
		if (turn_inflight && !turn_stale && (!turn_early || now >= answer_not_before) && now - last_tick >= 1000) {
			last_tick = now;
			ui_set_status(a, "thinking... %.0f s", (now - turn_started) / 1000);
		}

#ifdef INKLING_DEV
		int timeout = rp.active ? 5 : a->in_games == 2 ? 50 : 500;
#else
		int timeout = a->in_games == 2 ? 50 : 500;
#endif
		if (animating || net_pending()) timeout = net_pending() ? 0 : 10;
		else if (a->autosend_at > 0 || have_held) {
			double left = (a->autosend_at > 0 ? a->autosend_at : answer_not_before) - now;
			timeout = left < 0 ? 0 : left < 500 ? (int)left : 500;
		}
		struct pollfd p[3] = { { input_fd(), POLLIN, 0 }, { net_result_fd(), POLLIN, 0 }, { pw_fd, POLLIN, 0 } };
		if (poll(p, 3, timeout) < 0 && errno != EINTR) break;
		if (p[2].revents & (POLLIN | POLLHUP)) power_events_read(a);
		if (a->quit) break; /* going to sleep: don't touch the screen any more */
		if (p[0].revents & POLLIN) read_input(a);
		if (p[1].revents & POLLIN) handle_result(a);
		/* new ink is saved after 1.5 s without input, so sleep, a crash or a flat battery never loses it */
		if (a->page_dirty && !a->nops && !a->cur_active && mono_ms() - a->ev_t > 1500) page_autosave(a);
		clearpick_tick(a, mono_ms()); /* an armed Clear option goes back to normal after 4 s */
		update_tick(a);               /* download progress of an update */
		if (a->nops && !a->in_modes) {
			reply_step(a, mono_ms());
			if (!a->nops && !a->in_games) page_save(a); /* reply finished: keep the page */
		}
		games_tick(a, mono_ms());
#ifdef INKLING_DEV
		if (rp.active) replay_step(a, mono_ms());
#endif
		if (want_turn && !a->in_games && !a->in_pagemenu && !a->in_menu && !a->in_settings && !a->in_modes && !net_busy() && !a->nops && !a->npending && !a->cur_active) do_turn(a);
	}

	logf_("exit (%s)", a->quit_sleep ? "the Kindle is going to sleep" : a->quit_power ? "power key" : g_quit ? "signal" : "Exit button");
	power_events_stop();
	games_exit(a); /* an unfinished game is saved for next time */
	/* not when going to sleep: the Kindle may already be drawing its sleep screen, which must not end up in the
	 * page - the idle autosave has kept it */
	if (!a->quit_sleep && !a->in_settings && !a->in_games && !a->in_pagemenu) page_save(a);
	page_save_wait(); /* let the save thread finish writing the page file */
	state_save(a);
	input_stop();
	if (a->pen_fd >= 0) { ioctl(a->pen_fd, EVIOCGRAB, 0); close(a->pen_fd); }
	if (a->touch_fd >= 0) { ioctl(a->touch_fd, EVIOCGRAB, 0); close(a->touch_fd); }
	if (a->power_fd >= 0) close(a->power_fd);
	if (!a->quit_power && !a->quit_sleep && !update_restart_pending() && a->saved) {
		memcpy(a->fb, a->saved, a->fbsize);
		disp_refresh(&a->disp, 0, 0, 0, 0, &a->flash);
	}
	fonts_free(a);
	disp_font_free(&a->font_ui);
	disp_close(&a->disp);
	/* 4: an update is installed, launch.sh starts the new version; 3: launch.sh reopens it after the Kindle wakes */
	return update_restart_pending() ? UPDATE_EXIT_CODE : a->quit_power || a->quit_sleep ? 3 : 0;
}

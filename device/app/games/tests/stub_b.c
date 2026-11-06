/* Host stub of the game engine API (games.h) for the agent-B game tests. Every g_* call is logged (per action) and
 * checked: drawing must stay inside the play area, text must be printable ASCII. A small "screen model" keeps the
 * g_print texts that are still visible (a g_clear covering a text's centre removes it), so tests can read the board.
 * g_after is simulated by calling timer() as soon as the current start/stroke returns (or on demand). */
#include "games.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { C_LINE, C_POLY, C_CIRCLE, C_CROSS, C_FRAME, C_DOT, C_BOX, C_CLEAR, C_WRITE, C_PRINT, C_PAUSE, C_SPEED };

typedef struct { int type; int a[8]; char text[48]; } Call; /* a[]: x,y,w,h,px,bold / x0,y0,x1,y1,width ... */

#define MAXCALLS 20000
#define MAXTEXTS 4000

struct GameCtx {
	const GameDef *def;
	void *state;
	int level, ax, ay, aw, ah, busy, auto_timer, timer_pending, over, in_cb;
	unsigned long long rng;
	char result[256], say[256], remark[256];
	long violations, calls_total;
	char first_violation[160];
};

static Call calls[MAXCALLS];
static int ncalls;
static Call texts[MAXTEXTS];
static int ntexts;

int stub_ncalls(void) { return ncalls; }
const Call *stub_call(int i) { return &calls[i]; }
int stub_ntexts(void) { return ntexts; }
const Call *stub_text(int i) { return &texts[i]; }
long stub_violations(GameCtx *g) { return g->violations; }
const char *stub_first_violation(GameCtx *g) { return g->first_violation; }
const char *stub_result(GameCtx *g) { return g->result; }
const char *stub_say(GameCtx *g) { return g->say; }
int stub_over(GameCtx *g) { return g->over; }
int stub_timer_pending(GameCtx *g) { return g->timer_pending; }
void stub_set_busy(GameCtx *g, int b) { g->busy = b; }
void stub_set_auto_timer(GameCtx *g, int on) { g->auto_timer = on; }
void *stub_game_state(GameCtx *g) { return g->state; }

static void violation(GameCtx *g, const char *fmt, ...) {
	g->violations++;
	if (g->violations == 1) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(g->first_violation, sizeof g->first_violation, fmt, ap);
		va_end(ap);
	}
}

static void check_rect(GameCtx *g, const char *what, int x0, int y0, int x1, int y1) {
	if (x0 < g->ax || y0 < g->ay || x1 > g->ax + g->aw || y1 > g->ay + g->ah)
		violation(g, "%s outside the play area: %d,%d - %d,%d (area %d,%d %dx%d)", what, x0, y0, x1, y1, g->ax, g->ay, g->aw, g->ah);
}

static Call *log_call(GameCtx *g, int type) {
	g->calls_total++;
	if (!g->in_cb) violation(g, "drawing call outside start/stroke/timer");
	if (g->over && type != C_SPEED) violation(g, "drawing after g_over");
	if (ncalls >= MAXCALLS) { violation(g, "too many calls in one action"); ncalls = MAXCALLS - 1; }
	Call *c = &calls[ncalls++];
	memset(c, 0, sizeof *c);
	c->type = type;
	return c;
}

GameCtx *stub_new(const GameDef *def, int level, unsigned long long seed, int ax, int ay, int aw, int ah) {
	GameCtx *g = calloc(1, sizeof *g);
	g->def = def;
	g->state = calloc(1, def->state_size);
	g->level = level;
	g->rng = seed * 2654435761ULL + 0x9E3779B97F4A7C15ULL;
	g->ax = ax; g->ay = ay; g->aw = aw; g->ah = ah;
	g->auto_timer = 1;
	return g;
}

void stub_free(GameCtx *g) { free(g->state); free(g); }

static void run_timer(GameCtx *g) {
	while (g->timer_pending && !g->over) {
		g->timer_pending = 0;
		if (!g->def->timer) { violation(g, "g_after without a timer callback"); return; }
		g->in_cb = 1;
		g->def->timer(g);
		g->in_cb = 0;
	}
}

static void reset_log(void) { ncalls = 0; }

void stub_start(GameCtx *g) {
	reset_log();
	ntexts = 0;
	g->in_cb = 1;
	g->def->start(g);
	g->in_cb = 0;
	if (g->auto_timer) run_timer(g);
}

void stub_fire_timer(GameCtx *g) { reset_log(); run_timer(g); }

void stub_stroke(GameCtx *g, const short *xs, const short *ys, int n, int tap, int finger) {
	GStroke s;
	memset(&s, 0, sizeof s);
	s.n = n; s.x = xs; s.y = ys; s.tap = tap; s.finger = finger;
	s.x0 = s.x1 = xs[0]; s.y0 = s.y1 = ys[0];
	long sx = 0, sy = 0;
	for (int i = 0; i < n; i++) {
		if (xs[i] < s.x0) s.x0 = xs[i];
		if (xs[i] > s.x1) s.x1 = xs[i];
		if (ys[i] < s.y0) s.y0 = ys[i];
		if (ys[i] > s.y1) s.y1 = ys[i];
		sx += xs[i]; sy += ys[i];
		if (i) { int dx = xs[i] - xs[i - 1], dy = ys[i] - ys[i - 1]; s.len += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy); }
	}
	s.cx = (int)(sx / n); s.cy = (int)(sy / n);
	if (tap) { s.cx = xs[0]; s.cy = ys[0]; }
	reset_log();
	g->in_cb = 1;
	g->def->stroke(g, &s);
	g->in_cb = 0;
	if (g->auto_timer) run_timer(g);
}

void stub_tap(GameCtx *g, int x, int y, int finger) {
	short xs[1] = { (short)x }, ys[1] = { (short)y };
	stub_stroke(g, xs, ys, 1, 1, finger);
}

/* A pen stroke along a straight line, with npts points (>= 2). */
void stub_line(GameCtx *g, int x0, int y0, int x1, int y1, int npts) {
	short xs[256], ys[256];
	if (npts < 2) npts = 2;
	if (npts > 256) npts = 256;
	for (int i = 0; i < npts; i++) {
		xs[i] = (short)(x0 + (x1 - x0) * i / (npts - 1));
		ys[i] = (short)(y0 + (y1 - y0) * i / (npts - 1));
	}
	stub_stroke(g, xs, ys, npts, 0, 0);
}

/* --- the API --- */
void *g_state(GameCtx *g) { return g->state; }
int g_level(GameCtx *g) { return g->level; }
int g_rand(GameCtx *g, int n) {
	if (n <= 0) { violation(g, "g_rand(%d)", n); return 0; }
	g->rng ^= g->rng << 13; g->rng ^= g->rng >> 7; g->rng ^= g->rng << 17;
	return (int)((g->rng >> 11) % (unsigned long long)n);
}
void g_area(GameCtx *g, int *x, int *y, int *w, int *h) { *x = g->ax; *y = g->ay; *w = g->aw; *h = g->ah; }
int g_busy(GameCtx *g) { return g->busy; }

void g_speed(GameCtx *g, double f) {
	Call *c = log_call(g, C_SPEED);
	c->a[0] = (int)(f * 100);
	if (f <= 0 || f > 100) violation(g, "g_speed(%g)", f);
}
void g_line(GameCtx *g, int x0, int y0, int x1, int y1, int w) {
	Call *c = log_call(g, C_LINE);
	c->a[0] = x0; c->a[1] = y0; c->a[2] = x1; c->a[3] = y1; c->a[4] = w;
	check_rect(g, "line", x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 > x1 ? x0 : x1, y0 > y1 ? y0 : y1);
	if (w <= 0) violation(g, "line width %d", w);
}
void g_poly(GameCtx *g, const int *xy, int n, int w) {
	Call *c = log_call(g, C_POLY);
	int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
	for (int i = 0; i < n; i++) {
		if (xy[2 * i] < x0) x0 = xy[2 * i];
		if (xy[2 * i] > x1) x1 = xy[2 * i];
		if (xy[2 * i + 1] < y0) y0 = xy[2 * i + 1];
		if (xy[2 * i + 1] > y1) y1 = xy[2 * i + 1];
	}
	c->a[0] = n; c->a[1] = x0; c->a[2] = y0; c->a[3] = x1; c->a[4] = y1; c->a[5] = w;
	if (n < 2) violation(g, "poly with %d points", n);
	check_rect(g, "poly", x0, y0, x1, y1);
}
void g_circle(GameCtx *g, int cx, int cy, int r, int w) {
	Call *c = log_call(g, C_CIRCLE);
	c->a[0] = cx; c->a[1] = cy; c->a[2] = r; c->a[3] = w;
	check_rect(g, "circle", cx - r, cy - r, cx + r, cy + r);
}
void g_cross(GameCtx *g, int cx, int cy, int r, int w) {
	Call *c = log_call(g, C_CROSS);
	c->a[0] = cx; c->a[1] = cy; c->a[2] = r; c->a[3] = w;
	check_rect(g, "cross", cx - r, cy - r, cx + r, cy + r);
}
void g_frame(GameCtx *g, int x, int y, int w, int h, int width) {
	Call *c = log_call(g, C_FRAME);
	c->a[0] = x; c->a[1] = y; c->a[2] = w; c->a[3] = h; c->a[4] = width;
	check_rect(g, "frame", x, y, x + w, y + h);
	if (w <= 0 || h <= 0) violation(g, "frame %dx%d", w, h);
}
void g_dot(GameCtx *g, int cx, int cy, int r) {
	Call *c = log_call(g, C_DOT);
	c->a[0] = cx; c->a[1] = cy; c->a[2] = r;
	check_rect(g, "dot", cx - r, cy - r, cx + r, cy + r);
}
void g_box(GameCtx *g, int x, int y, int w, int h, int gray) {
	Call *c = log_call(g, C_BOX);
	c->a[0] = x; c->a[1] = y; c->a[2] = w; c->a[3] = h; c->a[4] = gray;
	check_rect(g, "box", x, y, x + w, y + h);
	if (w <= 0 || h <= 0 || gray < 0 || gray > 255) violation(g, "box %dx%d gray %d", w, h, gray);
}
void g_clear(GameCtx *g, int x, int y, int w, int h) {
	Call *c = log_call(g, C_CLEAR);
	c->a[0] = x; c->a[1] = y; c->a[2] = w; c->a[3] = h;
	check_rect(g, "clear", x, y, x + w, y + h);
	if (w <= 0 || h <= 0) violation(g, "clear %dx%d", w, h);
	int k = 0;
	for (int i = 0; i < ntexts; i++) {
		int cx = texts[i].a[0] + texts[i].a[2] / 2, cy = texts[i].a[1] + texts[i].a[3] / 2;
		if (cx >= x && cx < x + w && cy >= y && cy < y + h) continue;
		texts[k++] = texts[i];
	}
	ntexts = k;
}
static void check_text(GameCtx *g, const char *t) {
	if (!t) { violation(g, "NULL text"); return; }
	for (const char *p = t; *p; p++) if (*p < 32 || *p > 126) { violation(g, "non-ASCII text"); return; }
}
void g_write(GameCtx *g, int x, int y, int cap, const char *t) {
	Call *c = log_call(g, C_WRITE);
	c->a[0] = x; c->a[1] = y; c->a[2] = cap;
	check_text(g, t);
	snprintf(c->text, sizeof c->text, "%s", t ? t : "");
}
void g_write_center(GameCtx *g, int cx, int y, int cap, const char *t) { g_write(g, cx, y, cap, t); }
int g_write_width(GameCtx *g, int cap, const char *t) { (void)g; return (int)strlen(t) * cap * 7 / 10; }
void g_print(GameCtx *g, int x, int y, int w, int h, int px, int bold, const char *t) {
	Call *c = log_call(g, C_PRINT);
	c->a[0] = x; c->a[1] = y; c->a[2] = w; c->a[3] = h; c->a[4] = px; c->a[5] = bold;
	check_text(g, t);
	check_rect(g, "print", x, y, x + w, y + h);
	if (w <= 0 || h <= 0 || px <= 0 || px > h + 2) violation(g, "print box %dx%d px %d", w, h, px);
	snprintf(c->text, sizeof c->text, "%s", t ? t : "");
	if (ntexts < MAXTEXTS) texts[ntexts++] = *c;
	else violation(g, "screen model full");
}
void g_pause(GameCtx *g, int ms) { Call *c = log_call(g, C_PAUSE); c->a[0] = ms; }

void g_say(GameCtx *g, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g->say, sizeof g->say, fmt, ap);
	va_end(ap);
	check_text(g, g->say);
}
void g_remark(GameCtx *g, const char *text) { check_text(g, text); snprintf(g->remark, sizeof g->remark, "%s", text); }
void g_after(GameCtx *g, int ms) {
	if (ms < 0) violation(g, "g_after(%d)", ms);
	g->timer_pending = 1;
}
void g_over(GameCtx *g, const char *result) {
	if (g->over) violation(g, "g_over twice");
	check_text(g, result);
	g->over = 1;
	snprintf(g->result, sizeof g->result, "%s", result);
}

/* --- persistent storage (games.h g_load/g_save), kept in memory: it outlives each GameCtx like the engine's files
 * outlive an app run; stub_store_clear() is a fresh Kindle. Keys are checked like the engine does. --- */
typedef struct { char key[80]; unsigned char *data; size_t n; } StoreRec;
static StoreRec store[64];
static int nstore;
int stub_store_saves; /* g_save calls since the last clear */

void stub_store_clear(void) {
	for (int i = 0; i < nstore; i++) free(store[i].data);
	nstore = 0;
	stub_store_saves = 0;
}

static StoreRec *store_find(GameCtx *g, const char *key, int create) {
	size_t len = key ? strlen(key) : 0;
	if (len < 1 || len > 31) { violation(g, "record key length %zu", len); return 0; }
	for (size_t i = 0; i < len; i++)
		if (!((key[i] >= 'a' && key[i] <= 'z') || (key[i] >= '0' && key[i] <= '9') || key[i] == '_' || key[i] == '-')) {
			violation(g, "bad record key '%s'", key);
			return 0;
		}
	char full[80];
	snprintf(full, sizeof full, "%s.%s", g->def->id, key);
	for (int i = 0; i < nstore; i++) if (!strcmp(store[i].key, full)) return &store[i];
	if (!create) return 0;
	if (nstore == (int)(sizeof store / sizeof store[0])) { violation(g, "too many records"); return 0; }
	StoreRec *r = &store[nstore++];
	snprintf(r->key, sizeof r->key, "%s", full);
	r->data = 0;
	r->n = 0;
	return r;
}

/* Overwrite a stored record from a test (e.g. garbage, to check a game copes). */
void stub_store_put(GameCtx *g, const char *key, const void *buf, size_t n) {
	StoreRec *r = store_find(g, key, 1);
	if (!r) return;
	free(r->data);
	r->data = malloc(n ? n : 1);
	memcpy(r->data, buf, n);
	r->n = n;
}

int g_load(GameCtx *g, const char *key, void *buf, size_t n) {
	StoreRec *r = store_find(g, key, 0);
	if (!r) return 0;
	size_t m = r->n < n ? r->n : n;
	memcpy(buf, r->data, m);
	return (int)m;
}

void g_save(GameCtx *g, const char *key, const void *buf, size_t n) {
	if (n > 65536) { violation(g, "record of %zu bytes", n); return; }
	stub_store_put(g, key, buf, n);
	stub_store_saves++;
}

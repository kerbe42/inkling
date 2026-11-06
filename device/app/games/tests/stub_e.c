/* Host stub of the game engine (games.h) for suite e (Tic-tac-toe, Connect Four, difficulty checks): a copy of stub_a.c.
 * Every g_* call is recorded in an op log and drawn onto a full-size virtual canvas (1860x2480), so tests can check what
 * the player would see. g_after() is not run inside the game callback: the driver (st_input / st_begin) runs pending
 * timers right after the callback returns, which is what the engine does once the animation has played. */
#include "games.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 1860
#define SH 2480

struct GameCtx {
	const GameDef *def;
	void *state;
	int level;
	unsigned rng;
	int ax, ay, aw, ah;
};

typedef struct {
	int t; /* 'L' line/poly segment, 'C' circle, 'D' dot, 'B' box, 'P' print, 'W' write */
	int a, b, c, d, e;
	char s[48];
} StOp;

unsigned char st_canvas[SH][SW];
StOp *st_ops;
int st_nops, st_opcap;
int st_over, st_timer_pending, st_busy, st_errors, st_oob, st_draw_after_over, st_timer_calls, st_bad_rand;
char st_result[200], st_say_text[200], st_remark_text[200];
int st_verbose;
static struct GameCtx ctx;

void st_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "  ERROR [%s lvl %d]: ", ctx.def ? ctx.def->id : "?", ctx.level);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	st_errors++;
}

static void rec(int t, int a, int b, int c, int d, int e, const char *s) {
	if (st_nops == st_opcap) {
		st_opcap = st_opcap ? st_opcap * 2 : 4096;
		st_ops = realloc(st_ops, (size_t)st_opcap * sizeof *st_ops);
	}
	StOp *o = &st_ops[st_nops++];
	o->t = t; o->a = a; o->b = b; o->c = c; o->d = d; o->e = e;
	o->s[0] = 0;
	if (s) snprintf(o->s, sizeof o->s, "%s", s);
	if (st_over) st_draw_after_over++;
}

/* drawing must stay inside the play area */
static void check_box(int x0, int y0, int x1, int y1) {
	if (x0 < ctx.ax || y0 < ctx.ay || x1 > ctx.ax + ctx.aw || y1 > ctx.ay + ctx.ah) {
		if (st_oob < 3) st_error("drawing outside the play area: %d,%d - %d,%d (area %d,%d %dx%d)", x0, y0, x1, y1, ctx.ax, ctx.ay, ctx.aw, ctx.ah);
		st_oob++;
	}
}

static void fill(int x0, int y0, int x1, int y1, int v) { /* inclusive */
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > SW - 1) x1 = SW - 1;
	if (y1 > SH - 1) y1 = SH - 1;
	for (int y = y0; y <= y1; y++)
		if (x1 >= x0) memset(&st_canvas[y][x0], v, (size_t)(x1 - x0 + 1));
}

static void stamp(int x, int y, int r) { fill(x - r, y - r, x + r, y + r, 0); }

static void seg(int x0, int y0, int x1, int y1, int w) {
	int r = w / 2 > 1 ? w / 2 : 1;
	check_box((x0 < x1 ? x0 : x1) - r, (y0 < y1 ? y0 : y1) - r, (x0 > x1 ? x0 : x1) + r, (y0 > y1 ? y0 : y1) + r);
	int dx = x1 - x0, dy = y1 - y0, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
	for (int s = 0; s < n; s++) stamp(x0 + dx * s / n, y0 + dy * s / n, r);
	stamp(x1, y1, r);
}

/* ---------- the engine API ---------- */
void *g_state(GameCtx *g) { return g->state; }
int g_level(GameCtx *g) { return g->level; }
int g_rand(GameCtx *g, int n) {
	if (n <= 0) { st_bad_rand++; return 0; }
	g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
	return (int)(g->rng % (unsigned)n);
}
void g_area(GameCtx *g, int *x, int *y, int *w, int *h) { *x = g->ax; *y = g->ay; *w = g->aw; *h = g->ah; }
int g_busy(GameCtx *g) { return st_busy; }
void g_speed(GameCtx *g, double f) { if (f <= 0) st_error("g_speed %f", f); }
void g_line(GameCtx *g, int x0, int y0, int x1, int y1, int w) { rec('L', x0, y0, x1, y1, w, 0); seg(x0, y0, x1, y1, w); }
void g_poly(GameCtx *g, const int *xy, int n, int w) {
	for (int i = 1; i < n; i++) { rec('L', xy[2 * i - 2], xy[2 * i - 1], xy[2 * i], xy[2 * i + 1], w, 0); seg(xy[2 * i - 2], xy[2 * i - 1], xy[2 * i], xy[2 * i + 1], w); }
	if (n == 1) { rec('L', xy[0], xy[1], xy[0], xy[1], w, 0); seg(xy[0], xy[1], xy[0], xy[1], w); }
}
void g_circle(GameCtx *g, int cx, int cy, int r, int w) {
	rec('C', cx, cy, r, w, 0, 0);
	int px = cx, py = cy - r;
	for (int i = 1; i <= 48; i++) {
		double t = -M_PI / 2 + 2 * M_PI * i / 48;
		int x = cx + (int)lround(r * cos(t)), y = cy + (int)lround(r * sin(t));
		seg(px, py, x, y, w);
		px = x; py = y;
	}
}
void g_cross(GameCtx *g, int cx, int cy, int r, int w) { g_line(g, cx - r, cy - r, cx + r, cy + r, w); g_line(g, cx + r, cy - r, cx - r, cy + r, w); }
void g_frame(GameCtx *g, int x, int y, int w, int h, int lw) {
	rec('F', x, y, w, h, lw, 0);
	seg(x, y, x + w, y, lw); seg(x + w, y, x + w, y + h, lw); seg(x + w, y + h, x, y + h, lw); seg(x, y + h, x, y, lw);
}
void g_dot(GameCtx *g, int cx, int cy, int r) {
	rec('D', cx, cy, r, 0, 0, 0);
	check_box(cx - r, cy - r, cx + r, cy + r);
	for (int dy = -r; dy <= r; dy++) {
		int hw = (int)sqrt((double)(r * r - dy * dy));
		fill(cx - hw, cy + dy, cx + hw, cy + dy, 0);
	}
}
void g_box(GameCtx *g, int x, int y, int w, int h, int gray) {
	rec('B', x, y, w, h, gray, 0);
	if (w < 0 || h < 0 || gray < 0 || gray > 255) st_error("bad g_box %d,%d %dx%d gray %d", x, y, w, h, gray);
	check_box(x, y, x + w, y + h);
	fill(x, y, x + w - 1, y + h - 1, gray);
}
void g_clear(GameCtx *g, int x, int y, int w, int h) { g_box(g, x, y, w, h, 255); }
void g_write(GameCtx *g, int x, int y, int cap, const char *t) { rec('W', x, y, cap, 0, 0, t); check_box(x, y - cap, x + g_write_width(g, cap, t), y); }
void g_write_center(GameCtx *g, int cx, int y, int cap, const char *t) { g_write(g, cx - g_write_width(g, cap, t) / 2, y, cap, t); }
int g_write_width(GameCtx *g, int cap, const char *t) { return (int)(strlen(t) * cap * 0.7); }
void g_print(GameCtx *g, int x, int y, int w, int h, int px, int bold, const char *t) {
	rec('P', x, y, w, h, px, t);
	check_box(x, y, x + w, y + h);
	if (px > h + 4) st_error("g_print text %dpx in a %dpx box: '%s'", px, h, t);
	if (!t) st_error("g_print NULL");
}
void g_pause(GameCtx *g, int ms) {}
void g_say(GameCtx *g, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(st_say_text, sizeof st_say_text, fmt, ap);
	va_end(ap);
	if (st_verbose > 1) printf("    say: %s\n", st_say_text);
}
void g_remark(GameCtx *g, const char *t) {
	snprintf(st_remark_text, sizeof st_remark_text, "%s", t);
	if (strlen(t) > 80) st_error("remark too long for the strip (%zu chars): %s", strlen(t), t);
	if (st_verbose > 1) printf("    remark: %s\n", t);
}
void g_after(GameCtx *g, int ms) {
	if (st_over) st_error("g_after after g_over");
	st_timer_pending = 1;
}
void g_over(GameCtx *g, const char *result) {
	if (st_over) st_error("g_over called twice");
	st_over = 1;
	st_timer_pending = 0;
	snprintf(st_result, sizeof st_result, "%s", result);
	if (st_verbose) printf("    over: %s\n", result);
}

/* ---------- driver ---------- */
int st_ax = 40, st_ay = 200, st_aw = 1780, st_ah = 2040;

void st_run_timers(void) {
	int guard = 0;
	while (st_timer_pending && !st_over) {
		st_timer_pending = 0;
		if (!ctx.def->timer) { st_error("g_after without a timer()"); return; }
		st_timer_calls++;
		ctx.def->timer(&ctx);
		if (++guard > 10000) { st_error("timer loop does not stop"); return; }
	}
}

void st_begin(const GameDef *d, int level, unsigned seed) {
	free(ctx.state);
	memset(&ctx, 0, sizeof ctx);
	ctx.def = d;
	ctx.level = level;
	ctx.rng = seed * 2654435761u + 12345u;
	if (!ctx.rng) ctx.rng = 1;
	ctx.ax = st_ax; ctx.ay = st_ay; ctx.aw = st_aw; ctx.ah = st_ah;
	ctx.state = calloc(1, d->state_size);
	memset(st_canvas, 255, sizeof st_canvas);
	st_nops = 0;
	st_over = st_timer_pending = st_busy = st_oob = st_draw_after_over = st_timer_calls = st_bad_rand = 0;
	st_result[0] = st_say_text[0] = st_remark_text[0] = 0;
	d->start(&ctx);
	st_run_timers();
}

/* hands a stroke to the game; run=1 also plays out any timers it asked for */
void st_input(const GStroke *s, int run) {
	if (st_over) { st_error("test bug: input after g_over"); return; }
	ctx.def->stroke(&ctx, s);
	if (run) st_run_timers();
}

static short sx[4096], sy[4096];
static unsigned jrng = 2463534242u;
static int jit_rand(int jit) { jrng ^= jrng << 13; jrng ^= jrng >> 17; jrng ^= jrng << 5; return jit ? (int)(jrng % (unsigned)(2 * jit + 1)) - jit : 0; }

static void finish_stroke(GStroke *s, int n, int finger) {
	memset(s, 0, sizeof *s);
	s->n = n; s->x = sx; s->y = sy; s->finger = finger;
	s->x0 = s->x1 = sx[0]; s->y0 = s->y1 = sy[0];
	long tx = 0, ty = 0;
	for (int i = 0; i < n; i++) {
		tx += sx[i]; ty += sy[i];
		if (sx[i] < s->x0) s->x0 = sx[i];
		if (sx[i] > s->x1) s->x1 = sx[i];
		if (sy[i] < s->y0) s->y0 = sy[i];
		if (sy[i] > s->y1) s->y1 = sy[i];
		if (i) s->len += abs(sx[i] - sx[i - 1]) + abs(sy[i] - sy[i - 1]);
	}
	s->cx = (int)(tx / n); s->cy = (int)(ty / n);
	s->tap = s->len < 30 && s->x1 - s->x0 < 30 && s->y1 - s->y0 < 30;
}

void st_tap(int x, int y, int finger, int run) {
	GStroke s;
	sx[0] = (short)x; sy[0] = (short)y;
	int n = 1;
	if (!finger) { sx[1] = (short)(x + 3); sy[1] = (short)(y - 2); sx[2] = (short)(x + 1); sy[2] = (short)(y + 1); n = 3; } /* a pen tap wobbles a little */
	finish_stroke(&s, n, finger);
	st_input(&s, run);
}

/* a pen stroke through the given points (k of them), sampled every ~8 px, with up to `jit` px of wobble */
void st_pen(const int *pts, int k, int jit, int run) {
	GStroke s;
	int n = 0;
	for (int i = 1; i < k; i++) {
		int x0 = pts[2 * i - 2], y0 = pts[2 * i - 1], x1 = pts[2 * i], y1 = pts[2 * i + 1];
		int len = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0), steps = len / 8 + 1;
		for (int p = (i == 1 ? 0 : 1); p <= steps && n < 4096; p++) {
			int j1 = jit_rand(jit), j2 = jit_rand(jit);
			sx[n] = (short)(x0 + (x1 - x0) * p / steps + j1);
			sy[n] = (short)(y0 + (y1 - y0) * p / steps + j2);
			n++;
		}
	}
	finish_stroke(&s, n, 0);
	st_input(&s, run);
}

void st_line(int x0, int y0, int x1, int y1, int jit, int run) { int p[4] = { x0, y0, x1, y1 }; st_pen(p, 2, jit, run); }

int st_px(int x, int y) { return x < 0 || y < 0 || x >= SW || y >= SH ? 255 : st_canvas[y][x]; }

/* darkest pixel in a small square (robust against off-by-one in the game's geometry) */
int st_dark(int x, int y, int r) {
	int m = 255;
	for (int dy = -r; dy <= r; dy++)
		for (int dx = -r; dx <= r; dx++) { int v = st_px(x + dx, y + dy); if (v < m) m = v; }
	return m;
}

void *st_state(void) { return ctx.state; }
void st_end(void) { free(ctx.state); ctx.state = 0; }

/* persistent storage (g_load/g_save), in memory: it outlives each game like the engine's files; keys checked */
static struct { char key[80]; unsigned char *data; size_t n; } store[32];
static int nstore;

static int store_find(GameCtx *g, const char *key, int create) {
	size_t len = key ? strlen(key) : 0;
	if (len < 1 || len > 31 || strspn(key, "abcdefghijklmnopqrstuvwxyz0123456789_-") != len) { st_error("bad record key"); return -1; }
	char full[80];
	snprintf(full, sizeof full, "%s.%s", g->def->id, key);
	for (int i = 0; i < nstore; i++) if (!strcmp(store[i].key, full)) return i;
	if (!create || nstore == 32) return -1;
	snprintf(store[nstore].key, sizeof store[nstore].key, "%s", full);
	return nstore++;
}

int g_load(GameCtx *g, const char *key, void *buf, size_t n) {
	int i = store_find(g, key, 0);
	if (i < 0) return 0;
	size_t m = store[i].n < n ? store[i].n : n;
	memcpy(buf, store[i].data, m);
	return (int)m;
}

void g_save(GameCtx *g, const char *key, const void *buf, size_t n) {
	int i = store_find(g, key, 1);
	if (i < 0 || n > 65536) { st_error("record not saved"); return; }
	free(store[i].data);
	store[i].data = malloc(n ? n : 1);
	memcpy(store[i].data, buf, n);
	store[i].n = n;
}

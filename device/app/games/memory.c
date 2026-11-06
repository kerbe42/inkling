/* Memory: tap two cards to turn them over; find every matching pair. Easy 12 cards, Medium 16, Hard 20. */
#include "../games.h"
#include <stdio.h>

#define M 20
#define MAXC 20
#define NSYM 10
#define BACK_GRAY 190

/* symbol outlines in a -100..100 box (y down) */
static const short STAR[] = { 0, -92, 25, -26, 95, -23, 40, 21, 59, 89, 0, 50, -59, 89, -40, 21, -95, -23, -25, -26, 0, -92 };
static const short HEART[] = { 0, -32, 1, -37, 7, -51, 22, -65, 43, -71, 66, -65, 83, -49, 90, -26, 83, -3, 66, 20, 43, 40,
	22, 59, 7, 75, 1, 87, 0, 91, -1, 87, -7, 75, -22, 59, -43, 40, -66, 20, -83, -3, -90, -26, -83, -49, -66, -65,
	-43, -71, -22, -65, -7, -51, -1, -37, 0, -32 };
static const short MOON[] = { 81, 40, 66, 61, 47, 77, 24, 87, -1, 90, -26, 86, -48, 76, -68, 59, -81, 39, -89, 15, -89, -11,
	-83, -35, -70, -56, -52, -74, -30, -85, -5, -90, 20, -88, -3, -77, -21, -60, -32, -37, -34, -12, -27, 13, -12, 33,
	8, 48, 33, 54, 58, 51, 81, 40 };
static const short SMILE[] = { 45, 26, 39, 36, 31, 44, 22, 50, 11, 54, 0, 55, -11, 54, -22, 50, -31, 44, -39, 36, -45, 26 };
static const short TRIANGLE[] = { 0, -85, 85, 70, -85, 70, 0, -85 };
static const short DIAMOND[] = { 0, -95, 65, 0, 0, 95, -65, 0, 0, -95 };

typedef struct {
	int cols, rows, ncards, cw, ch, x, y;
	signed char sym[MAXC], up[MAXC], done[MAXC];
	int first, second, waiting, moves, pairs;
	char msg[96];
} S;

static void shape(GameCtx *g, const short *pts, int n, int cx, int cy, int r, int w) {
	int xy[64];
	for (int i = 0; i < n; i++) { xy[2 * i] = cx + pts[2 * i] * r / 100; xy[2 * i + 1] = cy + pts[2 * i + 1] * r / 100; }
	g_poly(g, xy, n, w);
}

#define NPTS(a) ((int)(sizeof(a) / sizeof((a)[0]) / 2))

static void draw_symbol(GameCtx *g, int id, int cx, int cy, int r) {
	int w = r / 14 + 4;
	switch (id) {
	case 0: g_circle(g, cx, cy, r * 80 / 100, w); break;
	case 1: g_cross(g, cx, cy, r * 72 / 100, w + 2); break;
	case 2: g_frame(g, cx - r * 70 / 100, cy - r * 70 / 100, r * 140 / 100, r * 140 / 100, w); break;
	case 3: shape(g, TRIANGLE, NPTS(TRIANGLE), cx, cy, r, w); break;
	case 4: shape(g, STAR, NPTS(STAR), cx, cy, r, w); break;
	case 5: shape(g, HEART, NPTS(HEART), cx, cy, r, w); break;
	case 6: shape(g, DIAMOND, NPTS(DIAMOND), cx, cy, r, w); break;
	case 7:
		g_line(g, cx - r * 80 / 100, cy, cx + r * 80 / 100, cy, w + 4);
		g_line(g, cx, cy - r * 80 / 100, cx, cy + r * 80 / 100, w + 4);
		break;
	case 8: shape(g, MOON, NPTS(MOON), cx, cy, r, w); break;
	case 9:
		g_circle(g, cx, cy, r * 90 / 100, w);
		g_dot(g, cx - r * 30 / 100, cy - r * 25 / 100, r / 10 + 3);
		g_dot(g, cx + r * 30 / 100, cy - r * 25 / 100, r / 10 + 3);
		shape(g, SMILE, NPTS(SMILE), cx, cy, r, w);
		break;
	}
}

static void card_box(S *s, int k, int *x, int *y, int *w, int *h) {
	int gap = (s->cw < s->ch ? s->cw : s->ch) / 12;
	*x = s->x + (k % s->cols) * s->cw + gap / 2;
	*y = s->y + (k / s->cols) * s->ch + gap / 2;
	*w = s->cw - gap;
	*h = s->ch - gap;
}

static void draw_back(GameCtx *g, S *s, int k) {
	int x, y, w, h;
	card_box(s, k, &x, &y, &w, &h);
	g_box(g, x + 12, y + 12, w - 24, h - 24, BACK_GRAY);
}

static void draw_face(GameCtx *g, S *s, int k) {
	int x, y, w, h;
	card_box(s, k, &x, &y, &w, &h);
	g_clear(g, x + 10, y + 10, w - 20, h - 20);
	draw_symbol(g, s->sym[k], x + w / 2, y + h / 2, (w < h ? w : h) * 32 / 100);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	static const int LONG[3] = { 4, 4, 5 }, SHORT[3] = { 3, 4, 4 };
	int lvl = g_level(g), a = LONG[lvl], b = SHORT[lvl];
	/* pick the orientation that gives the bigger cards */
	int w1 = (aw - 2 * M) / b, h1 = (ah - 2 * M) / a, w2 = (aw - 2 * M) / a, h2 = (ah - 2 * M) / b;
	int m1 = w1 < h1 ? w1 : h1, m2 = w2 < h2 ? w2 : h2;
	if (m1 >= m2) { s->cols = b; s->rows = a; s->cw = w1; s->ch = h1; }
	else { s->cols = a; s->rows = b; s->cw = w2; s->ch = h2; }
	if (s->cw > s->ch * 5 / 4) s->cw = s->ch * 5 / 4;
	if (s->ch > s->cw * 4 / 3) s->ch = s->cw * 4 / 3;
	s->ncards = s->cols * s->rows;
	s->x = ax + (aw - s->cols * s->cw) / 2;
	s->y = ay + (ah - s->rows * s->ch) / 2;

	/* choose ncards/2 different symbols and shuffle the deck */
	int pool[NSYM];
	for (int i = 0; i < NSYM; i++) pool[i] = i;
	for (int i = NSYM - 1; i > 0; i--) { int j = g_rand(g, i + 1), t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
	for (int k = 0; k < s->ncards; k++) s->sym[k] = (signed char)pool[k / 2];
	for (int k = s->ncards - 1; k > 0; k--) { int j = g_rand(g, k + 1); signed char t = s->sym[k]; s->sym[k] = s->sym[j]; s->sym[j] = t; }
	s->first = s->second = -1;

	g_speed(g, 10);
	for (int k = 0; k < s->ncards; k++) {
		int x, y, w, h;
		card_box(s, k, &x, &y, &w, &h);
		g_frame(g, x, y, w, h, 4);
		draw_back(g, s, k);
	}
	g_remark(g, "Find the pairs - tap two cards.");
	g_say(g, "%d pairs to find - tap a card", s->ncards / 2);
}

static void timer(GameCtx *g) {
	S *s = g_state(g);
	if (!s->waiting) return;
	g_speed(g, 8);
	s->up[s->first] = s->up[s->second] = 0;
	draw_back(g, s, s->first);
	draw_back(g, s, s->second);
	s->first = s->second = -1;
	s->waiting = 0;
	g_say(g, "%d moves, %d of %d pairs - tap a card", s->moves, s->pairs, s->ncards / 2);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->waiting) { g_say(g, "wait - those two turn back first"); return; }
	if (g_busy(g)) { g_say(g, "one moment - still drawing"); return; }
	int k = -1;
	for (int i = 0; i < s->ncards; i++) {
		int x, y, w, h;
		card_box(s, i, &x, &y, &w, &h);
		if (st->cx >= x && st->cx < x + w && st->cy >= y && st->cy < y + h) k = i;
	}
	if (k < 0) { g_say(g, "tap one of the cards"); return; }
	if (s->done[k] || s->up[k]) { g_say(g, "pick a card that is face down"); return; }
	s->up[k] = 1;
	g_speed(g, 4);
	draw_face(g, s, k);
	if (s->first < 0) { s->first = k; g_say(g, "now tap a second card"); return; }
	s->moves++;
	if (s->sym[k] == s->sym[s->first]) {
		int x, y, w, h;
		s->done[k] = s->done[s->first] = 1;
		s->pairs++;
		g_speed(g, 8);
		card_box(s, s->first, &x, &y, &w, &h);
		g_frame(g, x + 5, y + 5, w - 10, h - 10, 3);
		card_box(s, k, &x, &y, &w, &h);
		g_frame(g, x + 5, y + 5, w - 10, h - 10, 3);
		s->first = -1;
		if (s->pairs == s->ncards / 2) {
			snprintf(s->msg, sizeof s->msg, "All %d pairs found in %d moves!", s->pairs, s->moves);
			g_over(g, s->msg);
			return;
		}
		g_say(g, "a pair! %d moves, %d of %d pairs", s->moves, s->pairs, s->ncards / 2);
		return;
	}
	s->second = k;
	s->waiting = 1;
	g_say(g, "not a pair - remember them!");
	g_after(g, 1200);
}

const GameDef GAME_memory = { "memory", "Memory", "Turn over cards and find the pairs", sizeof(S), 0, 1, start, stroke, timer };

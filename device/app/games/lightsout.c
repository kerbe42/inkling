/* Lights Out: a 5x5 grid of lights. Tapping a light toggles it and its four neighbours; turn them all off. The start
 * is made by pressing random lights on a dark board (Easy 5, Medium 10, Hard 15), so it is always solvable; "par" is
 * the fewest taps that solve it (Medium and Hard pick a scramble with par 8+ / 12+). */
#include "../games.h"
#include <stdio.h>

#define N 5
typedef struct {
	unsigned lit;            /* bit r*5+c */
	int moves, par;
	int x, y, cell, hx, hy, hw, hh;
} S;

static unsigned press_mask(int i) {
	int r = i / N, c = i % N;
	unsigned m = 1u << i;
	if (r > 0) m |= 1u << (i - N);
	if (r < N - 1) m |= 1u << (i + N);
	if (c > 0) m |= 1u << (i - 1);
	if (c < N - 1) m |= 1u << (i + 1);
	return m;
}

static int bits(unsigned v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* fewest presses that turn every light off (light chasing: try each first row, chase down, check the last row) */
static int min_presses(unsigned lit) {
	int best = -1;
	for (unsigned first = 0; first < (1u << N); first++) {
		unsigned l = lit;
		int n = 0;
		for (int c = 0; c < N; c++) if (first >> c & 1) { l ^= press_mask(c); n++; }
		for (int r = 1; r < N; r++)
			for (int c = 0; c < N; c++)
				if (l >> ((r - 1) * N + c) & 1) { l ^= press_mask(r * N + c); n++; }
		if (!l && (best < 0 || n < best)) best = n;
	}
	return best;
}

static void cell_box(const S *s, int i, int *x, int *y, int *w) { *x = s->x + (i % N) * s->cell + 16; *y = s->y + (i / N) * s->cell + 16; *w = s->cell - 32; }

static void draw_light(GameCtx *g, const S *s, int i) {
	int x, y, w;
	cell_box(s, i, &x, &y, &w);
	if (s->lit >> i & 1) g_box(g, x, y, w, w, 0);
	else g_clear(g, x, y, w, w);
}

static void show_moves(GameCtx *g, S *s) {
	char t[64];
	snprintf(t, sizeof t, "Moves: %d      Par: %d", s->moves, s->par);
	g_clear(g, s->hx, s->hy, s->hw, s->hh);
	g_print(g, s->hx, s->hy, s->hw, s->hh, 60, 1, t);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->hx = ax; s->hy = ay; s->hw = aw; s->hh = 110;
	s->cell = (aw - 80) / N < (ah - s->hh - 80) / N ? (aw - 80) / N : (ah - s->hh - 80) / N;
	if (s->cell > 330) s->cell = 330;
	s->x = ax + (aw - N * s->cell) / 2;
	s->y = ay + s->hh + (ah - s->hh - N * s->cell) / 2;
	/* scramble with distinct random presses; Medium and Hard keep the hardest of a few tries (par 8+ / 12+) */
	int presses = 5 + 5 * g_level(g), want = g_level(g) == 0 ? 1 : g_level(g) == 1 ? 8 : 12;
	for (int tries = 0; tries < 60 && s->par < want; tries++) {
		unsigned lit = 0, used = 0;
		for (int k = 0; k < presses; k++) {
			int i;
			do i = g_rand(g, N * N); while (used >> i & 1);
			used |= 1u << i;
			lit ^= press_mask(i);
		}
		int par = lit ? min_presses(lit) : 0;
		if (par > s->par) { s->par = par; s->lit = lit; }
	}
	if (!s->lit) { s->lit = press_mask(12); s->par = 1; } /* cannot happen (no 5/10/15 distinct presses cancel out) */
	show_moves(g, s);
	g_speed(g, 10);
	for (int i = 0; i < N * N; i++) g_frame(g, s->x + (i % N) * s->cell + 8, s->y + (i / N) * s->cell + 8, s->cell - 16, s->cell - 16, 5);
	for (int i = 0; i < N * N; i++) if (s->lit >> i & 1) draw_light(g, s, i);
	g_remark(g, "Tap a light to flip it and its neighbours.");
	g_say(g, "turn every light off - a tap flips a light and the ones next to it");
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment..."); return; }
	int c = (st->cx - s->x) / s->cell, r = (st->cy - s->y) / s->cell;
	if (st->cx < s->x || st->cy < s->y || c >= N || r >= N) { g_say(g, "tap one of the lights"); return; }
	int i = r * N + c;
	unsigned m = press_mask(i);
	s->lit ^= m;
	s->moves++;
	g_speed(g, 10);
	draw_light(g, s, i);
	for (int k = 0; k < N * N; k++) if (k != i && (m >> k & 1)) draw_light(g, s, k);
	show_moves(g, s);
	if (!s->lit) {
		char t[64];
		if (s->moves <= s->par) snprintf(t, sizeof t, "All out in %d moves - perfect!", s->moves);
		else snprintf(t, sizeof t, "All out in %d moves (par %d)!", s->moves, s->par);
		g_over(g, t);
		return;
	}
	int left = bits(s->lit);
	g_say(g, "%d light%s still on", left, left == 1 ? "" : "s");
}

const GameDef GAME_lightsout = { "lightsout", "Lights Out", "Turn every light off", sizeof(S), 0, 1, start, stroke, 0 };

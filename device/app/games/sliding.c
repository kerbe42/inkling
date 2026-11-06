/* Sliding Puzzle: put the tiles back in order. Tap a tile next to the gap to slide it (or any tile in the gap's row
 * or column to slide the whole line). Easy 3x3, Medium 4x4, Hard 4x4 shuffled much further. Shuffled by random
 * legal moves from the solved board, so it is always solvable. */
#include "../games.h"
#include <stdio.h>

#define M 20
#define MAXT 16

typedef struct {
	int n, b[MAXT], gap, moves, cell, x, y;
	char msg[96];
} S;

static int solved(S *s) {
	for (int i = 0; i < s->n * s->n - 1; i++) if (s->b[i] != i + 1) return 0;
	return 1;
}

static void draw_tile(GameCtx *g, S *s, int i) {
	int x = s->x + (i % s->n) * s->cell, y = s->y + (i / s->n) * s->cell;
	g_clear(g, x + 2, y + 2, s->cell - 4, s->cell - 4);
	if (!s->b[i]) return;
	char t[12];
	snprintf(t, sizeof t, "%d", s->b[i]);
	g_frame(g, x + 10, y + 10, s->cell - 20, s->cell - 20, 5);
	g_print(g, x + 10, y + 10, s->cell - 20, s->cell - 20, s->cell * 42 / 100, 1, t);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	static const int N[3] = { 3, 4, 4 }, SHUFFLE[3] = { 40, 80, 300 };
	int lvl = g_level(g);
	s->n = N[lvl];
	int nn = s->n * s->n;
	for (int i = 0; i < nn - 1; i++) s->b[i] = i + 1;
	s->b[nn - 1] = 0;
	s->gap = nn - 1;
	int prev = -1;
	for (int k = 0; k < SHUFFLE[lvl] || solved(s); k++) {
		int opts[4], no = 0, r = s->gap / s->n, c = s->gap % s->n;
		if (r > 0) opts[no++] = s->gap - s->n;
		if (r < s->n - 1) opts[no++] = s->gap + s->n;
		if (c > 0) opts[no++] = s->gap - 1;
		if (c < s->n - 1) opts[no++] = s->gap + 1;
		int t;
		do t = opts[g_rand(g, no)]; while (t == prev); /* never undo the last move */
		s->b[s->gap] = s->b[t];
		s->b[t] = 0;
		prev = s->gap;
		s->gap = t;
	}
	s->cell = (aw - 2 * M) / s->n < (ah - 2 * M) / s->n ? (aw - 2 * M) / s->n : (ah - 2 * M) / s->n;
	if (s->cell > 420) s->cell = 420;
	int size = s->n * s->cell;
	s->x = ax + (aw - size) / 2;
	s->y = ay + (ah - size) / 2;
	g_speed(g, 10);
	g_frame(g, s->x - 8, s->y - 8, size + 16, size + 16, 6);
	for (int i = 0; i < nn; i++) if (s->b[i]) draw_tile(g, s, i);
	g_remark(g, "Put the numbers in order - tap a tile to slide it.");
	g_say(g, "tap a tile next to the gap");
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment - still sliding"); return; }
	int size = s->n * s->cell;
	if (st->cx < s->x || st->cy < s->y || st->cx >= s->x + size || st->cy >= s->y + size) { g_say(g, "tap a tile on the board"); return; }
	int c = (st->cx - s->x) / s->cell, r = (st->cy - s->y) / s->cell, i = r * s->n + c;
	int gr = s->gap / s->n, gc = s->gap % s->n;
	if (i == s->gap) { g_say(g, "that's the gap - tap a tile next to it"); return; }
	if (r != gr && c != gc) { g_say(g, "tap a tile in the gap's row or column"); return; }
	int step = r == gr ? (c < gc ? -1 : 1) : (r < gr ? -s->n : s->n);
	int from = s->gap;
	while (s->gap != i) { /* shift the tiles between the gap and the tapped one */
		int t = s->gap + step;
		s->b[s->gap] = s->b[t];
		s->b[t] = 0;
		s->gap = t;
	}
	g_speed(g, 8);
	for (int k = from;; k += step) { /* every cell from the old gap to the new one changed: redraw each once */
		draw_tile(g, s, k);
		if (k == i) break;
	}
	s->moves++;
	if (solved(s)) {
		snprintf(s->msg, sizeof s->msg, "Solved in %d move%s!", s->moves, s->moves == 1 ? "" : "s");
		g_over(g, s->msg);
		return;
	}
	g_say(g, "%d move%s", s->moves, s->moves == 1 ? "" : "s");
}

const GameDef GAME_sliding = { "sliding", "Sliding Puzzle", "Slide the tiles back into order", sizeof(S), 0, 1, start, stroke, 0 };

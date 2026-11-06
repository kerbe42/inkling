/* Sudoku: tap a square, then tap a digit on the pad below (Erase clears it). Every puzzle has exactly one solution.
 * Easy ~40 clues, Medium ~32, Hard ~26 (as few as the puzzle allows while staying unique). */
#include "../games.h"

#define M 20
#define CONFLICT_GRAY 215

typedef struct {
	signed char sol[81], given[81], cur[81], bad[81];
	int sel, x, y, cell, kx, ky, kw, kh, clues;
} S;

static int cand_mask(const signed char *b, int i) {
	int r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3, m = 0x3FE;
	for (int k = 0; k < 9; k++) {
		m &= ~(1 << b[r * 9 + k]);
		m &= ~(1 << b[k * 9 + c]);
		m &= ~(1 << b[(br + k / 3) * 9 + bc + k % 3]);
	}
	return m;
}

static int bits(int m) { int n = 0; while (m) { m &= m - 1; n++; } return n; }

/* Fill an empty grid with random digits, backtracking. */
static int fill(GameCtx *g, signed char *b, int i) {
	if (i == 81) return 1;
	int m = cand_mask(b, i), d[9], n = 0;
	for (int v = 1; v <= 9; v++) if (m & (1 << v)) d[n++] = v;
	for (int k = n - 1; k > 0; k--) { int j = g_rand(g, k + 1), t = d[k]; d[k] = d[j]; d[j] = t; }
	for (int k = 0; k < n; k++) {
		b[i] = (signed char)d[k];
		if (fill(g, b, i + 1)) return 1;
	}
	b[i] = 0;
	return 0;
}

/* Number of solutions, stopping at limit (most-constrained square first). */
static int count_solutions(signed char *b, int limit) {
	int best = -1, bestn = 10, bestm = 0;
	for (int i = 0; i < 81; i++) {
		if (b[i]) continue;
		int m = cand_mask(b, i), n = bits(m);
		if (n == 0) return 0;
		if (n < bestn) { bestn = n; best = i; bestm = m; if (n == 1) break; }
	}
	if (best < 0) return 1;
	int total = 0;
	for (int v = 1; v <= 9 && total < limit; v++) {
		if (!(bestm & (1 << v))) continue;
		b[best] = (signed char)v;
		total += count_solutions(b, limit - total);
	}
	b[best] = 0;
	return total;
}

/* Remove clues in random order while the solution stays unique; returns the clue count. */
static int carve(GameCtx *g, S *s, int target) {
	int order[81], clues = 81;
	signed char tmp[81];
	for (int i = 0; i < 81; i++) { order[i] = i; s->given[i] = s->sol[i]; }
	for (int k = 80; k > 0; k--) { int j = g_rand(g, k + 1), t = order[k]; order[k] = order[j]; order[j] = t; }
	for (int k = 0; k < 81 && clues > target; k++) {
		int i = order[k];
		signed char save = s->given[i];
		s->given[i] = 0;
		for (int j = 0; j < 81; j++) tmp[j] = s->given[j];
		if (count_solutions(tmp, 2) == 1) clues--;
		else s->given[i] = save;
	}
	return clues;
}

static void cell_xy(S *s, int i, int *x, int *y) { *x = s->x + (i % 9) * s->cell; *y = s->y + (i / 9) * s->cell; }

static void draw_cell(GameCtx *g, S *s, int i) {
	int x, y, in = 5;
	cell_xy(s, i, &x, &y);
	g_clear(g, x + in, y + in, s->cell - 2 * in, s->cell - 2 * in);
	if (s->bad[i]) g_box(g, x + in + 4, y + in + 4, s->cell - 2 * in - 8, s->cell - 2 * in - 8, CONFLICT_GRAY);
	if (s->cur[i]) {
		char t[2] = { (char)('0' + s->cur[i]), 0 };
		g_print(g, x, y, s->cell, s->cell, s->cell * 60 / 100, s->given[i] != 0, t);
	}
	if (i == s->sel) g_frame(g, x + 9, y + 9, s->cell - 18, s->cell - 18, 6);
}

/* Mark player digits that clash with another digit in their row, column or box; redraw what changed. */
static void update_conflicts(GameCtx *g, S *s) {
	for (int i = 0; i < 81; i++) {
		int bad = 0;
		if (s->cur[i] && !s->given[i]) {
			int r = i / 9, c = i % 9;
			for (int j = 0; j < 81 && !bad; j++) {
				if (j == i || s->cur[j] != s->cur[i]) continue;
				int rj = j / 9, cj = j % 9;
				if (rj == r || cj == c || (rj / 3 == r / 3 && cj / 3 == c / 3)) bad = 1;
			}
		}
		if (bad != s->bad[i]) { s->bad[i] = (signed char)bad; draw_cell(g, s, i); }
	}
}

static int key_at(S *s, int px, int py) {
	if (py < s->ky || py >= s->ky + s->kh || px < s->kx) return -1;
	int k = (px - s->kx) / s->kw;
	return k < 10 ? k : -1; /* 0..8 = digits 1..9, 9 = Erase */
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	static const int TARGET[3] = { 40, 32, 26 };
	int target = TARGET[g_level(g)];
	/* Hard: carving is greedy, so try a few grids and keep the one with the fewest clues */
	S best = *s;
	best.clues = 99;
	for (int attempt = 0; attempt < (target < 30 ? 6 : 1); attempt++) {
		for (int i = 0; i < 81; i++) s->sol[i] = 0;
		fill(g, s->sol, 0);
		s->clues = carve(g, s, target);
		if (s->clues < best.clues) best = *s;
		if (best.clues <= target) break;
	}
	*s = best;
	for (int i = 0; i < 81; i++) s->cur[i] = s->given[i];
	s->sel = -1;

	s->kw = (aw - 2 * M) / 10;
	if (s->kw > 180) s->kw = 180;
	s->kh = 160;
	int gap = 50, room = ah - 2 * M - gap - s->kh;
	s->cell = (aw - 2 * M) / 9 < room / 9 ? (aw - 2 * M) / 9 : room / 9;
	if (s->cell > 200) s->cell = 200;
	int size = 9 * s->cell, total = size + gap + s->kh;
	s->x = ax + (aw - size) / 2;
	s->y = ay + (ah - total) / 2;
	s->kx = ax + (aw - 10 * s->kw) / 2;
	s->ky = s->y + size + gap;

	g_speed(g, 10);
	for (int i = 1; i < 9; i++) {
		int w = i % 3 ? 2 : 6;
		g_line(g, s->x + i * s->cell, s->y, s->x + i * s->cell, s->y + size, w);
		g_line(g, s->x, s->y + i * s->cell, s->x + size, s->y + i * s->cell, w);
	}
	g_frame(g, s->x, s->y, size, size, 6);
	for (int i = 0; i < 81; i++) {
		if (!s->given[i]) continue;
		int x, y;
		char t[2] = { (char)('0' + s->given[i]), 0 };
		cell_xy(s, i, &x, &y);
		g_print(g, x, y, s->cell, s->cell, s->cell * 60 / 100, 1, t);
	}
	for (int k = 0; k < 10; k++) {
		char t[2] = { (char)('1' + k), 0 };
		int x = s->kx + k * s->kw;
		g_frame(g, x + 6, s->ky, s->kw - 12, s->kh, 3);
		g_print(g, x + 6, s->ky, s->kw - 12, s->kh, k < 9 ? s->kh * 50 / 100 : s->kh * 24 / 100, 0, k < 9 ? t : "Erase");
	}
	g_remark(g, "Tap a square, then tap a digit.");
	g_say(g, "%d clues - fill in the rest", s->clues);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment - still drawing"); return; }
	int size = 9 * s->cell;
	if (st->cx >= s->x && st->cx < s->x + size && st->cy >= s->y && st->cy < s->y + size) {
		int i = (st->cy - s->y) / s->cell * 9 + (st->cx - s->x) / s->cell;
		if (s->given[i]) { g_say(g, "that digit is given - pick an empty square"); return; }
		if (i == s->sel) return;
		int old = s->sel;
		s->sel = i;
		g_speed(g, 8);
		if (old >= 0) draw_cell(g, s, old);
		draw_cell(g, s, i);
		g_say(g, "now tap a digit below");
		return;
	}
	int k = key_at(s, st->cx, st->cy);
	if (k < 0) { g_say(g, "tap a square, then a digit"); return; }
	if (s->sel < 0) { g_say(g, "tap an empty square first"); return; }
	int v = k < 9 ? k + 1 : 0;
	if (s->cur[s->sel] == v) return;
	s->cur[s->sel] = (signed char)v;
	g_speed(g, 6);
	draw_cell(g, s, s->sel);
	update_conflicts(g, s);
	int left = 0, wrong = 0;
	for (int i = 0; i < 81; i++) { left += !s->cur[i]; wrong += s->cur[i] && s->cur[i] != s->sol[i]; }
	if (!left && !wrong) {
		int old = s->sel;
		s->sel = -1;
		draw_cell(g, s, old);
		g_over(g, "Solved - every row, column and box is complete!");
		return;
	}
	if (!left) g_say(g, "all filled, but something is not right yet");
	else g_say(g, "%d squares to go", left);
}

const GameDef GAME_sudoku = { "sudoku", "Sudoku", "Fill the grid: 1-9 in every row, column and box", sizeof(S), 0, 1, start, stroke, 0 };

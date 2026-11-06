/* Nonogram: a picture logic puzzle. The numbers by each row and column give the runs of filled squares in it, in
 * order. Easy is 8x8, Medium 10x10, Hard 15x15. On Easy and Medium one game in three is a hidden picture (a heart,
 * a house, a cat...; each picture once before any comes back, across restarts), the others are random patterns:
 * blobs, mirror-symmetric or symmetric both ways. Hard is a random blob pattern. Easy and Medium puzzles can always be solved by
 * row-and-column logic alone, so they have exactly one answer; Hard ones nearly always (any filling that matches the
 * clues is accepted anyway). Tap a square to fill it (tap again to empty it); a small mark or X drawn
 * in a square marks it as empty (tap to clear the mark); a straight line along a row or column fills the squares
 * it covers (or, started on a filled or marked square, empties them). The game uses no ink: it draws the marks. */
#include "../games.h"
#include "bag.h"
#include <stdio.h>
#include <string.h>

#define M 20
#define MAXN 15
#define MAXK 8 /* most runs in a line of 15 */

typedef struct {
	int n;                          /* first two fields are read by the host test */
	unsigned char sol[MAXN][MAXN];
	unsigned char cell[MAXN][MAXN]; /* 0 empty, 1 filled, 2 marked empty */
	unsigned char rc[MAXN][MAXK], rn[MAXN], cc[MAXN][MAXK], cn[MAXN];
	int gx, gy, cs, slot, px, kr, kc;
	const char *name;
	char msg[96];
} S;

typedef struct { const char *name; const char *rows[10]; } Pic;

static const Pic P8[] = {
	{ "a heart", { ".##..##.", "########", "########", "########", ".######.", "..####..", "...##...", "........" } },
	{ "a house", { "...##...", "..####..", ".######.", "########", ".#....#.", ".#.##.#.", ".#.##.#.", ".######." } },
	{ "a tree", { "...##...", "..####..", ".######.", "..####..", ".######.", "########", "...##...", "..####.." } },
	{ "a boat", { "...#....", "...##...", "...###..", "...####.", "...#....", "########", ".######.", "..####.." } },
	{ "a smiley face", { "..####..", ".######.", "##.##.##", "########", "#.####.#", "##....##", ".######.", "..####.." } },
	{ "a fish", { "...##...", "..####.#", ".#.#####", "########", ".#######", "..####.#", "...##...", "........" } },
	{ "a mushroom", { "..####..", ".######.", "##.##.##", "########", "...##...", "...##...", "..####..", "........" } },
	{ "a crown", { "#..##..#", "#..##..#", "##.##.##", "########", "########", "#.#..#.#", "########", "........" } },
	{ "a ghost", { "..####..", ".######.", "##.##.##", "##.##.##", "########", "########", "########", "#.#..#.#" } },
	{ "an arrow", { "...##...", "..####..", ".######.", "########", "...##...", "...##...", "...##...", "...##..." } },
	{ "a cup", { "........", "######..", "#######.", "######.#", "#######.", "######..", ".####...", "########" } },
};
static const Pic P10[] = {
	{ "an umbrella", { "....##....", "..######..", ".########.", "##########", "##########", "....##....", "....##....", "....##....", ".#..##....", "..##......" } },
	{ "a rocket", { "....##....", "...####...", "...####...", "...#.##...", "...####...", "...####...", "..######..", ".########.", ".##.##.##.", "....##...." } },
	{ "a duck", { "...###....", "..#####...", "..##.###..", "..#######.", "...####...", ".#######..", "##########", "##########", ".########.", "..######.." } },
	{ "an apple", { ".....#....", "....#.....", "..##.###..", ".########.", "##########", "##########", "##########", ".########.", ".########.", "..##..##.." } },
	{ "a star", { "....##....", "...####...", "...####...", "##########", ".########.", "..######..", "...####...", "..######..", ".###..###.", ".##....##." } },
	{ "an anchor", { "...####...", "...#..#...", "...####...", "....##....", ".########.", "....##....", "##..##..##", "##..##..##", ".########.", "...####..." } },
	{ "a bell", { "....##....", "...####...", "..######..", "..######..", "..######..", ".########.", ".########.", "##########", "##########", "....##...." } },
	{ "a tree", { "....##....", "...####...", "..######..", "...####...", "..######..", ".########.", "..######..", ".########.", "##########", "....##...." } },
	{ "a house", { "......##..", "...##.##..", "..######..", ".########.", "##########", ".#......#.", ".#.##.#.#.", ".#.##...#.", ".#.##.###.", ".########." } },
	{ "a cat", { "#........#", "##......##", "##########", "#..####..#", "#.#.##.#.#", "##########", "###.##.###", ".###..###.", "..######..", "...####..." } },
	{ "a sailboat", { "....#.....", "....##....", "....###...", "....####..", "....#####.", "....#.....", "##########", ".########.", "..######..", "...####..." } },
};
#define NP8 ((int)(sizeof P8 / sizeof P8[0]))
#define NP10 ((int)(sizeof P10 / sizeof P10[0]))

/* --- clues and the line solver --- */
static int runs(const unsigned char *v, int step, int n, unsigned char *out) { /* runs of 1s; step walks a row or column */
	int k = 0, len = 0;
	for (int i = 0; i < n; i++) {
		if (v[i * step] == 1) len++;
		else if (len) { out[k++] = (unsigned char)len; len = 0; }
	}
	if (len) out[k++] = (unsigned char)len;
	return k;
}

static void make_clues(S *s) {
	for (int i = 0; i < s->n; i++) {
		s->rn[i] = (unsigned char)runs(&s->sol[i][0], 1, s->n, s->rc[i]);
		s->cn[i] = (unsigned char)runs(&s->sol[0][i], MAXN, s->n, s->cc[i]);
	}
}

static int fits(int len, int i, const unsigned char *cells, int n) {
	if (i + len > n) return 0;
	for (int t = i; t < i + len; t++) if (cells[t] == 2) return 0;
	return 1;
}

/* One line of n cells (0 unknown, 1 filled, 2 empty) with runs cl[0..k-1]: settle the cells every arrangement agrees
 * on. Returns the number of cells settled, or -1 if no arrangement fits. */
static int solve_line(const unsigned char *cl, int k, unsigned char *cells, int n) {
	unsigned char suf[MAXN + 2][MAXK + 1], pre[MAXN + 2][MAXK + 1], canf[MAXN], cane[MAXN];
	memset(suf, 0, sizeof suf);
	memset(pre, 0, sizeof pre);
	memset(canf, 0, sizeof canf);
	memset(cane, 0, sizeof cane);
	suf[n][k] = 1; /* suf[i][j]: cells i.. can hold runs j.. */
	for (int i = n - 1; i >= 0; i--)
		for (int j = k; j >= 0; j--) {
			int v = cells[i] != 1 && suf[i + 1][j];
			if (!v && j < k && fits(cl[j], i, cells, n)) {
				int L = cl[j];
				v = i + L == n ? suf[n][j + 1] : cells[i + L] != 1 && suf[i + L + 1][j + 1];
			}
			suf[i][j] = (unsigned char)v;
		}
	if (!suf[0][0]) return -1;
	pre[0][0] = 1; /* pre[i][j]: runs ..j-1 fit in cells ..i-1 and a run may start at i */
	for (int i = 0; i < n; i++)
		for (int j = 0; j <= k; j++) {
			if (!pre[i][j]) continue;
			if (cells[i] != 1 && suf[i + 1][j]) { cane[i] = 1; pre[i + 1][j] = 1; }
			if (j < k && fits(cl[j], i, cells, n)) {
				int L = cl[j];
				if (i + L == n) {
					if (suf[n][j + 1]) { for (int t = i; t < n; t++) canf[t] = 1; pre[n][j + 1] = 1; }
				} else if (cells[i + L] != 1 && suf[i + L + 1][j + 1]) {
					for (int t = i; t < i + L; t++) canf[t] = 1;
					cane[i + L] = 1;
					pre[i + L + 1][j + 1] = 1;
				}
			}
		}
	int set = 0;
	for (int t = 0; t < n; t++) {
		if (cells[t]) continue;
		if (canf[t] && !cane[t]) { cells[t] = 1; set++; }
		else if (cane[t] && !canf[t]) { cells[t] = 2; set++; }
	}
	return set;
}

/* Can the clues be solved one row or column at a time, with no guessing? (Then the answer is unique.) */
static int line_solvable(S *s) {
	unsigned char g[MAXN][MAXN], line[MAXN];
	int n = s->n, changed = 1;
	memset(g, 0, sizeof g);
	while (changed) {
		changed = 0;
		for (int r = 0; r < n; r++) {
			int k = solve_line(s->rc[r], s->rn[r], g[r], n);
			if (k < 0) return 0;
			changed |= k > 0;
		}
		for (int c = 0; c < n; c++) {
			for (int r = 0; r < n; r++) line[r] = g[r][c];
			int k = solve_line(s->cc[c], s->cn[c], line, n);
			if (k < 0) return 0;
			changed |= k > 0;
			for (int r = 0; r < n; r++) g[r][c] = line[r];
		}
	}
	for (int r = 0; r < n; r++)
		for (int c = 0; c < n; c++) if (!g[r][c]) return 0;
	return 1;
}

/* --- making a puzzle --- */
static void from_picture(GameCtx *g, S *s, const Pic *p) {
	int flip = g_rand(g, 2);
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++) s->sol[r][c] = p->rows[r][flip ? s->n - 1 - c : c] == '#';
	s->name = p->name;
	make_clues(s);
}

/* sym: 0 none, 1 mirror left-right, 2 mirror both ways. Returns 1 when the pattern is solvable line by line. */
static int random_blobs(GameCtx *g, S *s, int sym) {
	int n = s->n;
	for (int tries = 0; tries < 400; tries++) {
		unsigned char t[MAXN][MAXN];
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) s->sol[r][c] = g_rand(g, 100) < 55;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) {
				if (sym >= 1 && c >= (n + 1) / 2) s->sol[r][c] = s->sol[r][n - 1 - c];
				if (sym == 2 && r >= (n + 1) / 2) s->sol[r][c] = s->sol[n - 1 - r][c];
			}
		for (int pass = 0; pass < 1; pass++) { /* smooth into blobs: each square follows the majority around it */
			for (int r = 0; r < n; r++)
				for (int c = 0; c < n; c++) {
					int on = 0, all = 0;
					for (int dr = -1; dr <= 1; dr++)
						for (int dc = -1; dc <= 1; dc++) {
							int rr = r + dr, cc = c + dc;
							if (rr < 0 || rr >= n || cc < 0 || cc >= n) continue;
							all++;
							on += s->sol[rr][cc];
						}
					t[r][c] = on * 2 > all || (on * 2 == all && s->sol[r][c]);
				}
			memcpy(s->sol, t, sizeof t);
		}
		int on = 0;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) on += s->sol[r][c];
		if (on * 100 < n * n * 40 || on * 100 > n * n * 70) continue;
		make_clues(s);
		if (line_solvable(s)) return 1;
	}
	make_clues(s); /* very unlikely: keep the last one (any answer matching the clues is accepted) */
	return 0;
}

/* the next picture of a level, from a bag kept on the Kindle by picture name (bag.h): each once before any comes back,
 * and none within 3 of its last time */
static const Pic *next_picture(GameCtx *g, int lvl) {
	const Pic *p = lvl ? P10 : P8;
	int np = lvl ? NP10 : NP8;
	uint32_t hash[NP8 > NP10 ? NP8 : NP10];
	for (int i = 0; i < np; i++) hash[i] = bag_hash(p[i].name);
	return &p[bag_next(g, lvl ? "pictures10" : "pictures8", hash, np, 3)];
}

/* --- drawing --- */
static void interior(S *s, int r, int c, int *x, int *y, int *w) { *x = s->gx + c * s->cs + 4; *y = s->gy + r * s->cs + 4; *w = s->cs - 8; }

static void set_cell(GameCtx *g, S *s, int r, int c, int v) {
	int x, y, w;
	if (s->cell[r][c] == v) return;
	interior(s, r, c, &x, &y, &w);
	if (s->cell[r][c]) g_clear(g, x, y, w, w);
	s->cell[r][c] = (unsigned char)v;
	if (v == 1) g_box(g, x, y, w, w, 0);
	else if (v == 2) g_cross(g, x + w / 2, y + w / 2, s->cs * 22 / 100, 3);
}

static void draw_board(GameCtx *g, S *s) {
	int n = s->n, size = n * s->cs, bx = s->gx - s->kr * s->slot, by = s->gy - s->kc * s->slot;
	g_speed(g, 30);
	for (int i = 0; i <= n; i++) {
		if (i % 5 == 0 || i == n) continue;
		g_line(g, s->gx, s->gy + i * s->cs, s->gx + size, s->gy + i * s->cs, 2);
		g_line(g, s->gx + i * s->cs, s->gy, s->gx + i * s->cs, s->gy + size, 2);
	}
	for (int i = 0; i <= n; i++) {
		if (i % 5 && i != n) continue;
		g_line(g, s->gx, s->gy + i * s->cs, s->gx + size, s->gy + i * s->cs, 5);
		g_line(g, s->gx + i * s->cs, s->gy, s->gx + i * s->cs, s->gy + size, 5);
		g_line(g, bx, s->gy + i * s->cs, s->gx, s->gy + i * s->cs, 2); /* guide lines through the clues */
		g_line(g, s->gx + i * s->cs, by, s->gx + i * s->cs, s->gy, 2);
	}
	g_speed(g, 40);
	char t[4];
	for (int r = 0; r < n; r++) {
		int k = s->rn[r] ? s->rn[r] : 1;
		for (int i = 0; i < k; i++) {
			snprintf(t, sizeof t, "%d", s->rn[r] ? s->rc[r][i] : 0);
			g_print(g, s->gx - (k - i) * s->slot, s->gy + r * s->cs, s->slot, s->cs, s->px, 0, t);
		}
	}
	for (int c = 0; c < n; c++) {
		int k = s->cn[c] ? s->cn[c] : 1;
		for (int i = 0; i < k; i++) {
			snprintf(t, sizeof t, "%d", s->cn[c] ? s->cc[c][i] : 0);
			g_print(g, s->gx + c * s->cs, s->gy - (k - i) * s->slot, s->cs, s->slot, s->px, 0, t);
		}
	}
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	static const int N[3] = { 8, 10, 15 };
	int ax, ay, aw, ah, lvl = g_level(g);
	g_area(g, &ax, &ay, &aw, &ah);
	s->n = N[lvl];
	s->name = 0;
	if (lvl == 2) random_blobs(g, s, 0);
	else { /* a picture, or a pattern: 2/5 plain blobs, 2/5 mirrored, 1/5 symmetric both ways (8x8 has only ~2000 of those) */
		int r = g_rand(g, 5);
		if (g_rand(g, 3) == 0 || !random_blobs(g, s, r < 2 ? 0 : r < 4 ? 1 : 2)) from_picture(g, s, next_picture(g, lvl));
	}
	s->kr = s->kc = 1;
	for (int i = 0; i < s->n; i++) {
		if (s->rn[i] > s->kr) s->kr = s->rn[i];
		if (s->cn[i] > s->kc) s->kc = s->cn[i];
	}
	/* square cells; a clue slot is 62% of a cell (at most 80 px) */
	int w = aw - 2 * M, h = ah - 2 * M;
	int csw = w * 100 / (s->n * 100 + 62 * s->kr), csh = h * 100 / (s->n * 100 + 62 * s->kc);
	s->cs = csw < csh ? csw : csh;
	if (s->cs > 150) s->cs = 150;
	s->slot = s->cs * 62 / 100;
	if (s->slot > 80) s->slot = 80;
	s->px = s->slot * 72 / 100 < s->cs * 55 / 100 ? s->slot * 72 / 100 : s->cs * 55 / 100;
	if (s->px > 48) s->px = 48;
	int totw = s->kr * s->slot + s->n * s->cs, toth = s->kc * s->slot + s->n * s->cs;
	s->gx = ax + (aw - totw) / 2 + s->kr * s->slot;
	s->gy = ay + (ah - toth) / 2 + s->kc * s->slot;
	draw_board(g, s);
	snprintf(s->msg, sizeof s->msg, s->name ? "A%s %dx%d picture is hidden here - what is it?" : "A%s %dx%d puzzle - fill the squares the numbers ask for",
	         s->n == 8 ? "n" : "", s->n, s->n);
	g_remark(g, s->msg);
	g_say(g, "tap to fill; draw a small X to mark empty; a line fills a run");
}

static int cell_at(S *s, int x, int y, int slack, int *r, int *c) {
	int size = s->n * s->cs;
	if (x < s->gx - slack || x >= s->gx + size + slack || y < s->gy - slack || y >= s->gy + size + slack) return 0;
	*c = x < s->gx ? 0 : (x - s->gx) / s->cs;
	*r = y < s->gy ? 0 : (y - s->gy) / s->cs;
	if (*c >= s->n) *c = s->n - 1;
	if (*r >= s->n) *r = s->n - 1;
	return 1;
}

static int line_ok(S *s, int i, int col) {
	unsigned char got[MAXK + 8];
	int k = col ? runs(&s->cell[0][i], MAXN, s->n, got) : runs(&s->cell[i][0], 1, s->n, got);
	int want = col ? s->cn[i] : s->rn[i];
	const unsigned char *cl = col ? s->cc[i] : s->rc[i];
	if (k != want) return 0;
	for (int j = 0; j < k; j++) if (got[j] != cl[j]) return 0;
	return 1;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	int r, c;
	/* the state is complete from the start, so input while the board is still being drawn is fine */
	g_speed(g, 12);
	if (st->tap || st->finger) {
		if (!cell_at(s, st->cx, st->cy, 0, &r, &c)) { g_say(g, "tap a square to fill it"); return; }
		set_cell(g, s, r, c, s->cell[r][c] == 0 ? 1 : 0);
	} else {
		int w = st->x1 - st->x0, h = st->y1 - st->y0, lim = s->cs * 16 / 10;
		if (w <= lim && h <= lim) { /* a small mark: the square under its middle is empty */
			if (!cell_at(s, (st->x0 + st->x1) / 2, (st->y0 + st->y1) / 2, 0, &r, &c)) { g_say(g, "draw the X inside a square"); return; }
			set_cell(g, s, r, c, 2);
		} else if (w >= 2 * h || h >= 2 * w) { /* a line along a row or column */
			int horiz = w >= 2 * h, r0, c0, r1, c1, rs, cs0;
			if (!cell_at(s, st->x0, st->y0, s->cs / 2, &r0, &c0) || !cell_at(s, st->x1, st->y1, s->cs / 2, &r1, &c1) ||
			    !cell_at(s, st->cx, st->cy, 0, &r, &c) || !cell_at(s, st->x[0], st->y[0], s->cs / 2, &rs, &cs0)) {
				g_say(g, "draw the line over the squares");
				return;
			}
			int from = horiz ? s->cell[r][cs0] : s->cell[rs][c], to = from == 0 ? 1 : 0;
			g_speed(g, 20);
			if (horiz) { for (int k = c0; k <= c1; k++) if (s->cell[r][k] == from) set_cell(g, s, r, k, to); }
			else for (int k = r0; k <= r1; k++) if (s->cell[k][c] == from) set_cell(g, s, k, c, to);
		} else {
			g_say(g, "draw a straight line along a row or column, or a small X in one square");
			return;
		}
	}
	int rows = 0, cols = 0;
	for (int i = 0; i < s->n; i++) { rows += line_ok(s, i, 0); cols += line_ok(s, i, 1); }
	if (rows == s->n && cols == s->n) {
		g_speed(g, 30);
		for (r = 0; r < s->n; r++)
			for (c = 0; c < s->n; c++) if (s->cell[r][c] == 2) set_cell(g, s, r, c, 0);
		if (s->name) snprintf(s->msg, sizeof s->msg, "Solved - it's %s!", s->name);
		else snprintf(s->msg, sizeof s->msg, "Solved! All %d rows and columns check out.", s->n);
		g_over(g, s->msg);
		return;
	}
	g_say(g, "%d of %d rows and %d of %d columns match their numbers", rows, s->n, cols, s->n);
}

const GameDef GAME_nonogram = { "nonogram", "Nonogram", "Fill the squares the numbers ask for - a hidden picture", sizeof(S), 0, 1, start, stroke, 0 };

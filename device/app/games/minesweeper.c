/* Minesweeper: tap a square to open it; draw a mark on a square (a line, a tick or an X) to plant a flag, and tap a
 * flag to lift it. (Drawing on a flag does nothing, so the second stroke of an X can't undo the first.)
 * The first square you open is always safe. Easy 9x9 / 10 mines, Medium 12x12 / 24, Hard 16x16 / 45. */
#include "../games.h"
#include <stdio.h>

#define MAXN 16
#define M 20
#define HIDDEN 0
#define OPEN 1
#define FLAG 2
#define GRAY 200

typedef struct {
	int n, mines, cell, x, y;
	unsigned char mine[MAXN][MAXN], adj[MAXN][MAXN], st[MAXN][MAXN];
	int placed, opened, flags;
	short stack[MAXN * MAXN * 2];
	char msg[96];
} S;

static void cell_box(S *s, int r, int c, int *x, int *y) { *x = s->x + c * s->cell; *y = s->y + r * s->cell; }

static void interior(S *s, int r, int c, int *x, int *y, int *w) {
	cell_box(s, r, c, x, y);
	*x += 3; *y += 3; *w = s->cell - 6;
}

static void draw_flag(GameCtx *g, S *s, int r, int c) {
	int x, y, cl = s->cell;
	cell_box(s, r, c, &x, &y);
	int px = x + cl * 38 / 100, top = y + cl * 20 / 100, bot = y + cl * 78 / 100;
	int tri[8] = { px, top, px + cl * 34 / 100, top + cl * 14 / 100, px, top + cl * 28 / 100, px, top };
	g_line(g, px, bot, px, top, 4);
	g_poly(g, tri, 4, 4);
	g_line(g, px - cl * 14 / 100, bot, px + cl * 14 / 100, bot, 4);
}

static void draw_open(GameCtx *g, S *s, int r, int c) {
	int x, y, w;
	interior(s, r, c, &x, &y, &w);
	g_clear(g, x, y, w, w);
	if (s->adj[r][c]) {
		char t[2] = { (char)('0' + s->adj[r][c]), 0 };
		g_print(g, x, y, w, w, s->cell * 55 / 100, 1, t);
	}
}

static void place_mines(GameCtx *g, S *s, int r0, int c0) {
	short cand[MAXN * MAXN];
	int nc = 0;
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++)
			if (r < r0 - 1 || r > r0 + 1 || c < c0 - 1 || c > c0 + 1) cand[nc++] = (short)(r * s->n + c); /* first tap opens an area */
	for (int k = 0; k < s->mines; k++) {
		int j = k + g_rand(g, nc - k);
		short t = cand[k]; cand[k] = cand[j]; cand[j] = t;
		s->mine[cand[k] / s->n][cand[k] % s->n] = 1;
	}
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++)
			for (int dr = -1; dr <= 1; dr++)
				for (int dc = -1; dc <= 1; dc++) {
					int rr = r + dr, cc = c + dc;
					if ((dr || dc) && rr >= 0 && rr < s->n && cc >= 0 && cc < s->n) s->adj[r][c] += s->mine[rr][cc];
				}
	s->placed = 1;
}

/* Open a safe square, flood-filling through zeros. */
static void open_from(GameCtx *g, S *s, int r0, int c0) {
	int sp = 0;
	s->stack[sp++] = (short)(r0 * s->n + c0);
	s->st[r0][c0] = OPEN;
	while (sp) {
		int i = s->stack[--sp], r = i / s->n, c = i % s->n;
		s->opened++;
		draw_open(g, s, r, c);
		if (s->adj[r][c]) continue;
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (rr < 0 || rr >= s->n || cc < 0 || cc >= s->n || s->st[rr][cc] != HIDDEN) continue;
				s->st[rr][cc] = OPEN;
				s->stack[sp++] = (short)(rr * s->n + cc);
			}
	}
}

static void lose(GameCtx *g, S *s, int hr, int hc) {
	int x, y, w;
	g_speed(g, 4);
	interior(s, hr, hc, &x, &y, &w);
	g_clear(g, x, y, w, w);
	g_frame(g, x + 4, y + 4, w - 8, w - 8, 6);
	g_dot(g, x + w / 2, y + w / 2, s->cell * 30 / 100);
	g_speed(g, 10);
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++) {
			if (r == hr && c == hc) continue;
			cell_box(s, r, c, &x, &y);
			if (s->mine[r][c] && s->st[r][c] != FLAG) g_dot(g, x + s->cell / 2, y + s->cell / 2, s->cell * 20 / 100);
			if (!s->mine[r][c] && s->st[r][c] == FLAG) g_cross(g, x + s->cell / 2, y + s->cell / 2, s->cell * 30 / 100, 4);
		}
	g_over(g, "Boom! You hit a mine.");
}

static int won(GameCtx *g, S *s) {
	if (s->opened != s->n * s->n - s->mines) return 0;
	g_speed(g, 10);
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++)
			if (s->mine[r][c] && s->st[r][c] != FLAG) { s->st[r][c] = FLAG; draw_flag(g, s, r, c); }
	snprintf(s->msg, sizeof s->msg, "All clear - you found every mine!");
	g_over(g, s->msg);
	return 1;
}

static void status(GameCtx *g, S *s) { g_say(g, "%d mines - %d flagged", s->mines, s->flags); }

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	static const int N[3] = { 9, 12, 16 }, MINES[3] = { 10, 24, 45 };
	s->n = N[g_level(g)];
	s->mines = MINES[g_level(g)];
	s->cell = (aw - 2 * M) / s->n < (ah - 2 * M) / s->n ? (aw - 2 * M) / s->n : (ah - 2 * M) / s->n;
	if (s->cell > 190) s->cell = 190;
	s->x = ax + (aw - s->n * s->cell) / 2;
	s->y = ay + (ah - s->n * s->cell) / 2;
	int size = s->n * s->cell;
	g_speed(g, 10);
	g_box(g, s->x, s->y, size, size, GRAY);
	for (int i = 1; i < s->n; i++) {
		g_line(g, s->x + i * s->cell, s->y, s->x + i * s->cell, s->y + size, 2);
		g_line(g, s->x, s->y + i * s->cell, s->x + size, s->y + i * s->cell, 2);
	}
	g_frame(g, s->x, s->y, size, size, 5);
	g_remark(g, "Tap to open a square. Draw a mark on one to flag it.");
	status(g, s);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment - still drawing"); return; }
	if (st->cx < s->x || st->cy < s->y) { g_say(g, "tap a square on the board"); return; }
	int c = (st->cx - s->x) / s->cell, r = (st->cy - s->y) / s->cell;
	if (c >= s->n || r >= s->n) { g_say(g, "tap a square on the board"); return; }
	int x, y, w;
	if (!st->tap) { /* a mark: plant a flag */
		if (s->st[r][c] == OPEN) { g_say(g, "that square is already open"); return; }
		if (s->st[r][c] == FLAG) { g_say(g, "flagged - tap the flag to lift it"); return; }
		s->st[r][c] = FLAG;
		s->flags++;
		g_speed(g, 5);
		draw_flag(g, s, r, c);
		status(g, s);
		return;
	}
	if (s->st[r][c] == FLAG) { /* a tap on a flag lifts it (it never opens the square) */
		s->st[r][c] = HIDDEN;
		s->flags--;
		interior(s, r, c, &x, &y, &w);
		g_speed(g, 6);
		g_box(g, x, y, w, w, GRAY);
		g_say(g, "flag lifted - %d mines, %d flagged", s->mines, s->flags);
		return;
	}
	if (!s->placed) place_mines(g, s, r, c);
	g_speed(g, 8);
	if (s->st[r][c] == OPEN) { /* tapping a number whose flags are all placed opens the rest around it */
		int fl = 0;
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (rr >= 0 && rr < s->n && cc >= 0 && cc < s->n && s->st[rr][cc] == FLAG) fl++;
			}
		if (!s->adj[r][c] || fl != s->adj[r][c]) { g_say(g, "that square is open - tap a gray one"); return; }
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++) {
				int rr = r + dr, cc = c + dc;
				if (rr < 0 || rr >= s->n || cc < 0 || cc >= s->n || s->st[rr][cc] != HIDDEN) continue;
				if (s->mine[rr][cc]) { lose(g, s, rr, cc); return; }
				open_from(g, s, rr, cc);
			}
		if (!won(g, s)) status(g, s);
		return;
	}
	if (s->mine[r][c]) { lose(g, s, r, c); return; }
	open_from(g, s, r, c);
	if (!won(g, s)) status(g, s);
}

const GameDef GAME_minesweeper = { "minesweeper", "Minesweeper", "Open every square that has no mine", sizeof(S), 0, 1, start, stroke, 0 };

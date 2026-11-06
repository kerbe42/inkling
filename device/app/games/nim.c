/* Nim (misere): a different board every game - 3 to 5 rows of 1 to 9 sticks (Easy 3 rows up to 5, Medium 4 up to 7,
 * Hard 4-5 up to 9), recent boards remembered so they don't come back soon. Take one or more sticks from ONE row per
 * turn - strike through them with the pen, or tap them and then tap Done. Whoever takes the last stick loses.
 * Whoever should win from the start position with perfect play is the player to move; if that is the computer's
 * side it starts, else you do, so you can always win. Hard plays perfectly. */
#include "../games.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROWS 5 /* most rows */
#define COLS 9 /* most sticks in a row */
#define RECENT 12 /* boards remembered per level */
typedef struct {
	int nr, heap[ROWS], len[ROWS]; /* rows, sticks left, sticks at the start */
	unsigned char taken[ROWS][COLS], sel[ROWS][COLS];
	int selrow; /* row with selected sticks + 1 (0 = none) */
	int my_turn;
	int cx, rowy[ROWS], rh, sh, gap, sp; /* layout: centre x, row tops, row height, stick height, gap above/below, spacing */
	int bx, by, bw, bh;                 /* Done button */
} S;

static int abs_i(int v) { return v < 0 ? -v : v; }

static int stick_x(const S *s, int r, int j) { return s->cx + (2 * j - (s->len[r] - 1)) * s->sp / 2; }
static int stick_top(const S *s, int r) { return s->rowy[r] + s->gap; }

static void draw_stick(GameCtx *g, const S *s, int r, int j) {
	int x = stick_x(s, r, j), t = stick_top(s, r);
	g_line(g, x, t + 20, x, t + s->sh, 10);
	g_dot(g, x, t + 16, 16);
}

static void strike(GameCtx *g, const S *s, int r, int j) {
	int x = stick_x(s, r, j), mid = stick_top(s, r) + s->sh / 2, dx = s->sp * 32 / 100, dy = s->sh * 30 / 100;
	g_line(g, x - dx, mid + dy, x + dx, mid - dy, 8);
}

static void mark(GameCtx *g, const S *s, int r, int j, int on) {
	int x = stick_x(s, r, j), y = stick_top(s, r) + s->sh + s->gap * 6 / 10, rad = s->gap * 3 / 10;
	if (on) g_circle(g, x, y, rad, 5);
	else g_clear(g, x - rad - 6, y - rad - 6, 2 * rad + 12, 2 * rad + 12);
}

static int total(const S *s) { int t = 0; for (int r = 0; r < s->nr; r++) t += s->heap[r]; return t; }

/* perfect misere play: returns 1 and the move if the position is a win for the side to move */
static int optimal(const int *h, int nr, int *row, int *take) {
	int big = 0, ones = 0, x = 0, bi = -1;
	for (int i = 0; i < nr; i++) { if (h[i] > 1) { big++; bi = i; } ones += h[i] == 1; x ^= h[i]; }
	if (big == 0) {
		for (int i = 0; i < nr; i++) if (h[i]) { *row = i; *take = 1; break; }
		return ones % 2 == 0;
	}
	if (big == 1) { *row = bi; *take = ones % 2 ? h[bi] : h[bi] - 1; return 1; } /* leave an odd number of single sticks */
	if (!x) return 0;
	for (int i = 0; i < nr; i++) if ((h[i] ^ x) < h[i]) { *row = i; *take = h[i] - (h[i] ^ x); return 1; }
	return 0;
}

static void random_move(GameCtx *g, const S *s, int *row, int *take) {
	int rows[ROWS], n = 0;
	for (int i = 0; i < s->nr; i++) if (s->heap[i]) rows[n++] = i;
	*row = rows[g_rand(g, n)];
	*take = 1 + g_rand(g, s->heap[*row]);
}

static int finish(GameCtx *g, S *s, int mover) {
	if (total(s)) return 0;
	g_over(g, mover == 1 ? "You took the last stick - I win!" : "I had to take the last stick - you win!");
	return 1;
}

/* a random board for the level, not one of the recently played (same level) ones; at least 10 sticks */
static void new_board(GameCtx *g, S *s) {
	static const int MAXN[3] = { 5, 7, 9 };
	int lvl = g_level(g);
	if (lvl < 0 || lvl > 2) lvl = 1;
	char key[16];
	snprintf(key, sizeof key, "recent%d", lvl);
	uint32_t rec[RECENT + 2] = { 0 }; /* [0] = count, [1] = next slot, then the hashes */
	int got = g_load(g, key, rec, sizeof rec);
	if (got != (int)sizeof rec || rec[0] > RECENT || rec[1] >= RECENT) memset(rec, 0, sizeof rec);
	int tries = 0, sum;
	uint32_t h;
	do {
		s->nr = lvl == 0 ? 3 : lvl == 1 ? 4 : 4 + g_rand(g, 2);
		sum = 0;
		h = 2166136261u;
		for (int r = 0; r < ROWS; r++) {
			s->len[r] = r < s->nr ? 1 + g_rand(g, MAXN[lvl]) : 0;
			sum += s->len[r];
			h = (h ^ (uint32_t)s->len[r]) * 16777619u;
		}
		int seen = 0;
		for (uint32_t i = 0; i < rec[0]; i++) seen |= rec[2 + i] == h;
		if (sum >= 10 && (!seen || tries >= 300)) break;
		tries++;
	} while (1);
	rec[2 + rec[1]] = h;
	rec[1] = (rec[1] + 1) % RECENT;
	if (rec[0] < RECENT) rec[0]++;
	g_save(g, key, rec, sizeof rec);
	for (int r = 0; r < ROWS; r++) s->heap[r] = s->len[r];
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	new_board(g, s);
	const int TOP = 80, BTN = 230;
	s->rh = (ah - TOP - BTN) / s->nr;
	if (s->rh > 330) s->rh = 330;
	s->gap = s->rh * 16 / 100;
	s->sh = s->rh - 2 * s->gap;
	s->sp = (aw - 120) / COLS < 200 ? (aw - 120) / COLS : 200;
	s->cx = ax + aw / 2;
	int y0 = ay + TOP + (ah - TOP - BTN - s->nr * s->rh) / 2;
	for (int r = 0; r < s->nr; r++) s->rowy[r] = y0 + r * s->rh;
	s->bw = 360; s->bh = 130;
	s->bx = s->cx - s->bw / 2;
	s->by = y0 + s->nr * s->rh + (BTN - s->bh) / 2;
	g_print(g, ax, ay, aw, TOP - 10, 40, 0, "Take sticks from ONE row. Whoever takes the last stick loses.");
	g_speed(g, 9);
	for (int r = 0; r < s->nr; r++)
		for (int j = 0; j < s->len[r]; j++) draw_stick(g, s, r, j);
	g_frame(g, s->bx, s->by, s->bw, s->bh, 5);
	g_print(g, s->bx, s->by, s->bw, s->bh, 60, 1, "Done");
	int tmp, tk;
	if (!optimal(s->heap, s->nr, &tmp, &tk)) { /* you would lose moving first: I open, so you can win */
		g_remark(g, "Last stick loses. I'll start.");
		s->my_turn = 1;
		g_say(g, "my turn...");
		g_after(g, 900);
	} else {
		g_remark(g, "Last stick loses. You start!");
		g_say(g, "strike through sticks in one row (or tap them, then Done)");
	}
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int lvl = g_level(g), row = 0, take = 1;
	int smart = lvl == 2 || (lvl == 1 && g_rand(g, 100) < 70) || (lvl == 0 && g_rand(g, 100) < 25);
	if (!smart || !optimal(s->heap, s->nr, &row, &take)) {
		if (smart) { /* a lost position: take one from the biggest row and hope */
			row = 0;
			for (int i = 1; i < s->nr; i++) if (s->heap[i] > s->heap[row]) row = i;
			take = 1;
		} else random_move(g, s, &row, &take);
	}
	g_speed(g, 3);
	int left = take;
	for (int j = s->len[row] - 1; j >= 0 && left; j--)
		if (!s->taken[row][j]) { s->taken[row][j] = 1; strike(g, s, row, j); left--; }
	s->heap[row] -= take;
	if (finish(g, s, 2)) return;
	s->my_turn = 0;
	g_say(g, "I took %d from row %d. Your move", take, row + 1);
}

/* the player's move: takes the given sticks of row r */
static void take_sticks(GameCtx *g, S *s, int r, const unsigned char *which) {
	int n = 0;
	if (s->selrow) { /* drop any tap selection */
		int sr = s->selrow - 1;
		for (int j = 0; j < s->len[sr]; j++) if (s->sel[sr][j]) { s->sel[sr][j] = 0; if (!which[j] || sr != r) mark(g, s, sr, j, 0); }
	}
	g_speed(g, 6);
	for (int j = 0; j < s->len[r]; j++)
		if (which[j] && !s->taken[r][j]) {
			s->taken[r][j] = 1;
			mark(g, s, r, j, 0);
			strike(g, s, r, j);
			n++;
		}
	s->selrow = 0;
	s->heap[r] -= n;
	if (finish(g, s, 1)) return;
	s->my_turn = 1;
	g_say(g, "you took %d from row %d. My turn...", n, r + 1);
	g_after(g, 700);
}

static int stick_at(const S *s, int x, int y, int *row) {
	for (int r = 0; r < s->nr; r++) {
		int t = stick_top(s, r);
		if (y < t - 30 || y > t + s->sh + 30) continue;
		for (int j = 0; j < s->len[r]; j++)
			if (abs_i(x - stick_x(s, r, j)) <= s->sp * 45 / 100) { *row = r; return j; }
	}
	return -1;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	if (st->tap || st->n < 2) {
		if (st->cx >= s->bx && st->cx <= s->bx + s->bw && st->cy >= s->by && st->cy <= s->by + s->bh) {
			if (!s->selrow) { g_say(g, "tap some sticks first (or strike through them)"); return; }
			int r = s->selrow - 1;
			unsigned char w[COLS];
			for (int j = 0; j < COLS; j++) w[j] = s->sel[r][j];
			take_sticks(g, s, r, w);
			return;
		}
		int r, j = stick_at(s, st->cx, st->cy, &r);
		if (j < 0) { g_say(g, "tap a stick to pick it, or strike through sticks"); return; }
		if (s->taken[r][j]) { g_say(g, "that stick is already gone"); return; }
		g_speed(g, 8);
		if (s->selrow && s->selrow - 1 != r) { /* one row per turn: move the selection */
			int sr = s->selrow - 1;
			for (int k = 0; k < s->len[sr]; k++) if (s->sel[sr][k]) { s->sel[sr][k] = 0; mark(g, s, sr, k, 0); }
			s->selrow = 0;
			g_say(g, "one row per turn - now picking from row %d", r + 1);
		}
		s->sel[r][j] ^= 1;
		mark(g, s, r, j, s->sel[r][j]);
		int any = 0;
		for (int k = 0; k < s->len[r]; k++) any += s->sel[r][k];
		s->selrow = any ? r + 1 : 0;
		if (any) g_say(g, "%d picked in row %d - tap Done to take them", any, r + 1);
		else g_say(g, "nothing picked");
		return;
	}
	/* a pen stroke: the sticks it crosses */
	unsigned char hit[ROWS][COLS] = { { 0 } };
	int rows = 0, last = -1, gone = 0;
	for (int i = 1; i < st->n; i++) {
		int xa = st->x[i - 1], ya = st->y[i - 1], xb = st->x[i], yb = st->y[i];
		if (xa == xb) continue;
		for (int r = 0; r < s->nr; r++) {
			int t = stick_top(s, r);
			for (int j = 0; j < s->len[r]; j++) {
				int x = stick_x(s, r, j);
				if (x < (xa < xb ? xa : xb) || x > (xa > xb ? xa : xb)) continue;
				int y = ya + (yb - ya) * (x - xa) / (xb - xa);
				if (y < t - 10 || y > t + s->sh + 10) continue;
				if (s->taken[r][j]) { gone++; continue; }
				hit[r][j] = 1;
			}
		}
	}
	for (int r = 0; r < s->nr; r++) {
		int any = 0;
		for (int j = 0; j < s->len[r]; j++) any |= hit[r][j];
		if (any) { rows++; last = r; }
	}
	if (rows > 1) { g_say(g, "only one row per turn - strike sticks in a single row"); return; }
	if (!rows) { g_say(g, gone ? "those sticks are already gone" : "draw a line across the sticks you want to take"); return; }
	take_sticks(g, s, last, hit[last]);
}

const GameDef GAME_nim = { "nim", "Nim", "Take sticks - don't take the last one", sizeof(S), 0, 1, start, stroke, my_move };

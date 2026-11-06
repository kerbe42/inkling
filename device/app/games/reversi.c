/* Reversi: tap a square with a small dot to place a black disc; every white ring you outflank turns black. The Kindle
 * plays the white rings. A side with no move passes; the game ends when neither can move. Hard searches 4 moves ahead. */
#include "../games.h"
#include <stdio.h>

typedef struct {
	unsigned char b[64]; /* 0 empty, 1 you (black), 2 me (white) */
	unsigned char hint[64];
	int x, y, cell, my_turn;
	int hx, hy, hw, hh;
} S;

static const int DR[8] = { -1, -1, -1, 0, 0, 1, 1, 1 }, DC[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };

static const int WEIGHT[64] = {
	100, -20, 10, 5, 5, 10, -20, 100,
	-20, -50, -2, -2, -2, -2, -50, -20,
	10, -2, -1, -1, -1, -1, -2, 10,
	5, -2, -1, -1, -1, -1, -2, 5,
	5, -2, -1, -1, -1, -1, -2, 5,
	10, -2, -1, -1, -1, -1, -2, 10,
	-20, -50, -2, -2, -2, -2, -50, -20,
	100, -20, 10, 5, 5, 10, -20, 100,
};

/* discs p would flip by playing at m (0 if illegal); fills flips[] in drawing order when given */
static int flips_for(const unsigned char *b, int m, int p, int *flips) {
	if (b[m]) return 0;
	int r = m / 8, c = m % 8, n = 0;
	for (int d = 0; d < 8; d++) {
		int rr = r + DR[d], cc = c + DC[d], k = 0;
		while (rr >= 0 && rr < 8 && cc >= 0 && cc < 8 && b[rr * 8 + cc] == 3 - p) { rr += DR[d]; cc += DC[d]; k++; }
		if (!k || rr < 0 || rr >= 8 || cc < 0 || cc >= 8 || b[rr * 8 + cc] != p) continue;
		for (int j = 1; j <= k; j++) {
			if (flips) flips[n] = (r + DR[d] * j) * 8 + c + DC[d] * j;
			n++;
		}
	}
	return n;
}

static int legal_moves(const unsigned char *b, int p, int *moves) {
	int n = 0;
	for (int m = 0; m < 64; m++)
		if (flips_for(b, m, p, 0)) { if (moves) moves[n] = m; n++; }
	return n;
}

static void apply(unsigned char *b, int m, int p) {
	int f[32], n = flips_for(b, m, p, f);
	b[m] = (unsigned char)p;
	for (int i = 0; i < n; i++) b[f[i]] = (unsigned char)p;
}

static int count(const unsigned char *b, int p) { int n = 0; for (int i = 0; i < 64; i++) n += b[i] == p; return n; }

/* board value for p: position (X/C squares next to a taken corner stop counting against you) + mobility */
static int evaluate(const unsigned char *b, int p) {
	int v = 0;
	for (int i = 0; i < 64; i++) {
		if (!b[i]) continue;
		int w = WEIGHT[i];
		if (w < 0) {
			int r = i / 8, c = i % 8, cr = r < 4 ? 0 : 7, cc = c < 4 ? 0 : 7;
			if (b[cr * 8 + cc]) w = 3;
		}
		v += b[i] == p ? w : -w;
	}
	return v + 4 * (legal_moves(b, p, 0) - legal_moves(b, 3 - p, 0));
}

static int negamax(const unsigned char *b, int p, int depth, int alpha, int beta, int passed) {
	int moves[32], n = legal_moves(b, p, moves);
	if (!n) {
		if (passed || !legal_moves(b, 3 - p, 0)) { int d = count(b, p) - count(b, 3 - p); return d * 1000; }
		return -negamax(b, 3 - p, depth, -beta, -alpha, 1);
	}
	if (depth == 0) return evaluate(b, p);
	for (int i = 0; i < n; i++) {
		unsigned char nb[64];
		for (int k = 0; k < 64; k++) nb[k] = b[k];
		apply(nb, moves[i], p);
		int v = -negamax(nb, 3 - p, depth - 1, -beta, -alpha, 0);
		if (v > alpha) alpha = v;
		if (alpha >= beta) break;
	}
	return alpha;
}

static int pick(GameCtx *g, S *s) {
	int moves[32], n = legal_moves(s->b, 2, moves), lvl = g_level(g), best = moves[0];
	if (lvl == 0) {
		if (g_rand(g, 2)) return moves[g_rand(g, n)];
		int bf = -1;
		for (int i = 0; i < n; i++) { int f = flips_for(s->b, moves[i], 2, 0) * 8 + g_rand(g, 8); if (f > bf) { bf = f; best = moves[i]; } }
		return best;
	}
	if (lvl == 1) {
		int bv = -(1 << 30);
		for (int i = 0; i < n; i++) {
			int v = WEIGHT[moves[i]] * 4 + flips_for(s->b, moves[i], 2, 0) + g_rand(g, 6);
			if (v > bv) { bv = v; best = moves[i]; }
		}
		return best;
	}
	int empty = 64 - count(s->b, 1) - count(s->b, 2), depth = empty <= 8 ? empty : 4, alpha = -(1 << 30);
	/* try the moves in a random order so equal moves vary between games */
	for (int i = n - 1; i > 0; i--) { int j = g_rand(g, i + 1), t = moves[i]; moves[i] = moves[j]; moves[j] = t; }
	for (int i = 0; i < n; i++) {
		unsigned char nb[64];
		for (int k = 0; k < 64; k++) nb[k] = s->b[k];
		apply(nb, moves[i], 2);
		int v = -negamax(nb, 1, depth - 1, -(1 << 30), -alpha, 0);
		if (v > alpha) { alpha = v; best = moves[i]; }
	}
	return best;
}

static void centre(const S *s, int m, int *x, int *y) { *x = s->x + (m % 8) * s->cell + s->cell / 2; *y = s->y + (m / 8) * s->cell + s->cell / 2; }

static void draw_disc(GameCtx *g, S *s, int m, int p, int clear) {
	int x, y, r = s->cell * 38 / 100;
	centre(s, m, &x, &y);
	if (clear) g_clear(g, s->x + (m % 8) * s->cell + 6, s->y + (m / 8) * s->cell + 6, s->cell - 12, s->cell - 12);
	if (p == 1) g_dot(g, x, y, r);
	else g_circle(g, x, y, r, 6);
}

static void show_score(GameCtx *g, S *s) {
	char t[64];
	snprintf(t, sizeof t, "You %d  -  Me %d", count(s->b, 1), count(s->b, 2));
	g_clear(g, s->hx, s->hy, s->hw, s->hh);
	g_print(g, s->hx, s->hy, s->hw, s->hh, 64, 1, t);
}

static void clear_hints(GameCtx *g, S *s, int except) {
	for (int m = 0; m < 64; m++) {
		if (!s->hint[m]) continue;
		s->hint[m] = 0;
		if (m == except) continue;
		int x, y;
		centre(s, m, &x, &y);
		g_clear(g, x - 16, y - 16, 32, 32);
	}
}

static void show_hints(GameCtx *g, S *s) {
	int moves[32], n = legal_moves(s->b, 1, moves);
	g_speed(g, 10);
	for (int i = 0; i < n; i++) {
		int x, y;
		centre(s, moves[i], &x, &y);
		g_dot(g, x, y, 12);
		s->hint[moves[i]] = 1;
	}
}

/* places p's disc at m and turns the outflanked discs over, one line at a time */
static void play(GameCtx *g, S *s, int m, int p) {
	int f[32], n = flips_for(s->b, m, p, f);
	g_speed(g, p == 1 ? 6 : 3);
	draw_disc(g, s, m, p, 0);
	g_speed(g, 6);
	for (int i = 0; i < n; i++) draw_disc(g, s, f[i], p, 1);
	apply(s->b, m, p);
	show_score(g, s);
}

static void game_over(GameCtx *g, S *s) {
	char t[64];
	int y = count(s->b, 1), m = count(s->b, 2);
	if (y > m) snprintf(t, sizeof t, "You win %d-%d! Well played.", y, m);
	else if (m > y) snprintf(t, sizeof t, "I win %d-%d!", m, y);
	else snprintf(t, sizeof t, "A draw, %d-%d.", y, m);
	g_over(g, t);
}

/* after a move: whose turn is next (or the end) */
static void next_turn(GameCtx *g, S *s, int just_moved) {
	int you = legal_moves(s->b, 1, 0), me = legal_moves(s->b, 2, 0);
	if (!you && !me) { game_over(g, s); return; }
	if (me && (just_moved == 1 || !you)) {
		if (just_moved == 2) g_remark(g, "You have no move - I go again.");
		s->my_turn = 1;
		g_say(g, just_moved == 2 ? "you have no move, so I go again..." : "my turn...");
		g_after(g, just_moved == 2 ? 900 : 450);
		return;
	}
	if (just_moved == 1) g_remark(g, "I have no move - your turn again.");
	s->my_turn = 0;
	show_hints(g, s);
	g_say(g, "your move - tap a square with a dot");
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->hx = ax; s->hy = ay; s->hw = aw; s->hh = 110;
	s->cell = (aw - 80) / 8 < (ah - s->hh - 80) / 8 ? (aw - 80) / 8 : (ah - s->hh - 80) / 8;
	if (s->cell > 210) s->cell = 210;
	s->x = ax + (aw - 8 * s->cell) / 2;
	s->y = ay + s->hh + (ah - s->hh - 8 * s->cell) / 2;
	s->b[27] = s->b[36] = 2;
	s->b[28] = s->b[35] = 1;
	show_score(g, s);
	g_speed(g, 9);
	g_frame(g, s->x, s->y, 8 * s->cell, 8 * s->cell, 6);
	for (int i = 1; i < 8; i++) {
		g_line(g, s->x + i * s->cell, s->y, s->x + i * s->cell, s->y + 8 * s->cell, 3);
		g_line(g, s->x, s->y + i * s->cell, s->x + 8 * s->cell, s->y + i * s->cell, 3);
	}
	g_speed(g, 6);
	for (int m = 0; m < 64; m++) if (s->b[m]) draw_disc(g, s, m, s->b[m], 0);
	show_hints(g, s);
	g_remark(g, "You're black. Outflank my rings!");
	g_say(g, "your move - tap a square with a dot");
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	if (!legal_moves(s->b, 2, 0)) { next_turn(g, s, 2); return; } /* not expected */
	play(g, s, pick(g, s), 2);
	next_turn(g, s, 2);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	int c = (st->cx - s->x) / s->cell, r = (st->cy - s->y) / s->cell;
	if (st->cx < s->x || st->cy < s->y || c > 7 || r > 7) { g_say(g, "tap a square on the board"); return; }
	int m = r * 8 + c;
	if (s->b[m]) { g_say(g, "that square is taken - tap one with a dot"); return; }
	if (!flips_for(s->b, m, 1, 0)) { g_say(g, "that wouldn't outflank anything - tap a square with a dot"); return; }
	clear_hints(g, s, m);
	play(g, s, m, 1);
	next_turn(g, s, 1);
}

const GameDef GAME_reversi = { "reversi", "Reversi", "Outflank and flip - most discs wins", sizeof(S), 0, 1, start, stroke, my_move };

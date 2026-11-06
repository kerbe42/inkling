/* Connect Four: tap a column to drop your disc (black); the Kindle plays the rings. Hard looks ahead further. */
#include "../games.h"

#define C 7
#define R 6
typedef struct {
	int b[R][C]; /* row 0 = bottom; 0 empty, 1 you, 2 me */
	int x, y, cell, my_turn, moves;
} S;

static int drop_row(int b[R][C], int c) { for (int r = 0; r < R; r++) if (!b[r][c]) return r; return -1; }

static int four(int b[R][C], int p, int *line) {
	static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
	for (int r = 0; r < R; r++)
		for (int c = 0; c < C; c++)
			for (int d = 0; d < 4; d++) {
				int k;
				for (k = 0; k < 4; k++) {
					int rr = r + D[d][0] * k, cc = c + D[d][1] * k;
					if (rr < 0 || rr >= R || cc < 0 || cc >= C || b[rr][cc] != p) break;
				}
				if (k == 4) { if (line) { line[0] = r; line[1] = c; line[2] = r + D[d][0] * 3; line[3] = c + D[d][1] * 3; } return 1; }
			}
	return 0;
}

static int window_score(int me, int you) {
	if (me == 4) return 1000;
	if (me == 3 && you == 0) return 6;
	if (me == 2 && you == 0) return 2;
	if (you == 3 && me == 0) return -8;
	return 0;
}

static int evaluate(int b[R][C]) {
	int s = 0;
	for (int r = 0; r < R; r++) s += (b[r][3] == 2) * 3 - (b[r][3] == 1) * 3;
	static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
	for (int r = 0; r < R; r++)
		for (int c = 0; c < C; c++)
			for (int d = 0; d < 4; d++) {
				int me = 0, you = 0, k;
				for (k = 0; k < 4; k++) {
					int rr = r + D[d][0] * k, cc = c + D[d][1] * k;
					if (rr < 0 || rr >= R || cc < 0 || cc >= C) break;
					me += b[rr][cc] == 2;
					you += b[rr][cc] == 1;
				}
				if (k == 4) s += window_score(me, you);
			}
	return s;
}

static const int ORDER[C] = { 3, 2, 4, 1, 5, 0, 6 };

static int negamax(int b[R][C], int depth, int alpha, int beta, int who) {
	if (four(b, 3 - who, 0)) return -100000 - depth;
	if (depth == 0) return who == 2 ? evaluate(b) : -evaluate(b);
	int any = 0;
	for (int k = 0; k < C; k++) {
		int c = ORDER[k], r = drop_row(b, c);
		if (r < 0) continue;
		any = 1;
		b[r][c] = who;
		int v = -negamax(b, depth - 1, -beta, -alpha, 3 - who);
		b[r][c] = 0;
		if (v > alpha) alpha = v;
		if (alpha >= beta) break;
	}
	return any ? alpha : 0;
}

static void center(S *s, int r, int c, int *cx, int *cy) { *cx = s->x + c * s->cell + s->cell / 2; *cy = s->y + (R - 1 - r) * s->cell + s->cell / 2; }

static int full(S *s) { for (int c = 0; c < C; c++) if (!s->b[R - 1][c]) return 0; return 1; }

static int finish(GameCtx *g, S *s, int p) {
	int line[4];
	if (four(s->b, p, line)) {
		int x0, y0, x1, y1;
		center(s, line[0], line[1], &x0, &y0);
		center(s, line[2], line[3], &x1, &y1);
		g_speed(g, 3);
		g_line(g, x0, y0, x1, y1, 10);
		g_over(g, p == 1 ? "Four in a row - you win!" : "Four in a row - I win!");
		return 1;
	}
	if (full(s)) { g_over(g, "The board is full - a draw."); return 1; }
	return 0;
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->cell = aw / C < ah / R ? aw / C : ah / R;
	if (s->cell > 230) s->cell = 230;
	s->x = ax + (aw - C * s->cell) / 2;
	s->y = ay + (ah - R * s->cell) / 2;
	g_speed(g, 8);
	g_frame(g, s->x, s->y, C * s->cell, R * s->cell, 6);
	for (int c = 1; c < C; c++) g_line(g, s->x + c * s->cell, s->y, s->x + c * s->cell, s->y + R * s->cell, 3);
	for (int r = 1; r < R; r++) g_line(g, s->x, s->y + r * s->cell, s->x + C * s->cell, s->y + r * s->cell, 3);
	g_remark(g, "Tap a column to drop your disc.");
	g_say(g, "you're the black discs - tap a column");
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int depth = g_level(g) == 0 ? 1 : g_level(g) == 1 ? 4 : 7;
	int best = -1, bestv = -1000000;
	for (int k = 0; k < C; k++) {
		int c = ORDER[k], r = drop_row(s->b, c);
		if (r < 0) continue;
		s->b[r][c] = 2;
		int v = -negamax(s->b, depth - 1, -1000000, 1000000, 1);
		s->b[r][c] = 0;
		if (g_level(g) == 0) v += g_rand(g, 40);
		if (v > bestv) { bestv = v; best = c; }
	}
	int r = drop_row(s->b, best), cx, cy;
	s->b[r][best] = 2;
	center(s, r, best, &cx, &cy);
	g_speed(g, 3);
	g_circle(g, cx, cy, s->cell * 36 / 100, 6);
	if (!finish(g, s, 2)) { s->my_turn = 0; g_say(g, "your move"); }
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	int c = (st->cx - s->x) / s->cell;
	if (st->cx < s->x || c >= C) { g_say(g, "tap one of the columns"); return; }
	int r = drop_row(s->b, c);
	if (r < 0) { g_say(g, "that column is full"); return; }
	s->b[r][c] = 1;
	int cx, cy;
	center(s, r, c, &cx, &cy);
	g_dot(g, cx, cy, s->cell * 36 / 100);
	if (finish(g, s, 1)) return;
	s->my_turn = 1;
	g_say(g, "my turn...");
	g_after(g, 400);
}

const GameDef GAME_connect4 = { "connect4", "Connect Four", "Drop discs - four in a row wins", sizeof(S), 0, 1, start, stroke, my_move };

/* Five in a Row (Gomoku) on a 13x13 board: tap a point where two lines cross; you're the black stones, the Kindle
 * plays the rings. Five or more in a row wins. Easy plays loosely, Hard also looks at the reply to each move. */
#include "../games.h"

#define N 13
typedef struct {
	unsigned char b[N][N]; /* 0 empty, 1 you, 2 me */
	int x, y, sp, my_turn, moves;
} S;

static const int DIRS[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };

static int abs_i(int v) { return v < 0 ? -v : v; }

/* the 11 cells through (r, c) in direction d, from p's point of view: 0 empty, 1 p's stone, 2 blocked (other/edge);
 * index 5 is (r, c) itself and counts as p's stone */
static void get_line(const unsigned char b[N][N], int r, int c, int d, int p, int *L) {
	for (int k = -5; k <= 5; k++) {
		int rr = r + DIRS[d][0] * k, cc = c + DIRS[d][1] * k;
		L[k + 5] = (rr < 0 || rr >= N || cc < 0 || cc >= N) ? 2 : !b[rr][cc] ? 0 : b[rr][cc] == p ? 1 : 2;
	}
	L[5] = 1;
}

static int is_five(const int *L) {
	for (int s = 1; s <= 5; s++) {
		int k = 0;
		while (k < 5 && L[s + k] == 1) k++;
		if (k == 5) return 1;
	}
	return 0;
}

/* empty cells that would complete five through the centre */
static int four_points(int *L) {
	int n = 0;
	for (int e = 1; e <= 9; e++) {
		if (L[e]) continue;
		L[e] = 1;
		n += is_five(L);
		L[e] = 0;
	}
	return n;
}

enum { T_NONE, T_TWO, T_C3, T_O3, T_FOUR, T_O4, T_FIVE };

static int classify(int *L, int *small) {
	*small = 0;
	if (is_five(L)) return T_FIVE;
	int fp = four_points(L);
	if (fp >= 2) return T_O4;
	if (fp == 1) return T_FOUR;
	int o3 = 0, c3 = 0;
	for (int e = 1; e <= 9; e++) {
		if (L[e]) continue;
		L[e] = 1;
		int f = four_points(L);
		L[e] = 0;
		if (f >= 2) o3 = 1;
		else if (f == 1) c3 = 1;
	}
	if (o3) return T_O3;
	if (c3) return T_C3;
	static const int WV[5] = { 0, 2, 12, 40, 40 };
	for (int s = 1; s <= 5; s++) {
		int cnt = 0, k;
		for (k = 0; k < 5; k++) { if (L[s + k] == 2) break; cnt += L[s + k]; }
		if (k == 5) *small += WV[cnt < 4 ? cnt : 4];
	}
	return T_TWO;
}

/* how good (r, c) would be for p (the cell must be empty) */
static long value(const unsigned char b[N][N], int r, int c, int p) {
	static const long TV[7] = { 0, 0, 600, 6000, 8000, 100000, 1000000 };
	long v = 0;
	int n4 = 0, n3 = 0;
	for (int d = 0; d < 4; d++) {
		int L[11], small;
		get_line(b, r, c, d, p, L);
		int t = classify(L, &small);
		v += TV[t] + small;
		n4 += t == T_FOUR || t == T_O4;
		n3 += t == T_O3;
	}
	if (n4 >= 2 || (n4 && n3)) v += 80000;
	else if (n3 >= 2) v += 40000;
	return v;
}

static int near_stone(const unsigned char b[N][N], int r, int c) {
	for (int dr = -2; dr <= 2; dr++)
		for (int dc = -2; dc <= 2; dc++) {
			int rr = r + dr, cc = c + dc;
			if (rr >= 0 && rr < N && cc >= 0 && cc < N && b[rr][cc]) return 1;
		}
	return 0;
}

static long cell_score(const unsigned char b[N][N], int r, int c, double defend) {
	int dr = r > 6 ? r - 6 : 6 - r, dc = c > 6 ? c - 6 : 6 - c;
	return value(b, r, c, 2) + (long)(defend * value(b, r, c, 1)) + 6 - (dr > dc ? dr : dc);
}

/* the opponent's strongest attacking move on b */
static long best_attack(const unsigned char b[N][N], int p) {
	long best = 0;
	for (int r = 0; r < N; r++)
		for (int c = 0; c < N; c++)
			if (!b[r][c] && near_stone(b, r, c)) { long v = value(b, r, c, p); if (v > best) best = v; }
	return best;
}

static int pick(GameCtx *g, S *s) {
	int lvl = g_level(g);
	enum { K = 10 };
	int top[K], nt = 0;
	long tv[K];
	double defend = lvl == 0 ? 0.45 : 0.85;
	for (int r = 0; r < N; r++)
		for (int c = 0; c < N; c++) {
			if (s->b[r][c] || !near_stone(s->b, r, c)) continue;
			long v = cell_score(s->b, r, c, defend) * 16 + g_rand(g, 16);
			int i;
			if (nt < K) i = nt++;
			else if (v > tv[K - 1]) i = K - 1;
			else continue;
			while (i > 0 && tv[i - 1] < v) { tv[i] = tv[i - 1]; top[i] = top[i - 1]; i--; }
			tv[i] = v; top[i] = r * N + c;
		}
	if (!nt) { /* empty board: centre */
		for (int r = 0; r < N; r++)
			for (int c = 0; c < N; c++)
				if (!s->b[r][c]) { int d = (r - 6) * (r - 6) + (c - 6) * (c - 6); if (!nt || d < tv[0]) { tv[0] = d; top[0] = r * N + c; nt = 1; } }
		return top[0];
	}
	if (lvl == 0) {
		if (tv[0] / 16 >= 1000000 && g_rand(g, 100) < 80) return top[0]; /* usually sees its own five */
		int m = nt < 5 ? nt : 5;
		return top[g_rand(g, 100) < 35 ? 0 : g_rand(g, m)];
	}
	if (lvl == 1 || tv[0] / 16 >= 1000000) return top[0];
	/* Hard: look at the reply - avoid moves that leave the opponent a winning attack */
	int best = top[0];
	double bestv = -1e18;
	for (int i = 0; i < nt; i++) {
		int r = top[i] / N, c = top[i] % N;
		long mine = value(s->b, r, c, 2);
		s->b[r][c] = 2;
		long opp = best_attack(s->b, 1);
		s->b[r][c] = 0;
		if (mine >= 100000 && opp < 1000000) opp = 0; /* we threaten to win first */
		double v = (double)(tv[i] / 16) - 0.7 * (double)opp;
		if (v > bestv) { bestv = v; best = top[i]; }
	}
	return best;
}

static void pt(const S *s, int r, int c, int *x, int *y) { *x = s->x + c * s->sp; *y = s->y + r * s->sp; }

/* five (or more) through (r, c) for p: draws the line and ends the game */
static int finish(GameCtx *g, S *s, int r, int c, int p) {
	for (int d = 0; d < 4; d++) {
		int a = 0, z = 0;
		while (1) { int rr = r - DIRS[d][0] * (a + 1), cc = c - DIRS[d][1] * (a + 1); if (rr < 0 || rr >= N || cc < 0 || cc >= N || s->b[rr][cc] != p) break; a++; }
		while (1) { int rr = r + DIRS[d][0] * (z + 1), cc = c + DIRS[d][1] * (z + 1); if (rr < 0 || rr >= N || cc < 0 || cc >= N || s->b[rr][cc] != p) break; z++; }
		if (a + z + 1 >= 5) {
			int x0, y0, x1, y1;
			pt(s, r - DIRS[d][0] * a, c - DIRS[d][1] * a, &x0, &y0);
			pt(s, r + DIRS[d][0] * z, c + DIRS[d][1] * z, &x1, &y1);
			g_speed(g, 3);
			g_line(g, x0, y0, x1, y1, 10);
			g_over(g, p == 1 ? "Five in a row - you win!" : "Five in a row - I win!");
			return 1;
		}
	}
	if (s->moves == N * N) { g_over(g, "The board is full - a draw."); return 1; }
	return 0;
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->sp = (aw - 160) / (N - 1) < (ah - 160) / (N - 1) ? (aw - 160) / (N - 1) : (ah - 160) / (N - 1);
	if (s->sp > 150) s->sp = 150;
	s->x = ax + (aw - (N - 1) * s->sp) / 2;
	s->y = ay + (ah - (N - 1) * s->sp) / 2;
	int L = (N - 1) * s->sp;
	g_speed(g, 9);
	g_frame(g, s->x, s->y, L, L, 6);
	for (int i = 1; i < N - 1; i++) {
		g_line(g, s->x + i * s->sp, s->y, s->x + i * s->sp, s->y + L, 3);
		g_line(g, s->x, s->y + i * s->sp, s->x + L, s->y + i * s->sp, 3);
	}
	static const int STAR[5][2] = { { 3, 3 }, { 3, 9 }, { 9, 3 }, { 9, 9 }, { 6, 6 } };
	for (int i = 0; i < 5; i++) { int x, y; pt(s, STAR[i][0], STAR[i][1], &x, &y); g_dot(g, x, y, 9); }
	g_remark(g, "Five in a row wins. You're black.");
	g_say(g, "tap a point where two lines cross");
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int m = pick(g, s), r = m / N, c = m % N, x, y, rad = s->sp * 40 / 100;
	s->b[r][c] = 2;
	s->moves++;
	pt(s, r, c, &x, &y);
	g_clear(g, x - rad, y - rad, 2 * rad, 2 * rad);
	g_speed(g, 3);
	g_circle(g, x, y, rad, 6);
	if (finish(g, s, r, c, 2)) return;
	s->my_turn = 0;
	g_say(g, "your move");
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	int c = (st->cx - s->x + s->sp / 2) / s->sp, r = (st->cy - s->y + s->sp / 2) / s->sp;
	if (st->cx < s->x - s->sp / 2 || st->cy < s->y - s->sp / 2 || r >= N || c >= N) { g_say(g, "tap a point on the board"); return; }
	int x, y;
	pt(s, r, c, &x, &y);
	if (abs_i(st->cx - x) > s->sp * 45 / 100 || abs_i(st->cy - y) > s->sp * 45 / 100) { g_say(g, "tap right where two lines cross"); return; }
	if (s->b[r][c]) { g_say(g, "that point is taken"); return; }
	s->b[r][c] = 1;
	s->moves++;
	g_dot(g, x, y, s->sp * 42 / 100);
	if (finish(g, s, r, c, 1)) return;
	s->my_turn = 1;
	g_say(g, "my turn...");
	g_after(g, 300);
}

const GameDef GAME_gomoku = { "gomoku", "Five in a Row", "Gomoku on 13x13 - five stones in a line", sizeof(S), 0, 1, start, stroke, my_move };

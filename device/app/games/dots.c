/* Dots & Boxes: join two neighbouring dots (draw from dot to dot, or tap the middle of the gap). Finishing a box
 * gives another turn. Easy 4x4 boxes, Medium 5x5, Hard 6x6; Hard also handles the long chains at the end. */
#include "../games.h"
#include <stdio.h>

#define MAXN 6
#define MAXE (2 * MAXN * (MAXN + 1))
typedef struct {
	int n, ne;                     /* boxes per side, number of edges */
	unsigned char e[MAXE];         /* 1 = drawn. Horizontal edges first (row r 0..n, col c 0..n-1), then vertical */
	unsigned char own[MAXN][MAXN]; /* 0 free, 1 you, 2 me */
	int x, y, cell, my_turn, score[3], left, took;
	int hx, hy, hw, hh; /* score line */
} S;

static int hid(int n, int r, int c) { return r * n + c; }
static int vid(int n, int r, int c) { return (n + 1) * n + r * (n + 1) + c; }

/* the two dots an edge joins (dot = row, col) */
static void edge_dots(int n, int id, int *r0, int *c0, int *r1, int *c1) {
	if (id < (n + 1) * n) { *r0 = *r1 = id / n; *c0 = id % n; *c1 = *c0 + 1; }
	else { id -= (n + 1) * n; *r0 = id / (n + 1); *c0 = *c1 = id % (n + 1); *r1 = *r0 + 1; }
}

/* boxes next to an edge (up to 2), as r * MAXN + c */
static int edge_boxes(int n, int id, int *b) {
	int k = 0;
	if (id < (n + 1) * n) {
		int r = id / n, c = id % n;
		if (r > 0) b[k++] = (r - 1) * MAXN + c;
		if (r < n) b[k++] = r * MAXN + c;
	} else {
		id -= (n + 1) * n;
		int r = id / (n + 1), c = id % (n + 1);
		if (c > 0) b[k++] = r * MAXN + c - 1;
		if (c < n) b[k++] = r * MAXN + c;
	}
	return k;
}

static void box_edges(int n, int r, int c, int *ed) { ed[0] = hid(n, r, c); ed[1] = hid(n, r + 1, c); ed[2] = vid(n, r, c); ed[3] = vid(n, r, c + 1); }

static int sides(const unsigned char *e, int n, int r, int c) {
	int ed[4], k = 0;
	box_edges(n, r, c, ed);
	for (int i = 0; i < 4; i++) k += e[ed[i]];
	return k;
}

/* the one undrawn edge of a box (or -1) */
static int missing(const unsigned char *e, int n, int r, int c) {
	int ed[4];
	box_edges(n, r, c, ed);
	for (int i = 0; i < 4; i++) if (!e[ed[i]]) return ed[i];
	return -1;
}

/* boxes that drawing this edge would complete */
static int completes(const unsigned char *e, int n, int id) {
	int b[2], k = edge_boxes(n, id, b), m = 0;
	for (int i = 0; i < k; i++) m += sides(e, n, b[i] / MAXN, b[i] % MAXN) == 3;
	return m;
}

/* boxes that drawing this edge would leave with exactly 3 sides (a gift to the next player) */
static int gives(const unsigned char *e, int n, int id) {
	int b[2], k = edge_boxes(n, id, b), m = 0;
	for (int i = 0; i < k; i++) m += sides(e, n, b[i] / MAXN, b[i] % MAXN) == 2;
	return m;
}

/* draws id on a copy, then lets the opponent take everything that falls: how many boxes they get */
static int give_count(const unsigned char *e0, int n, int ne, int id) {
	unsigned char e[MAXE];
	for (int i = 0; i < ne; i++) e[i] = e0[i];
	e[id] = 1;
	int got = 0, more = 1;
	while (more) {
		more = 0;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++)
				if (sides(e, n, r, c) == 3) {
					int m = missing(e, n, r, c);
					got += completes(e, n, m);
					e[m] = 1;
					more = 1;
				}
	}
	return got;
}

static int any_safe(const unsigned char *e, int n, int ne) {
	for (int i = 0; i < ne; i++) if (!e[i] && !completes(e, n, i) && !gives(e, n, i)) return 1;
	return 0;
}

/* Hard, while taking a chain at the end: when exactly two boxes of it are left and the rest of the board still holds a
 * long chain, decline them by drawing the far edge (the opponent gets 2 boxes but must then open the next chain). */
static int double_deal(const S *s) {
	int n = s->n, ne = s->ne, a = -1, cnt = 0;
	for (int r = 0; r < n; r++)
		for (int c = 0; c < n; c++)
			if (sides(s->e, n, r, c) == 3) { cnt++; a = r * MAXN + c; }
	if (cnt != 1) return -1;
	int m = missing(s->e, n, a / MAXN, a % MAXN), b[2], k = edge_boxes(n, m, b), bb = -1;
	for (int i = 0; i < k; i++) if (b[i] != a) bb = b[i];
	if (bb < 0 || sides(s->e, n, bb / MAXN, bb % MAXN) != 2) return -1;
	int ed[4], f = -1;
	box_edges(n, bb / MAXN, bb % MAXN, ed);
	for (int i = 0; i < 4; i++) if (!s->e[ed[i]] && ed[i] != m) f = ed[i];
	if (f < 0) return -1;
	/* the chain must end at bb: f must not hand over a further box */
	int fb[2], fk = edge_boxes(n, f, fb);
	for (int i = 0; i < fk; i++) if (fb[i] != bb && sides(s->e, n, fb[i] / MAXN, fb[i] % MAXN) >= 2) return -1;
	unsigned char e[MAXE];
	for (int i = 0; i < ne; i++) e[i] = s->e[i];
	e[m] = e[f] = 1;
	if (any_safe(e, n, ne)) return -1; /* not the endgame yet: just take them */
	int best = 0;
	for (int i = 0; i < ne; i++)
		if (!e[i]) { int v = give_count(e, n, ne, i); if (v > best) best = v; }
	return best >= 3 ? f : -1;
}

static int pick(GameCtx *g, S *s) {
	int n = s->n, ne = s->ne, lvl = g_level(g), cand[MAXE], nc = 0;
	for (int i = 0; i < ne; i++) if (!s->e[i] && completes(s->e, n, i)) cand[nc++] = i;
	if (nc && !(lvl == 0 && g_rand(g, 100) < 35)) {
		if (lvl == 2) { int d = double_deal(s); if (d >= 0) return d; }
		for (int i = 0; i < nc; i++) if (completes(s->e, n, cand[i]) == 2) return cand[i];
		return cand[g_rand(g, nc)];
	}
	nc = 0;
	for (int i = 0; i < ne; i++) if (!s->e[i] && !completes(s->e, n, i) && !gives(s->e, n, i)) cand[nc++] = i;
	if (nc && !(lvl == 0 && g_rand(g, 100) < 40)) return cand[g_rand(g, nc)];
	if (lvl == 0 || nc) {
		nc = 0;
		for (int i = 0; i < ne; i++) if (!s->e[i]) cand[nc++] = i;
		return cand[g_rand(g, nc)];
	}
	/* every move gives something away: give the least (Hard: and split two-box chains so they can't be declined) */
	int best = 1 << 20, bestg = -1;
	for (int i = 0; i < ne; i++) {
		if (s->e[i]) continue;
		int v = give_count(s->e, n, ne, i), gv = lvl == 2 ? gives(s->e, n, i) : 0;
		if (v < best || (v == best && gv > bestg)) { best = v; bestg = gv; nc = 0; }
		if (v == best && gv == bestg) cand[nc++] = i;
	}
	return cand[g_rand(g, nc)];
}

static void dot_xy(const S *s, int r, int c, int *x, int *y) { *x = s->x + c * s->cell; *y = s->y + r * s->cell; }

static void show_score(GameCtx *g, S *s) {
	char t[64];
	snprintf(t, sizeof t, "You %d  -  Me %d", s->score[1], s->score[2]);
	g_clear(g, s->hx, s->hy, s->hw, s->hh);
	g_print(g, s->hx, s->hy, s->hw, s->hh, 64, 1, t);
}

/* draws the edge for player p and claims any boxes it closes; returns how many */
static int play(GameCtx *g, S *s, int id, int p) {
	int n = s->n, r0, c0, r1, c1, x0, y0, x1, y1, b[2], k = edge_boxes(n, id, b), got = 0;
	edge_dots(n, id, &r0, &c0, &r1, &c1);
	dot_xy(s, r0, c0, &x0, &y0);
	dot_xy(s, r1, c1, &x1, &y1);
	s->e[id] = 1;
	g_speed(g, p == 1 ? 6 : 3);
	g_line(g, x0, y0, x1, y1, 8);
	for (int i = 0; i < k; i++) {
		int r = b[i] / MAXN, c = b[i] % MAXN;
		if (sides(s->e, n, r, c) != 4) continue;
		s->own[r][c] = (unsigned char)p;
		s->score[p]++;
		s->left--;
		got++;
		int bx, by, in = 16;
		dot_xy(s, r, c, &bx, &by);
		if (p == 2) g_box(g, bx + in, by + in, s->cell - 2 * in, s->cell - 2 * in, 205);
		g_print(g, bx + in, by + in, s->cell - 2 * in, s->cell - 2 * in, s->cell / 4, 1, p == 1 ? "You" : "Me");
	}
	if (got) show_score(g, s);
	return got;
}

static int finish(GameCtx *g, S *s) {
	if (s->left) return 0;
	char t[64];
	int y = s->score[1], m = s->score[2];
	if (y > m) snprintf(t, sizeof t, "You win %d-%d! Well played.", y, m);
	else if (m > y) snprintf(t, sizeof t, "I win %d-%d!", m, y);
	else snprintf(t, sizeof t, "A draw, %d-%d.", y, m);
	g_over(g, t);
	return 1;
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->n = 4 + g_level(g);
	s->ne = 2 * s->n * (s->n + 1);
	s->left = s->n * s->n;
	s->hx = ax; s->hy = ay; s->hw = aw; s->hh = 110;
	int top = ay + s->hh + 20, avail = ah - s->hh - 20;
	int side = (aw - 120 < avail - 80 ? aw - 120 : avail - 80);
	s->cell = side / s->n;
	if (s->cell > 380) s->cell = 380;
	s->x = ax + (aw - s->n * s->cell) / 2;
	s->y = top + (avail - s->n * s->cell) / 2;
	show_score(g, s);
	g_speed(g, 10);
	for (int r = 0; r <= s->n; r++)
		for (int c = 0; c <= s->n; c++) {
			int x, y;
			dot_xy(s, r, c, &x, &y);
			g_dot(g, x, y, 13);
		}
	g_remark(g, "Join two dots. Close a box to go again.");
	g_say(g, "draw a line between two neighbouring dots (or tap the gap between them)");
}

static int near_dot(const S *s, int x, int y) {
	int c = (x - s->x + s->cell / 2) / s->cell, r = (y - s->y + s->cell / 2) / s->cell;
	if (x < s->x - s->cell / 2 || y < s->y - s->cell / 2 || r > s->n || c > s->n) return -1;
	int dx, dy;
	dot_xy(s, r, c, &dx, &dy);
	if ((x - dx) * (x - dx) + (y - dy) * (y - dy) > (s->cell * 35 / 100) * (s->cell * 35 / 100)) return -1;
	return r * (MAXN + 1) + c;
}

/* the edge whose middle part is nearest (x, y), or -1; horiz: 1 only horizontal, 0 only vertical, -1 either */
static int near_edge(const S *s, int x, int y, int horiz) {
	int best = -1;
	long bestd = (long)(s->cell / 4) * (s->cell / 4);
	for (int id = 0; id < s->ne; id++) {
		int h = id < (s->n + 1) * s->n;
		if (horiz >= 0 && h != horiz) continue;
		int r0, c0, r1, c1, x0, y0, x1, y1;
		edge_dots(s->n, id, &r0, &c0, &r1, &c1);
		dot_xy(s, r0, c0, &x0, &y0);
		dot_xy(s, r1, c1, &x1, &y1);
		int along = h ? x - x0 : y - y0, across = h ? y - y0 : x - x0;
		if (along < s->cell * 15 / 100 || along > s->cell * 85 / 100) continue;
		long d = (long)across * across;
		if (d < bestd) { bestd = d; best = id; }
	}
	return best;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	int id = -1;
	if (st->tap || st->n < 2) id = near_edge(s, st->cx, st->cy, -1);
	else {
		int a = near_dot(s, st->x[0], st->y[0]), b = near_dot(s, st->x[st->n - 1], st->y[st->n - 1]);
		if (a >= 0 && b >= 0 && a != b) {
			int r0 = a / (MAXN + 1), c0 = a % (MAXN + 1), r1 = b / (MAXN + 1), c1 = b % (MAXN + 1);
			if (r0 == r1 && (c1 - c0 == 1 || c0 - c1 == 1)) id = hid(s->n, r0, c0 < c1 ? c0 : c1);
			else if (c0 == c1 && (r1 - r0 == 1 || r0 - r1 == 1)) id = vid(s->n, r0 < r1 ? r0 : r1, c0);
			else { g_say(g, "join two dots that are next to each other"); return; }
		} else id = near_edge(s, st->cx, st->cy, st->x1 - st->x0 >= st->y1 - st->y0);
	}
	if (id < 0) { g_say(g, "join two neighbouring dots - draw from one to the other, or tap between them"); return; }
	if (s->e[id]) { g_say(g, "those dots are already joined"); return; }
	if (play(g, s, id, 1)) {
		if (finish(g, s)) return;
		g_say(g, "a box! Go again. You %d - Me %d", s->score[1], s->score[2]);
		return;
	}
	s->my_turn = 1;
	s->took = 0;
	g_say(g, "my turn...");
	g_after(g, 450);
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int got = play(g, s, pick(g, s), 2);
	s->took += got;
	if (finish(g, s)) return;
	if (got) { g_after(g, 350); return; }
	s->my_turn = 0;
	if (s->took >= 3) {
		char t[48];
		snprintf(t, sizeof t, "I took %d boxes. Your move.", s->took);
		g_remark(g, t);
	}
	g_say(g, "your move. You %d - Me %d", s->score[1], s->score[2]);
}

const GameDef GAME_dots = { "dots", "Dots & Boxes", "Join the dots, close the boxes", sizeof(S), 0, 1, start, stroke, my_move };

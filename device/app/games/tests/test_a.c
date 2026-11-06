/* Host tests for the agent-A games (dots, gomoku, reversi, battleship, nim, lightsout), run against stub_a.c.
 * Each test plays many games with random legal input mixed with illegal input, keeps its own model of the rules,
 * and checks the screen (the stub's virtual canvas) and the game's calls against that model.
 * Build: gcc -O1 -fsanitize=address,undefined -I device/app device/app/games/tests/stub_a.c \
 *        device/app/games/tests/test_a.c device/app/games/{dots,gomoku,reversi,battleship,nim,lightsout}.c -lm */
#include "games.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- stub interface (stub_a.c) ---- */
typedef struct {
	int t, a, b, c, d, e;
	char s[48];
} StOp;
extern StOp *st_ops;
extern int st_nops, st_over, st_timer_pending, st_busy, st_errors, st_oob, st_draw_after_over, st_timer_calls, st_bad_rand, st_verbose;
extern int st_ax, st_ay, st_aw, st_ah;
extern char st_result[200], st_say_text[200], st_remark_text[200];
void st_error(const char *fmt, ...);
void st_begin(const GameDef *d, int level, unsigned seed);
void st_input(const GStroke *s, int run);
void st_run_timers(void);
void st_tap(int x, int y, int finger, int run);
void st_pen(const int *pts, int k, int jit, int run);
void st_line(int x0, int y0, int x1, int y1, int jit, int run);
int st_px(int x, int y);
int st_dark(int x, int y, int r);
void st_end(void);

#define W(id) extern const GameDef GAME_##id __attribute__((weak));
W(dots) W(gomoku) W(reversi) W(battleship) W(nim) W(lightsout)
#undef W

static unsigned trng = 88172645u;
static int tr(int n) { trng ^= trng << 13; trng ^= trng >> 17; trng ^= trng << 5; return n > 0 ? (int)(trng % (unsigned)n) : 0; }

static int games_run, games_over;
static const char *LV[3] = { "easy", "medium", "hard" };

/* common checks after each game */
static void end_checks(const char *what) {
	if (!st_over) st_error("%s: game did not end", what);
	if (st_oob) st_error("%s: %d drawing calls outside the play area", what, st_oob);
	if (st_bad_rand) st_error("%s: g_rand called with n <= 0 (%d times)", what, st_bad_rand);
	if (st_draw_after_over > 8) st_error("%s: %d drawing calls after g_over", what, st_draw_after_over);
	games_run++;
	games_over += st_over;
}

static int busy_check(int x, int y) { /* input while the engine is still drawing must be ignored */
	int before = st_nops, pend = st_timer_pending;
	st_busy = 1;
	st_tap(x, y, 0, 0);
	st_busy = 0;
	if (st_nops != before || st_timer_pending != pend || st_over) { st_error("input accepted while g_busy()"); return 0; }
	return 1;
}

/* ===================================================================== Dots & Boxes */
#define DN 6
static int dn, dcell, dx0, dy0, dne, d_declines;
static unsigned char de[2 * DN * (DN + 1)], down[DN][DN];

static void d_edge_pts(int id, int *x0, int *y0, int *x1, int *y1) {
	int n = dn;
	if (id < (n + 1) * n) { int r = id / n, c = id % n; *x0 = dx0 + c * dcell; *y0 = *y1 = dy0 + r * dcell; *x1 = *x0 + dcell; }
	else { id -= (n + 1) * n; int r = id / (n + 1), c = id % (n + 1); *x0 = *x1 = dx0 + c * dcell; *y0 = dy0 + r * dcell; *y1 = *y0 + dcell; }
}
static int d_box_sides(const unsigned char *e, int r, int c) {
	int n = dn;
	return e[r * n + c] + e[(r + 1) * n + c] + e[(n + 1) * n + r * (n + 1) + c] + e[(n + 1) * n + r * (n + 1) + c + 1];
}
/* applies edge id for player p in the model; returns boxes closed */
static int d_apply(int id, int p, int *score) {
	int n = dn, got = 0, bx[2][2], k = 0;
	if (id < (n + 1) * n) { int r = id / n, c = id % n; if (r > 0) { bx[k][0] = r - 1; bx[k++][1] = c; } if (r < n) { bx[k][0] = r; bx[k++][1] = c; } }
	else { int j = id - (n + 1) * n, r = j / (n + 1), c = j % (n + 1); if (c > 0) { bx[k][0] = r; bx[k++][1] = c - 1; } if (c < n) { bx[k][0] = r; bx[k++][1] = c; } }
	de[id] = 1;
	for (int i = 0; i < k; i++)
		if (d_box_sides(de, bx[i][0], bx[i][1]) == 4) { down[bx[i][0]][bx[i][1]] = (unsigned char)p; score[p]++; got++; }
	return got;
}
static int d_edge_from_line(const StOp *o) {
	for (int id = 0; id < dne; id++) {
		int x0, y0, x1, y1;
		d_edge_pts(id, &x0, &y0, &x1, &y1);
		if ((o->a == x0 && o->b == y0 && o->c == x1 && o->d == y1) || (o->a == x1 && o->b == y1 && o->c == x0 && o->d == y0)) return id;
	}
	return -1;
}
static int d_canvas_edge(int id) { int x0, y0, x1, y1; d_edge_pts(id, &x0, &y0, &x1, &y1); return st_dark((x0 + x1) / 2, (y0 + y1) / 2, 1) < 100; }

/* greedy test player: takes boxes, else avoids giving a 3rd side, else random */
static int d_greedy_pick(void) {
	int cand[2 * DN * (DN + 1)], nc = 0, n = dn;
	for (int pass = 0; pass < 2 && !nc; pass++)
		for (int id = 0; id < dne; id++) {
			if (de[id]) continue;
			int bx[2][2], k = 0, comp = 0, give = 0;
			if (id < (n + 1) * n) { int r = id / n, c = id % n; if (r > 0) { bx[k][0] = r - 1; bx[k++][1] = c; } if (r < n) { bx[k][0] = r; bx[k++][1] = c; } }
			else { int j = id - (n + 1) * n, r = j / (n + 1), c = j % (n + 1); if (c > 0) { bx[k][0] = r; bx[k++][1] = c - 1; } if (c < n) { bx[k][0] = r; bx[k++][1] = c; } }
			for (int i = 0; i < k; i++) { int sd = d_box_sides(de, bx[i][0], bx[i][1]); comp += sd == 3; give += sd == 2; }
			if (pass == 0 && comp) cand[nc++] = id;
			if (pass == 1 && !give) cand[nc++] = id;
		}
	if (nc) return cand[tr(nc)];
	for (int id = 0; id < dne; id++) if (!de[id]) cand[nc++] = id;
	return cand[tr(nc)];
}

static void test_dots(int games_per_level, int greedy) {
	int wins[3][3] = { { 0 } }; /* [level][0 draw,1 you,2 me] */
	for (int lvl = 0; lvl < 3; lvl++)
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_dots, lvl, 1000u + (unsigned)gi * 7u + (unsigned)lvl);
			dn = 4 + lvl;
			dne = 2 * dn * (dn + 1);
			int first = -1, last = -1;
			for (int i = 0; i < st_nops; i++) if (st_ops[i].t == 'D') { if (first < 0) first = i; last = i; }
			if (first < 0) { st_error("dots: no dots drawn"); st_end(); continue; }
			dx0 = st_ops[first].a; dy0 = st_ops[first].b;
			dcell = (st_ops[last].a - dx0) / dn;
			if (dcell < 100) st_error("dots: cells %d px (want >= 100)", dcell);
			memset(de, 0, sizeof de);
			memset(down, 0, sizeof down);
			int score[3] = { 0 }, steps = 0;
			while (!st_over && steps++ < 5000) {
				int act = tr(100), id = -1, legal = 0, mark = st_nops, tc = st_timer_calls;
				if (act < 18) { /* illegal input */
					int k = tr(6);
					if (k == 0) st_tap(dx0 + tr(dn) * dcell + dcell / 2, dy0 + tr(dn) * dcell + dcell / 2, tr(2), 1); /* box centre */
					else if (k == 1) st_tap(dx0 + tr(dn + 1) * dcell + tr(9) - 4, dy0 + tr(dn + 1) * dcell + tr(9) - 4, tr(2), 1); /* a dot */
					else if (k == 2) st_tap(st_ax + 10 + tr(st_aw - 20), st_ay + 5 + tr(50), tr(2), 1); /* score line */
					else if (k == 3) { int r = tr(dn + 1); st_line(dx0, dy0 + r * dcell, dx0 + 2 * dcell, dy0 + r * dcell, 4, 1); } /* 2 gaps */
					else if (k == 4) st_line(dx0, dy0, dx0 + dcell, dy0 + dcell, 4, 1); /* diagonal */
					else { /* a joined pair again */
						int c = 0;
						for (int i = 0; i < dne; i++) if (de[i]) c++;
						if (!c) continue;
						int pick = tr(c), x0, y0, x1, y1;
						for (int i = 0; i < dne; i++) if (de[i] && !pick--) { id = i; break; }
						d_edge_pts(id, &x0, &y0, &x1, &y1);
						st_tap((x0 + x1) / 2, (y0 + y1) / 2, 0, 1);
					}
				} else {
					id = greedy ? d_greedy_pick() : -1;
					if (id < 0) { int c = 0, cand[2 * DN * (DN + 1)]; for (int i = 0; i < dne; i++) if (!de[i]) cand[c++] = i; id = cand[tr(c)]; }
					int x0, y0, x1, y1, how = tr(4);
					d_edge_pts(id, &x0, &y0, &x1, &y1);
					legal = 1;
					if (how == 0) st_tap((x0 + x1) / 2 + tr(dcell / 5) - dcell / 10, (y0 + y1) / 2 + tr(dcell / 7) - dcell / 14, 0, 0);
					else if (how == 1) st_tap((x0 + x1) / 2, (y0 + y1) / 2, 1, 0);
					else if (how == 2) st_line(x0 + tr(31) - 15, y0 + tr(31) - 15, x1 + tr(31) - 15, y1 + tr(31) - 15, 5, 0);
					else st_line((3 * x0 + x1) / 4, (3 * y0 + y1) / 4 + tr(11) - 5, (x0 + 3 * x1) / 4, (y0 + 3 * y1) / 4, 3, 0); /* short dash in the gap */
					if (st_timer_pending && tr(4) == 0) { /* tap again during my turn: ignored */
						int before = st_nops;
						st_tap((x0 + x1) / 2, (y0 + y1) / 2, 0, 0);
						if (st_nops != before) st_error("dots: input accepted during the computer's turn");
					}
					if (tr(10) == 0 && !st_over) busy_check((x0 + x1) / 2, (y0 + y1) / 2);
					st_run_timers();
				}
				/* compare the board with the model */
				if (!legal) {
					for (int i = 0; i < dne; i++) if (d_canvas_edge(i) != de[i]) { st_error("dots: illegal input changed edge %d", i); break; }
					if (st_timer_calls != tc) st_error("dots: illegal input gave me a turn");
					continue;
				}
				int seq[2 * DN * (DN + 1)], ns = 0;
				for (int i = mark; i < st_nops; i++)
					if (st_ops[i].t == 'L') { int e = d_edge_from_line(&st_ops[i]); if (e >= 0) seq[ns++] = e; else st_error("dots: a line that is not an edge"); }
				if (ns < 1 || seq[0] != id) { st_error("dots: player's edge %d not drawn first (ns %d)", id, ns); break; }
				int pgot = d_apply(id, 1, score);
				if (pgot && ns != 1) st_error("dots: computer moved after the player closed a box");
				if (!pgot && !st_over && ns < 2) st_error("dots: computer did not move");
				for (int k = 1; k < ns; k++) {
					if (de[seq[k]]) st_error("dots: computer drew an existing edge");
					int could = 0;
					for (int r = 0; r < dn; r++)
						for (int c = 0; c < dn; c++) could += d_box_sides(de, r, c) == 3;
					int got = d_apply(seq[k], 2, score);
					if (could && !got) {
						if (lvl == 1) st_error("dots: medium declined a box it could take");
						if (lvl == 2) {
							int now = 0;
							for (int r = 0; r < dn; r++)
								for (int c = 0; c < dn; c++) now += d_box_sides(de, r, c) == 3;
							if (could != 1 || now != 2) st_error("dots: hard declined boxes but not as a double-deal (%d -> %d)", could, now);
							d_declines++;
						}
					}
					int more = k < ns - 1;
					if (more && !got) st_error("dots: computer moved twice without closing a box");
					if (!more && got && !st_over) st_error("dots: computer closed a box but did not move again");
				}
				for (int i = 0; i < dne; i++) if (d_canvas_edge(i) != de[i]) { st_error("dots: edge %d on screen %d, model %d", i, d_canvas_edge(i), de[i]); break; }
			}
			int all = 1;
			for (int i = 0; i < dne; i++) all &= de[i];
			if (st_over != all) st_error("dots: over=%d but all edges drawn=%d", st_over, all);
			/* boxes: mine are shaded */
			for (int r = 0; r < dn; r++)
				for (int c = 0; c < dn; c++) {
					int v = st_px(dx0 + c * dcell + dcell / 4, dy0 + r * dcell + dcell / 4);
					if ((down[r][c] == 2) != (v == 205)) st_error("dots: box %d,%d owner %d shade %d", r, c, down[r][c], v);
				}
			int y = score[1], m = score[2], w = y > m ? 1 : m > y ? 2 : 0;
			char want[64];
			if (w == 1) snprintf(want, sizeof want, "You win %d-%d", y, m);
			else if (w == 2) snprintf(want, sizeof want, "I win %d-%d", m, y);
			else snprintf(want, sizeof want, "draw, %d-%d", y, m);
			if (st_over && !strstr(st_result, want)) st_error("dots: result '%s', expected '%s'", st_result, want);
			wins[lvl][w]++;
			end_checks("dots");
			st_end();
		}
	for (int l = 0; l < 3; l++)
		printf("  dots %-6s vs %s player: %d games, computer won %d, you won %d, draws %d\n", LV[l], greedy ? "greedy" : "random",
		       games_per_level, wins[l][2], wins[l][1], wins[l][0]);
	printf("  dots hard double-deals so far: %d\n", d_declines);
}


/* ===================================================================== Five in a Row */
#define GN 13
static unsigned char gb[GN][GN];
static int gx0, gy0, gsp;
static const int GD[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };

static int g_run(int r, int c, int p, int d) { /* stones of p in a line through (r,c) if p stood there */
	int n = 1;
	for (int sgn = -1; sgn <= 1; sgn += 2)
		for (int k = 1; k < 6; k++) {
			int rr = r + sgn * GD[d][0] * k, cc = c + sgn * GD[d][1] * k;
			if (rr < 0 || rr >= GN || cc < 0 || cc >= GN || gb[rr][cc] != p) break;
			n++;
		}
	return n;
}
static int g_makes5(int r, int c, int p) { for (int d = 0; d < 4; d++) if (g_run(r, c, p, d) >= 5) return 1; return 0; }
static int g_win_cells(int p, int *cells) {
	int n = 0;
	for (int r = 0; r < GN; r++)
		for (int c = 0; c < GN; c++)
			if (!gb[r][c] && g_makes5(r, c, p)) cells[n++] = r * GN + c;
	return n;
}
static int g_near(int r, int c) {
	for (int dr = -1; dr <= 1; dr++)
		for (int dc = -1; dc <= 1; dc++) { int rr = r + dr, cc = c + dc; if (rr >= 0 && rr < GN && cc >= 0 && cc < GN && gb[rr][cc]) return 1; }
	return 0;
}
static int g_bot(int smart) {
	int cells[GN * GN], n;
	if (smart) {
		if ((n = g_win_cells(1, cells))) return cells[tr(n)];
		if ((n = g_win_cells(2, cells))) return cells[tr(n)];
		if (tr(100) < 60) { /* longest own or blocking line */
			int best = -1, bv = -1;
			for (int r = 0; r < GN; r++)
				for (int c = 0; c < GN; c++) {
					if (gb[r][c] || !g_near(r, c)) continue;
					int v = 0;
					for (int d = 0; d < 4; d++) { int a = g_run(r, c, 1, d), b = g_run(r, c, 2, d); v += a * a * 3 + b * b * 2; }
					v = v * 8 + tr(8);
					if (v > bv) { bv = v; best = r * GN + c; }
				}
			if (best >= 0) return best;
		}
	}
	n = 0;
	for (int r = 0; r < GN; r++)
		for (int c = 0; c < GN; c++)
			if (!gb[r][c] && (g_near(r, c) || tr(20) == 0)) cells[n++] = r * GN + c;
	if (!n) for (int r = 0; r < GN; r++) for (int c = 0; c < GN; c++) if (!gb[r][c]) cells[n++] = r * GN + c;
	return cells[tr(n)];
}

static void test_gomoku(int games_per_level, int smart) {
	int wins[3][3] = { { 0 } }, moves[3] = { 0 };
	for (int lvl = 0; lvl < 3; lvl++)
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_gomoku, lvl, 2000u + (unsigned)gi * 13u + (unsigned)lvl);
			int f = -1;
			for (int i = 0; i < st_nops; i++) if (st_ops[i].t == 'F') { f = i; break; }
			if (f < 0) { st_error("gomoku: no board frame"); st_end(); continue; }
			gx0 = st_ops[f].a; gy0 = st_ops[f].b; gsp = st_ops[f].c / (GN - 1);
			if (gsp < 100) st_error("gomoku: spacing %d px", gsp);
			memset(gb, 0, sizeof gb);
			int steps = 0, winner = 0, nst = 0;
			while (!st_over && steps++ < 2000) {
				int mark = st_nops, tc = st_timer_calls;
				if (tr(100) < 12) { /* illegal */
					int k = tr(3);
					if (k == 0 && nst) { int r, c; do { r = tr(GN); c = tr(GN); } while (!gb[r][c]); st_tap(gx0 + c * gsp, gy0 + r * gsp, tr(2), 1); }
					else if (k == 1) st_tap(gx0 + tr(GN - 1) * gsp + gsp / 2, gy0 + tr(GN - 1) * gsp + gsp / 2, tr(2), 1);
					else st_tap(st_ax + 5 + tr(st_aw - 10), gy0 + (GN - 1) * gsp + gsp, 0, 1);
					if (st_nops != mark || st_timer_calls != tc) st_error("gomoku: illegal tap changed the board");
					continue;
				}
				int m = g_bot(smart), r = m / GN, c = m % GN;
				st_tap(gx0 + c * gsp + tr(gsp * 3 / 5) - gsp * 3 / 10, gy0 + r * gsp + tr(gsp * 3 / 5) - gsp * 3 / 10, tr(3) == 0, 0);
				if (st_timer_pending && tr(5) == 0) { int b = st_nops; st_tap(gx0, gy0, 0, 0); if (st_nops != b) st_error("gomoku: input during my turn"); }
				if (tr(10) == 0 && !st_over) busy_check(gx0 + c * gsp, gy0 + r * gsp);
				gb[r][c] = 1;
				nst++;
				if (st_dark(gx0 + c * gsp + gsp / 4, gy0 + r * gsp, 0) != 0) st_error("gomoku: black stone not drawn");
				int pw = 0;
				for (int d = 0; d < 4; d++) if (g_run(r, c, 1, d) >= 5) pw = 1;
				if (pw) {
					if (!st_over || !strstr(st_result, "you win")) st_error("gomoku: player made five but result '%s'", st_result);
					winner = 1;
					break;
				}
				if (st_over && nst < GN * GN) { st_error("gomoku: game ended early: %s", st_result); break; }
				int wc[GN * GN], tcs[GN * GN], nw = g_win_cells(2, wc), nt = g_win_cells(1, tcs);
				st_run_timers();
				int cm = -1;
				for (int i = mark; i < st_nops; i++) if (st_ops[i].t == 'C') {
					int cc = (st_ops[i].a - gx0 + gsp / 2) / gsp, rr = (st_ops[i].b - gy0 + gsp / 2) / gsp;
					if (cm >= 0) st_error("gomoku: two rings in one turn");
					cm = rr * GN + cc;
				}
				if (cm < 0) { if (!st_over) st_error("gomoku: computer did not move"); break; }
				int R = cm / GN, C = cm % GN;
				if (gb[R][C]) { st_error("gomoku: computer played on a stone"); break; }
				if (lvl >= 1) {
					int ok = !nw;
					for (int i = 0; i < nw; i++) ok |= wc[i] == cm;
					if (!ok) st_error("gomoku: %s missed its own five", LV[lvl]);
					if (!nw && nt == 1 && tcs[0] != cm) st_error("gomoku: %s did not block my five", LV[lvl]);
				}
				gb[R][C] = 2;
				nst++;
				if (!st_over && st_dark(gx0 + C * gsp + gsp / 5, gy0 + R * gsp + gsp / 5, 0) != 255) st_error("gomoku: ring interior not white");
				if (st_dark(gx0 + C * gsp + gsp * 40 / 100, gy0 + R * gsp, 2) != 0) st_error("gomoku: ring not drawn");
				int cw = 0;
				for (int d = 0; d < 4; d++) if (g_run(R, C, 2, d) >= 5) cw = 1;
				if (cw != (st_over && strstr(st_result, "I win") != 0)) { st_error("gomoku: five=%d but over=%d '%s'", cw, st_over, st_result); break; }
				if (cw) winner = 2;
				if (!cw && st_over && nst < GN * GN) st_error("gomoku: over without five: %s", st_result);
			}
			wins[lvl][winner]++;
			moves[lvl] += nst;
			end_checks("gomoku");
			st_end();
		}
	for (int l = 0; l < 3; l++)
		printf("  gomoku %-6s vs %s player: %d games, computer won %d, you won %d, other %d, avg %d stones\n", LV[l],
		       smart ? "blocking" : "random", games_per_level, wins[l][2], wins[l][1], wins[l][0], moves[l] / (games_per_level ? games_per_level : 1));
}


/* ===================================================================== Reversi */
static unsigned char rb[64];
static int rx0, ry0, rcell;
static int r_flips(const unsigned char *b, int m, int p, int doit, unsigned char *out) {
	static const int DR[8] = { -1, -1, -1, 0, 0, 1, 1, 1 }, DC[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	if (b[m]) return 0;
	int n = 0;
	for (int d = 0; d < 8; d++) {
		int r = m / 8 + DR[d], c = m % 8 + DC[d], k = 0;
		while (r >= 0 && r < 8 && c >= 0 && c < 8 && b[r * 8 + c] == 3 - p) { r += DR[d]; c += DC[d]; k++; }
		if (k && r >= 0 && r < 8 && c >= 0 && c < 8 && b[r * 8 + c] == p) {
			n += k;
			if (doit) for (int j = 1; j <= k; j++) out[(m / 8 + DR[d] * j) * 8 + m % 8 + DC[d] * j] = (unsigned char)p;
		}
	}
	if (doit && n) out[m] = (unsigned char)p;
	return n;
}
static int r_moves(int p) { int n = 0; for (int m = 0; m < 64; m++) n += r_flips(rb, m, p, 0, 0) > 0; return n; }
/* what the screen shows in cell m: 0 empty, 1 black disc, 2 white ring, 3 hint dot, -1 nonsense */
static int r_screen(int m) {
	int cx = rx0 + (m % 8) * rcell + rcell / 2, cy = ry0 + (m / 8) * rcell + rcell / 2, rr = rcell * 38 / 100;
	int inner = st_px(cx + rcell / 4, cy), ctr = st_dark(cx, cy, 2), ring = st_dark(cx + rr, cy, 2);
	if (inner == 0) return 1;
	if (ring == 0) return ctr == 255 ? 2 : -1;
	if (ctr == 0) return 3;
	return 0;
}
static void test_reversi(int games_per_level, int smart) {
	static const int WT[64] = { 100, -20, 10, 5, 5, 10, -20, 100, -20, -50, -2, -2, -2, -2, -50, -20, 10, -2, -1, -1, -1, -1, -2, 10, 5, -2, -1, -1, -1, -1, -2, 5,
		5, -2, -1, -1, -1, -1, -2, 5, 10, -2, -1, -1, -1, -1, -2, 10, -20, -50, -2, -2, -2, -2, -50, -20, 100, -20, 10, 5, 5, 10, -20, 100 };
	int wins[3][3] = { { 0 } }, passes = 0;
	for (int lvl = 0; lvl < 3; lvl++)
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_reversi, lvl, 3000u + (unsigned)gi * 17u + (unsigned)lvl);
			int f = -1;
			for (int i = 0; i < st_nops; i++) if (st_ops[i].t == 'F') { f = i; break; }
			if (f < 0) { st_error("reversi: no board frame"); st_end(); continue; }
			rx0 = st_ops[f].a; ry0 = st_ops[f].b; rcell = st_ops[f].c / 8;
			if (rcell < 100) st_error("reversi: cells %d px", rcell);
			memset(rb, 0, sizeof rb);
			rb[27] = rb[36] = 2; rb[28] = rb[35] = 1;
			int steps = 0;
			while (!st_over && steps++ < 500) {
				/* the player's turn: screen = model + hints on exactly the legal squares */
				for (int m = 0; m < 64; m++) {
					int want = rb[m] ? rb[m] : r_flips(rb, m, 1, 0, 0) ? 3 : 0, got = r_screen(m);
					if (want != got) { st_error("reversi: cell %d shows %d, expected %d", m, got, want); steps = 1000; break; }
				}
				if (steps >= 1000) break;
				if (!r_moves(1)) { st_error("reversi: my turn but no legal move"); break; }
				int mark = st_nops, tc = st_timer_calls;
				if (tr(100) < 15) { /* illegal */
					int m = tr(64);
					if (!rb[m] && r_flips(rb, m, 1, 0, 0)) continue;
					st_tap(rx0 + (m % 8) * rcell + rcell / 2, ry0 + (m / 8) * rcell + rcell / 2, tr(2), 1);
					if (st_nops != mark || st_timer_calls != tc) st_error("reversi: illegal move %d accepted", m);
					continue;
				}
				int cand[64], nc = 0, best = -1, bv = -(1 << 30);
				for (int m = 0; m < 64; m++)
					if (r_flips(rb, m, 1, 0, 0)) {
						cand[nc++] = m;
						int v = WT[m] * 4 + r_flips(rb, m, 1, 0, 0) + tr(5);
						if (v > bv) { bv = v; best = m; }
					}
				int m = smart ? best : cand[tr(nc)];
				st_tap(rx0 + (m % 8) * rcell + rcell / 2 + tr(rcell / 2) - rcell / 4, ry0 + (m / 8) * rcell + rcell / 2 + tr(rcell / 2) - rcell / 4, tr(3) == 0, 0);
				r_flips(rb, m, 1, 1, rb);
				if (st_timer_pending && tr(5) == 0) { int b = st_nops; st_tap(rx0 + 5, ry0 + 5, 0, 0); if (st_nops != b) st_error("reversi: input during my turn"); }
				if (tr(10) == 0 && !st_over) busy_check(rx0 + rcell / 2, ry0 + rcell / 2);
				int mark2 = st_nops;
				st_run_timers();
				/* replay the computer's moves from its drawing: a ring on an empty square is a move */
				int nmoves = 0;
				for (int i = mark2; i < st_nops; i++) {
					if (st_ops[i].t != 'C') continue;
					int c = (st_ops[i].a - rx0) / rcell, r = (st_ops[i].b - ry0) / rcell, cm = r * 8 + c;
					if (rb[cm] == 2) continue;
					if (rb[cm] == 1) { st_error("reversi: ring over a black disc that was not flipped"); continue; }
					if (!r_flips(rb, cm, 2, 0, 0)) st_error("reversi: computer played illegal %d", cm);
					if (nmoves && r_moves(1)) st_error("reversi: computer moved again although I could move");
					r_flips(rb, cm, 2, 1, rb);
					nmoves++;
				}
				if (nmoves > 1) passes += nmoves - 1;
				if (!nmoves && !st_over && r_moves(2)) st_error("reversi: computer could move but did not");
				if (!st_over && !r_moves(1)) st_error("reversi: game goes on but I have no move");
			}
			if (st_over && (r_moves(1) || r_moves(2))) st_error("reversi: ended while a move was possible");
			int y = 0, me = 0;
			for (int m = 0; m < 64; m++) { y += rb[m] == 1; me += rb[m] == 2; }
			for (int m = 0; m < 64; m++) { int got = r_screen(m); if (got != rb[m]) { st_error("reversi: final cell %d shows %d, model %d", m, got, rb[m]); break; } }
			char want[64];
			if (y > me) snprintf(want, sizeof want, "You win %d-%d", y, me);
			else if (me > y) snprintf(want, sizeof want, "I win %d-%d", me, y);
			else snprintf(want, sizeof want, "draw, %d-%d", y, me);
			if (st_over && !strstr(st_result, want)) st_error("reversi: result '%s', expected '%s'", st_result, want);
			wins[lvl][y > me ? 1 : me > y ? 2 : 0]++;
			end_checks("reversi");
			st_end();
		}
	for (int l = 0; l < 3; l++)
		printf("  reversi %-6s vs %s player: %d games, computer won %d, you won %d, draws %d\n", LV[l], smart ? "positional" : "random",
		       games_per_level, wins[l][2], wins[l][1], wins[l][0]);
	printf("  reversi: %d extra turns after a pass\n", passes);
}


/* ===================================================================== Battleship */
static int bbx, bby, bB, bsx, bsy, bS;
static unsigned char bshot[10][10], bhit[10][10]; /* the player's shots at my waters */
static void b_cell(int big, int r, int c, int *x, int *y) {
	if (big) { *x = bbx + c * bB + bB / 2; *y = bby + r * bB + bB / 2; }
	else { *x = bsx + c * bS + bS / 2; *y = bsy + r * bS + bS / 2; }
}
static int b_pick(int smart) {
	int cand[100], n = 0;
	if (smart) {
		static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
		for (int r = 0; r < 10; r++)
			for (int c = 0; c < 10; c++)
				if (bhit[r][c])
					for (int d = 0; d < 4; d++) {
						int rr = r + D[d][0], cc = c + D[d][1];
						if (rr >= 0 && rr < 10 && cc >= 0 && cc < 10 && !bshot[rr][cc]) cand[n++] = rr * 10 + cc;
					}
		if (!n) for (int r = 0; r < 10; r++) for (int c = 0; c < 10; c++) if (!bshot[r][c] && (r + c) % 2 == 0) cand[n++] = r * 10 + c;
	}
	if (!n) for (int r = 0; r < 10; r++) for (int c = 0; c < 10; c++) if (!bshot[r][c]) cand[n++] = r * 10 + c;
	return cand[tr(n)];
}
static void test_battleship(int games_per_level, int smart) {
	int wins[3][3] = { { 0 } }, cshots[3] = { 0 }, cwins[3] = { 0 };
	for (int lvl = 0; lvl < 3; lvl++)
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_battleship, lvl, 4000u + (unsigned)gi * 19u + (unsigned)lvl);
			int f1 = -1, f2 = -1;
			for (int i = 0; i < st_nops; i++) if (st_ops[i].t == 'F') { if (f1 < 0) f1 = i; else { f2 = i; break; } }
			if (f2 < 0) { st_error("battleship: grids not found"); st_end(); continue; }
			bbx = st_ops[f1].a; bby = st_ops[f1].b; bB = st_ops[f1].c / 10;
			bsx = st_ops[f2].a; bsy = st_ops[f2].b; bS = st_ops[f2].c / 10;
			if (bB < 100) st_error("battleship: target cells %d px", bB);
			/* my fleet on the small grid: gray cells */
			unsigned char ship[10][10], cshot[10][10];
			int nship = 0, myhits = 0, chits = 0, nshots = 0, winner = 0;
			memset(ship, 0, sizeof ship); memset(cshot, 0, sizeof cshot); memset(bshot, 0, sizeof bshot); memset(bhit, 0, sizeof bhit);
			for (int r = 0; r < 10; r++)
				for (int c = 0; c < 10; c++) { int x, y; b_cell(0, r, c, &x, &y); ship[r][c] = st_px(x - bS / 3, y) == 150; nship += ship[r][c]; }
			if (nship != 17) st_error("battleship: my fleet has %d cells", nship);
			int steps = 0;
			while (!st_over && steps++ < 1000) {
				int mark = st_nops, tc = st_timer_calls;
				if (tr(100) < 12) { /* illegal */
					int k = tr(3), x, y;
					if (k == 0) { int any = 0; for (int r = 0; r < 10; r++) for (int c = 0; c < 10; c++) any |= bshot[r][c]; if (!any) continue;
						int r, c; do { r = tr(10); c = tr(10); } while (!bshot[r][c]); b_cell(1, r, c, &x, &y); }
					else if (k == 1) b_cell(0, tr(10), tr(10), &x, &y);
					else { x = bbx + tr(10 * bB); y = bby - 30; }
					st_tap(x, y, tr(2), 1);
					if (st_nops != mark || st_timer_calls != tc) st_error("battleship: illegal tap accepted");
					continue;
				}
				int m = b_pick(smart), r = m / 10, c = m % 10, x, y;
				b_cell(1, r, c, &x, &y);
				st_tap(x + tr(bB / 2) - bB / 4, y + tr(bB / 2) - bB / 4, tr(3) == 0, 0);
				if (st_timer_pending && tr(5) == 0) { int b = st_nops; b_cell(1, (r + 1) % 10, c, &x, &y); st_tap(x, y, 0, 0); if (st_nops != b) st_error("battleship: input during my turn"); }
				if (tr(10) == 0 && !st_over) busy_check(x, y);
				bshot[r][c] = 1;
				b_cell(1, r, c, &x, &y);
				if (st_dark(x, y, 1) != 0) st_error("battleship: shot not marked");
				int h = st_px(x + bB / 6, y + bB / 6) == 0;
				bhit[r][c] = (unsigned char)h;
				myhits += h;
				if (myhits == 17) {
					if (!st_over || !strstr(st_result, "you win")) st_error("battleship: 17 hits but '%s'", st_result);
					winner = 1;
					break;
				}
				if (st_over) { st_error("battleship: over after %d hits: %s", myhits, st_result); break; }
				st_run_timers();
				/* exactly one new shot on my fleet, X on ships only */
				int newshots = 0;
				for (int rr = 0; rr < 10; rr++)
					for (int cc = 0; cc < 10; cc++) {
						int sx2, sy2;
						b_cell(0, rr, cc, &sx2, &sy2);
						int marked = st_dark(sx2, sy2, 1) == 0;
						if (marked && !cshot[rr][cc]) {
							newshots++;
							cshot[rr][cc] = 1;
							int isx = st_px(sx2 + bS / 5, sy2 + bS / 5) == 0;
							if (isx != ship[rr][cc]) st_error("battleship: computer shot at %d,%d drawn as %s on %s", rr, cc, isx ? "X" : "dot", ship[rr][cc] ? "ship" : "water");
							chits += ship[rr][cc];
						} else if (!marked && cshot[rr][cc]) st_error("battleship: a shot mark disappeared");
					}
				nshots += newshots;
				if (newshots != 1) st_error("battleship: computer made %d shots in one turn", newshots);
				if (chits == 17) {
					if (!st_over || !strstr(st_result, "I win")) st_error("battleship: my fleet sunk but '%s'", st_result);
					winner = 2;
					cwins[lvl]++;
					cshots[lvl] += nshots;
					break;
				}
				if (st_over) { st_error("battleship: over early: %s", st_result); break; }
			}
			wins[lvl][winner]++;
			end_checks("battleship");
			st_end();
		}
	for (int l = 0; l < 3; l++)
		printf("  battleship %-6s vs %s player: %d games, computer won %d, you won %d; computer needed %.1f shots on average\n", LV[l],
		       smart ? "hunting" : "random", games_per_level, wins[l][2], wins[l][1], cwins[l] ? (double)cshots[l] / cwins[l] : 0.0);
}


/* ===================================================================== Nim */
static int nx[5][9], ntop[5], nbot[5], nlen[5], nrow, nsp, nbx, nby, nbw, nbh, nheap[5];
static unsigned char ntaken[5][9];
static int n_isP(const int *h) { /* the side to move loses with perfect play (misere) */
	int big = 0, ones = 0, x = 0;
	for (int i = 0; i < nrow; i++) { big += h[i] > 1; ones += h[i] == 1; x ^= h[i]; }
	return big ? x == 0 : ones % 2 == 1;
}
static int n_screen_taken(int r, int j) {
	int mid = (ntop[r] + nbot[r]) / 2, dx = nsp * 32 / 100, dy = (nbot[r] - ntop[r] + 20) * 30 / 100;
	return st_dark(nx[r][j] - dx / 2, mid + dy / 2, 2) == 0;
}
/* rows in which a straight stroke crosses sticks that are still there */
static int n_cross_rows(int x0, int y0, int x1, int y1) {
	int rows = 0;
	for (int r = 0; r < nrow; r++) {
		int any = 0;
		for (int j = 0; j < nlen[r]; j++) {
			int x = nx[r][j];
			if (ntaken[r][j] || x < x0 || x > x1 || x0 == x1) continue;
			int y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
			if (y >= ntop[r] - 30 && y <= nbot[r] + 10) any = 1;
		}
		rows += any;
	}
	return rows;
}
static int n_ring_y(int r) { int gap = (ntop[1] - ntop[0]) * 16 / 100; return nbot[r] + gap * 6 / 10 + gap * 3 / 10; }
static int n_perfect(int *row, int *take) { /* a winning move if there is one */
	int h[5];
	for (int r = 0; r < nrow; r++) h[r] = nheap[r];
	for (int r = 0; r < nrow; r++)
		for (int k = 1; k <= h[r]; k++) {
			h[r] -= k;
			int t = 0;
			for (int q = 0; q < nrow; q++) t += h[q];
			int p = t ? n_isP(h) : 0;
			h[r] += k;
			if (p) { *row = r; *take = k; return 1; }
		}
	return 0;
}
static int n_total(void) { int t = 0; for (int r = 0; r < nrow; r++) t += nheap[r]; return t; }
static void test_nim(int games_per_level, int perfect) {
	int wins[3][3] = { { 0 } }, firsts[3][2] = { { 0 } }, nonopt = 0, chances = 0;
	static const int MAXR[3] = { 3, 4, 5 }, MINR[3] = { 3, 4, 4 }, MAXS[3] = { 5, 7, 9 };
	for (int lvl = 0; lvl < 3; lvl++) {
		int seen[64][5], nseen = 0, same_recent = 0, minsum = 99, maxsum = 0, rowsseen[6] = { 0 };
		memset(seen, 0, sizeof seen);
		int prev[8][5], nprev = 0;
		memset(prev, 0, sizeof prev);
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_nim, lvl, 5000u + (unsigned)gi * 23u + (unsigned)lvl);
			/* read the board off the screen: sticks are the vertical 10-wide lines, rows share a top */
			int k = 0, fb = -1, nr = 0, lasttop = -1;
			memset(nlen, 0, sizeof nlen);
			for (int i = 0; i < st_nops; i++)
				if (st_ops[i].t == 'L' && st_ops[i].e == 10 && st_ops[i].a == st_ops[i].c) {
					if (st_ops[i].b != lasttop) { if (nr == 5) { nr = 6; break; } nr++; lasttop = st_ops[i].b; }
					int r = nr - 1;
					if (nlen[r] == 9) { nr = 6; break; }
					nx[r][nlen[r]++] = st_ops[i].a; ntop[r] = st_ops[i].b; nbot[r] = st_ops[i].d;
					k++;
				}
			for (int i = 0; i < st_nops; i++) if (st_ops[i].t == 'F') fb = i;
			if (nr > 5 || fb < 0 || nr < 3) { st_error("nim: bad board (%d rows)", nr); st_end(); continue; }
			nrow = nr;
			int sum = 0, ok = 1;
			for (int r = 0; r < nrow; r++) { sum += nlen[r]; if (nlen[r] < 1 || nlen[r] > MAXS[lvl]) ok = 0; }
			if (nrow < MINR[lvl] || nrow > MAXR[lvl] || !ok || sum < 10) {
				st_error("nim: %s board out of range (%d rows, %d sticks)", LV[lvl], nrow, sum);
				st_end();
				continue;
			}
			if (sum < minsum) minsum = sum;
			if (sum > maxsum) maxsum = sum;
			rowsseen[nrow]++;
			nbx = st_ops[fb].a; nby = st_ops[fb].b; nbw = st_ops[fb].c; nbh = st_ops[fb].d;
			/* layout: everything inside the play area, rows evenly spaced, Done below them */
			if (nbx < st_ax || nbx + nbw > st_ax + st_aw || nby + nbh > st_ay + st_ah) st_error("nim: Done button outside the area");
			if (ntop[0] < st_ay + 80) st_error("nim: rows start too high");
			if (nby < nbot[nrow - 1]) st_error("nim: Done overlaps the rows");
			for (int r = 1; r < nrow; r++) if (ntop[r] - ntop[r - 1] != ntop[1] - ntop[0]) st_error("nim: uneven rows");
			nsp = 0;
			for (int r = 0; r < nrow && !nsp; r++) if (nlen[r] > 1) nsp = nx[r][1] - nx[r][0];
			if (!nsp) nsp = 150;
			for (int r = 0; r < nrow; r++) {
				if (nx[r][0] < st_ax || nx[r][nlen[r] - 1] > st_ax + st_aw) st_error("nim: row %d outside the area", r);
				for (int j = 1; j < nlen[r]; j++) if (nx[r][j] - nx[r][j - 1] != nsp) st_error("nim: uneven spacing");
			}
			if (!strstr(st_remark_text, "last stick loses") && !strstr(st_remark_text, "Last stick loses")) st_error("nim: rule not said at start: %s", st_remark_text);
			memset(ntaken, 0, sizeof ntaken);
			for (int r = 0; r < nrow; r++) nheap[r] = nlen[r];
			/* distinct boards (as shown) and no repeat among the last 8 */
			int b5[5] = { 0 };
			for (int r = 0; r < nrow; r++) b5[r] = nlen[r];
			int isnew = 1;
			for (int q = 0; q < nseen; q++) if (!memcmp(seen[q], b5, sizeof b5)) isnew = 0;
			if (isnew && nseen < 64) memcpy(seen[nseen++], b5, sizeof b5);
			for (int q = 0; q < nprev && q < 8; q++) if (!memcmp(prev[q], b5, sizeof b5)) same_recent++;
			memmove(prev[1], prev[0], sizeof prev[0] * 7);
			memcpy(prev[0], b5, sizeof b5);
			if (nprev < 8) nprev++;
			int comp_first = 0, last_mover = 0, steps = 0;
			/* the computer may have opened */
			for (int r = 0; r < nrow; r++) for (int j = 0; j < nlen[r]; j++) if (n_screen_taken(r, j)) comp_first = 1;
			firsts[lvl][comp_first]++;
			/* fairness: the side to move wins with perfect play if it is you; so you start exactly when the start is a win */
			if (comp_first != n_isP(nheap)) st_error("nim: starter rule broken: %s starts a board that is %s for the mover", comp_first ? "I" : "you", n_isP(nheap) ? "lost" : "won");
			if (comp_first != (strstr(st_remark_text, "I'll start") != 0)) st_error("nim: remark does not match who starts: %s", st_remark_text);
			int pending_comp = comp_first;
			while (steps++ < 300) {
				if (pending_comp) { /* read the computer's move off the screen */
					int before[5], took = 0, row = -1;
					for (int r = 0; r < nrow; r++) before[r] = nheap[r];
					for (int r = 0; r < nrow; r++)
						for (int j = 0; j < nlen[r]; j++)
							if (n_screen_taken(r, j) && !ntaken[r][j]) {
								if (row >= 0 && row != r) st_error("nim: computer took from two rows");
								row = r; ntaken[r][j] = 1; nheap[r]--; took++;
							} else if (!n_screen_taken(r, j) && ntaken[r][j]) st_error("nim: a taken stick came back");
					if (!took) { st_error("nim: computer did not move"); break; }
					if (lvl == 2 && !n_isP(before) && n_total() != 0) {
						chances++;
						if (!n_isP(nheap)) { nonopt++; st_error("nim: hard missed the winning move (%d %d %d %d %d)", before[0], before[1], before[2], before[3], before[4]); }
					}
					last_mover = 2;
					pending_comp = 0;
				}
				int t = n_total();
				if (!t) break;
				if (st_over) { st_error("nim: over with %d sticks left", t); break; }
				/* no stale selection rings */
				for (int r = 0; r < nrow; r++)
					for (int j = 0; j < nlen[r]; j++) {
						int ry = n_ring_y(r);
						if (st_dark(nx[r][j], ry, 3) == 0) { st_error("nim: stale selection mark at row %d stick %d", r, j); r = nrow; break; }
					}
				int mark = st_nops, tc = st_timer_calls;
				if (tr(100) < 15) { /* illegal */
					int kk = tr(4);
					int lr = nrow - 1;
					if (kk == 0) { /* across rows */
						int x0 = nx[lr][0] - 40 + tr(200), y0 = ntop[0] + tr(nbot[lr] - ntop[0]), x1 = nx[lr][nlen[lr] - 1] + 40 - tr(200), y1 = ntop[0] + tr(nbot[lr] - ntop[0]);
						if (n_cross_rows(x0, y0, x1, y1) < 2) continue;
						st_line(x0, y0, x1, y1, 0, 1);
					}
					else if (kk == 1) st_tap(nbx + nbw / 2, nby + nbh / 2, tr(2), 1); /* Done with nothing picked */
					else if (kk == 2) st_tap(nx[lr][nlen[lr] - 1] + nsp, ntop[lr] - 40, tr(2), 1);
					else { int any = 0; for (int r = 0; r < nrow; r++) for (int j = 0; j < nlen[r]; j++) any |= ntaken[r][j];
						if (!any) continue;
						int r, j; do { r = tr(nrow); j = tr(nlen[r]); } while (!ntaken[r][j]);
						st_tap(nx[r][j], (ntop[r] + nbot[r]) / 2, tr(2), 1); }
					if (st_timer_calls != tc) st_error("nim: illegal input gave me a turn");
					for (int r = 0; r < nrow; r++) for (int j = 0; j < nlen[r]; j++) if (n_screen_taken(r, j) != ntaken[r][j]) { st_error("nim: illegal input took a stick"); r = nrow; break; }
					(void)mark;
					continue;
				}
				int row = 0, take = 1;
				if (!(perfect && n_perfect(&row, &take))) {
					int rows[5], nrw = 0;
					for (int r = 0; r < nrow; r++) if (nheap[r]) rows[nrw++] = r;
					row = rows[tr(nrw)];
					take = 1 + tr(nheap[row]);
				}
				/* which sticks: a run of untaken ones from a random end, possibly with taken ones in between */
				unsigned char want[9] = { 0 };
				int fromleft = tr(2), got = 0, j0 = -1, j1 = -1;
				for (int q = 0; q < nlen[row] && got < take; q++) {
					int j = fromleft ? q : nlen[row] - 1 - q;
					if (j0 < 0 && !ntaken[row][j]) j0 = j;
					if (!ntaken[row][j]) { want[j] = 1; got++; j1 = j; }
				}
				int lo = j0 < j1 ? j0 : j1, hi = j0 < j1 ? j1 : j0;
				if (tr(2)) { /* strike through */
					int mid = (ntop[row] + nbot[row]) / 2 + tr(41) - 20;
					st_line(nx[row][lo] - nsp / 3, mid + tr(31) - 15, nx[row][hi] + nsp / 3, mid + tr(31) - 15, 4, 0);
				} else { /* tap them, maybe with detours, then Done */
					if (tr(3) == 0) { int r2 = (row + 1 + tr(nrow - 1)) % nrow; for (int j = 0; j < nlen[r2]; j++) if (!ntaken[r2][j]) { st_tap(nx[r2][j], (ntop[r2] + nbot[r2]) / 2, 0, 0); break; } }
					for (int j = 0; j < nlen[row]; j++)
						if (want[j]) {
							if (tr(5) == 0) { st_tap(nx[row][j] + tr(21) - 10, (ntop[row] + nbot[row]) / 2, tr(2), 0); st_tap(nx[row][j], ntop[row] + 10, 0, 0); } /* on, off */
							st_tap(nx[row][j] + tr(31) - 15, (ntop[row] + nbot[row]) / 2 + tr(81) - 40, tr(2), 0);
							if (st_dark(nx[row][j], n_ring_y(row), 3) != 0) st_error("nim: picked stick has no mark");
						}
					st_tap(nbx + 20 + tr(nbw - 40), nby + 10 + tr(nbh - 20), tr(2), 0);
				}
				if (st_timer_pending && tr(5) == 0) { int b = st_nops; st_tap(nbx + nbw / 2, nby + nbh / 2, 0, 0); if (st_nops != b) st_error("nim: input during my turn"); }
				if (tr(10) == 0 && !st_over) busy_check(nx[nrow - 1][0], (ntop[nrow - 1] + nbot[nrow - 1]) / 2);
				for (int j = 0; j < nlen[row]; j++)
					if (want[j]) { ntaken[row][j] = 1; nheap[row]--; }
				for (int r = 0; r < nrow; r++) for (int j = 0; j < nlen[r]; j++) if (n_screen_taken(r, j) != ntaken[r][j]) { st_error("nim: stick %d/%d shows %d, model %d", r, j, n_screen_taken(r, j), ntaken[r][j]); r = nrow; break; }
				last_mover = 1;
				if (n_total() == 0) break;
				st_run_timers();
				pending_comp = 1;
			}
			int winner = last_mover == 1 ? 2 : 1;
			if (!st_over) st_error("nim: no g_over after the last stick");
			else if (!strstr(st_result, winner == 1 ? "you win" : "I win")) st_error("nim: last mover %d but result '%s'", last_mover, st_result);
			if (perfect && winner == 2) st_error("nim: a perfect player lost (%s level, %s started): every board must be winnable", LV[lvl], comp_first ? "I" : "you");
			wins[lvl][winner]++;
			end_checks("nim");
			st_end();
		}
		if (nseen < (lvl == 0 ? 20 : 50) && games_per_level >= 50) st_error("nim: %s only %d different boards in %d games", LV[lvl], nseen, games_per_level);
		if (same_recent) st_error("nim: %s repeated a board from the last 8 games %d times", LV[lvl], same_recent);
		printf("  nim %-6s: %d games, %d-%d rows, %d-%d sticks, %d different boards%s\n", LV[lvl], games_per_level, MINR[lvl], MAXR[lvl], minsum, maxsum, nseen, nseen == 64 ? "+" : "");
		(void)rowsseen;
	}
	for (int l = 0; l < 3; l++)
		printf("  nim %-6s vs %s player: %d games (I started %d), computer won %d, you won %d\n", LV[l], perfect ? "perfect" : "random",
		       games_per_level, firsts[l][1], wins[l][2], wins[l][1]);
	printf("  nim hard: took the winning move in %d of %d winning positions\n", chances - nonopt, chances);
}

/* 1000 starts per level: sizes, the starter rule and variety, read straight off the first screen */
static void test_nim_starts(void) {
	static const int MAXR[3] = { 3, 4, 5 }, MINR[3] = { 3, 4, 4 }, MAXS[3] = { 5, 7, 9 };
	for (int lvl = 0; lvl < 3; lvl++) {
		int distinct = 0, rowsn[6] = { 0 }, compfirst = 0;
		static unsigned hashes[1000];
		for (int gi = 0; gi < 1000; gi++) {
			st_begin(&GAME_nim, lvl, 91000u + (unsigned)gi * 7u);
			int nr = 0, lasttop = -1, sum = 0, ok = 1;
			memset(nlen, 0, sizeof nlen);
			for (int i = 0; i < st_nops && ok; i++)
				if (st_ops[i].t == 'L' && st_ops[i].e == 10 && st_ops[i].a == st_ops[i].c) {
					if (st_ops[i].b != lasttop) { if (nr == 5) { ok = 0; break; } nr++; lasttop = st_ops[i].b; }
					if (nlen[nr - 1] == 9) { ok = 0; break; }
					nlen[nr - 1]++;
				}
			if (!ok) { st_error("nim: too big a board"); st_end(); continue; }
			unsigned h = 2166136261u;
			for (int r = 0; r < nr; r++) { sum += nlen[r]; if (nlen[r] < 1 || nlen[r] > MAXS[lvl]) st_error("nim: %s row of %d sticks", LV[lvl], nlen[r]); h = (h ^ (unsigned)nlen[r]) * 16777619u; }
			if (nr < MINR[lvl] || nr > MAXR[lvl]) st_error("nim: %s has %d rows", LV[lvl], nr);
			if (sum < 10) st_error("nim: only %d sticks", sum);
			rowsn[nr]++;
			int heap[5] = { 0 }, win = 0;
			for (int r = 0; r < nr; r++) heap[r] = nlen[r];
			nrow = nr;
			win = !n_isP(heap);
			int told = strstr(st_remark_text, "I'll start") != 0;
			if (told == win) st_error("nim: %s board %d-%d-%d-%d-%d: starter rule (win=%d, remark '%s')", LV[lvl], heap[0], heap[1], heap[2], heap[3], heap[4], win, st_remark_text);
			compfirst += told;
			int dup = 0;
			for (int q = 0; q < distinct; q++) dup |= hashes[q] == h;
			if (!dup) hashes[distinct++] = h;
			st_end();
		}
		printf("  nim %-6s: 1000 starts, %d different boards, rows 3/4/5 = %d/%d/%d, I started %d\n", LV[lvl], distinct, rowsn[3], rowsn[4], rowsn[5], compfirst);
		if (distinct < (lvl == 0 ? 30 : 200)) st_error("nim: %s only %d different boards in 1000 starts", LV[lvl], distinct);
		if (lvl == 2 && (!rowsn[4] || !rowsn[5])) st_error("nim: hard should vary 4 and 5 rows");
		if (compfirst == 0 || compfirst == 1000) st_error("nim: %s the same side always starts", LV[lvl]);
	}
}


/* ===================================================================== Lights Out */
static int lx[25], ly[25], lw;
static unsigned l_mask(int i) {
	unsigned m = 1u << i;
	int r = i / 5, c = i % 5;
	if (r) m |= 1u << (i - 5);
	if (r < 4) m |= 1u << (i + 5);
	if (c) m |= 1u << (i - 1);
	if (c < 4) m |= 1u << (i + 1);
	return m;
}
/* Gaussian elimination over GF(2), then the lightest solution over the null space; -1 if unsolvable */
static int l_solve(unsigned lit, unsigned *best_sol) {
	unsigned eq[25], rhs[25];
	for (int j = 0; j < 25; j++) { eq[j] = l_mask(j); rhs[j] = lit >> j & 1; } /* the press matrix is symmetric */
	int piv[25], np = 0, row = 0;
	for (int v = 0; v < 25 && row < 25; v++) {
		int p = -1;
		for (int j = row; j < 25; j++) if (eq[j] >> v & 1) { p = j; break; }
		if (p < 0) continue;
		unsigned t = eq[p]; eq[p] = eq[row]; eq[row] = t;
		t = rhs[p]; rhs[p] = rhs[row]; rhs[row] = t;
		for (int j = 0; j < 25; j++) if (j != row && (eq[j] >> v & 1)) { eq[j] ^= eq[row]; rhs[j] ^= rhs[row]; }
		piv[np++] = v;
		row++;
	}
	for (int j = row; j < 25; j++) if (rhs[j]) return -1;
	unsigned pivmask = 0;
	for (int k = 0; k < np; k++) pivmask |= 1u << piv[k];
	int freev[25], nf = 0;
	for (int v = 0; v < 25; v++) if (!(pivmask >> v & 1)) freev[nf++] = v;
	int best = -1;
	for (unsigned a = 0; a < (1u << nf); a++) {
		unsigned x = 0;
		for (int k = 0; k < nf; k++) if (a >> k & 1) x |= 1u << freev[k];
		for (int k = 0; k < np; k++) { /* row k: x[piv] = rhs ^ sum of free vars in the row */
			unsigned others = eq[k] & ~(1u << piv[k]) & x;
			int par = rhs[k];
			while (others) { par ^= others & 1; others >>= 1; }
			if (par) x |= 1u << piv[k];
		}
		int w = 0;
		for (int v = 0; v < 25; v++) w += x >> v & 1;
		if (best < 0 || w < best) { best = w; *best_sol = x; }
	}
	return best;
}
static unsigned l_screen(void) {
	unsigned v = 0;
	for (int i = 0; i < 25; i++) if (st_px(lx[i] + lw / 2, ly[i] + lw / 2) == 0) v |= 1u << i;
	return v;
}
static int l_header(int *moves, int *par) {
	for (int i = st_nops - 1; i >= 0; i--)
		if (st_ops[i].t == 'P' && sscanf(st_ops[i].s, "Moves: %d Par: %d", moves, par) == 2) return 1;
	return 0;
}
static void test_lightsout(int games_per_level) {
	int perfect[3] = { 0 }, parsum[3] = { 0 };
	for (int lvl = 0; lvl < 3; lvl++)
		for (int gi = 0; gi < games_per_level; gi++) {
			st_begin(&GAME_lightsout, lvl, 6000u + (unsigned)gi * 29u + (unsigned)lvl);
			int k = 0;
			for (int i = 0; i < st_nops && k < 25; i++) if (st_ops[i].t == 'F') { lx[k] = st_ops[i].a; ly[k] = st_ops[i].b; lw = st_ops[i].c; k++; }
			if (k != 25) { st_error("lightsout: %d cells", k); st_end(); continue; }
			if (lw < 100) st_error("lightsout: cells %d px", lw);
			unsigned lit = l_screen(), sol = 0;
			int moves = -1, par = -1;
			if (!lit) st_error("lightsout: starts all off");
			int best = l_solve(lit, &sol);
			if (best < 0) { st_error("lightsout: start position is not solvable"); st_end(); continue; }
			if (best > 5 + 5 * lvl) st_error("lightsout: needs %d presses at level %d", best, lvl);
			if (!l_header(&moves, &par) || moves != 0 || par != best) st_error("lightsout: header moves %d par %d, solver says %d", moves, par, best);
			parsum[lvl] += best;
			int count = 0, wander = gi % 3 == 0 ? 0 : tr(12);
			for (int step = 0; step < 400 && !st_over; step++) {
				int tc = st_nops;
				if (tr(100) < 10) { /* outside the grid */
					st_tap(st_ax + tr(st_aw), st_ay + 20 + tr(60), tr(2), 1);
					if (l_screen() != lit) st_error("lightsout: tap outside changed lights");
					(void)tc;
					continue;
				}
				int i;
				if (wander > 0) { i = tr(25); wander--; }
				else { if (!sol) l_solve(lit, &sol); i = 0; while (!(sol >> i & 1)) i++; sol &= ~(1u << i); }
				if (tr(10) == 0) busy_check(lx[i] + lw / 2, ly[i] + lw / 2);
				st_tap(lx[i] + tr(lw) , ly[i] + tr(lw), tr(2), 1);
				lit ^= l_mask(i);
				count++;
				if (l_screen() != lit) { st_error("lightsout: screen %07x, model %07x after tapping %d", l_screen(), lit, i); break; }
				if (!l_header(&moves, &par) || moves != count) st_error("lightsout: header says %d moves, made %d", moves, count);
				if (!lit && !st_over) st_error("lightsout: all off but no g_over");
				if (lit && st_over) st_error("lightsout: over with lights on");
				if (wander == 0 && !sol && lit) { l_solve(lit, &sol); }
			}
			char want[64];
			snprintf(want, sizeof want, "in %d moves", count);
			if (st_over && !strstr(st_result, want)) st_error("lightsout: result '%s' (made %d)", st_result, count);
			if (st_over && count == best) perfect[lvl]++;
			end_checks("lightsout");
			st_end();
		}
	for (int l = 0; l < 3; l++)
		printf("  lightsout %-6s: %d games, all solvable, average par %.1f, %d solved in par by the test's solver\n", LV[l], games_per_level,
		       (double)parsum[l] / games_per_level, perfect[l]);
}

/* ===================================================================== main */
int main(int argc, char **argv) {
	int scale = argc > 1 ? atoi(argv[1]) : 1;
	if (argc > 2) st_verbose = atoi(argv[2]);
	int areas[2][4] = { { 40, 200, 1780, 2040 }, { 30, 150, 1800, 2110 } };
	for (int a = 0; a < 2; a++) {
		st_ax = areas[a][0]; st_ay = areas[a][1]; st_aw = areas[a][2]; st_ah = areas[a][3];
		printf("play area %d,%d %dx%d\n", st_ax, st_ay, st_aw, st_ah);
		int e0 = st_errors;
		if (&GAME_dots) { test_dots(40 * scale, 0); test_dots(20 * scale, 1); printf("  dots: %d errors\n", st_errors - e0); e0 = st_errors; }
		if (&GAME_gomoku) { test_gomoku(15 * scale, 0); test_gomoku(15 * scale, 1); printf("  gomoku: %d errors\n", st_errors - e0); e0 = st_errors; }
		if (&GAME_reversi) { test_reversi(20 * scale, 0); test_reversi(20 * scale, 1); printf("  reversi: %d errors\n", st_errors - e0); e0 = st_errors; }
		if (&GAME_battleship) { test_battleship(40 * scale, 0); test_battleship(40 * scale, 1); printf("  battleship: %d errors\n", st_errors - e0); e0 = st_errors; }
		if (&GAME_nim) { test_nim(60 * scale, 0); test_nim(60 * scale, 1); test_nim_starts(); printf("  nim: %d errors\n", st_errors - e0); e0 = st_errors; }
		if (&GAME_lightsout) { test_lightsout(60 * scale); printf("  lightsout: %d errors\n", st_errors - e0); e0 = st_errors; }
	}
	printf("%d games played, %d ended with g_over; %d errors\n", games_run, games_over, st_errors);
	return st_errors ? 1 : 0;
}

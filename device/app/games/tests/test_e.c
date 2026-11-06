/* Host tests for suite e: Tic-tac-toe and Connect Four (rules model, legal/illegal input, level strength) plus a
 * "difficulty really applies" check for every game in the registry that has levels=1 (except wordsearch).
 * Run against stub_e.c (a copy of stub_a.c: virtual canvas + op log).
 * Build: see device/app/tests/run_game_tests.sh (all games except wordsearch, plus registry.c)
 * Usage: test_e [scale] [verbose]   (scale 1 = about 2 s) */
#include "games.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int t, a, b, c, d, e;
	char s[48];
} StOp;
extern StOp *st_ops;
extern unsigned char st_canvas[2480][1860];
extern int st_nops, st_over, st_timer_pending, st_busy, st_errors, st_oob, st_draw_after_over, st_timer_calls, st_bad_rand, st_verbose;
extern int st_ax, st_ay, st_aw, st_ah;
extern char st_result[200], st_say_text[200], st_remark_text[200];
void st_error(const char *fmt, ...);
void st_begin(const GameDef *d, int level, unsigned seed);
void st_run_timers(void);
void st_tap(int x, int y, int finger, int run);
void st_pen(const int *pts, int k, int jit, int run);
void st_line(int x0, int y0, int x1, int y1, int jit, int run);
int st_dark(int x, int y, int r);
void *st_state(void);
void st_end(void);
extern const GameDef GAME_tictactoe, GAME_connect4;
extern const GameDef *const GAMES[];
extern const int NGAMES_ALL;

static unsigned trng = 88172645u;
static int tr(int n) { trng ^= trng << 13; trng ^= trng >> 17; trng ^= trng << 5; return n > 0 ? (int)(trng % (unsigned)n) : 0; }
static const char *LV[3] = { "easy", "medium", "hard" };
static int thin; /* 1 = lighter run (second play area) */

static void end_checks(const char *what) {
	if (!st_over) st_error("%s: game did not end", what);
	if (st_oob) st_error("%s: %d drawing calls outside the play area", what, st_oob);
	if (st_bad_rand) st_error("%s: g_rand called with n <= 0", what);
	if (st_draw_after_over > 8) st_error("%s: %d drawing calls after g_over", what, st_draw_after_over);
}

/* every drawing op must lie inside the play area (the stub only checks some op kinds) */
static void ops_inside(const char *what, int from) {
	for (int i = from; i < st_nops; i++) {
		StOp *o = &st_ops[i];
		int x0, y0, x1, y1;
		switch (o->t) {
		case 'L': x0 = o->a < o->c ? o->a : o->c; x1 = o->a < o->c ? o->c : o->a; y0 = o->b < o->d ? o->b : o->d; y1 = o->b < o->d ? o->d : o->b; break;
		case 'C': x0 = o->a - o->c; x1 = o->a + o->c; y0 = o->b - o->c; y1 = o->b + o->c; break;
		case 'F': x0 = o->a; y0 = o->b; x1 = o->a + o->c; y1 = o->b + o->d; break;
		default: continue;
		}
		if (x0 < st_ax || y0 < st_ay || x1 > st_ax + st_aw || y1 > st_ay + st_ah) { st_error("%s: op %c outside the play area (%d,%d-%d,%d)", what, o->t, x0, y0, x1, y1); return; }
	}
}

static int in_busy_check(int x, int y) { /* input while the engine is still drawing must be ignored */
	int before = st_nops, pend = st_timer_pending;
	st_busy = 1;
	st_tap(x, y, 0, 0);
	st_busy = 0;
	if (st_nops != before || st_timer_pending != pend || st_over) { st_error("input accepted while g_busy()"); return 0; }
	return 1;
}

/* ===================================================================== Tic-tac-toe */
static int tb[9], tx0, ty0, tcell; /* the test's own model: 0 empty, 1 you, 2 computer */
static const int TL[8][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 }, { 0, 3, 6 }, { 1, 4, 7 }, { 2, 5, 8 }, { 0, 4, 8 }, { 2, 4, 6 } };
static int t_win(const int *b) { /* 1/2 = that player has three, 3 = full board, 0 = going on */
	for (int i = 0; i < 8; i++) if (b[TL[i][0]] && b[TL[i][0]] == b[TL[i][1]] && b[TL[i][1]] == b[TL[i][2]]) return b[TL[i][0]];
	for (int i = 0; i < 9; i++) if (!b[i]) return 0;
	return 3;
}
/* game-theoretic value for the computer (2), no depth preference: +1 computer wins, 0 draw, -1 you win; `who` moves */
static signed char t_memo[2][19683]; /* 0 = not computed, else value + 2 */
static int t_val(int *b, int who) {
	int key = 0;
	for (int i = 0; i < 9; i++) key = key * 3 + b[i];
	if (t_memo[who - 1][key]) return t_memo[who - 1][key] - 2;
	int w = t_win(b), best;
	if (w) best = w == 2 ? 1 : w == 1 ? -1 : 0;
	else {
		best = who == 2 ? -2 : 2;
		for (int i = 0; i < 9; i++) if (!b[i]) { b[i] = who; int v = t_val(b, 3 - who); b[i] = 0; if (who == 2 ? v > best : v < best) best = v; }
	}
	t_memo[who - 1][key] = (signed char)(best + 2);
	return best;
}
static void t_cc(int i, int *cx, int *cy) { *cx = tx0 + (i % 3) * tcell + tcell / 2; *cy = ty0 + (i / 3) * tcell + tcell / 2; }
static int t_state_ok(const char *tag) {
	int *s = st_state();
	for (int i = 0; i < 9; i++) if (s[i] != tb[i]) { st_error("ttt %s: game board differs from the rules model at cell %d (%d vs %d)", tag, i, s[i], tb[i]); return 0; }
	return 1;
}

/* me = how the "player" chooses: 0 random, 1 perfect (never makes a mistake) */
static void test_ttt(int games, int lvl, int me, int *subopt, int *moves_total, int *outcome) {
	for (int gi = 0; gi < games; gi++) {
		st_begin(&GAME_tictactoe, lvl, 7000u + (unsigned)gi * 31u + (unsigned)lvl);
		int g0 = 0;
		for (; g0 < st_nops && st_ops[g0].t != 'L'; g0++) ;
		if (st_nops < 4 || st_ops[0].t != 'L') { st_error("ttt: board not drawn"); st_end(); return; }
		tcell = (st_ops[0].d - st_ops[0].b) / 3;
		tx0 = st_ops[0].a - tcell;
		ty0 = st_ops[0].b;
		if (tcell < 200) st_error("ttt: cell only %d px", tcell);
		ops_inside("ttt start", 0);
		memset(tb, 0, sizeof tb);
		int steps = 0, nm = 0;
		while (!st_over && steps++ < 40) {
			/* illegal input must change nothing at all */
			int ill = tr(100) < 40;
			while (ill) {
				int x, y, k = tr(6), nocc = 0, occ[9];
				for (int i = 0; i < 9; i++) if (tb[i]) occ[nocc++] = i;
				if (k == 0 && nocc) { t_cc(occ[tr(nocc)], &x, &y); }
				else if (k == 1) { x = tx0 - 8 - tr(300); y = ty0 + tr(3 * tcell); }                   /* left of the board */
				else if (k == 2) { x = tx0 + tr(3 * tcell); y = ty0 - 8 - tr(300); }                   /* above */
				else if (k == 3) { x = tx0 + 3 * tcell + 8 + tr(300); y = ty0 + tr(3 * tcell); }          /* right (cell 3 is outside) */
				else if (k == 4) { x = tx0 + tr(3 * tcell); y = ty0 + 3 * tcell + 8 + tr(300); }          /* below */
				else { x = st_ax + 2; y = st_ay + 2; }
				int mark = st_nops, tc = st_timer_calls;
				if (tr(2)) st_tap(x, y, tr(2), 1);
				else st_line(x - 40, y - 40, x + 40, y + 40, 2, 1);
				if (st_nops != mark || st_timer_calls != tc || st_over || st_timer_pending) { st_error("ttt: illegal input (kind %d at %d,%d) changed something", k, x, y); break; }
				if (!t_state_ok("after illegal")) break;
				ill = tr(100) < 40;
			}
			if (tr(100) < 10) {
				int x, y;
				t_cc(4, &x, &y);
				in_busy_check(x, y);
			}
			/* a legal move */
			int fr[9], nf = 0, i;
			for (int c = 0; c < 9; c++) if (!tb[c]) fr[nf++] = c;
			if (me == 1) { /* a move that keeps the best value for you */
				int bv = 2, cand[9], nc = 0;
				for (int k = 0; k < nf; k++) { tb[fr[k]] = 1; int v = t_val(tb, 2); tb[fr[k]] = 0; if (v < bv) { bv = v; nc = 0; } if (v == bv) cand[nc++] = fr[k]; }
				i = cand[tr(nc)];
			} else i = fr[tr(nf)];
			int cx, cy, mode = tr(4); /* 0 pen tap, 1 finger tap, 2 pen X stroke (ink), 3 pen line (ink) */
			t_cc(i, &cx, &cy);
			int mark = st_nops, r = tcell / 4 + tr(tcell / 8);
			int jx = tr(tcell / 5) - tcell / 10, jy = tr(tcell / 5) - tcell / 10; /* anywhere inside the square, not just the middle */
			cx += jx; cy += jy;
			if (mode == 0) st_tap(cx, cy, 0, 0);
			else if (mode == 1) st_tap(cx, cy, 1, 0);
			else if (mode == 2) { int p[8] = { cx - r, cy - r, cx + r, cy + r, cx - r, cy + r, cx + r, cy - r }; st_pen(p, 4, 3, 0); }
			else st_line(cx - r, cy, cx + r, cy, 2, 0); /* a long pen stroke: the pen already inked it */
			tb[i] = 1;
			nm++;
			if (!t_state_ok("after my move")) break;
			int w = t_win(tb);
			/* a drawn X appears exactly for taps and finger input, never for pen ink (the pen already drew it) */
			int drawn = 0;
			for (int k = mark; k < st_nops; k++) if (st_ops[k].t == 'L') drawn++;
			int want_x = mode <= 1;
			if (w == 1) { if (drawn < (want_x ? 3 : 1)) st_error("ttt: no win line drawn (%d lines)", drawn); }
			else if (want_x && drawn != 2) st_error("ttt: tap should draw an X (2 lines), saw %d", drawn);
			else if (!want_x && drawn) st_error("ttt: pen stroke got %d extra lines from the game", drawn);
			if (w == 1) {
				if (!st_over || !strstr(st_result, "You win") || st_timer_pending) st_error("ttt: you made three but over=%d '%s' pend=%d", st_over, st_result, st_timer_pending);
				*outcome += 0;
				outcome[1]++;
				break;
			}
			if (w == 3) {
				if (!st_over || !strstr(st_result, "draw") || st_timer_pending) st_error("ttt: full board without win but over=%d '%s'", st_over, st_result);
				outcome[0]++;
				break;
			}
			if (st_over) { st_error("ttt: game over early: %s", st_result); break; }
			if (!st_timer_pending) { st_error("ttt: computer did not ask for its move"); break; }
			{ /* out of turn: more input before the computer has moved must be ignored */
				int x, y, mk = st_nops, ok;
				for (ok = 0; ok < 9; ok++) if (!tb[ok]) break;
				t_cc(ok, &x, &y);
				st_tap(x, y, 0, 0);
				if (st_nops != mk || !t_state_ok("input out of turn") || !st_timer_pending) { st_error("ttt: input before the computer moved was not ignored"); break; }
			}
			/* the computer's move: exactly one, legal, one timer call */
			int before[9], tc = st_timer_calls;
			memcpy(before, tb, sizeof tb);
			mark = st_nops;
			st_run_timers();
			int *s = st_state(), ch = -1, nch = 0;
			for (int c = 0; c < 9; c++) if (s[c] != before[c]) { nch++; ch = c; }
			if (st_timer_calls != tc + 1) st_error("ttt: computer's turn took %d timer calls", st_timer_calls - tc);
			if (nch != 1 || before[ch] != 0 || s[ch] != 2) { st_error("ttt: computer move changed %d cells (cell %d: %d->%d)", nch, ch, nch ? before[ch] : -1, nch ? s[ch] : -1); break; }
			/* quality: compare with the game-theoretic best */
			int bestv = -2;
			for (int c = 0; c < 9; c++) if (!before[c]) { before[c] = 2; int v = t_val(before, 1); before[c] = 0; if (v > bestv) bestv = v; }
			tb[ch] = 2;
			int got = t_val(tb, 1);
			if (got < bestv) subopt[0]++;
			subopt[1]++;
			if (lvl == 2 && got < bestv) st_error("ttt: HARD blundered (best %d, got %d, cell %d)", bestv, got, ch);
			int cc, cyy;
			t_cc(ch, &cc, &cyy);
			int found = 0;
			for (int k = mark; k < st_nops; k++) if (st_ops[k].t == 'C' && abs(st_ops[k].a - cc) < 4 && abs(st_ops[k].b - cyy) < 4) found++;
			if (found != 1) st_error("ttt: computer's O not drawn once at cell %d (found %d)", ch, found);
			if (st_dark(cc + tcell * 30 / 100, cyy, 4) != 0) st_error("ttt: O ring not on the canvas");
			w = t_win(tb);
			if (w == 2) { if (!st_over || !strstr(st_result, "I win") || st_timer_pending) st_error("ttt: computer has three but over=%d '%s'", st_over, st_result); outcome[2]++; break; }
			if (w == 3) { if (!st_over || !strstr(st_result, "draw")) st_error("ttt: full board after my move, over=%d '%s'", st_over, st_result); outcome[0]++; break; }
			if (st_over || st_timer_pending) { st_error("ttt: still going but over=%d pend=%d", st_over, st_timer_pending); break; }
		}
		*moves_total += nm;
		if (steps > 40) st_error("ttt: game does not end");
		ops_inside("ttt", 0);
		end_checks("ttt");
		st_end();
	}
}

static void run_ttt(int scale) {
	int hard_loss_any = 0;
	for (int me = 0; me < 2; me++) {
		double sub_rate[3] = { 0 };
		for (int lvl = 0; lvl < 3; lvl++) {
			int sub[2] = { 0 }, mv = 0, out[3] = { 0 }, n = (me ? 400 : 1500) * scale / (thin ? 3 : 1);
			test_ttt(n, lvl, me, sub, &mv, out);
			sub_rate[lvl] = sub[1] ? (double)sub[0] / sub[1] : 0;
			printf("  tictactoe %-6s vs %s player: %d games, draws %d, you won %d, computer won %d; computer non-optimal moves %.1f%%\n", LV[lvl], me ? "perfect" : "random", n, out[0], out[1], out[2], 100 * sub_rate[lvl]);
			if (lvl == 2 && out[1]) { st_error("ttt: HARD lost %d of %d games (must never lose)", out[1], n); hard_loss_any = 1; }
			if (lvl == 2 && sub[0]) st_error("ttt: hard made %d non-optimal moves", sub[0]);
		}
		if (!(sub_rate[0] > sub_rate[1] + 0.05 && sub_rate[1] > 0.01)) st_error("ttt: levels do not differ in strength (non-optimal %.3f %.3f %.3f)", sub_rate[0], sub_rate[1], sub_rate[2]);
		if (me == 1 && !hard_loss_any) printf("  tictactoe: hard never lost\n");
	}
}

/* ===================================================================== Connect Four */
#define C 7
#define R 6
static int cb[R][C], cx0, cy0, ccell;
static int c4(int b[R][C], int p, int *dir) {
	static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
	for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) for (int d = 0; d < 4; d++) {
		int k;
		for (k = 0; k < 4; k++) { int rr = r + D[d][0] * k, cc = c + D[d][1] * k; if (rr < 0 || rr >= R || cc < 0 || cc >= C || b[rr][cc] != p) break; }
		if (k == 4) { if (dir) *dir = d; return 1; }
	}
	return 0;
}
static int c4_row(int b[R][C], int c) { for (int r = 0; r < R; r++) if (!b[r][c]) return r; return -1; }
static int c4_full(int b[R][C]) { for (int c = 0; c < C; c++) if (!b[R - 1][c]) return 0; return 1; }
static void c4_cc(int r, int c, int *x, int *y) { *x = cx0 + c * ccell + ccell / 2; *y = cy0 + (R - 1 - r) * ccell + ccell / 2; }
static int c4_state_ok(const char *tag) {
	int(*s)[C] = st_state();
	for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (s[r][c] != cb[r][c]) { st_error("c4 %s: board differs from the model at row %d col %d (%d vs %d)", tag, r, c, s[r][c], cb[r][c]); return 0; }
	return 1;
}
/* columns where p could win immediately */
static int c4_wins(int p, int *cols) {
	int n = 0;
	for (int c = 0; c < C; c++) { int r = c4_row(cb, c); if (r < 0) continue; cb[r][c] = p; if (c4(cb, p, 0)) cols[n++] = c; cb[r][c] = 0; }
	return n;
}
static int c4_wins_seen[3][4], c4_hwins_seen[4];

/* me: 0 random; 1 takes a win, blocks, else random */
static void test_c4(int games, int lvl, int me, int *res, int *blocks, int *nblock_cases, int *takes, int *ntake_cases) {
	for (int gi = 0; gi < games; gi++) {
		st_begin(&GAME_connect4, lvl, 9100u + (unsigned)gi * 17u + (unsigned)lvl);
		if (st_nops < 1 || st_ops[0].t != 'F') { st_error("c4: no frame"); st_end(); return; }
		cx0 = st_ops[0].a; cy0 = st_ops[0].b; ccell = st_ops[0].c / C;
		if (st_ops[0].d != R * ccell || ccell < 100) st_error("c4: frame %dx%d cell %d", st_ops[0].c, st_ops[0].d, ccell);
		ops_inside("c4 start", 0);
		memset(cb, 0, sizeof cb);
		int steps = 0;
		while (!st_over && steps++ < 60) {
			int ill = tr(100) < 35;
			while (ill) {
				int x, y, k = tr(4), fc[C], nfc = 0;
				for (int c = 0; c < C; c++) if (cb[R - 1][c]) fc[nfc++] = c;
				if (k == 0 && nfc) { x = cx0 + fc[tr(nfc)] * ccell + tr(ccell); y = cy0 + tr(R * ccell); }
				else if (k == 1) { x = cx0 - 8 - tr(400); y = cy0 + tr(R * ccell); }                     /* left of the board */
				else if (k == 2) { x = cx0 + C * ccell + tr(300); y = cy0 + tr(R * ccell); }             /* right of the board */
				else { x = st_ax; y = cy0 + tr(R * ccell); if (x >= cx0) x = cx0 - 8; }
				int mark = st_nops, tc = st_timer_calls;
				st_tap(x, y, tr(2), 1);
				if (st_nops != mark || st_timer_calls != tc || st_over || st_timer_pending) { st_error("c4: illegal input (kind %d at %d,%d) changed something", k, x, y); break; }
				if (!c4_state_ok("after illegal")) break;
				ill = tr(100) < 35;
			}
			if (tr(100) < 10) in_busy_check(cx0 + ccell / 2, cy0 + ccell / 2);
			int wc[C], nw = c4_wins(1, wc), c = -1;
			int mc[C], nmw = c4_wins(2, mc);
			if (me == 1 && nw) c = wc[tr(nw)];
			else if (me == 1 && nmw == 1) c = mc[0];
			if (c < 0) { int fc[C], nf = 0; for (int k = 0; k < C; k++) if (c4_row(cb, k) >= 0) fc[nf++] = k; c = fc[tr(nf)]; }
			int r = c4_row(cb, c), x, y;
			c4_cc(r, c, &x, &y);
			int mark = st_nops;
			st_tap(x - ccell / 3 + tr(2 * ccell / 3), cy0 + tr(R * ccell), tr(2), 0); /* anywhere in the column */
			cb[r][c] = 1;
			if (!c4_state_ok("after my move")) break;
			int found = 0;
			for (int k = mark; k < st_nops; k++) if (st_ops[k].t == 'D' && abs(st_ops[k].a - x) < 4 && abs(st_ops[k].b - y) < 4) found++;
			if (!found) st_error("c4: my disc not drawn in row %d col %d", r, c);
			if (st_dark(x, y, 1) != 0) st_error("c4: disc not on the canvas");
			int dir;
			if (c4(cb, 1, &dir)) {
				if (!st_over || !strstr(st_result, "you win") || st_timer_pending) st_error("c4: you made four but over=%d '%s' pend=%d", st_over, st_result, st_timer_pending);
				res[1]++; c4_hwins_seen[dir]++;
				break;
			}
			if (c4_full(cb)) { if (!st_over || !strstr(st_result, "draw")) st_error("c4: full board, no four, over=%d '%s'", st_over, st_result); res[0]++; break; }
			if (st_over) { st_error("c4: over early: %s", st_result); break; }
			if (!st_timer_pending) { st_error("c4: computer did not ask for its move"); break; }
			{ int mk = st_nops; st_tap(cx0 + ccell / 2, cy0 + ccell, 0, 0); if (st_nops != mk || !c4_state_ok("input out of turn")) { st_error("c4: input before the computer moved was not ignored"); break; } }
			/* computer's move */
			int cw[C], ncw = c4_wins(2, cw), hw[C], nhw = c4_wins(1, hw);
			int before[R][C], tc = st_timer_calls;
			memcpy(before, cb, sizeof cb);
			mark = st_nops;
			st_run_timers();
			int(*s)[C] = st_state(), nch = 0, cr = -1, cc = -1;
			for (int rr = 0; rr < R; rr++) for (int k = 0; k < C; k++) if (s[rr][k] != before[rr][k]) { nch++; cr = rr; cc = k; }
			if (st_timer_calls != tc + 1) st_error("c4: computer's turn took %d timer calls", st_timer_calls - tc);
			if (nch != 1 || s[cr][cc] != 2 || before[cr][cc] != 0 || cr != c4_row(before, cc)) { st_error("c4: computer move illegal (%d cells changed, row %d col %d)", nch, cr, cc); break; }
			cb[cr][cc] = 2;
			if (ncw) { (*ntake_cases)++; int ok = 0; for (int k = 0; k < ncw; k++) ok |= cw[k] == cc; takes[0] += ok; if (!ok) takes[1]++; }
			else if (nhw == 1) { (*nblock_cases)++; blocks[0] += hw[0] == cc; }
			int ex, ey;
			c4_cc(cr, cc, &ex, &ey);
			found = 0;
			for (int k = mark; k < st_nops; k++) if (st_ops[k].t == 'C' && abs(st_ops[k].a - ex) < 4 && abs(st_ops[k].b - ey) < 4) found++;
			if (found != 1) st_error("c4: computer's ring not drawn once (found %d)", found);
			if (c4(cb, 2, &dir)) {
				if (!st_over || !strstr(st_result, "I win") || st_timer_pending) st_error("c4: computer made four but over=%d '%s'", st_over, st_result);
				res[2]++; c4_wins_seen[lvl][dir]++;
				break;
			}
			if (c4_full(cb)) { if (!st_over || !strstr(st_result, "draw")) st_error("c4: board full after my move, over=%d '%s'", st_over, st_result); res[0]++; break; }
			if (st_over || st_timer_pending) { st_error("c4: still going but over=%d pend=%d", st_over, st_timer_pending); break; }
		}
		if (steps > 60) st_error("c4: game does not end");
		ops_inside("c4", 0);
		end_checks("c4");
		st_end();
	}
}

/* a full board without any four, with (r=5, c1) = 1 and (r=5, c2) = 2 (c2 < 0: only c1) */
static int c4_make_full(int out[R][C], int c1, int c2) {
	for (int tries = 0; tries < 4000000; tries++) {
		int ones = 0;
		for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) { out[r][c] = 1 + tr(2); ones += out[r][c] == 1; }
		out[R - 1][c1] = 1;
		if (c2 >= 0) out[R - 1][c2] = 2;
		if (!c4(out, 1, 0) && !c4(out, 2, 0)) return 1;
	}
	return 0;
}

static void test_c4_draws(int n) {
	for (int k = 0; k < n; k++) {
		for (int two = 0; two < 2; two++) {
			int full[R][C], c1 = tr(C), c2 = (c1 + 1 + tr(C - 1)) % C;
			if (!c4_make_full(full, c1, two ? c2 : -1)) { st_error("c4: could not make a drawn board"); return; }
			st_begin(&GAME_connect4, tr(3), 555u + (unsigned)k);
			cx0 = st_ops[0].a; cy0 = st_ops[0].b; ccell = st_ops[0].c / C;
			int(*s)[C] = st_state();
			memcpy(s, full, sizeof full);
			s[R - 1][c1] = 0;
			if (two) s[R - 1][c2] = 0;
			memcpy(cb, s, sizeof cb);
			int x, y;
			c4_cc(R - 1, c1, &x, &y);
			st_tap(x, y, 0, 1); /* you fill the last (or second last) hole */
			cb[R - 1][c1] = 1;
			if (two) cb[R - 1][c2] = 2; /* the only legal reply */
			if (!c4_state_ok("draw test")) { st_end(); continue; }
			if (!st_over) st_error("c4: board full after the last disc but the game did not end (two=%d)", two);
			else if (!strstr(st_result, "draw")) st_error("c4: full board result '%s'", st_result);
			end_checks("c4 draw");
			st_end();
		}
	}
}

/* each level must take a win and (medium, hard) block a single threat; strength must rise with the level */
static void comp_turn(void);
static void run_c4(int scale) {
	double wr[3] = { 0 };
	for (int me = 0; me < 2; me++)
		for (int lvl = 0; lvl < 3; lvl++) {
			int res[3] = { 0 }, bl[2] = { 0 }, nb = 0, tk[2] = { 0 }, nt = 0, n = (me ? (lvl == 2 ? 6 : 60) : lvl == 2 ? 8 : 60) * scale / (thin ? 2 : 1);
			test_c4(n, lvl, me, res, bl, &nb, tk, &nt);
			printf("  connect4 %-6s vs %s player: %d games, draws %d, you won %d, computer won %d; took %d/%d wins, blocked %d/%d threats\n", LV[lvl], me ? "blocking" : "random", n, res[0], res[1], res[2], nt - tk[1], nt, bl[0], nb);
			if (me == 1) wr[lvl] = n ? (double)res[2] / n : 0;
			if (tk[1]) st_error("c4: %s missed an immediate win %d times", LV[lvl], tk[1]);
			if (lvl >= 1 && nb && bl[0] != nb) st_error("c4: %s failed to block a single immediate loss %d of %d times", LV[lvl], nb - bl[0], nb);
			if (lvl == 0 && nb > 30 && bl[0] == nb) st_error("c4: easy never fails to block: depth 1 level looks like the others");
		}
	if (!(wr[2] >= wr[1] && wr[1] > wr[0] + 0.05)) st_error("c4: computer strength vs a blocking player does not rise with the level (%.2f %.2f %.2f)", wr[0], wr[1], wr[2]);
	if (wr[2] < 0.7) st_error("c4: hard wins only %.0f%% against a win-and-block player", 100 * wr[2]);
	printf("  connect4 computer win rate vs blocking player: easy %.0f%%, medium %.0f%%, hard %.0f%%\n", 100 * wr[0], 100 * wr[1], 100 * wr[2]);
	int dirs[4] = { 0 };
	for (int l = 0; l < 3; l++) for (int d = 0; d < 4; d++) dirs[d] += c4_wins_seen[l][d] + c4_hwins_seen[d];
	printf("  connect4 four-in-a-row seen: horizontal %d, vertical %d, diagonal/ %d, diagonal\\ %d\n", dirs[0], dirs[1], dirs[2], dirs[3]);
	for (int d = 0; d < 4; d++) if (!dirs[d]) st_error("c4: no win in direction %d was ever tested", d);
	test_c4_draws(10 * scale);
}

/* ===================================================================== difficulty must apply */
static unsigned long long fp_hash(void) {
	unsigned long long h = 1469598103934665603ull;
	for (int i = 0; i < st_nops; i++)
		for (const char *p = st_ops[i].s; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ull;
	return h;
}
typedef struct { double ops, ink; unsigned long long h; } Fp;
static Fp fingerprint(const GameDef *d, int lvl) {
	Fp f = { 0, 0, 0 };
	int n = 8;
	for (unsigned sd = 1; sd <= (unsigned)n; sd++) {
		st_begin(d, lvl, sd * 977u);
		f.ops += st_nops;
		for (int y = 0; y < 2480; y += 2) for (int x = 0; x < 1860; x += 2) f.ink += st_canvas[y][x] < 128;
		f.h = f.h * 31 + fp_hash();
		st_end();
	}
	f.ops /= n; f.ink /= n;
	return f;
}
static int fp_differs(Fp a, Fp b) { return a.ops != b.ops || a.ink != b.ink || a.h != b.h; }

/* games whose start screen is the same on every level: their level is the computer's play, probed separately */
static int behavioural(const char *id) {
	static const char *B[] = { "tictactoe", "connect4", "gomoku", "reversi", "battleship", "nim", "codebreaker", "sliding" };
	for (unsigned i = 0; i < sizeof B / sizeof *B; i++) if (!strcmp(id, B[i])) return 1;
	return 0;
}

/* ---- computer-play probes: put the state in a chosen position, let the computer move once, read its choice ---- */
extern const GameDef GAME_gomoku __attribute__((weak)), GAME_reversi __attribute__((weak)), GAME_nim __attribute__((weak)),
	GAME_codebreaker __attribute__((weak)), GAME_sliding __attribute__((weak));

static void comp_turn(void) { st_timer_pending = 1; st_run_timers(); }

/* gomoku (state starts with unsigned char b[13][13]) */
static int gm_pick(unsigned char (*b)[13], unsigned char (*after)[13]) {
	int ch = -1, n = 0;
	for (int r = 0; r < 13; r++) for (int c = 0; c < 13; c++) if (after[r][c] != b[r][c]) { ch = r * 13 + c; n++; }
	return n == 1 && after[ch / 13][ch % 13] == 2 ? ch : -2;
}
static void test_gomoku_levels(int scale) {
	if (!&GAME_gomoku) return;
	int n = 300 * scale, took[3] = { 0 }, differ = 0, cmp = 0;
	for (int k = 0; k < n; k++) { /* the computer has four in a row with an open end: does it finish it? */
		unsigned char pos[13][13];
		memset(pos, 0, sizeof pos);
		int c0 = 3 + tr(4), win_a = 6 * 13 + c0 - 1, win_b = 6 * 13 + c0 + 4;
		for (int j = 0; j < 4; j++) pos[6][c0 + j] = 2;
		pos[3][3 + tr(3)] = 1; pos[9][4 + tr(5)] = 1; pos[10][2 + tr(8)] = 1;
		for (int lvl = 0; lvl < 3; lvl++) {
			st_begin(&GAME_gomoku, lvl, 31u * (unsigned)k + 5u);
			memcpy(st_state(), pos, sizeof pos);
			comp_turn();
			int ch = gm_pick(pos, st_state());
			if (ch == win_a || ch == win_b) took[lvl]++;
			st_end();
		}
	}
	printf("  gomoku: completes its own open four: easy %d%%, medium %d%%, hard %d%%\n", 100 * took[0] / n, 100 * took[1] / n, 100 * took[2] / n);
	if (took[1] != n || took[2] != n) st_error("gomoku: medium/hard missed their own five (%d, %d of %d)", took[1], took[2], n);
	if (took[0] >= n) st_error("gomoku: easy never misses its own five - level makes no difference there");
	for (int k = 0; k < n; k++) { /* middle-game positions, same random numbers: does Hard ever choose differently from Medium? */
		unsigned char pos[13][13];
		memset(pos, 0, sizeof pos);
		int stones = 8 + tr(20);
		for (int i = 0; i < stones; i++) pos[3 + tr(7)][3 + tr(7)] = 1 + (i & 1);
		int ch[2];
		for (int lvl = 1; lvl < 3; lvl++) {
			st_begin(&GAME_gomoku, lvl, 77u * (unsigned)k + 3u);
			memcpy(st_state(), pos, sizeof pos);
			comp_turn();
			ch[lvl - 1] = gm_pick(pos, st_state());
			st_end();
		}
		if (ch[0] >= 0 && ch[1] >= 0) { cmp++; differ += ch[0] != ch[1]; }
	}
	printf("  gomoku: medium and hard chose differently in %d of %d identical positions\n", differ, cmp);
	if (!differ) st_error("gomoku: Hard plays exactly like Medium (no difference in %d positions)", cmp);
}

/* reversi (state starts with unsigned char b[64]): easy plays half at random, so it takes an open corner less often */
static int rv_flips(unsigned char *b, int m, int p, int doit) {
	if (b[m]) return 0;
	int tot = 0, r0 = m / 8, c0 = m % 8;
	for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++) {
		if (!dr && !dc) continue;
		int r = r0 + dr, c = c0 + dc, n = 0;
		while (r >= 0 && r < 8 && c >= 0 && c < 8 && b[r * 8 + c] == 3 - p) { r += dr; c += dc; n++; }
		if (n && r >= 0 && r < 8 && c >= 0 && c < 8 && b[r * 8 + c] == p) {
			tot += n;
			if (doit) for (int k = 1; k <= n; k++) b[(r0 + dr * k) * 8 + c0 + dc * k] = (unsigned char)p;
		}
	}
	if (doit && tot) b[m] = (unsigned char)p;
	return tot;
}
static void test_reversi_levels(int scale) {
	if (!&GAME_reversi) return;
	int want = 60 * scale, cases = 0, took[3] = { 0 };
	for (int tries = 0; cases < want && tries < 200000; tries++) {
		unsigned char b[64] = { 0 };
		b[27] = 2; b[36] = 2; b[28] = 1; b[35] = 1;
		int p = 1, plies = 6 + tr(30);
		for (int i = 0; i < plies; i++) {
			int mv[64], n = 0;
			for (int m = 0; m < 64; m++) if (rv_flips(b, m, p, 0)) mv[n++] = m;
			if (n) rv_flips(b, mv[tr(n)], p, 1);
			p = 3 - p;
		}
		int corners = 0, others = 0, cm[4];
		for (int m = 0; m < 64; m++) if (rv_flips(b, m, 2, 0)) { if (m == 0 || m == 7 || m == 56 || m == 63) cm[corners++] = m; else others++; }
		if (!corners || others < 2) continue;
		cases++;
		for (int lvl = 0; lvl < 3; lvl++)
			for (int rep = 0; rep < 4; rep++) {
				st_begin(&GAME_reversi, lvl, 13u * (unsigned)tries + (unsigned)rep);
				memcpy(st_state(), b, 64);
				comp_turn();
				unsigned char *a = st_state();
				for (int c = 0; c < corners; c++) if (a[cm[c]] == 2) { took[lvl]++; break; }
				st_end();
			}
	}
	int tot = cases * 4;
	if (!tot) { st_error("reversi: no probe positions"); return; }
	printf("  reversi: takes an open corner: easy %d%%, medium %d%%, hard %d%% (%d positions)\n", 100 * took[0] / tot, 100 * took[1] / tot, 100 * took[2] / tot, cases);
	if (cases < want) st_error("reversi: only %d probe positions", cases);
	if (!(took[0] + tot / 20 < took[1])) st_error("reversi: easy takes corners as often as medium (%d vs %d of %d)", took[0], took[1], tot);
}

/* nim (state starts with int nr, heap[5], len[5], then the taken sticks): how often the computer finds a winning move (misere) */
static int nm_lose(const int *h) { /* 1 = the side to move loses (misere nim-sum rule; no sticks = the mover already won) */
	int big = 0, ones = 0, x = 0;
	for (int i = 0; i < 4; i++) { big += h[i] > 1; ones += h[i] == 1; x ^= h[i]; }
	if (!big && !ones) return 0;
	return big ? x == 0 : ones % 2 == 1;
}
static void test_nim_levels(int scale) {
	if (!&GAME_nim) return;
	int n[3] = { 0 }, win[3] = { 0 }, want = 150 * scale;
	for (int lvl = 0; lvl < 3; lvl++)
		for (int tries = 0; n[lvl] < want && tries < 100000; tries++) {
			st_begin(&GAME_nim, lvl, 19u * (unsigned)tries + 1u);
			int *s = st_state(), nr = s[0], h[5] = { 0 };
			unsigned char (*taken)[9] = (unsigned char (*)[9])(s + 11);
			int tot = 0;
			for (int r = 0; r < nr; r++) { h[r] = tr(s[6 + r] + 1); tot += h[r]; } /* a position on this board: sticks above h are gone */
			int hh[4] = { h[0], h[1], h[2], h[3] + h[4] }; /* the lose test below sees 4 piles; fold the 5th into the 4th only when absent */
			if (nr == 5) { st_end(); continue; }
			if (tot < 2 || nm_lose(hh)) { st_end(); continue; } /* needs a winning move for the computer */
			n[lvl]++;
			for (int r = 0; r < nr; r++) { s[1 + r] = h[r]; for (int j = 0; j < 9; j++) taken[r][j] = j >= h[r]; }
			st_over = 0;
			comp_turn();
			int g[4] = { 0 }, gt = 0;
			for (int r = 0; r < nr; r++) { if (r < 4) g[r] = s[1 + r]; gt += s[1 + r]; }
			if (gt < tot && (st_over ? 0 : nm_lose(g))) win[lvl]++;
			st_end();
		}
	if (!n[0] || !n[1] || !n[2]) { st_error("nim: no probe positions"); return; }
	int p0 = 100 * win[0] / n[0], p1 = 100 * win[1] / n[1], p2 = 100 * win[2] / n[2];
	printf("  nim: finds the winning move: easy %d%%, medium %d%%, hard %d%% (%d positions each)\n", p0, p1, p2, n[0]);
	if (win[2] != n[2]) st_error("nim: hard missed a winning move %d of %d times", n[2] - win[2], n[2]);
	if (!(p0 + 10 < p1 && p1 + 10 < p2)) st_error("nim: win-finding rate does not rise with the level (%d%% %d%% %d%%)", p0, p1, p2);
}

/* codebreaker: easy rejects a repeated digit in a guess; hard's secret code may repeat digits */
typedef struct { int code[4], guess[10][4], row, ncur, bx, by, rh, lw, sp, ss, fx, kx, ky, kw, kh; } CBS;
static void test_codebreaker_levels(void) {
	if (!&GAME_codebreaker) return;
	int rep_secret[3] = { 0 }, accepted[3] = { 0 };
	for (int lvl = 0; lvl < 3; lvl++)
		for (unsigned sd = 1; sd <= 60; sd++) {
			st_begin(&GAME_codebreaker, lvl, sd * 41u);
			CBS *s = st_state();
			int dup = 0;
			for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) dup |= s->code[i] == s->code[j];
			rep_secret[lvl] += dup;
			if (sd <= 10) { /* tap digit 1 twice */
				int x = s->kx + s->kw / 2, y = s->ky + s->kh / 2;
				st_tap(x, y, 0, 1);
				int n1 = s->ncur;
				st_tap(x, y, 0, 1);
				accepted[lvl] += n1 == 1 && s->ncur == 2;
			}
			st_end();
		}
	printf("  codebreaker: secrets with a repeated digit (of 60): easy %d, medium %d, hard %d; repeated guess digit accepted (of 10): easy %d, medium %d, hard %d\n", rep_secret[0], rep_secret[1], rep_secret[2], accepted[0], accepted[1], accepted[2]);
	if (rep_secret[0] || rep_secret[1]) st_error("codebreaker: easy/medium secret has a repeated digit");
	if (!rep_secret[2]) st_error("codebreaker: hard never uses a repeated digit in the secret");
	if (accepted[0]) st_error("codebreaker: easy accepted a repeated digit in a guess");
	if (accepted[1] != 10 || accepted[2] != 10) st_error("codebreaker: medium/hard refused a repeated digit in a guess");
}

/* sliding: Medium and Hard are both 4x4; Hard is shuffled much further (tiles on average further from home) */
typedef struct { int n, b[16]; } SLS;
static void test_sliding_levels(void) {
	if (!&GAME_sliding) return;
	double dist[3] = { 0 };
	int size[3] = { 0 };
	for (int lvl = 0; lvl < 3; lvl++)
		for (unsigned sd = 1; sd <= 120; sd++) {
			st_begin(&GAME_sliding, lvl, sd * 7u);
			SLS *s = st_state();
			size[lvl] = s->n;
			int d = 0;
			for (int i = 0; i < s->n * s->n; i++) {
				int t = s->b[i];
				if (t) d += abs((t - 1) / s->n - i / s->n) + abs((t - 1) % s->n - i % s->n);
			}
			dist[lvl] += d / 120.0;
			st_end();
		}
	printf("  sliding: board %dx%d / %dx%d / %dx%d; mean tile distance from home %.1f / %.1f / %.1f\n", size[0], size[0], size[1], size[1], size[2], size[2], dist[0], dist[1], dist[2]);
	if (size[0] >= size[1] || size[1] != size[2]) st_error("sliding: unexpected board sizes");
	if (!(dist[2] > dist[1] + 1)) st_error("sliding: Hard is not shuffled further than Medium (%.1f vs %.1f)", dist[1], dist[2]);
}

/* connect4: Medium (4 plies) and Hard (7 plies) in identical middle-game positions must sometimes choose differently,
 * and the stronger one must be at least as good against a deeper judge (measured as: Hard never loses a position
 * that Medium wins). Here: only whether they differ at all. */
static void test_c4_levels(int scale) {
	int n = 16 * scale, differ = 0, cmp = 0;
	for (int k = 0; k < n; k++) {
		int pos[R][C];
		memset(pos, 0, sizeof pos);
		int discs = 10 + tr(14), p = 1;
		for (int i = 0; i < discs; i++) {
			int c = tr(C), r = c4_row(pos, c);
			if (r < 0) continue;
			pos[r][c] = p;
			if (c4(pos, p, 0)) { pos[r][c] = 0; continue; }
			p = 3 - p;
		}
		int ch[3] = { -1, -1, -1 };
		for (int lvl = 1; lvl < 3; lvl++) {
			st_begin(&GAME_connect4, lvl, 5u * (unsigned)k + 9u);
			memcpy(st_state(), pos, sizeof pos);
			comp_turn();
			int(*s)[C] = st_state();
			for (int r = 0; r < R; r++) for (int c = 0; c < C; c++) if (s[r][c] != pos[r][c]) ch[lvl] = c;
			st_end();
		}
		if (ch[1] >= 0 && ch[2] >= 0) { cmp++; differ += ch[1] != ch[2]; }
	}
	printf("  connect4: medium and hard chose differently in %d of %d identical positions\n", differ, cmp);
	if (!differ) st_error("connect4: Hard plays exactly like Medium (no difference in %d positions)", cmp);
}

static void test_levels_apply(int scale) {
	printf("  per-level start screen of every game with levels (mean ops / ink px, 8 seeds):\n");
	int checked = 0;
	for (int i = 0; i < NGAMES_ALL; i++) {
		const GameDef *d = GAMES[i];
		if (!d || !d->levels || !strcmp(d->id, "wordsearch")) continue;
		checked++;
		if (behavioural(d->id)) { printf("    %-12s (same start screen at every level: the computer's play is probed below)\n", d->id); continue; }
		Fp f[3];
		for (int l = 0; l < 3; l++) f[l] = fingerprint(d, l);
		printf("    %-12s ops %6.1f / %6.1f / %6.1f   ink %7.0f / %7.0f / %7.0f\n", d->id, f[0].ops, f[1].ops, f[2].ops, f[0].ink, f[1].ink, f[2].ink);
		if (!fp_differs(f[0], f[1])) st_error("%s: easy and medium start identically - level makes no difference", d->id);
		if (!fp_differs(f[1], f[2])) st_error("%s: medium and hard start identically - level makes no difference", d->id);
	}
	if (checked < 15) st_error("only %d games with levels found in the registry", checked);
	test_c4_levels(scale);
	test_gomoku_levels(scale);
	test_reversi_levels(scale);
	test_nim_levels(scale);
	test_codebreaker_levels();
	test_sliding_levels();
}

int main(int argc, char **argv) {
	int scale = argc > 1 ? atoi(argv[1]) : 1;
	if (argc > 2) st_verbose = atoi(argv[2]);
	int areas[2][4] = { { 40, 200, 1780, 2040 }, { 30, 150, 1800, 2110 } };
	for (int a = 0; a < 2; a++) {
		st_ax = areas[a][0]; st_ay = areas[a][1]; st_aw = areas[a][2]; st_ah = areas[a][3];
		printf("play area %d,%d %dx%d\n", st_ax, st_ay, st_aw, st_ah);
		int e0 = st_errors;
		thin = a;
		run_ttt(scale);
		printf("  tictactoe: %d errors\n", st_errors - e0);
		e0 = st_errors;
		run_c4(scale);
		printf("  connect4: %d errors\n", st_errors - e0);
	}
	printf("difficulty checks\n");
	{
		int e0 = st_errors;
		st_ax = 40; st_ay = 200; st_aw = 1780; st_ah = 2040;
		test_levels_apply(scale);
		printf("  levels: %d errors\n", st_errors - e0);
	}
	printf("%d errors\n", st_errors);
	return st_errors ? 1 : 0;
}

/* Battleship: both fleets (5, 4, 3, 3, 2) are placed at random. Tap a square of my waters (the big grid) to fire;
 * I fire back at your fleet (the small grid) after each shot. Hit = X, miss = dot. Sink all five ships to win.
 * Easy fires mostly at random, Medium hunts and then follows up hits, Hard also hunts on a parity grid by likelihood. */
#include "../games.h"
#include <stdio.h>

#define G 10
#define NS 5
static const int LEN[NS] = { 5, 4, 3, 3, 2 };
static const char *const NAME[NS] = { "Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer" };

typedef struct {
	unsigned char mine[G][G], yours[G][G];     /* fleets: 0 water, k+1 = ship k */
	unsigned char shot_me[G][G];               /* your shots at my fleet: 0 none, 1 miss, 2 hit */
	unsigned char known[G][G];                 /* my shots at yours: 0 none, 1 miss, 2 hit, 3 hit on a sunk ship */
	int ship_me[NS][3], ship_you[NS][3];       /* row, col, dir (0 across, 1 down) */
	int hit_me[NS], hit_you[NS], sunk_me, sunk_you, my_turn, par;
	int bx, by, B, sx, sy, s, lx, lw;          /* big grid, small grid, list of my ships */
} S;

static void place(GameCtx *g, unsigned char f[G][G], int ship[NS][3]) {
	for (int k = 0; k < NS; k++)
		for (;;) {
			int d = g_rand(g, 2), r = g_rand(g, d ? G - LEN[k] + 1 : G), c = g_rand(g, d ? G : G - LEN[k] + 1), ok = 1;
			for (int i = 0; i < LEN[k]; i++) ok &= !f[r + d * i][c + (1 - d) * i];
			if (!ok) continue;
			for (int i = 0; i < LEN[k]; i++) f[r + d * i][c + (1 - d) * i] = (unsigned char)(k + 1);
			ship[k][0] = r; ship[k][1] = c; ship[k][2] = d;
			break;
		}
}

/* ---- the computer's aim ---- */
static int unsunk_hits(const S *s) { int n = 0; for (int r = 0; r < G; r++) for (int c = 0; c < G; c++) n += s->known[r][c] == 2; return n; }

static int free_cell(const S *s, int r, int c) { return r >= 0 && r < G && c >= 0 && c < G && !s->known[r][c]; }

/* cells next to unsunk hits; prefers the ends of a line of two or more hits */
static int targets(const S *s, int *cand) {
	int n = 0;
	static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } };
	for (int r = 0; r < G; r++)
		for (int c = 0; c < G; c++) {
			if (s->known[r][c] != 2) continue;
			for (int d = 0; d < 4; d++) {
				int pr = r - D[d][0], pc = c - D[d][1]; /* the hit behind us in this direction */
				if (pr < 0 || pr >= G || pc < 0 || pc >= G || s->known[pr][pc] != 2) continue;
				int rr = r, cc = c;
				while (rr >= 0 && rr < G && cc >= 0 && cc < G && s->known[rr][cc] == 2) { rr += D[d][0]; cc += D[d][1]; }
				if (free_cell(s, rr, cc)) cand[n++] = rr * G + cc;
			}
		}
	if (n) return n;
	for (int r = 0; r < G; r++)
		for (int c = 0; c < G; c++) {
			if (s->known[r][c] != 2) continue;
			for (int d = 0; d < 4; d++) if (free_cell(s, r + D[d][0], c + D[d][1])) cand[n++] = (r + D[d][0]) * G + c + D[d][1];
		}
	return n;
}

/* how many ways the ships still afloat could cover each cell; placements through unsunk hits count much more */
static void density(const S *s, int dens[G][G]) {
	for (int r = 0; r < G; r++) for (int c = 0; c < G; c++) dens[r][c] = 0;
	for (int k = 0; k < NS; k++) {
		if (s->hit_you[k] == LEN[k]) continue;
		for (int d = 0; d < 2; d++)
			for (int r = 0; r + d * (LEN[k] - 1) < G; r++)
				for (int c = 0; c + (1 - d) * (LEN[k] - 1) < G; c++) {
					int ok = 1, hits = 0;
					for (int i = 0; i < LEN[k] && ok; i++) {
						int v = s->known[r + d * i][c + (1 - d) * i];
						if (v == 1 || v == 3) ok = 0;
						hits += v == 2;
					}
					if (!ok) continue;
					int w = 1 + hits * 30;
					for (int i = 0; i < LEN[k]; i++) dens[r + d * i][c + (1 - d) * i] += w;
				}
	}
}

static int aim(GameCtx *g, S *s) {
	int lvl = g_level(g), cand[G * G], n = 0;
	if (unsunk_hits(s) && (lvl > 0 || g_rand(g, 100) < 20)) n = targets(s, cand);
	if (n && lvl == 1) return cand[g_rand(g, n)];
	if (lvl == 2) {
		int dens[G][G], best = -1, bv = -1, minlen = 9;
		density(s, dens);
		for (int k = 0; k < NS; k++) if (s->hit_you[k] < LEN[k] && LEN[k] < minlen) minlen = LEN[k];
		int par = s->par % minlen;
		if (!n) /* hunt on a parity grid: every ship still afloat must cross one of these cells */
			for (int r = 0; r < G; r++)
				for (int c = 0; c < G; c++)
					if (!s->known[r][c] && (r + c) % minlen == par) cand[n++] = r * G + c;
		if (!n) for (int r = 0; r < G; r++) for (int c = 0; c < G; c++) if (!s->known[r][c]) cand[n++] = r * G + c;
		for (int i = 0; i < n; i++) {
			int v = dens[cand[i] / G][cand[i] % G] * 8 + g_rand(g, 8);
			if (v > bv) { bv = v; best = cand[i]; }
		}
		return best;
	}
	if (n) return cand[g_rand(g, n)];
	for (int r = 0; r < G; r++) for (int c = 0; c < G; c++) if (!s->known[r][c]) cand[n++] = r * G + c;
	return cand[g_rand(g, n)];
}

/* ---- drawing ---- */
static void big_xy(const S *s, int r, int c, int *x, int *y) { *x = s->bx + c * s->B + s->B / 2; *y = s->by + r * s->B + s->B / 2; }
static void small_xy(const S *s, int r, int c, int *x, int *y) { *x = s->sx + c * s->s + s->s / 2; *y = s->sy + r * s->s + s->s / 2; }

static void grid(GameCtx *g, int x, int y, int cell, int frame, int line) {
	g_frame(g, x, y, G * cell, G * cell, frame);
	for (int i = 1; i < G; i++) {
		g_line(g, x + i * cell, y, x + i * cell, y + G * cell, line);
		g_line(g, x, y + i * cell, x + G * cell, y + i * cell, line);
	}
}

static void outline_ship(GameCtx *g, const S *s, int k, int width) {
	int r = s->ship_me[k][0], c = s->ship_me[k][1], d = s->ship_me[k][2], in = s->B / 8;
	g_frame(g, s->bx + c * s->B + in, s->by + r * s->B + in, (d ? 1 : LEN[k]) * s->B - 2 * in, (d ? LEN[k] : 1) * s->B - 2 * in, width);
}

static void list_row(const S *s, int k, int *y, int *h) { *h = G * s->s / NS; *y = s->sy + k * *h; }

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	const int LH = 70, GAP = 50, LH2 = 60;
	s->B = (ah - LH - GAP - LH2 - 20) / 14;
	if (s->B > (aw - 100) / G) s->B = (aw - 100) / G;
	if (s->B > 150) s->B = 150;
	s->s = s->B * 2 / 5;
	s->bx = ax + (aw - G * s->B) / 2;
	s->by = ay + LH;
	s->sx = s->bx;
	s->sy = s->by + G * s->B + GAP + LH2;
	s->lx = s->sx + G * s->s + 50;
	s->lw = s->bx + G * s->B - s->lx;
	place(g, s->mine, s->ship_me);
	place(g, s->yours, s->ship_you);
	s->par = g_rand(g, 6);
	g_print(g, s->bx, ay, G * s->B, LH - 10, 44, 1, "My waters - tap a square to fire");
	g_print(g, s->sx, s->sy - LH2, G * s->s, LH2 - 8, 36, 1, "Your fleet");
	g_print(g, s->lx, s->sy - LH2, s->lw, LH2 - 8, 36, 1, "My ships");
	g_speed(g, 9);
	grid(g, s->bx, s->by, s->B, 5, 2);
	grid(g, s->sx, s->sy, s->s, 4, 1);
	for (int k = 0; k < NS; k++) {
		int r = s->ship_you[k][0], c = s->ship_you[k][1], d = s->ship_you[k][2], in = 6;
		g_box(g, s->sx + c * s->s + in, s->sy + r * s->s + in, (d ? 1 : LEN[k]) * s->s - 2 * in, (d ? LEN[k] : 1) * s->s - 2 * in, 150);
	}
	for (int k = 0; k < NS; k++) {
		char t[32];
		int y, h;
		list_row(s, k, &y, &h);
		snprintf(t, sizeof t, "%s (%d)", NAME[k], LEN[k]);
		g_print(g, s->lx, y, s->lw, h, 40, 0, t);
	}
	g_remark(g, "Tap my waters to fire. Sink all 5!");
	g_say(g, "tap a square in the big grid to fire");
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int m = aim(g, s), r = m / G, c = m % G, x, y;
	small_xy(s, r, c, &x, &y);
	g_speed(g, 3);
	int k = s->yours[r][c] - 1;
	if (k < 0) {
		s->known[r][c] = 1;
		g_dot(g, x, y, 7);
		g_say(g, "I missed. Your shot - tap the big grid");
	} else {
		s->known[r][c] = 2;
		g_cross(g, x, y, s->s * 32 / 100, 5);
		char t[64];
		if (++s->hit_you[k] == LEN[k]) {
			s->sunk_you++;
			int sr = s->ship_you[k][0], sc = s->ship_you[k][1], sd = s->ship_you[k][2];
			for (int i = 0; i < LEN[k]; i++) s->known[sr + sd * i][sc + (1 - sd) * i] = 3;
			if (s->sunk_you == NS) {
				g_speed(g, 6);
				for (int j = 0; j < NS; j++) if (s->hit_me[j] < LEN[j]) outline_ship(g, s, j, 4); /* show where mine were */
				g_over(g, "I sank your whole fleet - I win!");
				return;
			}
			snprintf(t, sizeof t, "I sank your %s!", NAME[k]);
		} else snprintf(t, sizeof t, "I hit your %s!", NAME[k]);
		g_remark(g, t);
		g_say(g, "%s Your shot - tap the big grid", t);
	}
	s->my_turn = 0;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my shot"); return; }
	int c = (st->cx - s->bx) / s->B, r = (st->cy - s->by) / s->B;
	if (st->cx < s->bx || st->cy < s->by || c >= G || r >= G) { g_say(g, "tap a square in my waters (the big grid) to fire"); return; }
	if (s->shot_me[r][c]) { g_say(g, "you already fired there - pick a fresh square"); return; }
	int x, y, k = s->mine[r][c] - 1;
	big_xy(s, r, c, &x, &y);
	g_speed(g, 5);
	if (k < 0) {
		s->shot_me[r][c] = 1;
		g_dot(g, x, y, 11);
		g_say(g, "miss. My shot...");
	} else {
		s->shot_me[r][c] = 2;
		g_cross(g, x, y, s->B * 32 / 100, 8);
		if (++s->hit_me[k] == LEN[k]) {
			s->sunk_me++;
			outline_ship(g, s, k, 6);
			int ry, rh;
			list_row(s, k, &ry, &rh);
			g_line(g, s->lx + 20, ry + rh / 2, s->lx + s->lw - 20, ry + rh / 2, 5);
			if (s->sunk_me == NS) { g_over(g, "You sank my whole fleet - you win!"); return; }
			char t[64];
			snprintf(t, sizeof t, "You sank my %s!", NAME[k]);
			g_remark(g, t);
			g_say(g, "%s My shot...", t);
		} else {
			g_remark(g, "Hit!");
			g_say(g, "hit! My shot...");
		}
	}
	s->my_turn = 1;
	g_after(g, 600);
}

const GameDef GAME_battleship = { "battleship", "Battleship", "Find and sink my fleet", sizeof(S), 0, 1, start, stroke, my_move };

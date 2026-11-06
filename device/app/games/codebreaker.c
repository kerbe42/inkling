/* Code Breaker: the Kindle hides a code of 4 digits (1-6); you have 10 guesses. After each guess: a black dot for
 * every right digit in the right place, a ring for every right digit in the wrong place. Easy and Medium codes have
 * no repeated digits (Easy won't let you repeat one either); Hard codes may repeat. */
#include "../games.h"
#include <stdio.h>

#define M 20
#define ROWS 10
#define PEGS 4
#define NKEYS 8 /* 1..6, Erase, Enter */

typedef struct {
	int code[PEGS], guess[ROWS][PEGS], row, ncur;
	int bx, by, rh, lw, sp, ss, fx, kx, ky, kw, kh;
	char msg[96];
} S;

static int row_y(S *s, int r) { return r < 0 ? s->by : s->by + s->rh + 30 + r * s->rh; } /* r = -1: the hidden code */

static void slot_box(S *s, int r, int j, int *x, int *y) {
	*x = s->bx + s->lw + j * s->sp + (s->sp - s->ss) / 2;
	*y = row_y(s, r) + (s->rh - s->ss) / 2;
}

static void print_digit(GameCtx *g, S *s, int r, int j, int d) {
	int x, y;
	char t[2] = { (char)('0' + d), 0 };
	slot_box(s, r, j, &x, &y);
	g_print(g, x, y, s->ss, s->ss, s->ss * 60 / 100, 1, t);
}

static void clear_slot(GameCtx *g, S *s, int r, int j) {
	int x, y;
	slot_box(s, r, j, &x, &y);
	g_clear(g, x + 4, y + 4, s->ss - 8, s->ss - 8);
}

static void marker(GameCtx *g, S *s, int r, int on) {
	int y = row_y(s, r), x = s->bx - 60;
	if (!on) { g_clear(g, x - 4, y + 4, 60, s->rh - 8); return; }
	int tri[8] = { x, y + s->rh / 2 - 22, x + 40, y + s->rh / 2, x, y + s->rh / 2 + 22, x, y + s->rh / 2 - 22 };
	g_poly(g, tri, 4, 5);
}

static void reveal(GameCtx *g, S *s) {
	g_speed(g, 3);
	for (int j = 0; j < PEGS; j++) { clear_slot(g, s, -1, j); print_digit(g, s, -1, j, s->code[j]); }
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	int lvl = g_level(g), used[7] = { 0 };
	for (int j = 0; j < PEGS; j++) {
		int d;
		do d = 1 + g_rand(g, 6); while (lvl < 2 && used[d]);
		used[d] = 1;
		s->code[j] = d;
	}
	s->kw = (aw - 2 * M) / NKEYS;
	if (s->kw > 220) s->kw = 220;
	s->kh = 150;
	s->kx = ax + (aw - NKEYS * s->kw) / 2;
	s->ky = ay + ah - M - s->kh;
	s->by = ay + M;
	s->rh = (s->ky - 40 - s->by - 30) / (ROWS + 1);
	if (s->rh > 170) s->rh = 170;
	s->lw = s->rh * 8 / 10;
	s->sp = s->rh * 115 / 100;
	s->ss = s->rh - 24;
	int total = s->lw + PEGS * s->sp + 30 + s->rh;
	s->bx = ax + (aw - total) / 2;
	s->fx = s->bx + s->lw + PEGS * s->sp + 30;

	g_speed(g, 10);
	for (int j = 0; j < PEGS; j++) {
		int x, y;
		slot_box(s, -1, j, &x, &y);
		g_frame(g, x, y, s->ss, s->ss, 5);
		g_print(g, x, y, s->ss, s->ss, s->ss * 50 / 100, 1, "?");
	}
	int sep = row_y(s, 0) - 15;
	g_line(g, s->bx - 60, sep, s->bx + total, sep, 3);
	for (int r = 0; r < ROWS; r++) {
		char t[12];
		snprintf(t, sizeof t, "%d", r + 1);
		g_print(g, s->bx, row_y(s, r), s->lw, s->rh, s->rh * 32 / 100, 0, t);
		for (int j = 0; j < PEGS; j++) {
			int x, y;
			slot_box(s, r, j, &x, &y);
			g_frame(g, x, y, s->ss, s->ss, 2);
		}
	}
	for (int k = 0; k < NKEYS; k++) {
		char t[2] = { (char)('1' + k), 0 };
		int x = s->kx + k * s->kw;
		g_frame(g, x + 6, s->ky, s->kw - 12, s->kh, 3);
		g_print(g, x + 6, s->ky, s->kw - 12, s->kh, k < 6 ? s->kh * 50 / 100 : s->kh * 26 / 100, k == 7, k < 6 ? t : k == 6 ? "Erase" : "Enter");
	}
	marker(g, s, 0, 1);
	g_remark(g, lvl < 2 ? "Crack my code: 4 different digits, 1 to 6." : "Crack my code: 4 digits, 1 to 6 (repeats allowed).");
	g_say(g, "tap 4 digits, then Enter");
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment - still drawing"); return; }
	if (st->cy < s->ky || st->cy >= s->ky + s->kh || st->cx < s->kx || st->cx >= s->kx + NKEYS * s->kw) {
		g_say(g, "use the keys at the bottom");
		return;
	}
	int k = (st->cx - s->kx) / s->kw;
	g_speed(g, 6);
	if (k < 6) {
		if (s->ncur == PEGS) { g_say(g, "4 digits in - tap Enter (or Erase)"); return; }
		if (g_level(g) == 0)
			for (int j = 0; j < s->ncur; j++)
				if (s->guess[s->row][j] == k + 1) { g_say(g, "no repeats - my code has 4 different digits"); return; }
		s->guess[s->row][s->ncur] = k + 1;
		print_digit(g, s, s->row, s->ncur, k + 1);
		s->ncur++;
		if (s->ncur < PEGS) g_say(g, "%d more digit%s", PEGS - s->ncur, PEGS - s->ncur == 1 ? "" : "s");
		else g_say(g, "tap Enter to check");
		return;
	}
	if (k == 6) {
		if (!s->ncur) { g_say(g, "nothing to erase"); return; }
		s->ncur--;
		clear_slot(g, s, s->row, s->ncur);
		g_say(g, "tap a digit");
		return;
	}
	if (s->ncur < PEGS) { g_say(g, "fill all 4 digits first"); return; }
	int black = 0, white = 0, cg[7] = { 0 }, cc[7] = { 0 };
	for (int j = 0; j < PEGS; j++) {
		black += s->guess[s->row][j] == s->code[j];
		cg[s->guess[s->row][j]]++;
		cc[s->code[j]]++;
	}
	for (int d = 1; d <= 6; d++) white += cg[d] < cc[d] ? cg[d] : cc[d];
	white -= black;
	int y = row_y(s, s->row), r = s->rh * 11 / 100;
	g_speed(g, 3);
	for (int p = 0; p < black + white; p++) {
		int px = s->fx + s->rh * 30 / 100 + (p % 2) * s->rh * 40 / 100, py = y + s->rh * 30 / 100 + (p / 2) * s->rh * 40 / 100;
		if (p < black) g_dot(g, px, py, r);
		else g_circle(g, px, py, r, 4);
	}
	if (black == PEGS) {
		reveal(g, s);
		snprintf(s->msg, sizeof s->msg, "Cracked it in %d guess%s!", s->row + 1, s->row ? "es" : "");
		g_over(g, s->msg);
		return;
	}
	marker(g, s, s->row, 0);
	s->row++;
	s->ncur = 0;
	if (s->row == ROWS) {
		reveal(g, s);
		snprintf(s->msg, sizeof s->msg, "Out of guesses - the code was %d%d%d%d.", s->code[0], s->code[1], s->code[2], s->code[3]);
		g_over(g, s->msg);
		return;
	}
	g_speed(g, 6);
	marker(g, s, s->row, 1);
	g_say(g, "%d right place, %d wrong place - guess %d of %d", black, white, s->row + 1, ROWS);
}

const GameDef GAME_codebreaker = { "codebreaker", "Code Breaker", "Crack the hidden 4-digit code", sizeof(S), 0, 1, start, stroke, 0 };

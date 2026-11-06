/* Word Ladder: turn the start word into the goal word one letter at a time; every step must be a real word.
 * Easy = 3-letter words, Medium = 4, Hard = 5. Puzzles come from a breadth-first search over common words, so a
 * solution of known length exists (Easy 3-4 steps, Medium 4-6, Hard 5-8); the player's steps may use any word in the
 * list. The next row starts as a copy of the last word: tap a letter box, tap a new letter, then Enter. */
#include "../games.h"
#include "dict_words.h"
#include <stdio.h>
#include <string.h>

#define MAXL 5
#define MAXW (DICT_N5 > DICT_N4 ? (DICT_N5 > DICT_N3 ? DICT_N5 : DICT_N3) : (DICT_N4 > DICT_N3 ? DICT_N4 : DICT_N3))
#define MAXROWS 16 /* entry rows, not counting the start and goal rows */
#define M 20
#define KCOLS 13
#define NCTL 4
#define UNSEEN 255

enum { K_ERASE, K_UNDO, K_ENTER, K_GIVEUP };
static const char *const CTL_LABEL[NCTL] = { "Erase", "Undo", "Enter", "Give up" };

typedef struct {
	int L, shortest, lo, hi;
	char start[MAXL + 1], goal[MAXL + 1];
	char rows[MAXROWS + 1][MAXL + 1]; /* rows[0] = start, then the committed steps */
	int nrows;                         /* committed rows including the start */
	char cur[MAXL + 1];                /* the row being edited (screen row nrows) */
	int sel, maxrows, has_cur;
	char sol[MAXROWS + 2][MAXL + 1];   /* a solution through common words */
	int nsol;
	int giveup_armed, over, ready;
	/* layout */
	int rx, ry, rh, bw, gy;            /* ladder: left edge, top of row 0, row pitch, box pitch, top of the goal row */
	int kx, ky, kw, kh, cy, cw;        /* keypad letters; control row top and key width */
	/* breadth-first search scratch */
	unsigned char dist[MAXW];
	unsigned short parent[MAXW], queue[MAXW];
	char msg[200];
} S;

/* ---------------- puzzle generation ---------------- */

/* BFS from word index src over words of length L (common ones only if common_only); fills dist/parent */
static void bfs(S *s, int L, int src, int common_only) {
	int n = dict_count(L), qh = 0, qt = 0;
	memset(s->dist, UNSEEN, sizeof s->dist);
	s->dist[src] = 0;
	s->parent[src] = (unsigned short)src;
	s->queue[qt++] = (unsigned short)src;
	while (qh < qt) {
		int u = s->queue[qh++];
		char w[MAXL + 1];
		dict_word(L, u, w);
		if (s->dist[u] >= 250) continue;
		for (int p = 0; p < L; p++) {
			char keep = w[p];
			for (char c = 'a'; c <= 'z'; c++) {
				if (c == keep) continue;
				w[p] = c;
				int v = dict_find(w, L);
				if (v < 0 || s->dist[v] != UNSEEN || (common_only && !dict_common(L, v))) continue;
				s->dist[v] = (unsigned char)(s->dist[u] + 1);
				s->parent[v] = (unsigned short)u;
				if (qt < n) s->queue[qt++] = (unsigned short)v;
			}
			w[p] = keep;
		}
	}
}

static void upper(char *t) { for (; *t; t++) if (*t >= 'a' && *t <= 'z') *t = (char)(*t - 'a' + 'A'); }

static void make_puzzle(GameCtx *g, S *s) {
	int L = s->L, n = dict_count(L);
	for (int attempt = 0; attempt < 400; attempt++) {
		int src = g_rand(g, n);
		if (!dict_common(L, src)) continue;
		bfs(s, L, src, 1);
		/* pick a distance in range that has words, then a word at that distance */
		int have[256] = { 0 }, nd = 0, ds[16];
		for (int i = 0; i < n; i++) if (s->dist[i] != UNSEEN) have[s->dist[i]]++;
		for (int d = s->lo; d <= s->hi; d++) if (have[d]) ds[nd++] = d;
		if (!nd) continue;
		int d = ds[g_rand(g, nd)], pick = g_rand(g, have[d]), dst = -1;
		for (int i = 0; i < n && dst < 0; i++) if (s->dist[i] == d && pick-- == 0) dst = i;
		/* the common-word path, start .. goal */
		s->nsol = d + 1;
		for (int k = d, v = dst; k >= 0; k--, v = s->parent[v]) dict_word(L, v, s->sol[k]);
		/* the true shortest ladder may use rarer words: it must still be in range */
		bfs(s, L, src, 0);
		if (s->dist[dst] < s->lo && attempt < 399) continue;
		s->shortest = s->dist[dst];
		dict_word(L, src, s->start);
		dict_word(L, dst, s->goal);
		return;
	}
	/* (not reached with this word list; keep a valid fallback) */
	strcpy(s->start, L == 3 ? "cat" : L == 4 ? "cold" : "black");
	strcpy(s->goal, L == 3 ? "dog" : L == 4 ? "warm" : "white");
	s->shortest = s->nsol = 0;
}

/* ---------------- layout and drawing ---------------- */

static int box_x(const S *s, int p) { return s->rx + p * s->bw; }
static int row_y(const S *s, int r) { return s->ry + r * s->rh; }

static void key_box(const S *s, int k, int *x, int *y) { *x = s->kx + (k % KCOLS) * s->kw; *y = s->ky + (k / KCOLS) * s->kh; }
static void ctl_box(const S *s, int k, int *x, int *y) { *x = s->kx + k * (s->cw + 20); *y = s->cy; }

/* (re)draw the inside of a letter box: clear it, an inner frame when selected, the letter */
static void draw_letter(GameCtx *g, S *s, int x, int y, char ch, int selected, int bold) {
	int b = s->rh - 12, w = s->bw - 12;
	g_clear(g, x + 5, y + 5, w - 10, b - 10);
	if (selected) g_frame(g, x + 10, y + 10, w - 20, b - 20, 6);
	if (ch) {
		char t[2] = { (char)(ch - 'a' + 'A'), 0 };
		g_print(g, x + 14, y + 14, w - 28, b - 28, (b - 28) * 80 / 100, bold, t);
	}
}

static void draw_row_letters(GameCtx *g, S *s, int r, const char *w, int bold) {
	for (int p = 0; p < s->L; p++) draw_letter(g, s, box_x(s, p), row_y(s, r), w ? w[p] : 0, r == s->nrows && p == s->sel, bold);
}

static void draw_row_frames(GameCtx *g, S *s, int y, int width) {
	for (int p = 0; p < s->L; p++) g_frame(g, box_x(s, p), y, s->bw - 12, s->rh - 12, width);
}

static void draw_key(GameCtx *g, S *s, int k) {
	int x, y;
	char t[2] = { (char)('A' + k), 0 };
	key_box(s, k, &x, &y);
	g_frame(g, x + 5, y + 5, s->kw - 10, s->kh - 10, 3);
	g_print(g, x + 8, y + 8, s->kw - 16, s->kh - 16, (s->kh - 16) * 55 / 100, 1, t);
}

static void draw_ctl(GameCtx *g, S *s, int k) {
	int x, y;
	ctl_box(s, k, &x, &y);
	g_frame(g, x, y, s->cw, s->kh - 10, 4);
	g_print(g, x + 8, y + 8, s->cw - 16, s->kh - 26, (s->kh - 26) * 50 / 100, 1, CTL_LABEL[k]);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah, lvl = g_level(g);
	g_area(g, &ax, &ay, &aw, &ah);
	s->L = 3 + lvl;
	s->lo = lvl == 0 ? 3 : lvl == 1 ? 4 : 5;
	s->hi = lvl == 0 ? 4 : lvl == 1 ? 6 : 8;
	make_puzzle(g, s);
	strcpy(s->rows[0], s->start);
	s->nrows = 1;
	s->sel = -1;

	/* keypad: two rows of 13 letters and a row of controls at the bottom */
	s->kw = (aw - 2 * M) / KCOLS;
	if (s->kw > 150) s->kw = 150;
	s->kh = s->kw < 130 ? s->kw : 130;
	s->kx = ax + (aw - KCOLS * s->kw) / 2;
	s->cw = (KCOLS * s->kw - (NCTL - 1) * 20) / NCTL;
	s->cy = ay + ah - M - s->kh;
	s->ky = s->cy - 10 - 2 * s->kh;
	/* the ladder: start row, entry rows, a gap, the goal row */
	int top = ay + M, bottom = s->ky - 40;
	s->maxrows = s->shortest + 5;
	if (s->maxrows > MAXROWS) s->maxrows = MAXROWS;
	for (;;) {
		s->rh = (bottom - top - 40) / (s->maxrows + 2);
		if (s->rh > 140) s->rh = 140;
		if (s->rh >= 80 || s->maxrows <= s->shortest) break;
		s->maxrows--;
	}
	s->bw = s->rh * 3 / 2 < 190 ? s->rh * 3 / 2 : 190; /* wider than tall: easier to tap */
	s->rx = ax + (aw - s->L * s->bw) / 2;
	s->ry = top;
	s->gy = row_y(s, s->maxrows + 1) + 40;

	g_speed(g, 10);
	draw_row_frames(g, s, row_y(s, 0), 5);
	for (int r = 1; r <= s->maxrows; r++) draw_row_frames(g, s, row_y(s, r), 3);
	draw_row_frames(g, s, s->gy, 5);
	/* step numbers left of the entry rows, labels for the ends */
	int lw = 240, lx = s->rx - lw - 20, lh = s->rh - 12, px = lh * 45 / 100;
	if (lx < ax) lx = ax;
	g_print(g, lx, row_y(s, 0), lw, lh, px, 0, "start");
	for (int r = 1; r <= s->maxrows; r++) {
		char t[12];
		snprintf(t, sizeof t, "%d", r);
		g_print(g, lx, row_y(s, r), lw, lh, px, 0, t);
	}
	g_print(g, lx, s->gy, lw, lh, px, 0, "goal");
	g_speed(g, 6);
	for (int p = 0; p < s->L; p++) {
		draw_letter(g, s, box_x(s, p), row_y(s, 0), s->start[p], 0, 1);
		draw_letter(g, s, box_x(s, p), s->gy, s->goal[p], 0, 1);
	}
	g_speed(g, 20);
	for (int k = 0; k < 26; k++) draw_key(g, s, k);
	for (int k = 0; k < NCTL; k++) draw_ctl(g, s, k);
	/* the first entry row starts as a copy of the start word */
	strcpy(s->cur, s->start);
	s->has_cur = 1;
	g_speed(g, 12);
	draw_row_letters(g, s, 1, s->cur, 0);
	char a[MAXL + 1], b[MAXL + 1];
	strcpy(a, s->start); strcpy(b, s->goal); upper(a); upper(b);
	snprintf(s->msg, sizeof s->msg, "%s to %s, one letter at a time - it can be done in %d steps.", a, b, s->shortest);
	g_remark(g, s->msg);
	g_say(g, "tap a letter box in row 1, then a new letter");
}

/* ---------------- play ---------------- */

static int key_at(const S *s, int px, int py) {
	for (int k = 0; k < 26; k++) {
		int x, y;
		key_box(s, k, &x, &y);
		if (px >= x && px < x + s->kw && py >= y && py < y + s->kh) return k;
	}
	for (int k = 0; k < NCTL; k++) {
		int x, y;
		ctl_box(s, k, &x, &y);
		if (px >= x && px < x + s->cw && py >= y && py < y + s->kh) return 100 + k;
	}
	return -1;
}

/* which box (row, position) is under the point; returns 0 if none. Row maxrows+1 = the goal row. */
static int box_at(const S *s, int px, int py, int *row, int *pos) {
	if (px < s->rx || px >= s->rx + s->L * s->bw) return 0;
	*pos = (px - s->rx) / s->bw;
	if (py >= s->gy && py < s->gy + s->rh) { *row = s->maxrows + 1; return 1; }
	if (py < s->ry || py >= row_y(s, s->maxrows + 1)) return 0;
	*row = (py - s->ry) / s->rh;
	return 1;
}

static void select_box(GameCtx *g, S *s, int pos) {
	int old = s->sel;
	s->sel = pos;
	g_speed(g, 30);
	if (old >= 0 && old != pos) draw_letter(g, s, box_x(s, old), row_y(s, s->nrows), s->cur[old], 0, 0);
	if (pos >= 0) draw_letter(g, s, box_x(s, pos), row_y(s, s->nrows), s->cur[pos], 1, 0);
}

static void give_up(GameCtx *g, S *s) {
	char path[160] = "";
	for (int k = 0; k < s->nsol; k++) {
		char w[MAXL + 1];
		strcpy(w, s->sol[k]);
		upper(w);
		if (k) strcat(path, " ");
		strcat(path, w);
	}
	snprintf(s->msg, sizeof s->msg, "One way (%d steps): %s", s->nsol - 1, path);
	s->over = 1;
	g_over(g, s->msg);
}

static void enter(GameCtx *g, S *s) {
	const char *prev = s->rows[s->nrows - 1];
	char w[MAXL + 1];
	int diff = 0;
	for (int p = 0; p < s->L; p++) diff += s->cur[p] != prev[p];
	strcpy(w, s->cur);
	upper(w);
	if (!diff) { g_say(g, "change one letter first"); return; }
	if (diff > 1) { g_say(g, "only one letter may change per step - Erase starts the row again"); return; }
	if (dict_find(s->cur, s->L) < 0) { g_say(g, "%s isn't in my word list", w); return; }
	for (int r = 0; r < s->nrows; r++)
		if (!strcmp(s->rows[r], s->cur)) { g_say(g, "%s is already in your ladder", w); return; }
	strcpy(s->rows[s->nrows], s->cur);
	int r = s->nrows++;
	s->sel = -1;
	g_speed(g, 25);
	for (int p = 0; p < s->L; p++) draw_letter(g, s, box_x(s, p), row_y(s, r), s->cur[p], 0, 1);
	int steps = s->nrows - 1;
	if (!strcmp(s->cur, s->goal)) {
		if (steps <= s->shortest) snprintf(s->msg, sizeof s->msg, "Solved in %d steps - the shortest possible!", steps);
		else snprintf(s->msg, sizeof s->msg, "Solved in %d steps - the shortest is %d.", steps, s->shortest);
		s->over = 1;
		g_over(g, s->msg);
		return;
	}
	if (steps >= s->maxrows) {
		s->has_cur = 0;
		g_say(g, "%s - no rows left: Undo a step and try another way", w);
		return;
	}
	s->has_cur = 1;
	g_speed(g, 12);
	draw_row_letters(g, s, s->nrows, s->cur, 0);
	int to_go = 0;
	for (int p = 0; p < s->L; p++) to_go += s->cur[p] != s->goal[p];
	g_say(g, "%s - step %d; %d letter%s differ from the goal", w, steps, to_go, to_go == 1 ? "" : "s");
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->over) return;
	if (!s->ready) {
		if (g_busy(g)) { g_say(g, "one moment - still drawing the ladder"); return; }
		s->ready = 1;
	}
	if (!st->tap && (st->x1 - st->x0 > 60 || st->y1 - st->y0 > 60)) { g_say(g, "tap a letter box, then a letter key"); return; }
	int k = key_at(s, st->cx, st->cy), row, pos;
	if (k != 100 + K_GIVEUP) s->giveup_armed = 0;
	if (k >= 0 && k < 26) {
		if (!s->has_cur) { g_say(g, "no rows left - Undo a step first"); return; }
		if (s->sel < 0) { g_say(g, "tap the letter box to change first"); return; }
		s->cur[s->sel] = (char)('a' + k);
		g_speed(g, 8);
		draw_letter(g, s, box_x(s, s->sel), row_y(s, s->nrows), s->cur[s->sel], 1, 0);
		char w[MAXL + 1];
		strcpy(w, s->cur);
		upper(w);
		g_say(g, "%s - tap Enter to take this step", w);
		return;
	}
	if (k == 100 + K_ENTER) {
		if (!s->has_cur) { g_say(g, "no rows left - Undo a step first"); return; }
		enter(g, s);
		return;
	}
	if (k == 100 + K_ERASE) {
		if (!s->has_cur) { g_say(g, "nothing to erase - Undo goes back a step"); return; }
		const char *prev = s->rows[s->nrows - 1];
		g_speed(g, 25);
		for (int p = 0; p < s->L; p++)
			if (s->cur[p] != prev[p]) {
				s->cur[p] = prev[p];
				draw_letter(g, s, box_x(s, p), row_y(s, s->nrows), s->cur[p], p == s->sel, 0);
			}
		g_say(g, "row %d is back to the last word", s->nrows);
		return;
	}
	if (k == 100 + K_UNDO) {
		if (s->nrows <= 1) { g_say(g, "nothing to undo yet"); return; }
		g_speed(g, 30);
		s->sel = -1;
		if (s->has_cur) draw_row_letters(g, s, s->nrows, 0, 0); /* empty the row being edited */
		s->nrows--;
		strcpy(s->cur, s->rows[s->nrows]);
		s->has_cur = 1;
		s->sel = -1;
		draw_row_letters(g, s, s->nrows, s->cur, 0);
		g_say(g, "back a step - row %d can be changed again", s->nrows);
		return;
	}
	if (k == 100 + K_GIVEUP) {
		if (!s->giveup_armed) { s->giveup_armed = 1; g_say(g, "tap Give up again to see a solution"); return; }
		give_up(g, s);
		return;
	}
	if (box_at(s, st->cx, st->cy, &row, &pos)) {
		if (row == s->nrows && s->has_cur) {
			select_box(g, s, pos);
			g_say(g, "now tap the new letter for box %d", pos + 1);
			return;
		}
		if (row == 0 || row == s->maxrows + 1) { g_say(g, "that's the %s word - change the row being written", row ? "goal" : "start"); return; }
		if (row < s->nrows) { g_say(g, "row %d is done - Undo to go back to it", row); return; }
		g_say(g, "work in row %d - one step at a time", s->nrows);
		return;
	}
	g_say(g, "tap a letter box, then a letter key");
}

const GameDef GAME_wordladder = { "wordladder", "Word Ladder", "Change one letter at a time to reach the goal word", sizeof(S), 0, 1, start, stroke, 0 };

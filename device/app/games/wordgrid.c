/* Word Grid: a square of letters (4x4 Easy, 5x5 Medium, 6x6 Hard). Draw a path through neighbouring letters (across,
 * up/down or diagonal, each letter at most once) to spell a word; Q stands for QU. Words need 3+ letters (4+ on Hard)
 * and must be in the word list. Every grid is solved before play and regenerated until it holds plenty of words; the
 * level sets a target. Done ends the round and shows some of the words the player missed. Finger players can tap the
 * letters one by one instead and tap Enter (or the last letter again). */
#include "../games.h"
#include "dict_words.h"
#include <stdio.h>
#include <string.h>

#define MAXN 6
#define MAXSOL 900
#define MAXFOUND MAXSOL
#define MAXSLOTS 160
#define NBTN 3
#define M 20
#define LCOLS 6
#define MISSED_SHOWN 12

enum { B_ENTER, B_CLEAR, B_DONE };
static const char *const BTN_LABEL[NBTN] = { "Enter", "Clear", "Done" };

typedef struct { unsigned char len; unsigned short idx; } WRef;

typedef struct {
	int n, minlen, target, total;
	char grid[MAXN * MAXN];        /* 'a'..'z'; 'q' is shown and spelled as "qu" */
	int ax, ay, aw, ah;
	int gx, gy, cell;              /* grid origin and cell size */
	int sx, sy, sw, sh;            /* score box */
	int bx[NBTN], by, bw, bh;      /* buttons */
	int lx, ly, lw, lh, lrows;     /* found-word list */
	int nsol;
	WRef sol[MAXSOL];
	int nfound, score, target_hit, done_armed, over;
	WRef found[MAXFOUND];
	int tpath[MAXN * MAXN], ntap;  /* tapped path (finger play) */
	unsigned char dirty_cell[MAXN * MAXN], dirty_slot[MAXSLOTS], dirty_btn[NBTN], dirty_score;
	char msg[200];
} S;

/* ---------------- grid generation and solving ---------------- */

/* letter weights for the grid (roughly English frequency, vowels slightly favoured) */
static const unsigned char WEIGHT[26] = {
	/* a  b  c  d   e  f  g  h  i  j  k  l  m  n  o  p  q  r  s  t  u  v  w  x  y  z */
	   9, 2, 3, 4, 12, 2, 3, 3, 8, 1, 1, 4, 3, 6, 8, 2, 1, 6, 5, 7, 4, 1, 2, 1, 2, 1 };

static char random_letter(GameCtx *g) {
	int total = 0;
	for (int i = 0; i < 26; i++) total += WEIGHT[i];
	int r = g_rand(g, total);
	for (int i = 0; i < 26; i++) {
		if (r < WEIGHT[i]) return (char)('a' + i);
		r -= WEIGHT[i];
	}
	return 'e';
}

static int is_vowel(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }

static void random_grid(GameCtx *g, S *s) {
	int cells = s->n * s->n;
	for (;;) {
		int vowels = 0, rare = 0;
		for (int i = 0; i < cells; i++) {
			s->grid[i] = random_letter(g);
			vowels += is_vowel(s->grid[i]);
			rare += strchr("qjxz", s->grid[i]) != 0;
		}
		if (vowels * 10 < cells * 3 || vowels * 2 > cells || rare > (s->n >= 6 ? 2 : 1)) continue;
		return;
	}
}

/* A word as grid units: "qu" becomes one 'q' unit. Returns the unit count, or -1 if a q isn't followed by u. */
static int to_units(const char *w, int len, char *u) {
	int n = 0;
	for (int i = 0; i < len; i++) {
		u[n++] = w[i];
		if (w[i] == 'q') {
			if (i + 1 >= len || w[i + 1] != 'u') return -1;
			i++; /* the u is part of the Q tile */
		}
	}
	return n;
}

static int adjacent(const S *s, int a, int b) {
	int dr = a / s->n - b / s->n, dc = a % s->n - b % s->n;
	return a != b && dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1;
}

static int trace(const S *s, const char *u, int nu, int k, int cell, unsigned long long used) {
	if (k == nu) return 1;
	int n = s->n, r = cell / n, c = cell % n;
	for (int dr = -1; dr <= 1; dr++)
		for (int dc = -1; dc <= 1; dc++) {
			int rr = r + dr, cc = c + dc;
			if ((!dr && !dc) || rr < 0 || cc < 0 || rr >= n || cc >= n) continue;
			int j = rr * n + cc;
			if ((used >> j & 1) || s->grid[j] != u[k]) continue;
			if (trace(s, u, nu, k + 1, j, used | 1ULL << j)) return 1;
		}
	return 0;
}

/* Find every dictionary word in the grid (s->sol); returns how many. */
static int solve(S *s) {
	int cells = s->n * s->n, have[26] = { 0 };
	for (int i = 0; i < cells; i++) have[s->grid[i] - 'a']++;
	s->nsol = 0;
	for (int len = s->minlen; len <= DICT_MAXLEN; len++) {
		int cnt = dict_count(len);
		for (int i = 0; i < cnt; i++) {
			char w[DICT_MAXLEN + 1], u[DICT_MAXLEN];
			int need[26] = { 0 }, ok = 1;
			dict_word(len, i, w);
			int nu = to_units(w, len, u);
			if (nu < 1 || nu > cells) continue;
			for (int k = 0; k < nu && ok; k++) ok = ++need[u[k] - 'a'] <= have[u[k] - 'a'];
			if (!ok) continue;
			int hit = 0;
			for (int c = 0; c < cells && !hit; c++)
				if (s->grid[c] == u[0]) hit = trace(s, u, nu, 1, c, 1ULL << c);
			if (hit && s->nsol < MAXSOL) {
				s->sol[s->nsol].len = (unsigned char)len;
				s->sol[s->nsol].idx = (unsigned short)i;
				s->nsol++;
			}
		}
	}
	return s->nsol;
}

static int points(int len) { return len <= 4 ? 1 : len == 5 ? 2 : len == 6 ? 3 : len == 7 ? 5 : 11; }

/* ---------------- layout and drawing ---------------- */

static void cell_xy(const S *s, int i, int *x, int *y) { *x = s->gx + (i % s->n) * s->cell; *y = s->gy + (i / s->n) * s->cell; }

static void draw_tile(GameCtx *g, S *s, int i, int marked) {
	int x, y, in = s->cell / 24 + 4;
	cell_xy(s, i, &x, &y);
	g_frame(g, x + in, y + in, s->cell - 2 * in, s->cell - 2 * in, 4);
	if (marked) g_frame(g, x + in + 12, y + in + 12, s->cell - 2 * in - 24, s->cell - 2 * in - 24, 7);
	char t[3] = { (char)(s->grid[i] - 'a' + 'A'), 0, 0 };
	int px = s->cell * 50 / 100;
	if (s->grid[i] == 'q') { t[1] = 'u'; px = s->cell * 40 / 100; }
	g_print(g, x + in + 14, y + in + 14, s->cell - 2 * in - 28, s->cell - 2 * in - 28, px, 1, t);
}

static int tap_marked(const S *s, int i) {
	for (int k = 0; k < s->ntap; k++) if (s->tpath[k] == i) return 1;
	return 0;
}

static void redraw_tile(GameCtx *g, S *s, int i) {
	int x, y;
	cell_xy(s, i, &x, &y);
	g_clear(g, x, y, s->cell, s->cell);
	draw_tile(g, s, i, tap_marked(s, i));
}

static void draw_button(GameCtx *g, S *s, int b) {
	g_frame(g, s->bx[b], s->by, s->bw, s->bh, 4);
	g_print(g, s->bx[b] + 8, s->by + 8, s->bw - 16, s->bh - 16, s->bh * 40 / 100, 1, BTN_LABEL[b]);
}

static void draw_score(GameCtx *g, S *s) {
	char t[64];
	snprintf(t, sizeof t, "Score %d   %d of %d words", s->score, s->nfound, s->target);
	g_print(g, s->sx, s->sy, s->sw, s->sh, s->sh * 42 / 100, 1, t);
}

static int nslots(const S *s) { return s->lrows * LCOLS; }

static void slot_xy(const S *s, int k, int *x, int *y) { *x = s->lx + (k % LCOLS) * s->lw; *y = s->ly + (k / LCOLS) * s->lh; }

/* what list slot k shows: found word k, or "+N more" in the last slot when the list overflows */
static void draw_slot(GameCtx *g, S *s, int k) {
	int x, y, last = nslots(s) - 1;
	char t[24];
	if (k >= s->nfound || k > last) return;
	if (k == last && s->nfound > nslots(s)) snprintf(t, sizeof t, "+%d more", s->nfound - last);
	else {
		dict_word(s->found[k].len, s->found[k].idx, t);
		for (char *p = t; *p; p++) *p = (char)(*p - 'a' + 'A');
	}
	slot_xy(s, k, &x, &y);
	g_print(g, x + 4, y + 4, s->lw - 8, s->lh - 8, s->lh * 62 / 100, 1, t);
}

/* ---------------- ink cleanup ---------------- */

static int rect_hit(int x, int y, int w, int h, int x0, int y0, int x1, int y1) { return x < x1 && x + w > x0 && y < y1 && y + h > y0; }

/* mark every element touching the box; returns 1 if (px,py) itself lies on an element */
static int mark_box(S *s, int px, int py, int r) {
	int x0 = px - r, y0 = py - r, x1 = px + r, y1 = py + r, inside = 0;
	if (rect_hit(s->sx, s->sy, s->sw, s->sh, x0, y0, x1, y1)) s->dirty_score = 1;
	if (rect_hit(s->sx, s->sy, s->sw, s->sh, px, py, px + 1, py + 1)) inside = 1;
	for (int b = 0; b < NBTN; b++) {
		if (rect_hit(s->bx[b], s->by, s->bw, s->bh, x0, y0, x1, y1)) s->dirty_btn[b] = 1;
		if (rect_hit(s->bx[b], s->by, s->bw, s->bh, px, py, px + 1, py + 1)) inside = 1;
	}
	for (int i = 0; i < s->n * s->n; i++) {
		int x, y;
		cell_xy(s, i, &x, &y);
		if (rect_hit(x, y, s->cell, s->cell, x0, y0, x1, y1)) s->dirty_cell[i] = 1;
		if (rect_hit(x, y, s->cell, s->cell, px, py, px + 1, py + 1)) inside = 1;
	}
	for (int k = 0; k < nslots(s); k++) {
		int x, y;
		slot_xy(s, k, &x, &y);
		if (rect_hit(x, y, s->lw, s->lh, x0, y0, x1, y1)) s->dirty_slot[k] = 1;
		if (rect_hit(x, y, s->lw, s->lh, px, py, px + 1, py + 1)) inside = 1;
	}
	return inside;
}

/* Wipe the player's ink: clear patches where it crossed empty paper, then redraw every element it touched. */
static void wipe_ink(GameCtx *g, S *s, const GStroke *st) {
	const int R = 18;
	int lastx = -10000, lasty = -10000;
	memset(s->dirty_cell, 0, sizeof s->dirty_cell);
	memset(s->dirty_slot, 0, sizeof s->dirty_slot);
	memset(s->dirty_btn, 0, sizeof s->dirty_btn);
	s->dirty_score = 0;
	g_speed(g, 50);
	for (int i = 0; i < st->n; i++) {
		int x0 = st->x[i > 0 ? i - 1 : 0], y0 = st->y[i > 0 ? i - 1 : 0], x1 = st->x[i], y1 = st->y[i];
		int d = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1), steps = d / 10 + 1;
		for (int k = 1; k <= steps; k++) {
			int px = x0 + (x1 - x0) * k / steps, py = y0 + (y1 - y0) * k / steps;
			int inside = mark_box(s, px, py, R);
			int dx = px - lastx, dy = py - lasty;
			if (!inside && (dx > 12 || dx < -12 || dy > 12 || dy < -12)) {
				int cx0 = px - R, cy0 = py - R, cx1 = px + R, cy1 = py + R;
				if (cx0 < s->ax) cx0 = s->ax;
				if (cy0 < s->ay) cy0 = s->ay;
				if (cx1 > s->ax + s->aw) cx1 = s->ax + s->aw;
				if (cy1 > s->ay + s->ah) cy1 = s->ay + s->ah;
				if (cx1 > cx0 && cy1 > cy0) g_clear(g, cx0, cy0, cx1 - cx0, cy1 - cy0);
				lastx = px; lasty = py;
			}
		}
	}
	if (s->dirty_score) { g_clear(g, s->sx, s->sy, s->sw, s->sh); draw_score(g, s); }
	for (int b = 0; b < NBTN; b++)
		if (s->dirty_btn[b]) { g_clear(g, s->bx[b], s->by, s->bw, s->bh); draw_button(g, s, b); }
	for (int i = 0; i < s->n * s->n; i++) if (s->dirty_cell[i]) redraw_tile(g, s, i);
	for (int k = 0; k < nslots(s); k++)
		if (s->dirty_slot[k]) {
			int x, y;
			slot_xy(s, k, &x, &y);
			g_clear(g, x, y, s->lw, s->lh);
			draw_slot(g, s, k);
		}
}

/* ---------------- play ---------------- */

static void start(GameCtx *g) {
	S *s = g_state(g);
	int lvl = g_level(g);
	g_area(g, &s->ax, &s->ay, &s->aw, &s->ah);
	s->n = 4 + lvl;
	s->minlen = lvl == 2 ? 4 : 3;
	int want = lvl == 0 ? 25 : lvl == 1 ? 45 : 60, best = -1;
	char best_grid[MAXN * MAXN];
	for (int attempt = 0; attempt < 25; attempt++) {
		random_grid(g, s);
		int n = solve(s);
		if (n > best) { best = n; memcpy(best_grid, s->grid, sizeof best_grid); }
		if (n >= want) break;
	}
	memcpy(s->grid, best_grid, sizeof best_grid);
	s->total = solve(s);
	int goal = lvl == 0 ? 12 : lvl == 1 ? 20 : 25;
	s->target = goal < s->total / 3 ? goal : s->total / 3;
	if (s->target < 1) s->target = 1;

	/* layout: score + buttons on top, the grid, then the list of found words */
	int ax = s->ax, ay = s->ay, aw = s->aw, ah = s->ah;
	s->bh = 110;
	s->by = ay + M;
	s->bw = aw / 8 < 240 ? aw / 8 : 240;
	for (int b = 0; b < NBTN; b++) s->bx[b] = ax + aw - M - (NBTN - b) * (s->bw + 20) + 20;
	s->sx = ax + M; s->sy = s->by; s->sh = s->bh;
	s->sw = s->bx[0] - 30 - s->sx;
	int cap = s->n == 4 ? 260 : 240;
	s->cell = (aw - 2 * M) / s->n;
	if (s->cell > cap) s->cell = cap;
	int room = ay + ah - M - 30 - 8 * 60 - 30 - (s->by + s->bh); /* keep at least 8 rows for the word list */
	if (s->cell > room / s->n) s->cell = room / s->n;
	s->gx = ax + (aw - s->n * s->cell) / 2;
	s->gy = s->by + s->bh + 30;
	s->lx = ax + M;
	s->lw = (aw - 2 * M) / LCOLS;
	s->lh = 60;
	s->ly = s->gy + s->n * s->cell + 30;
	s->lrows = (ay + ah - M - s->ly) / s->lh;
	if (s->lrows < 1) s->lrows = 1;
	if (s->lrows * LCOLS > MAXSLOTS) s->lrows = MAXSLOTS / LCOLS;

	g_speed(g, 9);
	for (int i = 0; i < s->n * s->n; i++) draw_tile(g, s, i, 0);
	g_speed(g, 12);
	for (int b = 0; b < NBTN; b++) draw_button(g, s, b);
	draw_score(g, s);
	snprintf(s->msg, sizeof s->msg, "Find %d words of %d+ letters - draw through neighbouring letters.", s->target, s->minlen);
	g_remark(g, s->msg);
	g_say(g, "draw a word through neighbouring letters (Q = QU)");
}

static void clear_taps(GameCtx *g, S *s) {
	int old[MAXN * MAXN], n = s->ntap;
	memcpy(old, s->tpath, sizeof old);
	s->ntap = 0;
	g_speed(g, 30);
	for (int k = 0; k < n; k++) redraw_tile(g, s, old[k]);
}

static void finish_round(GameCtx *g, S *s) {
	/* some words the player missed: the longest first, common words before rare ones */
	WRef miss[MISSED_SHOWN];
	int nmiss = 0;
	for (int pass = 0; pass < 2 * (DICT_MAXLEN + 1); pass++) {
		int len = DICT_MAXLEN - pass / 2, want_common = pass % 2 == 0;
		if (len < s->minlen) break;
		for (int i = 0; i < s->nsol; i++) {
			if (s->sol[i].len != len || dict_common(len, s->sol[i].idx) != want_common) continue;
			int had = 0;
			for (int k = 0; k < s->nfound && !had; k++) had = s->found[k].len == len && s->found[k].idx == s->sol[i].idx;
			if (had) continue;
			if (nmiss < MISSED_SHOWN) miss[nmiss++] = s->sol[i];
		}
	}
	int shown = s->nfound < nslots(s) ? s->nfound : nslots(s);
	int row = (shown + LCOLS - 1) / LCOLS, rows_left = s->lrows - row;
	if (nmiss && rows_left >= 2) {
		int x, y, can = (rows_left - 1) * LCOLS;
		slot_xy(s, row * LCOLS, &x, &y);
		g_speed(g, 8);
		g_print(g, x + 4, y + 4, 3 * s->lw - 8, s->lh - 8, s->lh * 55 / 100, 0, "Some you missed:");
		for (int k = 0; k < nmiss && k < can; k++) {
			char t[DICT_MAXLEN + 1];
			dict_word(miss[k].len, miss[k].idx, t);
			for (char *p = t; *p; p++) *p = (char)(*p - 'a' + 'A');
			slot_xy(s, (row + 1) * LCOLS + k, &x, &y);
			g_print(g, x + 4, y + 4, s->lw - 8, s->lh - 8, s->lh * 55 / 100, 0, t);
		}
	}
	snprintf(s->msg, sizeof s->msg, "%s Score %d, %d word%s (target %d). There were %d in all.",
	         s->nfound >= s->total ? "You found every word!" : s->nfound >= s->target ? "Target reached!" : "Round over.",
	         s->score, s->nfound, s->nfound == 1 ? "" : "s", s->target, s->total);
	s->over = 1;
	g_over(g, s->msg);
}

/* Judge a path of cells as a word. */
static void judge(GameCtx *g, S *s, const int *path, int np) {
	char w[MAXN * MAXN * 2 + 1], shout[MAXN * MAXN * 2 + 1];
	int len = 0;
	for (int k = 0; k < np; k++) {
		w[len++] = s->grid[path[k]];
		if (s->grid[path[k]] == 'q') w[len++] = 'u';
	}
	w[len] = 0;
	for (int k = 0; k <= len; k++) shout[k] = w[k] ? (char)(w[k] - 'a' + 'A') : 0;
	if (len < s->minlen) { g_say(g, "%s is too short - words need %d+ letters", shout, s->minlen); return; }
	if (len > DICT_MAXLEN) { g_say(g, "%s - my word list stops at %d letters", shout, DICT_MAXLEN); return; }
	int idx = dict_find(w, len);
	if (idx < 0) { g_say(g, "%s isn't in my word list", shout); return; }
	for (int k = 0; k < s->nfound; k++)
		if (s->found[k].len == len && s->found[k].idx == idx) { g_say(g, "you already have %s", shout); return; }
	if (s->nfound >= MAXFOUND) { g_say(g, "that's all the words I can keep - tap Done"); return; }
	int p = points(len);
	s->found[s->nfound].len = (unsigned char)len;
	s->found[s->nfound].idx = (unsigned short)idx;
	s->nfound++;
	s->score += p;
	g_speed(g, 4);
	int k = s->nfound - 1, last = nslots(s) - 1;
	if (k < last || (k == last && s->nfound <= nslots(s))) draw_slot(g, s, k);
	else {
		int x, y;
		slot_xy(s, last, &x, &y);
		g_clear(g, x, y, s->lw, s->lh);
		draw_slot(g, s, last);
	}
	g_speed(g, 20);
	g_clear(g, s->sx, s->sy, s->sw, s->sh);
	draw_score(g, s);
	if (s->nfound >= s->total) { finish_round(g, s); return; }
	if (!s->target_hit && s->nfound >= s->target) {
		s->target_hit = 1;
		g_remark(g, "Target reached! Keep going, or tap Done.");
	}
	g_say(g, "%s +%d - %d of %d found", shout, p, s->nfound, s->target);
}

static int cell_at(const S *s, int x, int y, int strict) {
	if (x < s->gx || y < s->gy || x >= s->gx + s->n * s->cell || y >= s->gy + s->n * s->cell) return -1;
	int c = (x - s->gx) / s->cell, r = (y - s->gy) / s->cell, i = r * s->n + c;
	if (!strict) return i;
	int dx = x - (s->gx + c * s->cell + s->cell / 2), dy = y - (s->gy + r * s->cell + s->cell / 2), rad = s->cell * 42 / 100;
	return dx * dx + dy * dy <= rad * rad ? i : -1;
}

static int button_at(const S *s, int x, int y) {
	for (int b = 0; b < NBTN; b++)
		if (x >= s->bx[b] && x < s->bx[b] + s->bw && y >= s->by && y < s->by + s->bh) return b;
	return -1;
}

static void say_taps(GameCtx *g, S *s) {
	char t[MAXN * MAXN * 2 + 1];
	int len = 0;
	for (int k = 0; k < s->ntap; k++) {
		t[len++] = (char)(s->grid[s->tpath[k]] - 'a' + 'A');
		if (s->grid[s->tpath[k]] == 'q') t[len++] = 'U';
	}
	t[len] = 0;
	g_say(g, "%s - tap the next letter, or Enter", t);
}

static void tap(GameCtx *g, S *s, int x, int y) {
	int b = button_at(s, x, y);
	if (b == B_DONE) {
		if (!s->done_armed) { s->done_armed = 1; g_say(g, "tap Done again to end the round"); return; }
		if (s->ntap) clear_taps(g, s);
		finish_round(g, s);
		return;
	}
	s->done_armed = 0;
	if (b == B_ENTER) {
		if (!s->ntap) { g_say(g, "tap letters first (or draw a word with the pen)"); return; }
		int path[MAXN * MAXN], n = s->ntap;
		memcpy(path, s->tpath, sizeof path);
		clear_taps(g, s);
		judge(g, s, path, n);
		return;
	}
	if (b == B_CLEAR) {
		if (s->ntap) clear_taps(g, s);
		g_say(g, "cleared - draw or tap a word");
		return;
	}
	int c = cell_at(s, x, y, 0);
	if (c < 0) { g_say(g, "draw a word on the letters"); return; }
	g_speed(g, 30);
	if (s->ntap && c == s->tpath[s->ntap - 1]) { /* the last letter again: submit */
		int path[MAXN * MAXN], n = s->ntap;
		memcpy(path, s->tpath, sizeof path);
		clear_taps(g, s);
		judge(g, s, path, n);
		return;
	}
	for (int k = 0; k < s->ntap; k++)
		if (s->tpath[k] == c) { /* back to an earlier letter: drop the ones after it */
			int old[MAXN * MAXN], n = s->ntap;
			memcpy(old, s->tpath, sizeof old);
			s->ntap = k + 1;
			for (int j = k + 1; j < n; j++) redraw_tile(g, s, old[j]);
			say_taps(g, s);
			return;
		}
	if (s->ntap && !adjacent(s, s->tpath[s->ntap - 1], c)) clear_taps(g, s); /* not next to the last: start over */
	s->tpath[s->ntap++] = c;
	redraw_tile(g, s, c);
	say_taps(g, s);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->over) return;
	if (!st->finger) wipe_ink(g, s, st);
	if (st->tap || st->n < 2) { tap(g, s, st->cx, st->cy); return; }
	s->done_armed = 0;
	/* snap the stroke to the cells it passes through (near their centres), in order */
	int path[MAXN * MAXN], np = 0, bad = 0;
	for (int i = 0; i < st->n && !bad; i++) {
		int x0 = st->x[i > 0 ? i - 1 : 0], y0 = st->y[i > 0 ? i - 1 : 0], x1 = st->x[i], y1 = st->y[i];
		int d = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1), steps = d / 8 + 1;
		for (int k = 1; k <= steps && !bad; k++) {
			int c = cell_at(s, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps, 1);
			if (c < 0 || (np && c == path[np - 1])) continue;
			if (np >= 2 && c == path[np - 2]) { np--; continue; } /* retraced: undo the last letter */
			for (int j = 0; j < np; j++) if (path[j] == c) bad = 1;
			if (bad) break;
			if (np && !adjacent(s, path[np - 1], c)) { bad = 2; break; }
			path[np++] = c;
		}
	}
	if (s->ntap) clear_taps(g, s);
	if (bad == 1) { g_say(g, "each letter can be used only once in a word"); return; }
	if (bad == 2) { g_say(g, "go from letter to neighbouring letter"); return; }
	if (np == 0) { g_say(g, "draw through the letters of a word"); return; }
	if (np == 1) { int x, y; cell_xy(s, path[0], &x, &y); tap(g, s, x + s->cell / 2, y + s->cell / 2); return; }
	judge(g, s, path, np);
}

const GameDef GAME_wordgrid = { "wordgrid", "Word Grid", "Find words in a grid of letters - draw through them", sizeof(S), 1, 1, start, stroke, 0 };

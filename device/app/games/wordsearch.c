/* Word Search: find the hidden words by drawing a line across each one with the pen. Easy 15x15 / 12 words,
 * Medium 20x20 / 20 words, Hard 25x25 / 28 words, with longer words on the harder levels (Medium at least 6 and
 * Hard at least 14 words of 8-14 letters). Words always read forwards: left to right, top to bottom, and on Medium
 * and Hard also diagonally down-right or up-right - never backwards. The filler letters never spell a listed word
 * a second time (in any direction, backwards included), and words in the same direction never overlap.
 * The player's line is wiped after each stroke; a found word gets a neat outline and is crossed off the list.
 * Themes (ws_themes.h, 130+) come from a shuffle bag kept on the Kindle: none comes back until every theme has had
 * its turn, across app restarts. An unfinished puzzle is saved when the player leaves and can be continued. */
#include "../games.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ws_themes.h"

#define M 20
#define MAXN 25
#define MAXW 28
#define MAXL 14
#define POOL 128   /* words of the theme that fit the level, sampled at random if a theme has more */
#define BAGMAX 256 /* themes remembered in the bag (current round) */
#define SAVE_VERSION 1 /* bump when S changes */

/* per level: grid size, words, word lengths, how many words of 8+ letters at least */
static const int N[3] = { 15, 20, 25 }, NW[3] = { 12, 20, 28 }, MINLEN[3] = { 4, 5, 6 }, MAXLEN[3] = { 8, 10, 14 },
	NLONG[3] = { 0, 6, 14 };
/* the word list under the grid: columns, row height, letter size */
static const int LC[3] = { 4, 5, 5 }, LH[3] = { 58, 52, 50 }, LPX[3] = { 40, 36, 34 };

/* the four forward directions (dr, dc): across, down, down-right, up-right; Easy uses the first two */
static const int D4[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { -1, 1 } };
/* all eight, for checking that no word appears a second time (forwards or backwards) */
static const int D8[8][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { -1, 1 }, { 0, -1 }, { -1, 0 }, { -1, -1 }, { 1, -1 } };

/* 16 directions (22.5 degree steps, y down) on a unit circle x1000, for the rounded ends of a found word's outline */
static const short UC[16][2] = { { 1000, 0 }, { 924, 383 }, { 707, 707 }, { 383, 924 }, { 0, 1000 }, { -383, 924 },
	{ -707, 707 }, { -924, 383 }, { -1000, 0 }, { -924, -383 }, { -707, -707 }, { -383, -924 }, { 0, -1000 },
	{ 383, -924 }, { 707, -707 }, { 924, -383 } };

/* The whole game state. It is saved as raw bytes when the player leaves (see games.h), so: no pointers, and the
 * theme by name (an index would point at another theme once themes are added). */
typedef struct {
	int n, nw, nfound, lvl;
	char theme[32];
	char grid[MAXN][MAXN];
	unsigned char used[MAXN][MAXN], dirs[MAXN][MAXN]; /* cell belongs to a word; bit d = a word in direction d */
	char words[MAXW][MAXL + 1];
	int wr[MAXW], wc[MAXW], wdr[MAXW], wdc[MAXW], wlen[MAXW], found[MAXW];
	int x, y, cell, lx, ly, lw, lh, lpx, lcols;
	unsigned char dirty[MAXN][MAXN], ldirty[MAXW];
	char msg[96];
} S;

static int sgn(int v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
static int iabs(int v) { return v < 0 ? -v : v; }

/* Can w go at r,c in direction d? *shared = letters it would share with words already placed. */
static int fits(S *s, const char *w, int len, int r, int c, int d, int *shared) {
	int dr = D4[d][0], dc = D4[d][1], er = r + dr * (len - 1), ec = c + dc * (len - 1);
	if (er < 0 || er >= s->n || ec < 0 || ec >= s->n) return 0;
	*shared = 0;
	for (int i = 0; i < len; i++) {
		int rr = r + dr * i, cc = c + dc * i;
		char ch = s->grid[rr][cc];
		if (ch && ch != w[i]) return 0;
		if (s->dirs[rr][cc] & (1 << d)) return 0; /* would run along another word in the same direction */
		*shared += ch != 0;
	}
	return *shared < len;
}

/* Put word w somewhere random (a random allowed direction first); harder levels like crossing other words. */
static int place(GameCtx *g, S *s, const char *w) {
	int nd = s->lvl == 0 ? 2 : 4, len = (int)strlen(w), order[4] = { 0, 1, 2, 3 };
	for (int i = nd - 1; i > 0; i--) { int j = g_rand(g, i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
	for (int oi = 0; oi < nd; oi++) {
		int d = order[oi], nany = 0, nshare = 0, ar = 0, ac = 0, sr = 0, sc = 0, sh;
		for (int r = 0; r < s->n; r++)
			for (int c = 0; c < s->n; c++) {
				if (!fits(s, w, len, r, c, d, &sh)) continue;
				if (g_rand(g, ++nany) == 0) { ar = r; ac = c; }
				if (sh && g_rand(g, ++nshare) == 0) { sr = r; sc = c; }
			}
		if (!nany) continue;
		if (nshare && s->lvl > 0 && g_rand(g, 2)) { ar = sr; ac = sc; }
		for (int i = 0; i < len; i++) {
			int rr = ar + D4[d][0] * i, cc = ac + D4[d][1] * i;
			s->grid[rr][cc] = w[i];
			s->used[rr][cc] = 1;
			s->dirs[rr][cc] |= (unsigned char)(1 << d);
		}
		int k = s->nw++;
		memcpy(s->words[k], w, (size_t)len + 1);
		s->wr[k] = ar; s->wc[k] = ac; s->wdr[k] = D4[d][0]; s->wdc[k] = D4[d][1]; s->wlen[k] = len;
		return 1;
	}
	return 0;
}

/* Is a listed word spelled anywhere other than where it was put (any of the 8 directions)? */
static int find_extra(S *s, int *ek, int *er, int *ec, int *ed) {
	for (int k = 0; k < s->nw; k++) {
		const char *w = s->words[k];
		int len = s->wlen[k];
		for (int r = 0; r < s->n; r++)
			for (int c = 0; c < s->n; c++) {
				if (s->grid[r][c] != w[0]) continue;
				for (int d = 0; d < 8; d++) {
					int dr = D8[d][0], dc = D8[d][1], i = 1;
					if (r == s->wr[k] && c == s->wc[k] && dr == s->wdr[k] && dc == s->wdc[k]) continue;
					int lr = r + dr * (len - 1), lc = c + dc * (len - 1);
					if (lr < 0 || lr >= s->n || lc < 0 || lc >= s->n) continue;
					while (i < len && s->grid[r + dr * i][c + dc * i] == w[i]) i++;
					if (i == len) { *ek = k; *er = r; *ec = c; *ed = d; return 1; }
				}
			}
	}
	return 0;
}

static char filler(GameCtx *g, const char *bag, int nb, int lvl) {
	/* Easy: any letter; harder: mostly letters from the words themselves, so stray letters don't give words away */
	if (lvl == 0 || nb == 0 || g_rand(g, 10) < 3) return (char)('A' + g_rand(g, 26));
	return bag[g_rand(g, nb)];
}

static int fill(GameCtx *g, S *s) {
	char bag[MAXW * MAXL];
	int nb = 0;
	for (int k = 0; k < s->nw; k++) for (int i = 0; i < s->wlen[k]; i++) bag[nb++] = s->words[k][i];
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++)
			if (!s->used[r][c]) s->grid[r][c] = filler(g, bag, nb, s->lvl);
	for (int it = 0; it < 1000; it++) {
		int k, r, c, d, cand[MAXL], nc = 0;
		if (!find_extra(s, &k, &r, &c, &d)) return 1;
		for (int i = 0; i < s->wlen[k]; i++) {
			int rr = r + D8[d][0] * i, cc = c + D8[d][1] * i;
			if (!s->used[rr][cc]) cand[nc++] = rr * MAXN + cc;
		}
		if (!nc) return 0; /* formed by the placed words themselves: start again */
		int p = cand[g_rand(g, nc)], rr = p / MAXN, cc = p % MAXN;
		char old = s->grid[rr][cc];
		while (s->grid[rr][cc] == old) s->grid[rr][cc] = filler(g, bag, nb, s->lvl);
	}
	return 0;
}

static int build(GameCtx *g, S *s, const Theme *th) {
	char pool[POOL][MAXL + 1];
	int np = 0, seen = 0, lvl = s->lvl, want = NW[lvl];
	const char *p = th->words;
	while (*p) { /* every word that fits the level has the same chance (reservoir sampling once the pool is full) */
		const char *e = strchr(p, ' ');
		int len = e ? (int)(e - p) : (int)strlen(p), pal = 1;
		for (int i = 0; i < len / 2; i++) pal &= p[i] == p[len - 1 - i];
		if (len >= MINLEN[lvl] && len <= MAXLEN[lvl] && len <= s->n && len <= MAXL && !pal) {
			int k = seen < POOL ? seen : g_rand(g, seen + 1);
			seen++;
			if (k < POOL) {
				memcpy(pool[k], p, (size_t)len);
				pool[k][len] = 0;
				if (k == np) np++;
			}
		}
		if (!e) break;
		p = e + 1;
	}
	for (int i = np - 1; i > 0; i--) {
		int j = g_rand(g, i + 1);
		char t[MAXL + 1];
		memcpy(t, pool[i], sizeof t); memcpy(pool[i], pool[j], sizeof t); memcpy(pool[j], t, sizeof t);
	}
	int pick[MAXW], npk = 0, nlong = 0;
	unsigned char taken[POOL] = { 0 };
	for (int pass = 0; pass < 2; pass++)
		for (int k = 0; k < np && npk < want; k++) {
			int len = (int)strlen(pool[k]), clash = 0;
			if (taken[k] || (pass == 0 && (len < 8 || nlong >= NLONG[lvl]))) continue;
			for (int j = 0; j < npk; j++) if (strstr(pool[pick[j]], pool[k]) || strstr(pool[k], pool[pick[j]])) clash = 1;
			if (clash) continue;
			taken[k] = 1;
			pick[npk++] = k;
			nlong += len >= 8;
		}
	if (npk < want || nlong < NLONG[lvl]) return 0;
	for (int i = 1; i < npk; i++) /* longest first */
		for (int j = i; j > 0 && strlen(pool[pick[j]]) > strlen(pool[pick[j - 1]]); j--) { int t = pick[j]; pick[j] = pick[j - 1]; pick[j - 1] = t; }
	memset(s->grid, 0, sizeof s->grid);
	memset(s->used, 0, sizeof s->used);
	memset(s->dirs, 0, sizeof s->dirs);
	s->nw = 0;
	for (int k = 0; k < npk; k++) if (!place(g, s, pool[pick[k]])) return 0;
	if (!fill(g, s)) return 0;
	/* list the words alphabetically */
	for (int i = 1; i < s->nw; i++)
		for (int j = i; j > 0 && strcmp(s->words[j], s->words[j - 1]) < 0; j--) {
			char t[MAXL + 1];
			int tr = s->wr[j], tc = s->wc[j], tdr = s->wdr[j], tdc = s->wdc[j], tl = s->wlen[j];
			memcpy(t, s->words[j], sizeof t); memcpy(s->words[j], s->words[j - 1], sizeof t); memcpy(s->words[j - 1], t, sizeof t);
			s->wr[j] = s->wr[j - 1]; s->wc[j] = s->wc[j - 1]; s->wdr[j] = s->wdr[j - 1]; s->wdc[j] = s->wdc[j - 1]; s->wlen[j] = s->wlen[j - 1];
			s->wr[j - 1] = tr; s->wc[j - 1] = tc; s->wdr[j - 1] = tdr; s->wdc[j - 1] = tdc; s->wlen[j - 1] = tl;
		}
	return 1;
}

static void cell_xy(S *s, int r, int c, int *x, int *y) { *x = s->x + c * s->cell; *y = s->y + r * s->cell; }

static void print_cell(GameCtx *g, S *s, int r, int c) {
	int x, y;
	char t[2] = { s->grid[r][c], 0 };
	cell_xy(s, r, c, &x, &y);
	g_print(g, x, y, s->cell, s->cell, s->cell * (s->cell < 80 ? 58 : 55) / 100, 0, t);
}

static void list_box(S *s, int k, int *x, int *y) { *x = s->lx + (k % s->lcols) * s->lw; *y = s->ly + (k / s->lcols) * s->lh; }

static void draw_list_entry(GameCtx *g, S *s, int k) {
	int x, y;
	list_box(s, k, &x, &y);
	g_print(g, x, y, s->lw, s->lh, s->lpx, 0, s->words[k]);
	if (s->found[k]) {
		int tw = s->wlen[k] * s->lpx * 70 / 100 + 24;
		if (tw > s->lw - 10) tw = s->lw - 10;
		g_line(g, x + (s->lw - tw) / 2, y + s->lh / 2, x + (s->lw + tw) / 2, y + s->lh / 2, 5);
	}
}

static void capsule_bounds(S *s, int k, int *x0, int *y0, int *x1, int *y1) {
	int ax, ay, bx, by, rr = s->cell * 42 / 100 + 6;
	cell_xy(s, s->wr[k], s->wc[k], &ax, &ay);
	cell_xy(s, s->wr[k] + s->wdr[k] * (s->wlen[k] - 1), s->wc[k] + s->wdc[k] * (s->wlen[k] - 1), &bx, &by);
	*x0 = (ax < bx ? ax : bx) + s->cell / 2 - rr;
	*y0 = (ay < by ? ay : by) + s->cell / 2 - rr;
	*x1 = (ax > bx ? ax : bx) + s->cell / 2 + rr;
	*y1 = (ay > by ? ay : by) + s->cell / 2 + rr;
}

/* A rounded outline around a found word. */
static void draw_capsule(GameCtx *g, S *s, int k) {
	static const int DIR16[3][3] = { { 10, 12, 14 }, { 8, -1, 0 }, { 6, 4, 2 } }; /* [dr+1][dc+1] */
	int d = DIR16[s->wdr[k] + 1][s->wdc[k] + 1], rr = s->cell * 42 / 100;
	int ax, ay, bx, by, xy[38], n = 0;
	cell_xy(s, s->wr[k], s->wc[k], &ax, &ay);
	cell_xy(s, s->wr[k] + s->wdr[k] * (s->wlen[k] - 1), s->wc[k] + s->wdc[k] * (s->wlen[k] - 1), &bx, &by);
	ax += s->cell / 2; ay += s->cell / 2; bx += s->cell / 2; by += s->cell / 2;
	for (int i = -4; i <= 4; i++) { int a = (d + i + 16) % 16; xy[n++] = bx + UC[a][0] * rr / 1000; xy[n++] = by + UC[a][1] * rr / 1000; }
	for (int i = 4; i <= 12; i++) { int a = (d + i + 16) % 16; xy[n++] = ax + UC[a][0] * rr / 1000; xy[n++] = ay + UC[a][1] * rr / 1000; }
	xy[n++] = xy[0]; xy[n++] = xy[1];
	g_poly(g, xy, n / 2, 4);
}

/* ---- the theme bag: every theme once, in random order, before any comes back (kept across restarts) ---- */
typedef struct { uint32_t magic, n, used[BAGMAX]; } Bag; /* used: FNV-1a hashes of the names used this round */
#define BAGMAGIC 0x57534231u /* "WSB1" */
typedef char bag_fits[NTHEMES < BAGMAX ? 1 : -1];

static uint32_t name_hash(const char *t) {
	uint32_t h = 2166136261u;
	while (*t) { h ^= (unsigned char)*t++; h *= 16777619u; }
	return h;
}

static int in_bag(const Bag *b, uint32_t h) {
	for (uint32_t i = 0; i < b->n; i++) if (b->used[i] == h) return 1;
	return 0;
}

static const Theme *next_theme(GameCtx *g) {
	Bag b;
	int got = g_load(g, "bag", &b, sizeof b), fresh[NTHEMES], nf = 0;
	if (got < (int)(2 * sizeof(uint32_t)) || b.magic != BAGMAGIC || b.n > BAGMAX || got < (int)((2 + b.n) * sizeof(uint32_t)))
		b.n = 0;
	for (int i = 0; i < NTHEMES; i++) if (!in_bag(&b, name_hash(THEMES[i].name))) fresh[nf++] = i;
	if (!nf) { /* round over: start the next one, but not with the theme just played */
		uint32_t last = b.n ? b.used[b.n - 1] : 0;
		b.n = 0;
		for (int i = 0; i < NTHEMES; i++) if (NTHEMES == 1 || name_hash(THEMES[i].name) != last) fresh[nf++] = i;
	}
	const Theme *t = &THEMES[fresh[g_rand(g, nf)]];
	if (b.n >= BAGMAX) b.n = 0; /* only with lots of stale names of removed themes */
	b.magic = BAGMAGIC;
	b.used[b.n++] = name_hash(t->name);
	g_save(g, "bag", &b, (2 + b.n) * sizeof(uint32_t));
	return t;
}

/* Board and list positions for the current play area (recomputed on resume: the saved ones may be stale). */
static void layout(GameCtx *g, S *s) {
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->lcols = LC[s->lvl];
	s->lh = LH[s->lvl];
	s->lpx = LPX[s->lvl];
	int rows = (s->nw + s->lcols - 1) / s->lcols, gap = 30;
	int listh = rows * s->lh, room = ah - 2 * M - gap - listh;
	s->cell = (aw - 2 * M) / s->n < room / s->n ? (aw - 2 * M) / s->n : room / s->n;
	if (s->cell > 150) s->cell = 150;
	int size = s->n * s->cell, total = size + gap + listh;
	s->x = ax + (aw - size) / 2;
	s->y = ay + (ah - total) / 2;
	s->lw = (aw - 2 * M) / s->lcols;
	s->lx = ax + M;
	s->ly = s->y + size + gap;
}

/* The letters, the line under the grid and the word list (found words crossed off). */
static void draw_board(GameCtx *g, S *s) {
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	g_speed(g, 40);
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++) print_cell(g, s, r, c);
	g_line(g, ax + M, s->ly - 15, ax + aw - M, s->ly - 15, 2);
	for (int k = 0; k < s->nw; k++) draw_list_entry(g, s, k);
}

static void say_rules(GameCtx *g, S *s) {
	g_say(g, s->lvl == 0 ? "words go across or down - draw a line across each one" : "words go across, down or diagonally - never backwards");
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	s->lvl = g_level(g);
	s->n = N[s->lvl];
	const Theme *th = 0;
	int ok = 0;
	for (int tries = 0; tries < 400 && !ok; tries++) { /* nearly always the first try works */
		if (tries % 50 == 0) th = next_theme(g); /* a theme that keeps failing (it shouldn't) gives way to the next */
		ok = build(g, s, th);
	}
	snprintf(s->theme, sizeof s->theme, "%s", th->name);
	layout(g, s);
	draw_board(g, s);
	snprintf(s->msg, sizeof s->msg, "Find the %d %s!", s->nw, s->theme);
	g_remark(g, s->msg);
	say_rules(g, s);
}

/* Is a restored state sane? (It comes from our own file, but a bad one must never index out of bounds.) */
static int state_ok(GameCtx *g, S *s) {
	if (s->lvl != g_level(g) || s->lvl < 0 || s->lvl > 2 || s->n != N[s->lvl] || s->nw != NW[s->lvl]) return 0;
	if (!memchr(s->theme, 0, sizeof s->theme) || !memchr(s->msg, 0, sizeof s->msg)) return 0;
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++) if (s->grid[r][c] < 'A' || s->grid[r][c] > 'Z') return 0;
	int nf = 0;
	for (int k = 0; k < s->nw; k++) {
		int len = s->wlen[k], er = s->wr[k] + s->wdr[k] * (len - 1), ec = s->wc[k] + s->wdc[k] * (len - 1);
		if (len < 1 || len > MAXL || s->words[k][len] || (int)strlen(s->words[k]) != len) return 0;
		if (s->wdr[k] < -1 || s->wdr[k] > 1 || s->wdc[k] < 0 || s->wdc[k] > 1 || (!s->wdr[k] && !s->wdc[k])) return 0;
		if (s->wr[k] < 0 || s->wr[k] >= s->n || s->wc[k] < 0 || s->wc[k] >= s->n || er < 0 || er >= s->n || ec < 0 || ec >= s->n) return 0;
		for (int i = 0; i < len; i++) if (s->grid[s->wr[k] + s->wdr[k] * i][s->wc[k] + s->wdc[k] * i] != s->words[k][i]) return 0;
		if (s->found[k] != 0 && s->found[k] != 1) return 0;
		nf += s->found[k];
	}
	return nf == s->nfound && nf < s->nw;
}

/* Continue a saved puzzle: the same board, with the words found so far outlined and crossed off. */
static void resume(GameCtx *g) {
	S *s = g_state(g);
	if (!state_ok(g, s)) { /* (can't happen with the engine's checks; a fresh puzzle rather than garbage) */
		memset(s, 0, sizeof *s);
		start(g);
		return;
	}
	layout(g, s);
	draw_board(g, s);
	for (int k = 0; k < s->nw; k++) if (s->found[k]) draw_capsule(g, s, k);
	snprintf(s->msg, sizeof s->msg, "%d %s - %d to go!", s->nw, s->theme, s->nw - s->nfound);
	g_remark(g, s->msg);
	say_rules(g, s);
}

static void progress(GameCtx *g, char *buf, int n) {
	S *s = g_state(g);
	snprintf(buf, (size_t)n, "%d of %d found", s->nfound, s->nw);
}

/* Remember which cells / list entries the player's ink touched so they can be redrawn clean. */
static void mark_dirty(S *s, int px, int py) {
	int size = s->n * s->cell;
	for (int oy = -18; oy <= 18; oy += 18)
		for (int ox = -18; ox <= 18; ox += 18) {
			int x = px + ox, y = py + oy;
			if (x >= s->x && x < s->x + size && y >= s->y && y < s->y + size) s->dirty[(y - s->y) / s->cell][(x - s->x) / s->cell] = 1;
			if (x >= s->lx && x < s->lx + s->lcols * s->lw && y >= s->ly) {
				int k = (y - s->ly) / s->lh * s->lcols + (x - s->lx) / s->lw;
				if (k >= 0 && k < s->nw) s->ldirty[k] = 1;
			}
		}
}

static void wipe_ink(GameCtx *g, S *s, const GStroke *st, int skip) {
	int ax, ay, aw, ah, size = s->n * s->cell, lastx = -1000, lasty = -1000;
	g_area(g, &ax, &ay, &aw, &ah);
	memset(s->dirty, 0, sizeof s->dirty);
	memset(s->ldirty, 0, sizeof s->ldirty);
	g_speed(g, 50);
	for (int i = 0; i < st->n; i++) {
		int px = st->x[i], py = st->y[i];
		mark_dirty(s, px, py);
		int in_grid = px >= s->x && px < s->x + size && py >= s->y && py < s->y + size;
		if (!in_grid && (i == 0 || i == st->n - 1 || px - lastx > 14 || lastx - px > 14 || py - lasty > 14 || lasty - py > 14)) {
			int x0 = px - 16, y0 = py - 16, x1 = px + 16, y1 = py + 16;
			if (x0 < ax) x0 = ax;
			if (y0 < ay) y0 = ay;
			if (x1 > ax + aw) x1 = ax + aw;
			if (y1 > ay + ah) y1 = ay + ah;
			if (x1 > x0 && y1 > y0) g_clear(g, x0, y0, x1 - x0, y1 - y0);
			lastx = px; lasty = py;
		}
	}
	for (int r = 0; r < s->n; r++)
		for (int c = 0; c < s->n; c++) {
			if (!s->dirty[r][c]) continue;
			int x, y;
			cell_xy(s, r, c, &x, &y);
			g_clear(g, x, y, s->cell, s->cell);
			print_cell(g, s, r, c);
		}
	for (int k = 0; k < s->nw; k++) {
		if (!s->found[k] || k == skip) continue;
		int x0, y0, x1, y1, hit = 0;
		capsule_bounds(s, k, &x0, &y0, &x1, &y1);
		for (int r = 0; r < s->n && !hit; r++)
			for (int c = 0; c < s->n && !hit; c++) {
				int x, y;
				cell_xy(s, r, c, &x, &y);
				hit = s->dirty[r][c] && x < x1 && x + s->cell > x0 && y < y1 && y + s->cell > y0;
			}
		if (hit) draw_capsule(g, s, k);
	}
	for (int k = 0; k < s->nw; k++) {
		if (!s->ldirty[k] || k == skip) continue;
		int x, y;
		list_box(s, k, &x, &y);
		g_clear(g, x, y, s->lw, s->lh);
		draw_list_entry(g, s, k);
	}
}

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* Which word lies under the line from the stroke's first to last point, snapped to a row, column or diagonal?
 * Either end may be drawn first, an end may stray up to a letter past the grid, and the ends may be off by one
 * letter. Returns the word, -1 (not a line along the grid), -2 (no word there) or -3 - k (word k, already found). */
static int word_under(S *s, int x0, int y0, int x1, int y1) {
	int size = s->n * s->cell, h = s->cell;
	if (x0 < s->x - h || x0 >= s->x + size + h || y0 < s->y - h || y0 >= s->y + size + h) return -1;
	if (x1 < s->x - h || x1 >= s->x + size + h || y1 < s->y - h || y1 >= s->y + size + h) return -1;
	int r0 = floordiv(y0 - s->y, s->cell), c0 = floordiv(x0 - s->x, s->cell), r1 = floordiv(y1 - s->y, s->cell), c1 = floordiv(x1 - s->x, s->cell), dr, dc, len;
	int dx = x1 - x0, dy = y1 - y0, adx = iabs(dx), ady = iabs(dy);
	if (ady * 5 < adx * 2) { dr = 0; dc = sgn(dx); len = iabs(c1 - c0); }
	else if (adx * 5 < ady * 2) { dc = 0; dr = sgn(dy); len = iabs(r1 - r0); }
	else { dr = sgn(dy); dc = sgn(dx); len = (iabs(r1 - r0) + iabs(c1 - c0) + 1) / 2; }
	if (len < 1 || (dr == 0 && dc == 0)) return -1;
	if (dc < 0 || (dc == 0 && dr < 0)) { /* drawn from the last letter back to the first */
		r0 += dr * len; c0 += dc * len;
		dr = -dr; dc = -dc;
	}
	int best = -2, bestd = 99;
	for (int k = 0; k < s->nw; k++) {
		if (s->wdr[k] != dr || s->wdc[k] != dc) continue;
		int t = dc ? s->wc[k] - c0 : s->wr[k] - r0; /* the word's start, in steps along the line */
		if (s->wr[k] != r0 + dr * t || s->wc[k] != c0 + dc * t || iabs(t) > 1) continue;
		int e = t + s->wlen[k] - 1 - len; /* its end against the drawn end */
		if (iabs(e) > 1 || iabs(t) + iabs(e) >= bestd) continue;
		bestd = iabs(t) + iabs(e);
		best = s->found[k] ? -3 - k : k;
	}
	return best;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	/* drawing is queued in order, so a line drawn while the last one animates is still handled */
	int k = st->tap || st->n < 2 ? -1 : word_under(s, st->x[0], st->y[0], st->x[st->n - 1], st->y[st->n - 1]);
	if (k >= 0) s->found[k] = 1;
	if (!st->finger) wipe_ink(g, s, st, k);
	if (k < 0) {
		if (k <= -3) g_say(g, "you already found %s", s->words[-3 - k]);
		else g_say(g, st->tap ? "draw a line across a word, first letter to last" : k == -2 ? "that's not one of the words" : "draw along a row, a column or a diagonal");
		return;
	}
	s->nfound++;
	g_speed(g, 4);
	draw_capsule(g, s, k);
	int x, y;
	list_box(s, k, &x, &y);
	g_clear(g, x, y, s->lw, s->lh);
	draw_list_entry(g, s, k);
	if (s->nfound == s->nw) {
		snprintf(s->msg, sizeof s->msg, "You found all %d %s!", s->nw, s->theme);
		g_over(g, s->msg);
		return;
	}
	g_say(g, "found %s - %d to go", s->words[k], s->nw - s->nfound);
}

const GameDef GAME_wordsearch = {
	.id = "wordsearch", .name = "Word Search", .desc = "Find the hidden words - draw a line across each",
	.state_size = sizeof(S), .ink = 1, .levels = 1, .start = start, .stroke = stroke,
	.resume = resume, .progress = progress, .save_version = SAVE_VERSION,
};

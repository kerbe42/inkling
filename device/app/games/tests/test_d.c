/* Host tests for Word Grid, Word Ladder and Criss-Cross (agent D).
 * Plays each game many times on every level through the stub engine (stub_d.c), reading the board back from the
 * logged drawing calls (the screen model), and checks the rules with independent solvers.
 * Build: gcc -O1 -g -fsanitize=address,undefined -I device/app -I device/app/games device/app/games/tests/stub_d.c \
 *        device/app/games/tests/test_d.c device/app/games/{dict_words,wordgrid,wordladder,crisscross}.c -lm
 * Run: ./a.out [quick|full] [game] */
#include "games.h"
#include "dict_words.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- stub interface (stub_d.c) --- */
enum { C_LINE, C_POLY, C_CIRCLE, C_CROSS, C_FRAME, C_DOT, C_BOX, C_CLEAR, C_WRITE, C_PRINT, C_PAUSE, C_SPEED };
typedef struct { int type; int a[8]; char text[48]; } Call;
int stub_ncalls(void);
const Call *stub_call(int i);
int stub_ntexts(void);
const Call *stub_text(int i);
long stub_violations(GameCtx *g);
const char *stub_first_violation(GameCtx *g);
const char *stub_result(GameCtx *g);
const char *stub_say(GameCtx *g);
int stub_over(GameCtx *g);
void stub_set_busy(GameCtx *g, int b);
GameCtx *stub_new(const GameDef *def, int level, unsigned long long seed, int ax, int ay, int aw, int ah);
void stub_free(GameCtx *g);
void stub_start(GameCtx *g);
void stub_stroke(GameCtx *g, const short *xs, const short *ys, int n, int tap, int finger);
void stub_tap(GameCtx *g, int x, int y, int finger);

extern const GameDef GAME_wordgrid, GAME_wordladder, GAME_crisscross;

static int AX = 40, AY = 200, AW = 1780, AH = 2040;
static unsigned long long trng = 88172645463325252ULL;
static int rnd(int n) { trng ^= trng << 13; trng ^= trng >> 7; trng ^= trng << 17; return (int)((trng >> 11) % (unsigned long long)n); }
static unsigned long long seed_ctr = 5000;
static const char *LV[3] = { "easy", "medium", "hard" };

static int failures;
#define FAIL(...) do { failures++; if (failures <= 40) { printf("  FAIL [%s] ", __func__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int draw_calls(void) { int n = 0; for (int i = 0; i < stub_ncalls(); i++) n += stub_call(i)->type != C_SPEED; return n; }

static void finish(GameCtx *g, const char *name) {
	if (stub_violations(g)) FAIL("%s: %ld engine-rule violations, first: %s", name, stub_violations(g), stub_first_violation(g));
	stub_free(g);
}

static GameCtx *new_game(const GameDef *d, int level, unsigned long long *seed) {
	*seed = seed_ctr++;
	GameCtx *g = stub_new(d, level, *seed, AX, AY, AW, AH);
	stub_start(g);
	return g;
}

/* the visible text exactly equal to t (or starting with t when prefix=1); returns its box centre */
static const Call *find_text(const char *t, int prefix) {
	for (int i = stub_ntexts() - 1; i >= 0; i--) {
		const Call *c = stub_text(i);
		if (prefix ? !strncmp(c->text, t, strlen(t)) : !strcmp(c->text, t)) return c;
	}
	return 0;
}
static int tap_text(GameCtx *g, const char *t, int finger) {
	const Call *c = find_text(t, 0);
	if (!c) return 0;
	stub_tap(g, c->a[0] + c->a[2] / 2, c->a[1] + c->a[3] / 2, finger);
	return 1;
}

/* a pen stroke through the given points, densified to ~step px, with a little wobble */
static void pen_path(GameCtx *g, const int *px, const int *py, int n, int wobble) {
	static short xs[4000], ys[4000];
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0) { xs[k] = (short)px[0]; ys[k] = (short)py[0]; k++; continue; }
		int dx = px[i] - px[i - 1], dy = py[i] - py[i - 1];
		int d = abs(dx) + abs(dy), steps = d / 25 + 1;
		for (int s = 1; s <= steps && k < 4000; s++) {
			int w = wobble ? rnd(2 * wobble + 1) - wobble : 0;
			xs[k] = (short)(px[i - 1] + dx * s / steps + w);
			ys[k] = (short)(py[i - 1] + dy * s / steps - w);
			k++;
		}
	}
	stub_stroke(g, xs, ys, k, 0, 0);
}

/* ======================= Word Grid ======================= */
#define WG_MAX 6
static int wg_n, wg_cx[36], wg_cy[36], wg_cell;
static char wg_grid[36];

static int wg_read(void) {
	int idx[64], k = 0;
	for (int i = 0; i < stub_ntexts() && k < 64; i++) {
		const Call *c = stub_text(i);
		int one = strlen(c->text) == 1 && c->text[0] >= 'A' && c->text[0] <= 'Z';
		if ((one || !strcmp(c->text, "Qu")) && c->a[5] && c->a[2] > 80) idx[k++] = i;
	}
	if (k != 16 && k != 25 && k != 36) return 0;
	wg_n = k == 16 ? 4 : k == 25 ? 5 : 6;
	/* sort by y then x */
	for (int a = 0; a < k; a++)
		for (int b = a + 1; b < k; b++) {
			const Call *p = stub_text(idx[a]), *q = stub_text(idx[b]);
			if (q->a[1] < p->a[1] - 10 || (abs(q->a[1] - p->a[1]) <= 10 && q->a[0] < p->a[0])) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
		}
	for (int i = 0; i < k; i++) {
		const Call *c = stub_text(idx[i]);
		wg_cx[i] = c->a[0] + c->a[2] / 2;
		wg_cy[i] = c->a[1] + c->a[3] / 2;
		wg_grid[i] = (char)(c->text[0] - 'A' + 'a');
	}
	wg_cell = wg_cx[1] - wg_cx[0];
	return k;
}

static int wg_adj(int a, int b) { int dr = a / wg_n - b / wg_n, dc = a % wg_n - b % wg_n; return a != b && abs(dr) <= 1 && abs(dc) <= 1; }

/* independent tracer: finds a cell path for the word (q = "qu"), returns its length or 0 */
static int wg_dfs(const char *w, int pos, int len, int *path, int np, unsigned long long used) {
	if (pos == len) return np;
	for (int c = 0; c < wg_n * wg_n; c++) {
		if (used >> c & 1) continue;
		if (np && !wg_adj(path[np - 1], c)) continue;
		if (wg_grid[c] != w[pos]) continue;
		int adv = 1;
		if (w[pos] == 'q') { if (pos + 1 >= len || w[pos + 1] != 'u') return 0; adv = 2; }
		path[np] = c;
		int r = wg_dfs(w, pos + adv, len, path, np + 1, used | 1ULL << c);
		if (r) return r;
	}
	return 0;
}

typedef struct { char w[9]; int len, path[9], np; } WGWord;
static WGWord wg_words[1500];
static int wg_nwords;

static void wg_solve(int minlen) {
	wg_nwords = 0;
	for (int len = minlen; len <= DICT_MAXLEN; len++)
		for (int i = 0; i < dict_count(len); i++) {
			WGWord *x = &wg_words[wg_nwords];
			dict_word(len, i, x->w);
			x->np = wg_dfs(x->w, 0, len, x->path, 0, 0);
			if (x->np) { x->len = len; if (wg_nwords < 1499) wg_nwords++; }
		}
}

static int wg_score(int *found) {
	const Call *c = find_text("Score ", 1);
	int sc = -1, f = -1, t = -1;
	if (c) sscanf(c->text, "Score %d %d of %d", &sc, &f, &t);
	if (found) *found = f;
	return sc;
}

static void wg_draw(GameCtx *g, const int *path, int np, int wobble) {
	int px[40], py[40];
	for (int i = 0; i < np; i++) { px[i] = wg_cx[path[i]]; py[i] = wg_cy[path[i]]; }
	pen_path(g, px, py, np, wobble);
}

static int wg_points(int len) { return len <= 4 ? 1 : len == 5 ? 2 : len == 6 ? 3 : len == 7 ? 5 : 11; }

static int in_words(const char *w) { for (int i = 0; i < wg_nwords; i++) if (!strcmp(wg_words[i].w, w)) return 1; return 0; }

static void test_wordgrid(int level, int games) {
	int minwords = 1 << 30, maxwords = 0, sumwords = 0, accepted = 0, rejected = 0, minlen = level == 2 ? 4 : 3;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_wordgrid, level, &seed);
		int k = wg_read();
		if (k != (4 + level) * (4 + level)) { FAIL("%s seed %llu: read %d tiles", LV[level], seed, k); finish(g, "wordgrid"); continue; }
		wg_solve(minlen);
		if (getenv("WGDEBUG")) { for (int i = 0; i < wg_n * wg_n; i++) printf("%c%s", wg_grid[i], i % wg_n == wg_n - 1 ? "\n" : ""); for (int i = 0; i < wg_nwords; i++) printf("%s ", wg_words[i].w); printf("\n"); }
		if (wg_nwords < minwords) minwords = wg_nwords;
		if (wg_nwords > maxwords) maxwords = wg_nwords;
		sumwords += wg_nwords;
		int floor = level == 0 ? 20 : level == 1 ? 30 : 40;
		if (wg_nwords < floor) FAIL("%s seed %llu: only %d words in the grid", LV[level], seed, wg_nwords);
		int found = 0, score = 0, f;
		if (wg_score(&f) != 0 || f != 0) FAIL("%s: score box not at 0", LV[level]);

		/* invalid: a jump between non-neighbours (centre of (0,0) to centre of (1,2)) */
		{ int p[2] = { 0, wg_n + 2 }; wg_draw(g, p, 2, 0); }
		if (!strstr(stub_say(g), "neighbouring")) FAIL("%s: jump accepted? say '%s'", LV[level], stub_say(g));
		/* invalid: a cell used twice (0,1,n+1,n,0) */
		{ int p[5] = { 0, 1, wg_n + 1, wg_n, 0 }; wg_draw(g, p, 5, 0); }
		if (!strstr(stub_say(g), "once")) FAIL("%s: reused cell accepted? say '%s'", LV[level], stub_say(g));
		/* invalid: a non-word (a row of letters that isn't a word) */
		for (int r = 0; r < wg_n; r++) {
			char w[16]; int p[8], len = 0, np = 0;
			for (int c = 0; c < minlen; c++) {
				int i = r * wg_n + c;
				p[np++] = i;
				w[len++] = wg_grid[i];
				if (wg_grid[i] == 'q') w[len++] = 'u';
			}
			w[len] = 0;
			if (len > DICT_MAXLEN || dict_find(w, len) >= 0) continue;
			wg_draw(g, p, np, 3);
			if (!strstr(stub_say(g), "isn't in my word list")) FAIL("%s: non-word %s: say '%s'", LV[level], w, stub_say(g));
			rejected++;
			break;
		}
		/* too short on Hard */
		if (level == 2 && wg_grid[0] != 'q' && wg_grid[1] != 'q' && wg_grid[2] != 'q') {
			int p[3] = { 0, 1, 2 };
			wg_draw(g, p, 3, 0);
			if (!strstr(stub_say(g), "too short")) FAIL("hard: 3 letters: say '%s'", stub_say(g));
		}
		if (wg_score(&f) != 0 || f != 0) FAIL("%s seed %llu: invalid strokes changed the score", LV[level], seed);

		/* play some real words with the pen (and one by finger taps) */
		int want = gi == 0 ? wg_nwords : 6 + rnd(10), hits[1500] = { 0 };
		for (int t = 0; t < want && t < wg_nwords; t++) {
			int wi = gi == 0 ? t : rnd(wg_nwords);
			if (hits[wi]) continue;
			hits[wi] = 1;
			WGWord *x = &wg_words[wi];
			if (t == 2) { /* finger taps, then Enter */
				for (int i = 0; i < x->np; i++) stub_tap(g, wg_cx[x->path[i]], wg_cy[x->path[i]], 1);
				tap_text(g, "Enter", 1);
			} else if (t == 3 && x->np >= 3) { /* a wiggle: go to the 3rd cell, back to the 2nd, then on */
				int p[12], m = 0;
				p[m++] = x->path[0]; p[m++] = x->path[1]; p[m++] = x->path[2]; p[m++] = x->path[1];
				for (int i = 2; i < x->np; i++) p[m++] = x->path[i];
				wg_draw(g, p, m, 2);
			} else wg_draw(g, x->path, x->np, 6);
			int sc = wg_score(&f);
			if (stub_over(g)) break;
			found++; score += wg_points(x->len);
			if (sc != score || f != found) { FAIL("%s seed %llu: word %s not counted (score %d/%d found %d/%d, say '%s')", LV[level], seed, x->w, sc, score, f, found, stub_say(g)); break; }
			accepted++;
			if (t == 0) { /* the same word again */
				wg_draw(g, x->path, x->np, 4);
				if (!strstr(stub_say(g), "already")) FAIL("%s: duplicate %s: say '%s'", LV[level], x->w, stub_say(g));
			}
		}
		/* the ink cleanup must leave every tile letter on screen */
		int tiles = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *c = stub_text(i);
			if (((strlen(c->text) == 1 && c->text[0] >= 'A' && c->text[0] <= 'Z') || !strcmp(c->text, "Qu")) && c->a[5] && c->a[2] > 80) tiles++;
		}
		if (tiles != wg_n * wg_n && !stub_over(g)) FAIL("%s seed %llu: %d tiles visible after play", LV[level], seed, tiles);
		if (!stub_over(g)) {
			tap_text(g, "Done", 0);
			if (stub_over(g)) FAIL("%s: one tap on Done ended the round", LV[level]);
			tap_text(g, "Done", 0);
			if (!stub_over(g)) FAIL("%s: Done twice did not end the round", LV[level]);
		}
		if (gi == 0 && !strstr(stub_result(g), "every word")) FAIL("%s seed %llu: found all %d words, result '%s'", LV[level], seed, wg_nwords, stub_result(g));
		if (gi == 0 && wg_nwords > 100 && !find_text("+", 1)) FAIL("%s seed %llu: no '+N more' entry after %d words", LV[level], seed, wg_nwords);
		char want_tail[64];
		snprintf(want_tail, sizeof want_tail, "There were %d in all.", wg_nwords);
		if (!strstr(stub_result(g), want_tail)) FAIL("%s seed %llu: result '%s', my solver found %d", LV[level], seed, stub_result(g), wg_nwords);
		/* the missed words shown must be real grid words */
		const Call *hdr = find_text("Some you missed:", 0);
		if (hdr)
			for (int i = 0; i < stub_ntexts(); i++) {
				const Call *c = stub_text(i);
				if (c->a[5] || c->a[1] <= hdr->a[1]) continue;
				char w[16]; int n = (int)strlen(c->text);
				for (int j = 0; j <= n; j++) w[j] = (char)(c->text[j] ? c->text[j] - 'A' + 'a' : 0);
				if (!in_words(w)) FAIL("%s: missed word %s isn't in the grid", LV[level], c->text);
			}
		finish(g, "wordgrid");
	}
	printf("  wordgrid %-6s %3d games: words per grid %d..%d (avg %d), %d words accepted, %d non-words rejected\n", LV[level], games,
	       minwords, maxwords, games ? sumwords / games : 0, accepted, rejected);
}

/* ======================= Word Ladder ======================= */
const char *stub_remark(GameCtx *g);
static int wl_L, wl_bx[8], wl_rowy[24], wl_nrowlab, wl_goaly, wl_starty, wl_box;
static int wl_kx[26], wl_ky[26];

static int wl_read(void) {
	const Call *st = find_text("start", 0), *go = find_text("goal", 0);
	if (!st || !go) return 0;
	wl_starty = st->a[1];
	wl_goaly = go->a[1];
	wl_nrowlab = 0;
	wl_rowy[wl_nrowlab++] = wl_starty;
	for (int r = 1; r < 24; r++) {
		char t[16];
		snprintf(t, sizeof t, "%d", r);
		const Call *c = find_text(t, 0);
		if (!c || c->a[1] <= wl_starty || c->a[1] >= wl_goaly) break;
		wl_rowy[wl_nrowlab++] = c->a[1];
	}
	/* start-row letters give the box columns; keys are bold letters below the goal row */
	wl_L = 0;
	for (int k = 0; k < 26; k++) wl_kx[k] = -1;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *c = stub_text(i);
		if (strlen(c->text) != 1 || c->text[0] < 'A' || c->text[0] > 'Z') continue;
		int cx = c->a[0] + c->a[2] / 2, cy = c->a[1] + c->a[3] / 2;
		if (cy > wl_starty && cy < wl_starty + (wl_rowy[1] - wl_starty) && wl_L < 8) { wl_bx[wl_L++] = cx; wl_box = c->a[3]; }
		else if (cy > wl_goaly + (wl_rowy[1] - wl_starty) && c->a[5]) { wl_kx[c->text[0] - 'A'] = cx; wl_ky[c->text[0] - 'A'] = cy; }
	}
	for (int a = 0; a < wl_L; a++) for (int b = a + 1; b < wl_L; b++) if (wl_bx[b] < wl_bx[a]) { int t = wl_bx[a]; wl_bx[a] = wl_bx[b]; wl_bx[b] = t; }
	for (int k = 0; k < 26; k++) if (wl_kx[k] < 0) return 0;
	return wl_L;
}

/* the word shown in a row (top of that row = y); '.' for an empty box */
static void wl_row_word(int y, char *out) {
	int pitch = wl_rowy[1] - wl_rowy[0];
	for (int p = 0; p < wl_L; p++) out[p] = '.';
	out[wl_L] = 0;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *c = stub_text(i);
		if (strlen(c->text) != 1 || c->text[0] < 'A' || c->text[0] > 'Z') continue;
		int cx = c->a[0] + c->a[2] / 2, cy = c->a[1] + c->a[3] / 2;
		if (cy < y || cy >= y + pitch) continue;
		for (int p = 0; p < wl_L; p++) if (abs(cx - wl_bx[p]) < pitch / 3) out[p] = (char)(c->text[0] - 'A' + 'a');
	}
}

static int wl_box_cy(int row) { return wl_rowy[row] + (wl_rowy[1] - wl_rowy[0] - 12) / 2; }
static void wl_tap_box(GameCtx *g, int row, int p) { stub_tap(g, wl_bx[p], wl_box_cy(row), rnd(2)); }
static void wl_key(GameCtx *g, char ch) { stub_tap(g, wl_kx[ch - 'a'], wl_ky[ch - 'a'], rnd(2)); }

/* set the row's word to w by tapping boxes and keys (only the positions that differ from cur) */
static void wl_type(GameCtx *g, int row, const char *cur, const char *w) {
	for (int p = 0; p < wl_L; p++) if (cur[p] != w[p]) {
		wl_tap_box(g, row, p);
		if (getenv("WLDEBUG")) printf("    row %d box %d: %s\n", row, p, stub_say(g));
		wl_key(g, w[p]);
		if (getenv("WLDEBUG")) printf("    key %c: %s\n", w[p], stub_say(g));
	}
}

/* full-dictionary BFS: distances from src (index) */
static unsigned char wl_dist[DICT_N5 + DICT_N4 + DICT_N3];
static int wl_par[DICT_N5 + DICT_N4 + DICT_N3];
static void wl_bfs(int L, int src, int block) {
	static int q[DICT_N5 + DICT_N4 + DICT_N3];
	int qh = 0, qt = 0;
	memset(wl_dist, 255, sizeof wl_dist);
	if (block >= 0) wl_dist[block] = 254; /* never step on this word */
	wl_dist[src] = 0; wl_par[src] = -1; q[qt++] = src;
	while (qh < qt) {
		int u = q[qh++];
		char w[8];
		dict_word(L, u, w);
		for (int p = 0; p < L; p++) {
			char keep = w[p];
			for (char c = 'a'; c <= 'z'; c++) {
				w[p] = c;
				int v = dict_find(w, L);
				if (v >= 0 && wl_dist[v] == 255) { wl_dist[v] = (unsigned char)(wl_dist[u] + 1); wl_par[v] = u; q[qt++] = v; }
			}
			w[p] = keep;
		}
	}
}

static int one_apart(const char *a, const char *b) { int d = 0; for (; *a && *b; a++, b++) d += *a != *b; return d == 1 && !*a && !*b; }

static void test_wordladder(int level, int games) {
	int lo = level == 0 ? 3 : level == 1 ? 4 : 5, hi = level == 0 ? 4 : level == 1 ? 6 : 8, L = 3 + level;
	int dmin = 99, dmax = 0, solved = 0, detours = 0, gaveup = 0, rejected = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_wordladder, level, &seed);
		if (wl_read() != L) { FAIL("%s seed %llu: read %d letters", LV[level], seed, wl_L); finish(g, "wordladder"); continue; }
		char start[8], goal[8], row[8];
		wl_row_word(wl_starty, start);
		wl_row_word(wl_goaly, goal);
		int si = dict_find(start, L), gidx = dict_find(goal, L);
		if (si < 0 || gidx < 0 || !dict_common(L, si) || !dict_common(L, gidx)) { FAIL("%s: start/goal %s/%s not common words", LV[level], start, goal); finish(g, "wordladder"); continue; }
		wl_bfs(L, si, -1);
		int d = wl_dist[gidx];
		if (d < dmin) dmin = d;
		if (d > dmax) dmax = d;
		if (d < lo || d > hi) FAIL("%s seed %llu: %s -> %s shortest %d, outside %d..%d", LV[level], seed, start, goal, d, lo, hi);
		char want[64];
		snprintf(want, sizeof want, "in %d steps", d);
		if (!strstr(stub_remark(g), want)) FAIL("%s: remark '%s' but shortest is %d", LV[level], stub_remark(g), d);
		if (wl_nrowlab - 1 < d) FAIL("%s: only %d rows for a %d-step ladder", LV[level], wl_nrowlab - 1, d);
		/* busy before the board is ready: nothing happens */
		stub_set_busy(g, 1);
		stub_tap(g, wl_kx[0], wl_ky[0], 0);
		if (draw_calls() || !strstr(stub_say(g), "moment")) FAIL("%s: input while drawing the board: say '%s'", LV[level], stub_say(g));
		stub_set_busy(g, 0);
		/* the shortest path, goal back to start */
		char path[16][8];
		int np = d + 1;
		for (int k = d, v = gidx; k >= 0; k--, v = wl_par[v]) dict_word(L, v, path[k]);
		wl_row_word(wl_rowy[1], row);
		if (strcmp(row, start)) FAIL("%s: row 1 starts as '%s', not a copy of %s", LV[level], row, start);

		/* invalid steps on row 1 */
		tap_text(g, "Enter", 0);
		if (!strstr(stub_say(g), "change one letter first")) FAIL("%s: unchanged Enter: say '%s'", LV[level], stub_say(g));
		{ /* two letters changed */
			char two[8]; strcpy(two, start);
			two[0] = two[0] == 'z' ? 'y' : 'z'; two[1] = two[1] == 'x' ? 'w' : 'x';
			wl_type(g, 1, start, two);
			tap_text(g, "Enter", 0);
			if (!strstr(stub_say(g), "only one letter")) FAIL("%s: two-letter change: say '%s'", LV[level], stub_say(g));
			else rejected++;
			tap_text(g, "Erase", 0);
			wl_row_word(wl_rowy[1], row);
			if (strcmp(row, start)) FAIL("%s: Erase left '%s' (start %s)", LV[level], row, start);
		}
		{ /* a non-word, one letter away */
			char nw[8] = "";
			for (int p = 0; p < L && !nw[0]; p++)
				for (char c = 'a'; c <= 'z' && !nw[0]; c++) {
					char t[8]; strcpy(t, start); t[p] = c;
					if (c != start[p] && dict_find(t, L) < 0) strcpy(nw, t);
				}
			wl_type(g, 1, start, nw);
			tap_text(g, "Enter", 0);
			if (!strstr(stub_say(g), "isn't in my word list")) FAIL("%s: non-word %s: say '%s'", LV[level], nw, stub_say(g));
			else rejected++;
			tap_text(g, "Erase", 0);
		}
		/* tapping a letter key with no box selected after a commit; tapping the start row */
		stub_tap(g, wl_bx[0], wl_box_cy(0), 0);
		if (!strstr(stub_say(g), "start word")) FAIL("%s: tap on start row: say '%s'", LV[level], stub_say(g));

		int mode = gi % 4; /* 0,1: shortest path; 2: a detour first; 3: give up after a step */
		char cur[8];
		strcpy(cur, start);
		int r = 1, steps = 0;
		if (mode == 2) { /* a detour: a neighbour of start that is not the next word on the shortest path */
			char det[8] = "", dots[8];
			for (int p = 0; p < L; p++) dots[p] = '.';
			dots[L] = 0;
			for (int p = 0; p < L && !det[0]; p++)
				for (char c = 'a'; c <= 'z' && !det[0]; c++) {
					char t[8]; strcpy(t, start); t[p] = c;
					int v = dict_find(t, L);
					if (c == start[p] || v < 0 || !strcmp(t, path[1])) continue;
					wl_bfs(L, v, si); /* the rest of the way must not pass the start word again */
					if (wl_dist[gidx] < 200 && 1 + wl_dist[gidx] <= wl_nrowlab - 1) strcpy(det, t); /* must fit the rows */
				}
			if (det[0]) {
				wl_type(g, 1, start, det);
				tap_text(g, "Enter", 0);
				wl_row_word(wl_rowy[1], row);
				if (strcmp(row, det)) FAIL("%s: detour %s not committed (row '%s', say '%s')", LV[level], det, row, stub_say(g));
				/* going back to the start word is refused */
				wl_type(g, 2, det, start);
				tap_text(g, "Enter", 0);
				if (!strstr(stub_say(g), "already in your ladder")) FAIL("%s: repeat word: say '%s'", LV[level], stub_say(g));
				tap_text(g, "Erase", 0);
				/* Undo: row 2 empties, row 1 is editable again (still showing the detour) */
				tap_text(g, "Undo", 0);
				wl_row_word(wl_rowy[2], row);
				if (strcmp(row, dots)) FAIL("%s: Undo left row 2 as '%s'", LV[level], row);
				wl_row_word(wl_rowy[1], row);
				if (strcmp(row, det)) FAIL("%s: after Undo row 1 is '%s', want %s", LV[level], row, det);
				tap_text(g, "Undo", 0);
				if (!strstr(stub_say(g), "nothing to undo")) FAIL("%s: Undo at the start: say '%s'", LV[level], stub_say(g));
				/* commit the detour again and finish the shortest way from there */
				tap_text(g, "Enter", 0);
				if (!strstr(stub_say(g), "step 1")) FAIL("%s: re-entering %s: say '%s'", LV[level], det, stub_say(g));
				strcpy(cur, det); r = 2; steps = 1;
				detours++;
				wl_bfs(L, dict_find(det, L), si);
				int dd = wl_dist[gidx];
				for (int k = dd, v = gidx; k >= 0; k--, v = wl_par[v]) dict_word(L, v, path[k]);
				np = dd + 1;
			}
		}
		for (int k = 1; k < np && !stub_over(g); k++) {
			if (mode == 3 && k == 2) break;
			wl_type(g, r, cur, path[k]);
			tap_text(g, "Enter", 0);
			if (stub_over(g)) { steps++; break; }
			wl_row_word(wl_rowy[r], row);
			if (strcmp(row, path[k])) { FAIL("%s seed %llu: step %s not shown (row '%s', say '%s')", LV[level], seed, path[k], row, stub_say(g)); break; }
			strcpy(cur, path[k]); r++; steps++;
		}
		if (mode == 3) {
			tap_text(g, "Give up", 0);
			if (stub_over(g)) FAIL("%s: one tap on Give up ended the game", LV[level]);
			tap_text(g, "Give up", 0);
			if (!stub_over(g)) FAIL("%s: Give up twice did not end the game", LV[level]);
			/* the solution shown must be a real ladder from start to goal */
			const char *res = stub_result(g), *p = strchr(res, ':');
			int n = 0, sd = -1, ok = 1;
			char prev[8] = "", w[8];
			sscanf(res, "One way (%d steps)", &sd);
			while (p && *p) {
				while (*p == ':' || *p == ' ') p++;
				int m = 0;
				while (*p >= 'A' && *p <= 'Z' && m < 7) w[m++] = (char)(*p++ - 'A' + 'a');
				w[m] = 0;
				if (!m) break;
				if (n == 0 && strcmp(w, start)) ok = 0;
				if (n && !one_apart(prev, w)) ok = 0;
				if (dict_find(w, L) < 0) ok = 0;
				strcpy(prev, w); n++;
			}
			if (!ok || strcmp(prev, goal) || sd != n - 1 || sd < d) FAIL("%s: bad give-up ladder '%s' (shortest %d)", LV[level], res, d);
			else gaveup++;
		} else {
			if (!stub_over(g)) FAIL("%s seed %llu: reached %s but not over (say '%s')", LV[level], seed, goal, stub_say(g));
			else {
				char exp[80];
				if (mode == 2 && steps > d) snprintf(exp, sizeof exp, "Solved in %d steps - the shortest is %d.", steps, d);
				else snprintf(exp, sizeof exp, "Solved in %d steps - the shortest possible!", steps);
				if (strcmp(stub_result(g), exp)) FAIL("%s seed %llu: result '%s', want '%s'", LV[level], seed, stub_result(g), exp);
				else solved++;
			}
		}
		finish(g, "wordladder");
	}
	printf("  wordladder %-6s %3d games: shortest %d..%d steps, %d solved (%d via a detour), %d give-ups checked, %d bad steps rejected\n",
	       LV[level], games, dmin, dmax, solved, detours, gaveup, rejected);
}

/* ======================= Criss-Cross ======================= */
#define CG 16
static int cc_rows, cc_cols, cc_cell, cc_gx, cc_gy;
static char cc_sq[CG][CG], cc_given[CG][CG], cc_fill[CG][CG], cc_sol[CG][CG];
typedef struct { int r, c, dir, len; } CCSlot;
static CCSlot cc_slot[40];
static int cc_nslot;
static char cc_words[40][16];
static int cc_nwords, cc_ex[40], cc_ey[40], cc_ew, cc_eh;
static int cc_kx[27], cc_ky[27];
static int cc_used[40], cc_nsols;
static long cc_nodes;

static int cc_read(GameCtx *g) {
	(void)g;
	/* list labels give the top of the list */
	int listtop = 1 << 30;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *c = stub_text(i);
		if (strstr(c->text, " letters") && c->a[1] < listtop) listtop = c->a[1];
	}
	if (listtop == 1 << 30) return 0;
	/* squares: the start call log's square frames above the list */
	int minx = 1 << 30, miny = 1 << 30, n = 0;
	cc_cell = 0;
	for (int i = 0; i < stub_ncalls(); i++) {
		const Call *c = stub_call(i);
		if (c->type != C_FRAME || c->a[4] != 3 || c->a[2] != c->a[3] || c->a[1] + c->a[3] > listtop) continue;
		cc_cell = c->a[2];
		if (c->a[0] < minx) minx = c->a[0];
		if (c->a[1] < miny) miny = c->a[1];
		n++;
	}
	if (!n) return 0;
	cc_gx = minx; cc_gy = miny; cc_rows = cc_cols = 0;
	memset(cc_sq, 0, sizeof cc_sq);
	for (int i = 0; i < stub_ncalls(); i++) {
		const Call *c = stub_call(i);
		if (c->type != C_FRAME || c->a[4] != 3 || c->a[2] != c->a[3] || c->a[1] + c->a[3] > listtop) continue;
		int col = (c->a[0] - minx) / cc_cell, row = (c->a[1] - miny) / cc_cell;
		if (row >= CG || col >= CG) return 0;
		cc_sq[row][col] = 1;
		if (row + 1 > cc_rows) cc_rows = row + 1;
		if (col + 1 > cc_cols) cc_cols = col + 1;
	}
	/* givens and words; keys */
	memset(cc_given, 0, sizeof cc_given);
	cc_nwords = 0;
	for (int k = 0; k < 27; k++) cc_kx[k] = -1;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *c = stub_text(i);
		int cx = c->a[0] + c->a[2] / 2, cy = c->a[1] + c->a[3] / 2, len = (int)strlen(c->text);
		if (cy < listtop) {
			if (len == 1) cc_given[(cy - cc_gy) / cc_cell][(cx - cc_gx) / cc_cell] = c->text[0];
		} else if (len >= 4 && c->a[5] && c->text[0] >= 'A' && c->text[0] <= 'Z' && c->text[1] >= 'A' && c->text[1] <= 'Z' && cc_nwords < 40) {
			strcpy(cc_words[cc_nwords], c->text);
			cc_ex[cc_nwords] = c->a[0]; cc_ey[cc_nwords] = c->a[1]; cc_ew = c->a[2]; cc_eh = c->a[3];
			cc_nwords++;
		} else if (len == 1 && c->text[0] >= 'A' && c->text[0] <= 'Z') { cc_kx[c->text[0] - 'A'] = cx; cc_ky[c->text[0] - 'A'] = cy; }
		else if (!strcmp(c->text, "Erase")) { cc_kx[26] = cx; cc_ky[26] = cy; }
	}
	for (int k = 0; k < 27; k++) if (cc_kx[k] < 0) return 0;
	/* slots = maximal runs of 2+ squares */
	cc_nslot = 0;
	for (int d = 0; d < 2; d++)
		for (int r = 0; r < cc_rows; r++)
			for (int c = 0; c < cc_cols; c++) {
				int dr = d, dc = !d;
				if (!cc_sq[r][c] || (r - dr >= 0 && c - dc >= 0 && cc_sq[r - dr][c - dc])) continue;
				int len = 0;
				while (r + dr * len < CG && c + dc * len < CG && cc_sq[r + dr * len][c + dc * len]) len++;
				if (len >= 2 && cc_nslot < 40) { cc_slot[cc_nslot].r = r; cc_slot[cc_nslot].c = c; cc_slot[cc_nslot].dir = d; cc_slot[cc_nslot].len = len; cc_nslot++; }
			}
	return n;
}

static void cc_count(int k) {
	if (cc_nsols >= 2 || ++cc_nodes > 2000000) return;
	if (k == cc_nslot) { if (!cc_nsols) memcpy(cc_sol, cc_fill, sizeof cc_sol); cc_nsols++; return; }
	CCSlot *sl = &cc_slot[k];
	int dr = sl->dir, dc = !sl->dir;
	for (int i = 0; i < cc_nwords; i++) {
		if (cc_used[i] || (int)strlen(cc_words[i]) != sl->len) continue;
		int ok = 1;
		for (int p = 0; p < sl->len && ok; p++) { char ch = cc_fill[sl->r + dr * p][sl->c + dc * p]; ok = !ch || ch == cc_words[i][p]; }
		if (!ok) continue;
		int fresh[16], nf = 0;
		for (int p = 0; p < sl->len; p++) { char *ch = &cc_fill[sl->r + dr * p][sl->c + dc * p]; if (!*ch) { *ch = cc_words[i][p]; fresh[nf++] = p; } }
		cc_used[i] = 1;
		cc_count(k + 1);
		cc_used[i] = 0;
		for (int f = 0; f < nf; f++) cc_fill[sl->r + dr * fresh[f]][sl->c + dc * fresh[f]] = 0;
	}
}

static int cc_connected(void) {
	int seen[CG][CG] = { { 0 } }, st[CG * CG][2], sp = 0, total = 0, reach = 0;
	for (int r = 0; r < cc_rows; r++) for (int c = 0; c < cc_cols; c++) if (cc_sq[r][c]) { total++; if (!sp && !reach) { st[sp][0] = r; st[sp][1] = c; sp++; seen[r][c] = 1; } }
	while (sp) {
		sp--; int r = st[sp][0], c = st[sp][1]; reach++;
		static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
		for (int k = 0; k < 4; k++) {
			int rr = r + D[k][0], cc = c + D[k][1];
			if (rr >= 0 && cc >= 0 && rr < CG && cc < CG && cc_sq[rr][cc] && !seen[rr][cc]) { seen[rr][cc] = 1; st[sp][0] = rr; st[sp][1] = cc; sp++; }
		}
	}
	return reach == total;
}

static void cc_tap_cell(GameCtx *g, int r, int c) { stub_tap(g, cc_gx + c * cc_cell + cc_cell / 2, cc_gy + r * cc_cell + cc_cell / 2, rnd(2)); }
static void cc_key(GameCtx *g, int k) { stub_tap(g, cc_kx[k], cc_ky[k], rnd(2)); }
/* the letter shown in a square (0 if none) */
static char cc_shown(int r, int c, int *bold) {
	int x0 = cc_gx + c * cc_cell, y0 = cc_gy + r * cc_cell;
	for (int i = stub_ntexts() - 1; i >= 0; i--) {
		const Call *t = stub_text(i);
		int cx = t->a[0] + t->a[2] / 2, cy = t->a[1] + t->a[3] / 2;
		if (strlen(t->text) == 1 && cx >= x0 && cx < x0 + cc_cell && cy >= y0 && cy < y0 + cc_cell) { if (bold) *bold = t->a[5]; return t->text[0]; }
	}
	return 0;
}
/* did this action strike through list entry i? */
static int cc_struck(int i) {
	for (int k = 0; k < stub_ncalls(); k++) {
		const Call *c = stub_call(k);
		if (c->type == C_LINE && c->a[1] == c->a[3] && c->a[1] > cc_ey[i] && c->a[1] < cc_ey[i] + cc_eh && c->a[0] >= cc_ex[i] && c->a[2] <= cc_ex[i] + cc_ew) return 1;
	}
	return 0;
}
static int cc_word_index(const char *w) { for (int i = 0; i < cc_nwords; i++) if (!strcmp(cc_words[i], w)) return i; return -1; }

/* select the slot's first square the player fills in, facing the slot's direction */
static int cc_select(GameCtx *g, const CCSlot *sl, int *pr, int *pc) {
	int dr = sl->dir, dc = !sl->dir;
	for (int p = 0; p < sl->len; p++) {
		int r = sl->r + dr * p, c = sl->c + dc * p;
		if (cc_given[r][c]) continue;
		cc_tap_cell(g, r, c);
		if (!strstr(stub_say(g), sl->dir ? "down" : "across")) cc_tap_cell(g, r, c);
		if (!strstr(stub_say(g), sl->dir ? "down" : "across")) return 0;
		*pr = r; *pc = c;
		return 1;
	}
	return 0;
}

static void test_crisscross(int level, int games) {
	int want = level == 0 ? 8 : level == 1 ? 12 : 16, maxlen = level == 0 ? 6 : level == 1 ? 8 : 13;
	int unique = 0, solved = 0, minside = 99, maxside = 0, mincell = 1 << 30, longest = 0, starters = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_crisscross, level, &seed);
		if (!cc_read(g)) { FAIL("%s seed %llu: could not read the board", LV[level], seed); finish(g, "crisscross"); continue; }
		if (cc_rows > maxside) maxside = cc_rows;
		if (cc_cols > maxside) maxside = cc_cols;
		if (cc_rows < minside) minside = cc_rows;
		if (cc_cell < mincell) mincell = cc_cell;
		/* every word placed; runs of squares are exactly the words (no side-by-side touching); one connected grid */
		if (cc_nwords != want) FAIL("%s seed %llu: %d words listed, want %d", LV[level], seed, cc_nwords, want);
		if (cc_nslot != cc_nwords) FAIL("%s seed %llu: %d runs of squares for %d words", LV[level], seed, cc_nslot, cc_nwords);
		int lens[20] = { 0 }, lens2[20] = { 0 }, given_n = 0;
		for (int i = 0; i < cc_nwords; i++) {
			int l = (int)strlen(cc_words[i]);
			if (l < 4 || l > maxlen) FAIL("%s: word %s has %d letters", LV[level], cc_words[i], l);
			if (l > longest) longest = l;
			if (l < 20) lens[l]++;
		}
		for (int i = 0; i < cc_nslot; i++) if (cc_slot[i].len < 20) lens2[cc_slot[i].len]++;
		if (memcmp(lens, lens2, sizeof lens)) FAIL("%s seed %llu: run lengths do not match the word lengths", LV[level], seed);
		if (!cc_connected()) FAIL("%s seed %llu: the grid is in pieces", LV[level], seed);
		for (int i = 0; i < cc_nslot; i++) { /* each word crosses another */
			int cross = 0, dr = cc_slot[i].dir, dc = !cc_slot[i].dir;
			for (int p = 0; p < cc_slot[i].len; p++) {
				int r = cc_slot[i].r + dr * p, c = cc_slot[i].c + dc * p;
				cross += dc ? ((r > 0 && cc_sq[r - 1][c]) || (r + 1 < CG && cc_sq[r + 1][c])) : ((c > 0 && cc_sq[r][c - 1]) || (c + 1 < CG && cc_sq[r][c + 1]));
			}
			if (!cross && cc_nslot > 1) FAIL("%s: a word crosses nothing", LV[level]);
		}
		for (int r = 0; r < CG; r++) for (int c = 0; c < CG; c++) given_n += cc_given[r][c] != 0;
		if (level == 0 && given_n < cc_nwords - 2) FAIL("easy seed %llu: only %d letters given for %d words", seed, given_n, cc_nwords);
		if (level > 0) { /* at most one starter letter, and only when no word length is unique */
			int uniq = 0;
			for (int l = 0; l < 20; l++) uniq |= lens[l] == 1;
			if (given_n > 1 || (given_n == 1 && uniq) || (given_n == 0 && !uniq)) FAIL("%s seed %llu: %d letters given (unique length: %d)", LV[level], seed, given_n, uniq);
			starters += given_n;
		}
		/* solve it ourselves (crossings must agree) */
		memcpy(cc_fill, cc_given, sizeof cc_fill);
		memset(cc_used, 0, sizeof cc_used);
		cc_nsols = 0; cc_nodes = 0;
		cc_count(0);
		if (!cc_nsols) { FAIL("%s seed %llu: no way to fill the grid with the words", LV[level], seed); finish(g, "crisscross"); continue; }
		if (cc_nsols == 1) unique++;
		/* busy before ready */
		stub_set_busy(g, 1);
		stub_tap(g, cc_kx[0], cc_ky[0], 0);
		if (draw_calls()) FAIL("%s: drew while the board was still drawing", LV[level]);
		stub_set_busy(g, 0);
		cc_key(g, 0);
		if (!strstr(stub_say(g), "first")) FAIL("%s: key without a square: say '%s'", LV[level], stub_say(g));
		/* fill slot 0: a wrong letter first, erased */
		int struck[40] = { 0 }, r0, c0;
		CCSlot *s0 = &cc_slot[0];
		if (!cc_select(g, s0, &r0, &c0)) { FAIL("%s: cannot select slot 0", LV[level]); finish(g, "crisscross"); continue; }
		char right = cc_sol[r0][c0], wrong = right == 'Z' ? 'Y' : (char)(right + 1);
		cc_key(g, wrong - 'A');
		if (cc_shown(r0, c0, 0) != wrong) FAIL("%s: typed %c, square shows %c", LV[level], wrong, cc_shown(r0, c0, 0));
		cc_tap_cell(g, r0, c0); /* back to it (the selection moved on), keep the direction */
		if (!strstr(stub_say(g), s0->dir ? "down" : "across")) cc_tap_cell(g, r0, c0);
		cc_key(g, 26);
		if (cc_shown(r0, c0, 0)) FAIL("%s: Erase left %c", LV[level], cc_shown(r0, c0, 0));
		/* now fill every slot by typing along it (the selection advances by itself, skipping given letters) */
		int order[40];
		for (int i = 0; i < cc_nslot; i++) order[i] = i;
		for (int i = cc_nslot - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
		for (int oi = 0; oi < cc_nslot && !stub_over(g); oi++) {
			CCSlot *sl = &cc_slot[order[oi]];
			int r, c, dr = sl->dir, dc = !sl->dir;
			if (!cc_select(g, sl, &r, &c)) continue; /* all of it given */
			for (int p = 0; p < sl->len && !stub_over(g); p++) {
				int rr = sl->r + dr * p, cc = sl->c + dc * p;
				if (cc_given[rr][cc] || (dr ? rr < r : cc < c)) continue;
				cc_key(g, cc_sol[rr][cc] - 'A');
				for (int i = 0; i < cc_nwords; i++) struck[i] |= cc_struck(i);
				if (!stub_over(g) && cc_shown(rr, cc, 0) != cc_sol[rr][cc]) { FAIL("%s seed %llu: square %d,%d shows %c, typed %c", LV[level], seed, rr, cc, cc_shown(rr, cc, 0), cc_sol[rr][cc]); break; }
			}
			if (oi == 0 && !stub_over(g)) {
				/* break the finished word with a wrong letter: it must be un-crossed, then crossed again */
				char w[16];
				for (int p = 0; p < sl->len; p++) w[p] = cc_sol[sl->r + dr * p][sl->c + dc * p];
				w[sl->len] = 0;
				int wi = cc_word_index(w);
				if (wi < 0 || !struck[wi]) FAIL("%s seed %llu: %s complete but not crossed off", LV[level], seed, w);
				int pr, pc;
				if (wi >= 0 && cc_select(g, sl, &pr, &pc)) {
					char ok = cc_sol[pr][pc], bad = ok == 'A' ? 'B' : 'A';
					cc_key(g, bad - 'A');
					int cleared = 0;
					for (int k = 0; k < stub_ncalls(); k++) { const Call *cl = stub_call(k); if (cl->type == C_CLEAR && cl->a[0] == cc_ex[wi] && cl->a[1] == cc_ey[wi]) cleared = 1; }
					if (!cleared || !find_text(w, 0)) FAIL("%s: %s not un-crossed after a wrong letter", LV[level], w);
					cc_tap_cell(g, pr, pc);
					if (!strstr(stub_say(g), sl->dir ? "down" : "across")) cc_tap_cell(g, pr, pc);
					cc_key(g, ok - 'A');
					if (!cc_struck(wi)) FAIL("%s: %s not crossed again", LV[level], w);
				}
			}
		}
		if (!stub_over(g)) FAIL("%s seed %llu: grid filled but not solved (say '%s')", LV[level], seed, stub_say(g));
		else if (!strstr(stub_result(g), "Solved")) FAIL("%s: result '%s'", LV[level], stub_result(g));
		else solved++;
		for (int i = 0; i < cc_nwords; i++) if (!struck[i] && stub_over(g)) { FAIL("%s seed %llu: %s never crossed off", LV[level], seed, cc_words[i]); break; }
		finish(g, "crisscross");
	}
	printf("  crisscross %-6s %3d games: grids %d..%d squares a side, cells >= %dpx, longest word %d, %d unique fills, %d solved, %d starter letters\n",
	       LV[level], games, minside, maxside, mincell, longest, unique, solved, starters);
}

/* many starts (no play): word-list themes' words follow their rule, hand-made themes don't come back within 6 (each
 * once per round) nor a word-list theme within 40, and the word sets are nearly all different */
static int cc_word_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static int cc_rule_ok(const char *theme, const char *w) {
	char a[16] = "";
	int n = (int)strlen(w), k;
	if (sscanf(theme, "words starting with %15[A-Z]", a) == 1) return !strncmp(w, a, strlen(a));
	if (sscanf(theme, "words ending in -%15[A-Z]", a) == 1 || sscanf(theme, "words ending in %15[A-Z]", a) == 1)
		return n >= (int)strlen(a) && !strcmp(w + n - strlen(a), a);
	if (sscanf(theme, "words with %15[A-Z] in them", a) == 1 || sscanf(theme, "words with an %15[A-Z]", a) == 1 ||
	    sscanf(theme, "words with a %15[A-Z]", a) == 1)
		return strstr(w, a) != 0;
	if (!strcmp(theme, "words with a double letter")) { for (k = 0; k + 1 < n; k++) if (w[k] == w[k + 1]) return 1; return 0; }
	if (!strcmp(theme, "words that start and end alike")) return w[0] == w[n - 1];
	return 1;
}
static void test_crisscross_variety(int level, int starts) {
	static unsigned long long sets[2000];
	static char hand[2000][64], rules[2000][64];
	int nsets = 0, nhand = 0, nrules = 0, dhand = 0, drules = 0, hand_rep = 0, rule_rep = 0;
	if (starts > 2000) starts = 2000;
	for (int gi = 0; gi < starts; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_crisscross, level, &seed);
		char theme[64] = "";
		const char *rm = stub_remark(g), *end = strstr(rm, " - fit all");
		if (!cc_read(g) || strncmp(rm, "Theme: ", 7) || !end || end - rm - 7 >= (int)sizeof theme) {
			FAIL("%s: board or theme not read ('%s')", LV[level], rm);
			finish(g, "crisscross");
			continue;
		}
		memcpy(theme, rm + 7, (size_t)(end - rm - 7));
		theme[end - rm - 7] = 0;
		char ws[40][16];
		memcpy(ws, cc_words, sizeof ws);
		qsort(ws, (size_t)cc_nwords, sizeof ws[0], cc_word_cmp);
		unsigned long long h = 1469598103934665603ULL;
		for (int i = 0; i < cc_nwords; i++) for (const char *p = ws[i]; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
		int dup = 0;
		for (int i = 0; i < nsets && !dup; i++) dup = sets[i] == h;
		if (!dup) sets[nsets++] = h;
		int is_rule = theme[0] >= 'a' && theme[0] <= 'z';
		if (is_rule) {
			for (int i = 0; i < cc_nwords; i++) if (!cc_rule_ok(theme, cc_words[i])) FAIL("%s: '%s' is not one of the %s", LV[level], cc_words[i], theme);
			int seen = 0;
			for (int i = nrules - 1; i >= 0; i--) if (!strcmp(rules[i], theme)) { seen = 1; if (nrules - i < 40) rule_rep++; break; }
			drules += !seen;
			snprintf(rules[nrules++], sizeof rules[0], "%s", theme);
		} else {
			int seen = 0;
			for (int i = nhand - 1; i >= 0; i--) if (!strcmp(hand[i], theme)) { seen = 1; if (nhand - i < 6) hand_rep++; break; }
			dhand += !seen;
			snprintf(hand[nhand++], sizeof hand[0], "%s", theme);
		}
		finish(g, "crisscross");
	}
	if (hand_rep) FAIL("%s: a hand-made theme came back %d times within 6", LV[level], hand_rep);
	if (rule_rep) FAIL("%s: a word-list theme came back %d times within 40", LV[level], rule_rep);
	if (nsets < starts * 98 / 100) FAIL("%s: only %d different word sets in %d", LV[level], nsets, starts);
	if (nrules < starts / 3) FAIL("%s: only %d word-list themes in %d", LV[level], nrules, starts);
	/* the theme history lives in the store (by name hash: a bag of hand-made themes, a ring of rules); the game
	 * keeps none of its own, so that is what survives a restart; damaged records are ignored */
	GameCtx *g = stub_new(&GAME_crisscross, level, 4321, AX, AY, AW, AH);
	struct { unsigned magic, nused, nlast, lpos, last[128], used[1024]; } bag;
	struct { unsigned magic, n, pos, h[256]; } ring;
	if (g_load(g, "themes", &bag, sizeof bag) != (int)sizeof bag || !bag.nused || bag.nused > 18) FAIL("%s: no theme bag in the store", LV[level]);
	if (g_load(g, "rules", &ring, sizeof ring) != (int)sizeof ring || !ring.n) FAIL("%s: no rule history in the store", LV[level]);
	g_save(g, "themes", "x", 1);
	g_save(g, "rules", "yy", 2);
	stub_free(g);
	for (int i = 0; i < 5; i++) {
		unsigned long long seed;
		g = new_game(&GAME_crisscross, level, &seed);
		if (!cc_read(g) || cc_nwords < 8) FAIL("%s: no proper puzzle after damaged theme records", LV[level]);
		finish(g, "crisscross");
	}
	printf("  crisscross %-6s %3d starts: %d different word sets; %d hand-made themes (%d different), %d word-list themes (%d different)\n",
	       LV[level], starts, nsets, nhand, dhand, nrules, drules);
}

int main(int argc, char **argv) {
	int quick = argc > 1 && !strcmp(argv[1], "quick");
	const char *only = argc > 2 ? argv[2] : 0;
	setvbuf(stdout, 0, _IONBF, 0);
	static const int AREAS[3][4] = { { 40, 200, 1780, 2040 }, { 0, 140, 1860, 2200 }, { 60, 240, 1740, 1800 } };
	for (int a = 0; a < (quick ? 1 : 3); a++) {
		AX = AREAS[a][0]; AY = AREAS[a][1]; AW = AREAS[a][2]; AH = AREAS[a][3];
		int div = a == 0 ? 1 : 4;
		printf("play area %d,%d %dx%d\n", AX, AY, AW, AH);
		for (int lv = 0; lv < 3; lv++) {
#define RUN(name, call) if (!only || !strcmp(only, name)) call
			RUN("wordgrid", test_wordgrid(lv, (quick ? 10 : 80) / div));
			RUN("wordladder", test_wordladder(lv, (quick ? 20 : 120) / div));
			RUN("crisscross", test_crisscross(lv, (quick ? 20 : 120) / div));
			if (a == 0) RUN("crisscross", test_crisscross_variety(lv, quick ? 60 : 300));
		}
	}
	(void)draw_calls;
	printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
	return failures != 0;
}

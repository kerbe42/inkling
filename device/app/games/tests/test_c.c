/* Host tests for Word Search, Maze, Nonogram and Cryptogram (agent C). Plays each game many times on every level
 * through the stub engine (stub_c.c), reading the board back from the logged drawing calls, and checks the rules.
 * Build: gcc -O1 -g -fsanitize=address,undefined -I device/app device/app/games/tests/stub_c.c \
 *        device/app/games/tests/test_c.c device/app/games/{wordsearch,maze,nonogram,cryptogram}.c -lm
 * Run: ./a.out [quick|full] [game] */
#include "games.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- stub interface (stub_c.c) --- */
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
void *stub_game_state(GameCtx *g);
const char *stub_remark(GameCtx *g);
void stub_store_clear(void);
void stub_store_put(GameCtx *g, const char *key, const void *buf, size_t n);
void stub_resume(GameCtx *g);
extern int stub_store_saves;
GameCtx *stub_new(const GameDef *def, int level, unsigned long long seed, int ax, int ay, int aw, int ah);
void stub_free(GameCtx *g);
void stub_start(GameCtx *g);
void stub_stroke(GameCtx *g, const short *xs, const short *ys, int n, int tap, int finger);
void stub_tap(GameCtx *g, int x, int y, int finger);
void stub_line(GameCtx *g, int x0, int y0, int x1, int y1, int npts);

extern const GameDef GAME_wordsearch __attribute__((weak)), GAME_maze __attribute__((weak)),
	GAME_nonogram __attribute__((weak)), GAME_cryptogram __attribute__((weak));

static int AX = 40, AY = 200, AW = 1780, AH = 2040;
static unsigned long long trng = 88172645463325252ULL;
static int rnd(int n) { trng ^= trng << 13; trng ^= trng >> 7; trng ^= trng << 17; return (int)((trng >> 11) % (unsigned long long)n); }
static unsigned long long seed_ctr = 5000;
static const char *LV[3] = { "easy", "medium", "hard" };

static int failures;
#define FAIL(...) do { failures++; if (failures <= 60) { printf("  FAIL [%s] ", __func__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void finish(GameCtx *g, const char *name) {
	if (stub_violations(g)) FAIL("%s: %ld engine-rule violations, first: %s", name, stub_violations(g), stub_first_violation(g));
	stub_free(g);
}

static GameCtx *new_game(const GameDef *d, int level) {
	GameCtx *g = stub_new(d, level, seed_ctr++, AX, AY, AW, AH);
	stub_start(g);
	return g;
}

static int count_type(int type) { int n = 0; for (int i = 0; i < stub_ncalls(); i++) n += stub_call(i)->type == type; return n; }

/* A pen stroke through the given points (each segment split into short steps), as the engine would deliver it. */
static void stroke_path(GameCtx *g, const int *px, const int *py, int n, int step) {
	static short xs[20000], ys[20000];
	int k = 0;
	for (int i = 0; i + 1 < n && k < 19990; i++) {
		int dx = px[i + 1] - px[i], dy = py[i + 1] - py[i], len = abs(dx) > abs(dy) ? abs(dx) : abs(dy), m = len / step + 1;
		for (int j = 0; j < m && k < 19990; j++) { xs[k] = (short)(px[i] + dx * j / m); ys[k] = (short)(py[i] + dy * j / m); k++; }
	}
	xs[k] = (short)px[n - 1]; ys[k] = (short)py[n - 1]; k++;
	stub_stroke(g, xs, ys, k, 0, 0);
}

/* ======================= Word Search ======================= */
#include "ws_themes.h" /* the same theme list, to check every theme gets played */
static const int D8[8][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { -1, 1 }, { 0, -1 }, { -1, 0 }, { -1, -1 }, { 1, -1 } };

/* Which theme is this puzzle? The remark says "Find the 12 <theme>!" on a new puzzle. -1 if none. */
static int ws_theme(GameCtx *g) {
	const char *r = stub_remark(g), *p = strchr(r, ' ');
	if (strncmp(r, "Find the ", 9) || !(p = strchr(r + 9, ' '))) return -1;
	size_t len = strlen(++p);
	for (int t = 0; t < NTHEMES; t++)
		if (len == strlen(THEMES[t].name) + 1 && !strncmp(p, THEMES[t].name, len - 1) && p[len - 1] == '!') return t;
	return -1;
}

static void test_wordsearch(int level, int games) {
	static const int N[3] = { 15, 20, 25 }, NW[3] = { 12, 20, 28 }, MINL[3] = { 4, 5, 6 }, MAXL[3] = { 8, 10, 14 }, NLONG[3] = { 0, 6, 14 };
	int n = N[level], finished = 0, junk = 0, sloppy = 0, again = 0, mincell = 9999, minpx = 9999, maxlen = 0, longs = 0, words_total = 0;
	int dirs[4] = { 0 }, ntheme = 0;
	static char seen[NTHEMES];
	memset(seen, 0, sizeof seen);
	stub_store_clear(); /* a fresh theme bag: `games` >= NTHEMES puzzles must use every theme */
	for (int gi = 0; gi < games; gi++) {
		GameCtx *g = new_game(&GAME_wordsearch, level);
		int th = ws_theme(g);
		if (th < 0) FAIL("remark '%s' names no theme", stub_remark(g));
		else if (!seen[th]) { seen[th] = 1; ntheme++; }
		static char grid[25][25];
		char words[32][48];
		int nl = 0, nw = 0, gx = 1 << 30, gy = 1 << 30, cell = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1) { nl++; if (t->a[0] < gx) gx = t->a[0]; if (t->a[1] < gy) gy = t->a[1]; cell = t->a[2]; if (t->a[4] < minpx) minpx = t->a[4]; }
		}
		if (nl != n * n) { FAIL("%d letters for a %dx%d grid", nl, n, n); finish(g, "wordsearch"); continue; }
		if (cell < mincell) mincell = cell;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1) grid[(t->a[1] - gy) / cell][(t->a[0] - gx) / cell] = t->text[0];
			else if (nw < 32) snprintf(words[nw++], sizeof words[0], "%s", t->text);
		}
		if (nw != NW[level]) FAIL("%d words listed (want %d)", nw, NW[level]);
		int pr[32], pc[32], pd[32], ok = 1, nlong = 0;
		for (int w = 0; w < nw; w++) {
			int len = (int)strlen(words[w]), hits = 0;
			for (int i = 0; i < len; i++) if (words[w][i] < 'A' || words[w][i] > 'Z') { FAIL("word '%s' is not A-Z", words[w]); break; }
			if (len < MINL[level] || len > MAXL[level]) FAIL("'%s' has %d letters (%s)", words[w], len, LV[level]);
			if (len > maxlen) maxlen = len;
			nlong += len >= 8;
			for (int v = 0; v < nw; v++) if (v != w && strstr(words[v], words[w])) FAIL("'%s' is inside '%s'", words[w], words[v]);
			for (int d = 0; d < 8; d++)
				for (int r = 0; r < n; r++)
					for (int c = 0; c < n; c++) {
						int k = 0;
						while (k < len) {
							int rr = r + D8[d][0] * k, cc = c + D8[d][1] * k;
							if (rr < 0 || rr >= n || cc < 0 || cc >= n || grid[rr][cc] != words[w][k]) break;
							k++;
						}
						if (k == len) { hits++; pr[w] = r; pc[w] = c; pd[w] = d; }
					}
			if (hits != 1) { FAIL("'%s' is in the grid %d times (%s)", words[w], hits, LV[level]); ok = 0; continue; }
			if (pd[w] >= (level == 0 ? 2 : 4)) { FAIL("'%s' runs in direction %d,%d on %s (backwards or not allowed)", words[w], D8[pd[w]][0], D8[pd[w]][1], LV[level]); ok = 0; }
			else dirs[pd[w]]++;
		}
		if (nlong < NLONG[level]) FAIL("only %d words of 8+ letters (%s)", nlong, LV[level]);
		longs += nlong;
		words_total += nw;
		if (!ok) { finish(g, "wordsearch"); continue; }
		int order[32];
		for (int w = 0; w < nw; w++) order[w] = w;
		for (int i = nw - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
		for (int e = 0; e < nw; e++) {
			int w = order[e], len = (int)strlen(words[w]);
			if (rnd(4) == 0) { /* junk: a tap, or a stroke under the list */
				junk++;
				if (rnd(2)) stub_tap(g, gx + rnd(n * cell), gy + rnd(n * cell), 0);
				else stub_line(g, AX + 30 + rnd(AW / 2), AY + AH - 30, AX + AW / 2 + rnd(AW / 2 - 30), AY + AH - 30 - rnd(40), 12);
				if (!strncmp(stub_say(g), "found", 5) || stub_over(g)) FAIL("junk input found a word");
			}
			int j = cell / 5, s0 = 0, s1 = len - 1;
			if (rnd(4) == 0) { s0 = rnd(3) - 1; s1 = len - 1 + rnd(3) - 1; sloppy++; } /* one letter short or long at either end */
			if (s1 - s0 < 2) { s0 = 0; s1 = len - 1; } /* (a 4-letter word short at both ends: a 1-letter line, any angle) */
			int x0 = gx + (pc[w] + D8[pd[w]][1] * s0) * cell + cell / 2 + rnd(2 * j + 1) - j, y0 = gy + (pr[w] + D8[pd[w]][0] * s0) * cell + cell / 2 + rnd(2 * j + 1) - j;
			int x1 = gx + (pc[w] + D8[pd[w]][1] * s1) * cell + cell / 2 + rnd(2 * j + 1) - j;
			int y1 = gy + (pr[w] + D8[pd[w]][0] * s1) * cell + cell / 2 + rnd(2 * j + 1) - j;
			if (x0 < AX) x0 = AX;
			if (y0 < AY) y0 = AY;
			if (x1 < AX) x1 = AX;
			if (y1 < AY) y1 = AY;
			if (rnd(2)) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
			if (stub_over(g)) FAIL("over before every word was found");
			stub_line(g, x0, y0, x1, y1, 2 + rnd(60));
			int capsule = 0;
			for (int i = 0; i < stub_ncalls(); i++) capsule |= stub_call(i)->type == C_POLY && stub_call(i)->a[0] == 19;
			char want[64];
			snprintf(want, sizeof want, "found %s ", words[w]);
			if (!capsule || (!stub_over(g) && strncmp(stub_say(g), want, strlen(want)))) FAIL("line over '%s' was not accepted (say: %s)", words[w], stub_say(g));
			if (!stub_over(g) && rnd(6) == 0) { /* the same word again */
				again++;
				stub_line(g, x1, y1, x0, y0, 20);
				if (!strstr(stub_say(g), "already found")) FAIL("found word again: say '%s'", stub_say(g));
			}
		}
		if (!stub_over(g) || !strstr(stub_result(g), "You found all")) FAIL("all words found but result '%s'", stub_result(g));
		else finished++;
		int left = 0;
		for (int i = 0; i < stub_ntexts(); i++) left += strlen(stub_text(i)->text) == 1;
		if (left != n * n) FAIL("%d letters left on screen after clean-ups", left);
		finish(g, "wordsearch");
	}
	if (games >= NTHEMES && ntheme != NTHEMES) FAIL("%d %s games used %d of the %d themes", games, LV[level], ntheme, NTHEMES);
	printf("  wordsearch %-6s %4d games, %d themes: %dx%d, cells >= %d px, letters >= %d px; every word once, forwards only (across %d, down %d, "
	       "down-right %d, up-right %d); %.1f words of 8+ letters per game, longest %d; %d finished (%d sloppy ends, %d junk, %d repeats)\n",
	       LV[level], games, ntheme, n, n, mincell, minpx, dirs[0], dirs[1], dirs[2], dirs[3], games ? (double)longs / games : 0, maxlen,
	       finished, sloppy, junk, again);
	(void)words_total;
}

/* The theme bag: N puzzles in a row use N different themes, the next round doesn't start with the last one, and the bag
 * lives in the game's stored record (so it survives app restarts); a damaged record is ignored. */
static void test_ws_bag(void) {
	static char seen[NTHEMES];
	int last = -1, total = 3 * NTHEMES + 1;
	stub_store_clear();
	for (int i = 0; i < total; i++) {
		if (i % NTHEMES == 0) memset(seen, 0, sizeof seen);
		GameCtx *g = new_game(&GAME_wordsearch, i % 3);
		int t = ws_theme(g);
		finish(g, "wordsearch");
		if (t < 0) { FAIL("puzzle %d: no theme in the remark", i); continue; }
		if (seen[t]) FAIL("puzzle %d: '%s' again after %d of the %d themes", i, THEMES[t].name, i % NTHEMES, NTHEMES);
		if (t == last) FAIL("puzzle %d: '%s' twice in a row", i, THEMES[t].name);
		seen[t] = 1;
		last = t;
	}
	if (stub_store_saves != total) FAIL("%d bag saves for %d puzzles", stub_store_saves, total);
	/* damaged records: short, garbage, an absurd count */
	static const unsigned huge[2] = { 0x57534231u, 1000000u };
	unsigned char junk[300];
	for (int i = 0; i < (int)sizeof junk; i++) junk[i] = (unsigned char)(i * 37 + 11);
	const void *bad[3] = { "x", junk, huge };
	size_t badn[3] = { 1, sizeof junk, sizeof huge };
	for (int b = 0; b < 3; b++) {
		GameCtx *g = stub_new(&GAME_wordsearch, b, seed_ctr++, AX, AY, AW, AH);
		stub_store_put(g, "bag", bad[b], badn[b]);
		stub_start(g);
		if (ws_theme(g) < 0) FAIL("damaged bag %d: no theme", b);
		finish(g, "wordsearch");
	}
	printf("  wordsearch bag: %d puzzles in a row, each round of %d uses every theme once, never the same twice running; "
	       "damaged bag records ignored\n", total, NTHEMES);
}

/* The board as the screen model shows it right after start() or resume(): letters, the listed words (in drawing
 * order, i.e. alphabetical) and where each word is. 0 if it can't be read. */
typedef struct { int n, nw, gx, gy, cell; char grid[25][25]; char words[32][48]; int pr[32], pc[32], pd[32]; } WsBoard;

static int ws_read(WsBoard *b, int n) {
	memset(b, 0, sizeof *b);
	b->n = n;
	b->gx = b->gy = 1 << 30;
	int nl = 0;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *t = stub_text(i);
		if (strlen(t->text) != 1) continue;
		nl++;
		if (t->a[0] < b->gx) b->gx = t->a[0];
		if (t->a[1] < b->gy) b->gy = t->a[1];
		b->cell = t->a[2];
	}
	if (nl != n * n || !b->cell) return 0;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *t = stub_text(i);
		if (strlen(t->text) == 1) b->grid[(t->a[1] - b->gy) / b->cell][(t->a[0] - b->gx) / b->cell] = t->text[0];
		else if (b->nw < 32) snprintf(b->words[b->nw++], sizeof b->words[0], "%s", t->text);
	}
	for (int w = 0; w < b->nw; w++) {
		int len = (int)strlen(b->words[w]), hits = 0;
		for (int d = 0; d < 4; d++)
			for (int r = 0; r < n; r++)
				for (int c = 0; c < n; c++) {
					int k = 0;
					while (k < len) {
						int rr = r + D8[d][0] * k, cc = c + D8[d][1] * k;
						if (rr < 0 || rr >= n || cc < 0 || cc >= n || b->grid[rr][cc] != b->words[w][k]) break;
						k++;
					}
					if (k == len) { hits++; b->pr[w] = r; b->pc[w] = c; b->pd[w] = d; }
				}
		if (hits != 1) return 0;
	}
	return 1;
}

static void ws_ends(const WsBoard *b, int w, int *x0, int *y0, int *x1, int *y1) {
	int len = (int)strlen(b->words[w]);
	*x0 = b->gx + b->pc[w] * b->cell + b->cell / 2;
	*y0 = b->gy + b->pr[w] * b->cell + b->cell / 2;
	*x1 = b->gx + (b->pc[w] + D8[b->pd[w]][1] * (len - 1)) * b->cell + b->cell / 2;
	*y1 = b->gy + (b->pr[w] + D8[b->pd[w]][0] * (len - 1)) * b->cell + b->cell / 2;
}

static void ws_draw(GameCtx *g, const WsBoard *b, int w) {
	int x0, y0, x1, y1;
	ws_ends(b, w, &x0, &y0, &x1, &y1);
	stub_line(g, x0, y0, x1, y1, 24);
}

/* Save -> resume: a puzzle left part-way (its state bytes, as the engine saves them) comes back with the same board
 * and word list, the found words outlined and crossed off, the rest still to find; also into a different play area,
 * and a damaged state gives a fresh puzzle instead of garbage. */
static void test_ws_resume(void) {
	static const int N[3] = { 15, 20, 25 };
	static const int AREA2[4] = { 60, 240, 1740, 1800 };
	size_t ss = GAME_wordsearch.state_size;
	unsigned char *saved = malloc(ss);
	int resumed = 0, found_before = 0, moved = 0;
	if (!GAME_wordsearch.resume || !GAME_wordsearch.progress || GAME_wordsearch.save_version < 1) FAIL("word search doesn't opt in to save/resume");
	stub_store_clear();
	for (int level = 0; level < 3; level++)
		for (int gi = 0; gi < 8; gi++) {
			int n = N[level];
			GameCtx *g = new_game(&GAME_wordsearch, level);
			WsBoard a, b;
			int th = ws_theme(g);
			if (!ws_read(&a, n)) { FAIL("can't read the new %s board", LV[level]); finish(g, "wordsearch"); continue; }
			int order[32], k = gi == 0 ? 0 : 1 + rnd(a.nw - 1), was[32] = { 0 }; /* words found before leaving: 0 .. nw-1 */
			for (int w = 0; w < a.nw; w++) order[w] = w;
			for (int i = a.nw - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
			for (int i = 0; i < k; i++) { ws_draw(g, &a, order[i]); was[order[i]] = 1; }
			if (stub_over(g)) { FAIL("over after %d of %d words", k, a.nw); finish(g, "wordsearch"); continue; }
			char prog[48], want[48];
			GAME_wordsearch.progress(g, prog, sizeof prog);
			snprintf(want, sizeof want, "%d of %d found", k, a.nw);
			if (strcmp(prog, want)) FAIL("progress '%s', want '%s'", prog, want);
			memcpy(saved, stub_game_state(g), ss); /* what the engine writes to the save file */
			finish(g, "wordsearch");

			/* a new app run: a fresh context gets the bytes back; every other puzzle in another play area */
			int other = gi % 2;
			moved += other;
			GameCtx *h = other ? stub_new(&GAME_wordsearch, level, seed_ctr++, AREA2[0], AREA2[1], AREA2[2], AREA2[3])
					   : stub_new(&GAME_wordsearch, level, seed_ctr++, AX, AY, AW, AH);
			memcpy(stub_game_state(h), saved, ss);
			stub_resume(h);
			if (!ws_read(&b, n)) { FAIL("can't read the resumed %s board", LV[level]); finish(h, "wordsearch"); continue; }
			int same = a.nw == b.nw;
			for (int r = 0; r < n && same; r++) same = !memcmp(a.grid[r], b.grid[r], (size_t)n);
			for (int w = 0; w < a.nw && same; w++) same = !strcmp(a.words[w], b.words[w]) && a.pr[w] == b.pr[w] && a.pc[w] == b.pc[w] && a.pd[w] == b.pd[w];
			if (!same) FAIL("resumed %s board differs from the saved one", LV[level]);
			int caps = 0, crossed = 0;
			for (int i = 0; i < stub_ncalls(); i++) {
				const Call *c = stub_call(i);
				if (c->type == C_LINE && c->a[4] == 5) crossed++;
				if (c->type != C_POLY || c->a[0] != 19) continue;
				caps++;
				int hit = 0; /* the outline goes round a found word: both end cells inside its box */
				for (int w = 0; w < b.nw && !hit; w++) {
					int x0, y0, x1, y1;
					ws_ends(&b, w, &x0, &y0, &x1, &y1);
					hit = was[w] && x0 > c->a[1] && x0 < c->a[3] && x1 > c->a[1] && x1 < c->a[3] && y0 > c->a[2] && y0 < c->a[4] &&
					      y1 > c->a[2] && y1 < c->a[4];
				}
				if (!hit) FAIL("an outline on the resumed board isn't round a found word");
			}
			if (caps != k || crossed != k) FAIL("resumed with %d found: %d outlines, %d crossed off", k, caps, crossed);
			if (th >= 0 && !strstr(stub_remark(h), THEMES[th].name)) FAIL("resumed remark '%s' lost the theme '%s'", stub_remark(h), THEMES[th].name);
			if (!strstr(stub_remark(h), "to go")) FAIL("resumed remark '%s'", stub_remark(h));
			/* found words stay found; the rest can be found, and the puzzle finishes */
			for (int w = 0; w < b.nw && !stub_over(h); w++) {
				int x0, y0, x1, y1;
				ws_ends(&b, w, &x0, &y0, &x1, &y1);
				if (was[w]) {
					stub_line(h, x1, y1, x0, y0, 24);
					if (!strstr(stub_say(h), "already found")) FAIL("'%s' was found before leaving, now: '%s'", b.words[w], stub_say(h));
				}
			}
			for (int i = k; i < b.nw; i++) {
				ws_draw(h, &b, order[i]);
				if (!stub_over(h) && strncmp(stub_say(h), "found ", 6)) FAIL("after resume, '%s' not accepted: '%s'", b.words[order[i]], stub_say(h));
			}
			if (!stub_over(h) || !strstr(stub_result(h), "You found all")) FAIL("resumed puzzle not finished: '%s'", stub_result(h));
			else if (th >= 0 && !strstr(stub_result(h), THEMES[th].name)) FAIL("result '%s' lost the theme", stub_result(h));
			resumed++;
			found_before += k;
			finish(h, "wordsearch");
		}
	/* a damaged state (garbage, or zeros) must give a fresh puzzle, not crash or draw nonsense */
	for (int bad = 0; bad < 2; bad++)
		for (int level = 0; level < 3; level++) {
			GameCtx *h = stub_new(&GAME_wordsearch, level, seed_ctr++, AX, AY, AW, AH);
			memset(stub_game_state(h), bad ? 0 : 0x7f, ss);
			stub_resume(h);
			WsBoard b;
			if (!ws_read(&b, N[level]) || ws_theme(h) < 0) FAIL("damaged %s state: no fresh puzzle (remark '%s')", LV[level], stub_remark(h));
			finish(h, "wordsearch");
		}
	free(saved);
	printf("  wordsearch save/resume: %d puzzles left part-way (%d words found in all, %d resumed in another play area): same "
	       "board and list, found words outlined and crossed off, then finished; damaged states start afresh\n", resumed, found_before, moved);
}

/* ======================= Maze ======================= */
typedef struct { int R, C, cs, gx, gy; unsigned char h[50][50], v[50][50]; /* h[y][c]: wall on top of row y; v[r][x]: left of column x */ } Maze;

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* Rebuild the maze from the wall lines drawn by start(). */
static int read_maze(Maze *m) {
	int xs[4000], ys[4000], nx = 0, ny = 0;
	memset(m, 0, sizeof *m);
	for (int i = 0; i < stub_ncalls(); i++) {
		const Call *c = stub_call(i);
		if (c->type != C_LINE) continue;
		if (c->a[1] == c->a[3] && ny < 4000) ys[ny++] = c->a[1];
		else if (c->a[0] == c->a[2] && nx < 4000) xs[nx++] = c->a[0];
		else return 0;
	}
	if (nx < 2 || ny < 2) return 0;
	qsort(xs, (size_t)nx, sizeof xs[0], cmp_int);
	qsort(ys, (size_t)ny, sizeof ys[0], cmp_int);
	int cs = 1 << 30;
	for (int i = 1; i < nx; i++) if (xs[i] != xs[i - 1] && xs[i] - xs[i - 1] < cs) cs = xs[i] - xs[i - 1];
	m->cs = cs; m->gx = xs[0]; m->gy = ys[0];
	m->C = (xs[nx - 1] - m->gx) / cs; m->R = (ys[ny - 1] - m->gy) / cs;
	if (m->C > 49 || m->R > 49 || m->C < 2 || m->R < 2) return 0;
	for (int i = 0; i < stub_ncalls(); i++) {
		const Call *c = stub_call(i);
		if (c->type != C_LINE) continue;
		int lo, hi;
		if (c->a[1] == c->a[3]) {
			int y = (c->a[1] - m->gy) / cs;
			if ((c->a[1] - m->gy) % cs) return 0;
			lo = c->a[0] < c->a[2] ? c->a[0] : c->a[2]; hi = c->a[0] + c->a[2] - lo;
			for (int k = 0; k < m->C; k++) { int mid = m->gx + k * cs + cs / 2; if (mid >= lo && mid <= hi) m->h[y][k] = 1; }
		} else {
			int x = (c->a[0] - m->gx) / cs;
			if ((c->a[0] - m->gx) % cs) return 0;
			lo = c->a[1] < c->a[3] ? c->a[1] : c->a[3]; hi = c->a[1] + c->a[3] - lo;
			for (int k = 0; k < m->R; k++) { int mid = m->gy + k * cs + cs / 2; if (mid >= lo && mid <= hi) m->v[k][x] = 1; }
		}
	}
	return 1;
}

static int mz_open(const Maze *m, int r, int c, int dr, int dc) {
	int nr = r + dr, nc = c + dc;
	if (nr < 0 || nr >= m->R || nc < 0 || nc >= m->C) return 0;
	if (dc == 1) return !m->v[r][c + 1];
	if (dc == -1) return !m->v[r][c];
	if (dr == 1) return !m->h[r + 1][c];
	return !m->h[r][c];
}

static const int MD[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };

/* BFS from cell a; fills dist and prev; returns the number of reachable cells */
static int mz_bfs(const Maze *m, int a, int *dist, int *prev) {
	int q[2500], qh = 0, qt = 0, n = 0;
	for (int i = 0; i < m->R * m->C; i++) dist[i] = -1;
	dist[a] = 0; prev[a] = -1; q[qt++] = a;
	while (qh < qt) {
		int v = q[qh++], r = v / m->C, c = v % m->C;
		n++;
		for (int d = 0; d < 4; d++) {
			if (!mz_open(m, r, c, MD[d][0], MD[d][1])) continue;
			int w = (r + MD[d][0]) * m->C + c + MD[d][1];
			if (dist[w] >= 0) continue;
			dist[w] = dist[v] + 1; prev[w] = v; q[qt++] = w;
		}
	}
	return n;
}

static void mz_center(const Maze *m, int v, int jit, int *x, int *y) {
	*x = m->gx + (v % m->C) * m->cs + m->cs / 2 + (jit ? rnd(2 * jit + 1) - jit : 0);
	*y = m->gy + (v / m->C) * m->cs + m->cs / 2 + (jit ? rnd(2 * jit + 1) - jit : 0);
}

static int said_wall(GameCtx *g) { return strstr(stub_say(g), "wall") && strstr(stub_say(g), "wiped"); }

static void test_maze(int level, int games) {
	static const int ROWS[3] = { 16, 26, 36 };
	int won = 0, rejected = 0, kept = 0, deadends = 0, minR = 99, maxR = 0, minC = 99, maxC = 0, mincs = 9999, longest = 0;
	static int dist[2500], prev[2500], path[2500];
	for (int gi = 0; gi < games; gi++) {
		GameCtx *g = new_game(&GAME_maze, level);
		Maze m;
		if (!read_maze(&m)) { FAIL("could not read the maze from the wall lines"); finish(g, "maze"); continue; }
		if (m.R < minR) minR = m.R;
		if (m.R > maxR) maxR = m.R;
		if (m.C < minC) minC = m.C;
		if (m.C > maxC) maxC = m.C;
		if (m.cs < mincs) mincs = m.cs;
		if (AW == 1780 && AH == 2040 && m.R != ROWS[level]) FAIL("%d rows on %s", m.R, LV[level]);
		/* outer wall closed except the entrance (top of the first cell) and the exit (bottom of the last) */
		for (int c = 0; c < m.C; c++) {
			if (m.h[0][c] != (c != 0)) FAIL("top wall wrong at column %d", c);
			if (m.h[m.R][c] != (c != m.C - 1)) FAIL("bottom wall wrong at column %d", c);
		}
		for (int r = 0; r < m.R; r++) if (!m.v[r][0] || !m.v[r][m.C]) FAIL("side wall open at row %d", r);
		/* perfect: connected, and exactly cells - 1 passages */
		int passages = 0;
		for (int r = 0; r < m.R; r++)
			for (int c = 0; c < m.C; c++) passages += mz_open(&m, r, c, 0, 1) + mz_open(&m, r, c, 1, 0);
		int reach = mz_bfs(&m, 0, dist, prev), N = m.R * m.C;
		if (reach != N) FAIL("only %d of %d cells reachable", reach, N);
		if (passages != N - 1) FAIL("%d passages for %d cells (not a perfect maze)", passages, N);
		if (reach != N || passages != N - 1) { finish(g, "maze"); continue; }
		int L = 0;
		for (int v = N - 1; v >= 0; v = prev[v]) path[L++] = v;
		for (int i = 0; i < L / 2; i++) { int t = path[i]; path[i] = path[L - 1 - i]; path[L - 1 - i] = t; }
		if (L > longest) longest = L;
		int jit = m.cs / 6, px[600], py[600];

		/* 1. a stroke through a wall is wiped and doesn't count */
		for (int tries = 0; tries < 3; tries++) {
			int r = rnd(m.R), c = rnd(m.C - 1);
			if (mz_open(&m, r, c, 0, 1)) continue;
			int x0, y0, x1, y1;
			mz_center(&m, r * m.C + c, jit, &x0, &y0);
			mz_center(&m, r * m.C + c + 1, jit, &x1, &y1);
			stub_line(g, x0, y0, x1, y1, 30);
			if (!said_wall(g) || !count_type(C_CLEAR) || !count_type(C_CIRCLE)) FAIL("wall-crossing stroke not rejected (say: %s)", stub_say(g));
			else rejected++;
			int redrawn = 0, wx = m.gx + (c + 1) * m.cs, wy = m.gy + r * m.cs + m.cs / 2, last_clear = -1;
			for (int k = 0; k < stub_ncalls(); k++) if (stub_call(k)->type == C_CLEAR) last_clear = k;
			for (int k = last_clear + 1; k < stub_ncalls(); k++) {
				const Call *cl = stub_call(k);
				if (cl->type == C_LINE && cl->a[0] == wx && cl->a[2] == wx && (cl->a[1] - wy) * (cl->a[3] - wy) < 0) redrawn = 1;
			}
			if (!redrawn) FAIL("the wall under a wiped stroke was not redrawn");
			break;
		}
		/* 2. coming in through the side wall is rejected */
		{
			int r = rnd(m.R), x1, y1;
			mz_center(&m, r * m.C, 0, &x1, &y1);
			stub_line(g, m.gx - m.cs / 2 > AX ? m.gx - m.cs / 2 : AX, y1, x1, y1, 20);
			if (!said_wall(g)) FAIL("stroke through the outer wall not rejected (say: %s)", stub_say(g));
			else rejected++;
		}
		/* 3. a straight cheat from Start to Finish is rejected */
		{
			int x0, y0, x1, y1;
			mz_center(&m, 0, 0, &x0, &y0);
			mz_center(&m, N - 1, 0, &x1, &y1);
			stub_line(g, x0, y0, x1, y1, 200);
			if (!said_wall(g) || stub_over(g)) FAIL("straight Start-Finish line not rejected (say: %s)", stub_say(g));
			else rejected++;
		}
		/* 4. a dead-end branch (valid, kept, no win) */
		{
			int b = -1;
			for (int tries = 0; tries < 50 && b < 0; tries++) {
				int v = path[1 + rnd(L - 2)];
				for (int d = 0; d < 4; d++) {
					int r = v / m.C, c = v % m.C;
					if (!mz_open(&m, r, c, MD[d][0], MD[d][1])) continue;
					int w = (r + MD[d][0]) * m.C + c + MD[d][1];
					int on = 0;
					for (int i = 0; i < L; i++) on |= path[i] == w;
					if (!on) { b = v; px[0] = w; break; }
				}
			}
			if (b >= 0) { /* walk from b into the side branch for a few cells */
				int n = 0, cur = px[0], from = b, cells[20];
				cells[n++] = b; cells[n++] = cur;
				while (n < 12) {
					int nxt = -1, r = cur / m.C, c = cur % m.C;
					for (int d = 0; d < 4; d++) {
						if (!mz_open(&m, r, c, MD[d][0], MD[d][1])) continue;
						int w = (r + MD[d][0]) * m.C + c + MD[d][1];
						if (w != from) { nxt = w; break; }
					}
					if (nxt < 0) break;
					from = cur; cur = nxt; cells[n++] = cur;
				}
				for (int i = 0; i < n; i++) mz_center(&m, cells[i], jit, &px[i], &py[i]);
				stroke_path(g, px, py, n, 4);
				if (said_wall(g) || stub_over(g)) FAIL("dead-end stroke rejected or won (say: %s)", stub_say(g));
				else deadends++;
			}
		}
		/* 5. the solution in chunks (some drawn backwards), entering above Start and leaving below Finish */
		int i = 0, ok = 1;
		while (i < L - 1 && ok) {
			int len = 3 + rnd(level == 0 ? 10 : 40), j = i + len < L - 1 ? i + len : L - 1, n = 0;
			if (i == 0) { px[n] = m.gx + m.cs / 2; py[n] = m.gy - m.cs / 3 > AY ? m.gy - m.cs / 3 : AY; n++; }
			for (int k = i; k <= j && n < 590; k++) { mz_center(&m, path[k], jit, &px[n], &py[n]); n++; }
			if (j == L - 1) { px[n] = m.gx + m.C * m.cs - m.cs / 2; py[n] = m.gy + m.R * m.cs + m.cs / 3; n++; }
			if (rnd(3) == 0) for (int a = 0; a < n / 2; a++) { int t = px[a]; px[a] = px[n - 1 - a]; px[n - 1 - a] = t; t = py[a]; py[a] = py[n - 1 - a]; py[n - 1 - a] = t; }
			if (stub_over(g)) { FAIL("won before the path was complete"); ok = 0; break; }
			stroke_path(g, px, py, n, 2 + rnd(6));
			if (said_wall(g)) { FAIL("valid path stroke %d-%d rejected (say: %s)", i, j, stub_say(g)); ok = 0; }
			else kept++;
			i = j;
		}
		if (ok) {
			if (!stub_over(g) || !strstr(stub_result(g), "made it")) FAIL("complete path but not won (result '%s', say '%s')", stub_result(g), stub_say(g));
			else won++;
		}
		finish(g, "maze");
	}
	printf("  maze %-6s %4d games: %d-%dx%d-%d cells of %d+ px, all perfect (every cell reachable, cells-1 passages), path up to %d cells; "
	       "%d won by drawing the solution in %d strokes; %d wall-crossing strokes rejected; %d dead-end strokes kept\n",
	       LV[level], games, minC, maxC, minR, maxR, mincs, longest, won, kept, rejected, deadends);
}

/* ======================= Nonogram ======================= */
typedef struct { int n; unsigned char sol[15][15], cell[15][15]; } NonoHead; /* the first fields of nonogram.c's state */

static int nn_runs(const unsigned char *v, int step, int n, int *out) {
	int k = 0, len = 0;
	for (int i = 0; i < n; i++) {
		if (v[i * step] == 1) len++;
		else if (len) { out[k++] = len; len = 0; }
	}
	if (len) out[k++] = len;
	return k;
}

/* settle one line by trying every arrangement (independent of the game's solver); -1 = none fits */
static int nn_cnt, nn_and[15], nn_or[15];
static void nn_rec(const int *cl, int k, int j, int pos, int n, const unsigned char *known, unsigned char *cur) {
	if (j == k) {
		for (int t = pos; t < n; t++) { if (known[t] == 1) return; cur[t] = 0; }
		for (int t = 0; t < n; t++) { if (known[t] == 2 && cur[t]) return; if (known[t] == 1 && !cur[t]) return; }
		nn_cnt++;
		for (int t = 0; t < n; t++) { nn_and[t] &= cur[t]; nn_or[t] |= cur[t]; }
		return;
	}
	int rest = 0;
	for (int q = j + 1; q < k; q++) rest += cl[q] + 1;
	for (int st = pos; st + cl[j] + rest <= n; st++) {
		int bad = 0;
		for (int t = pos; t < st; t++) { cur[t] = 0; if (known[t] == 1) bad = 1; }
		if (bad) return; /* a filled square was skipped */
		for (int t = st; t < st + cl[j]; t++) cur[t] = 1;
		if (st + cl[j] < n) cur[st + cl[j]] = 0;
		nn_rec(cl, k, j + 1, st + cl[j] + 1, n, known, cur);
	}
}
static int nn_line(const int *cl, int k, unsigned char *cells, int n) {
	unsigned char cur[16];
	nn_cnt = 0;
	for (int t = 0; t < n; t++) { nn_and[t] = 1; nn_or[t] = 0; }
	nn_rec(cl, k, 0, 0, n, cells, cur);
	if (!nn_cnt) return -1;
	int set = 0;
	for (int t = 0; t < n; t++) {
		if (cells[t]) continue;
		if (nn_and[t]) { cells[t] = 1; set++; }
		else if (!nn_or[t]) { cells[t] = 2; set++; }
	}
	return set;
}

static void test_nonogram(int level, int games) {
	static const int N[3] = { 8, 10, 15 };
	int solved = 0, linesolvable = 0, maxk = 0, taps = 0, lines = 0, marks = 0, fills = 0, cells = 0, mincs = 9999, minpx = 9999;
	for (int gi = 0; gi < games; gi++) {
		GameCtx *g = new_game(&GAME_nonogram, level);
		const NonoHead *h = stub_game_state(g);
		int n = h->n;
		if (n != N[level]) { FAIL("grid %d on %s", n, LV[level]); finish(g, "nonogram"); continue; }
		/* clues from the screen: row clue boxes are taller than wide, column clue boxes wider than tall */
		int gx = 1 << 30, gy = 1 << 30, cs = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (t->a[2] < t->a[3]) { if (t->a[1] < gy) gy = t->a[1]; cs = t->a[3]; }
			else if (t->a[0] < gx) gx = t->a[0];
			if (t->a[4] < minpx) minpx = t->a[4];
		}
		if (cs < mincs) mincs = cs;
		int rc[15][15], rn[15] = { 0 }, rx[15][15], cc[15][15], cn[15] = { 0 }, cy[15][15], bad = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			int v = atoi(t->text);
			if (t->a[2] < t->a[3]) {
				int r = (t->a[1] - gy) / cs;
				if (r < 0 || r >= n || (t->a[1] - gy) % cs || rn[r] >= 15 || t->a[0] + t->a[2] > gx) { bad = 1; continue; }
				int k = rn[r]++;
				while (k > 0 && rx[r][k - 1] > t->a[0]) { rx[r][k] = rx[r][k - 1]; rc[r][k] = rc[r][k - 1]; k--; }
				rx[r][k] = t->a[0]; rc[r][k] = v;
			} else {
				int c = (t->a[0] - gx) / cs;
				if (c < 0 || c >= n || (t->a[0] - gx) % cs || cn[c] >= 15 || t->a[1] + t->a[3] > gy) { bad = 1; continue; }
				int k = cn[c]++;
				while (k > 0 && cy[c][k - 1] > t->a[1]) { cy[c][k] = cy[c][k - 1]; cc[c][k] = cc[c][k - 1]; k--; }
				cy[c][k] = t->a[1]; cc[c][k] = v;
			}
		}
		if (bad) FAIL("clue boxes out of place");
		/* the clues match the solution (an empty line shows a single 0) */
		int want[15];
		for (int i = 0; i < n; i++) {
			int k = nn_runs(&h->sol[i][0], 1, n, want), ok = k ? rn[i] == k : rn[i] == 1 && rc[i][0] == 0;
			for (int j = 0; ok && j < k; j++) ok = rc[i][j] == want[j];
			if (!ok) FAIL("row %d clues don't match the solution", i);
			if (k > maxk) maxk = k;
			k = nn_runs(&h->sol[0][i], 15, n, want), ok = k ? cn[i] == k : cn[i] == 1 && cc[i][0] == 0;
			for (int j = 0; ok && j < k; j++) ok = cc[i][j] == want[j];
			if (!ok) FAIL("column %d clues don't match the solution", i);
			if (k > maxk) maxk = k;
		}
		/* unique: solvable line by line (by brute force over each line) */
		unsigned char gr[15][15], line[15];
		memset(gr, 0, sizeof gr);
		for (int changed = 1; changed;) {
			changed = 0;
			for (int r = 0; r < n; r++) { int k = nn_line(rc[r], rc[r][0] ? rn[r] : 0, gr[r], n); if (k > 0) changed = 1; }
			for (int c = 0; c < n; c++) {
				for (int r = 0; r < n; r++) line[r] = gr[r][c];
				int k = nn_line(cc[c], cc[c][0] ? cn[c] : 0, line, n);
				if (k > 0) changed = 1;
				for (int r = 0; r < n; r++) gr[r][c] = line[r];
			}
		}
		int unknown = 0, wrong = 0;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) { unknown += !gr[r][c]; wrong += gr[r][c] && (gr[r][c] == 1) != h->sol[r][c]; }
		if (wrong) FAIL("line logic contradicts the solution");
		if (!unknown) linesolvable++;
		else if (level < 2) FAIL("a %s picture is not solvable line by line (%d unknown)", LV[level], unknown);
		/* play: fill the solution with taps and row/column lines, mark some empty squares, make some mistakes */
		int todo = 0;
		for (int r = 0; r < n; r++) for (int c = 0; c < n; c++) { todo += h->sol[r][c]; cells++; }
		fills += todo;
		unsigned char st[15][15];
		memset(st, 0, sizeof st);
		int guard = 0;
		while (todo > 0 && guard++ < 2000) {
			if (stub_over(g)) { FAIL("solved before every square was filled"); break; }
			int r = rnd(n), c = rnd(n);
			if (rnd(2)) { /* half the time, one of the squares still to fill */
				int pick = rnd(todo);
				for (int q = 0; q < n * n; q++) if (h->sol[q / n][q % n] && !st[q / n][q % n] && pick-- == 0) { r = q / n; c = q % n; break; }
			}
			int x = gx + c * cs + cs / 2, y = gy + r * cs + cs / 2, act = rnd(10);
			if (act == 0 && !h->sol[r][c] && st[r][c] != 2) { /* an X in an empty square */
				short xs[2] = { (short)(x - cs / 4), (short)(x + cs / 4) }, ys[2] = { (short)(y - cs / 4), (short)(y + cs / 4) };
				stub_stroke(g, xs, ys, 2, 0, 0);
				if (!count_type(C_CROSS)) FAIL("no X drawn for a mark");
				st[r][c] = 2; marks++;
			} else if (act == 1 && st[r][c] == 1) { /* a mistake: unfill and refill */
				stub_tap(g, x, y, rnd(2));
				stub_tap(g, x, y, 0);
				taps += 2;
			} else if (act <= 3 && h->sol[r][c] && !st[r][c]) { /* a line over the run of filled squares in this row */
				int c0 = c, c1 = c;
				while (c0 > 0 && h->sol[r][c0 - 1] && st[r][c0 - 1] == 0) c0--;
				while (c1 < n - 1 && h->sol[r][c1 + 1] && st[r][c1 + 1] == 0) c1++;
				if (c1 - c0 < 2) continue;
				stub_line(g, gx + c0 * cs + cs / 3, y + rnd(cs / 5), gx + c1 * cs + cs * 2 / 3, y - rnd(cs / 5), 20);
				for (int k = c0; k <= c1; k++) { st[r][k] = 1; todo--; }
				lines++;
			} else if (h->sol[r][c] && !st[r][c]) {
				stub_tap(g, x, y, rnd(4) == 0);
				if (!count_type(C_BOX)) FAIL("tap drew no filled square");
				st[r][c] = 1; todo--; taps++;
			}
		}
		if (!stub_over(g) || !strstr(stub_result(g), "Solved")) {
			FAIL("solution filled but not solved (result '%s', say '%s')", stub_result(g), stub_say(g));
			for (int r = 0; r < n; r++)
				for (int c = 0; c < n; c++)
					if ((h->cell[r][c] == 1) != h->sol[r][c]) printf("    square %d,%d: game %d, solution %d, test %d\n", r, c, h->cell[r][c], h->sol[r][c], st[r][c]);
		}
		else solved++;
		finish(g, "nonogram");
	}
	printf("  nonogram %-6s %4d games: %dx%d, cells >= %d px, clue text >= %d px, up to %d runs per line; clues match the solution; "
	       "%d of %d solvable line by line (unique); %d solved (%.0f%% filled; %d taps, %d run lines, %d X marks)\n",
	       LV[level], games, N[level], N[level], mincs, minpx, maxk, linesolvable, games, solved, cells ? 100.0 * fills / cells : 0, taps, lines, marks);
}

/* ======================= Cryptogram ======================= */
/* many starts (no play): every puzzle is solvable line by line from its own clues (Easy/Medium always), and the
 * solutions are nearly all different (pictures, one game in three on Easy/Medium, do repeat) */
static void test_nonogram_variety(int level, int starts) {
	static unsigned long long seen[4000];
	int distinct = 0, solvable = 0;
	long filled = 0;
	if (starts > 4000) starts = 4000;
	for (int gi = 0; gi < starts; gi++) {
		GameCtx *g = new_game(&GAME_nonogram, level);
		const NonoHead *h = stub_game_state(g);
		int n = h->n, rc[15][15], rn[15], cc[15][15], cn[15];
		unsigned long long hash = 1469598103934665603ULL;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) { hash = (hash ^ h->sol[r][c]) * 1099511628211ULL; filled += h->sol[r][c]; }
		int dup = 0;
		for (int i = 0; i < distinct && !dup; i++) dup = seen[i] == hash;
		if (!dup) seen[distinct++] = hash;
		for (int i = 0; i < n; i++) { rn[i] = nn_runs(&h->sol[i][0], 1, n, rc[i]); cn[i] = nn_runs(&h->sol[0][i], 15, n, cc[i]); }
		unsigned char gr[15][15], line[15];
		memset(gr, 0, sizeof gr);
		for (int changed = 1; changed;) {
			changed = 0;
			for (int r = 0; r < n; r++) if (nn_line(rc[r], rn[r], gr[r], n) > 0) changed = 1;
			for (int c = 0; c < n; c++) {
				for (int r = 0; r < n; r++) line[r] = gr[r][c];
				if (nn_line(cc[c], cn[c], line, n) > 0) changed = 1;
				for (int r = 0; r < n; r++) gr[r][c] = line[r];
			}
		}
		int unknown = 0;
		for (int r = 0; r < n; r++)
			for (int c = 0; c < n; c++) unknown += !gr[r][c];
		if (!unknown) solvable++;
		else if (level < 2) FAIL("%s puzzle not solvable line by line", LV[level]);
		finish(g, "nonogram");
	}
	int want = level < 2 ? starts / 2 : starts * 95 / 100, pics = 0;
	if (distinct < want) FAIL("%s: only %d different puzzles in %d", LV[level], distinct, starts);
	if (level < 2) { /* the picture bag in the store (by name hash): every picture once per round, none back within 3 */
		GameCtx *g = stub_new(&GAME_nonogram, level, 99, AX, AY, AW, AH);
		struct { unsigned magic, nused, nlast, lpos, last[128], used[1024]; } bag;
		if (g_load(g, level ? "pictures10" : "pictures8", &bag, sizeof bag) != (int)sizeof bag || bag.nlast < 30) FAIL("%s: no picture bag in the store", LV[level]);
		else {
			unsigned seq[128], kinds[128];
			int nk = 0;
			for (unsigned k = 0; k < bag.nlast; k++) seq[k] = bag.last[(bag.lpos + 128 - bag.nlast + k) % 128];
			for (unsigned k = 0; k < bag.nlast; k++) {
				for (unsigned j = k + 1; j < bag.nlast && j <= k + 3; j++) if (seq[j] == seq[k]) FAIL("%s: a picture came back after %u others", LV[level], j - k - 1);
				int seen = 0;
				for (int i = 0; i < nk; i++) seen |= kinds[i] == seq[k];
				if (!seen) kinds[nk++] = seq[k];
			}
			pics = nk;
			if (nk != 11) FAIL("%s: %d different pictures in the last %u", LV[level], nk, bag.nlast);
		}
		stub_store_put(g, level ? "pictures10" : "pictures8", "bad", 3); /* a damaged bag is ignored */
		stub_free(g);
		for (int i = 0; i < 9; i++) { g = new_game(&GAME_nonogram, level); finish(g, "nonogram"); }
	}
	printf("  nonogram %-6s %4d starts: %d different solutions, %d solvable line by line, %.0f%% filled; %d pictures in rotation\n", LV[level],
	       starts, distinct, solvable, starts ? 100.0 * filled / starts / (level == 0 ? 64 : level == 1 ? 100 : 225) : 0, pics);
}

typedef struct { char plain[121]; char enc[26]; } CryptoHead; /* the first fields of cryptogram.c's state */

static void test_cryptogram(int level, int games) {
	static const int GIVE_MIN[3] = { 3, 1, 0 }, GIVE_MAX[3] = { 4, 2, 0 };
	static unsigned seen_quote[4096];
	int solved = 0, mistakes = 0, erases = 0, minbw = 9999, maxlen = 0, distinct = 0, givens = 0;
	for (int gi = 0; gi < games; gi++) {
		GameCtx *g = new_game(&GAME_cryptogram, level);
		const CryptoHead *h = stub_game_state(g);
		int len = (int)strlen(h->plain), inv[26], ok = 1;
		if (len > maxlen) maxlen = len;
		/* the swap is one-to-one and no letter stands for itself */
		for (int i = 0; i < 26; i++) inv[i] = -1;
		for (int i = 0; i < 26; i++) {
			int c = h->enc[i] - 'A';
			if (c < 0 || c >= 26 || inv[c] >= 0) { FAIL("enc is not a permutation"); ok = 0; break; }
			inv[c] = i;
			if (c == i) { FAIL("%c stands for itself", 'A' + i); ok = 0; }
		}
		for (int i = 0; i < len; i++)
			if (!((h->plain[i] >= 'A' && h->plain[i] <= 'Z') || strchr(" .,;:'!?-", h->plain[i]))) { FAIL("odd character '%c' in '%s'", h->plain[i], h->plain); ok = 0; }
		unsigned hash = 0;
		for (int i = 0; i < len; i++) hash = hash * 31 + (unsigned char)h->plain[i];
		int seen = 0;
		for (int i = 0; i < distinct && !seen; i++) seen = seen_quote[i] == hash;
		if (!seen && distinct < 4096) seen_quote[distinct++] = hash;
		if (!ok) { finish(g, "cryptogram"); continue; }
		/* boxes (square frames above the keypad) in reading order = the quote's letters */
		const Call *erase = 0;
		for (int i = 0; i < stub_ntexts(); i++) if (!strcmp(stub_text(i)->text, "Erase")) erase = stub_text(i);
		if (!erase) { FAIL("no Erase key"); finish(g, "cryptogram"); continue; }
		int kh = erase->a[3], keytop = erase->a[1] - 2 * (kh + 10);
		int bx[121], by[121], nb = 0, bw = 0;
		for (int i = 0; i < stub_ncalls(); i++) {
			const Call *c = stub_call(i);
			if (c->type == C_FRAME && c->a[2] == c->a[3] && c->a[1] < keytop && nb < 121) { bx[nb] = c->a[0]; by[nb] = c->a[1]; bw = c->a[2]; nb++; }
		}
		if (bw < minbw) minbw = bw;
		for (int i = 1; i < nb; i++) /* sort by row, then x */
			for (int j = i; j > 0 && (by[j] < by[j - 1] || (by[j] == by[j - 1] && bx[j] < bx[j - 1])); j--) {
				int t = bx[j]; bx[j] = bx[j - 1]; bx[j - 1] = t; t = by[j]; by[j] = by[j - 1]; by[j - 1] = t;
			}
		int letters = 0, li[121];
		for (int i = 0; i < len; i++) if (h->plain[i] >= 'A' && h->plain[i] <= 'Z') li[letters++] = i;
		if (nb != letters) { FAIL("%d boxes for %d letters", nb, letters); finish(g, "cryptogram"); continue; }
		/* each box's small code letter decodes to the quote; count the given letters */
		int given[26] = { 0 }, ngiven = 0, bad = 0;
		for (int b = 0; b < nb; b++) {
			char code = 0, shown = 0;
			for (int i = 0; i < stub_ntexts(); i++) {
				const Call *t = stub_text(i);
				if (strlen(t->text) != 1 || t->a[0] != bx[b] || t->a[2] != bw) continue;
				if (t->a[1] == by[b] + bw + 2) code = t->text[0];
				else if (t->a[1] == by[b]) { shown = t->text[0]; if (!t->a[5]) bad = 1; }
			}
			char p = h->plain[li[b]];
			if (!code || inv[code - 'A'] != p - 'A') { FAIL("box %d: code letter '%c' does not decode to '%c'", b, code ? code : '?', p); bad = 1; break; }
			if (shown) {
				if (shown != p) { FAIL("given letter %c should be %c", shown, p); bad = 1; }
				if (!given[code - 'A']++) ngiven++;
			}
		}
		if (bad) { finish(g, "cryptogram"); continue; }
		givens += ngiven;
		if (ngiven < GIVE_MIN[level] || ngiven > GIVE_MAX[level]) FAIL("%d letters given on %s", ngiven, LV[level]);
		/* keys */
		int kx[27], ky[27];
		for (int i = 0; i < 27; i++) kx[i] = -1;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (t->a[1] < keytop) continue;
			int k = !strcmp(t->text, "Erase") ? 26 : strlen(t->text) == 1 ? t->text[0] - 'A' : -1;
			if (k >= 0 && k < 27) { kx[k] = t->a[0] + t->a[2] / 2; ky[k] = t->a[1] + t->a[3] / 2; }
		}
		for (int i = 0; i < 27; i++) if (kx[i] < 0) { FAIL("key %d missing", i); bad = 1; }
		if (bad) { finish(g, "cryptogram"); continue; }
		/* a given letter can't be selected */
		for (int b = 0; b < nb; b++) {
			int code = h->enc[h->plain[li[b]] - 'A'] - 'A';
			if (!given[code]) continue;
			stub_tap(g, bx[b] + bw / 2, by[b] + bw / 2, 0);
			if (!strstr(stub_say(g), "given")) FAIL("tapping a given letter: say '%s'", stub_say(g));
			break;
		}
		/* solve: each code letter once (sometimes wrong first, sometimes erased and redone) */
		int order[121];
		for (int b = 0; b < nb; b++) order[b] = b;
		for (int i = nb - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
		int done[26] = { 0 };
		for (int e = 0; e < nb; e++) {
			int b = order[e], p = h->plain[li[b]] - 'A', code = h->enc[p] - 'A';
			if (given[code] || done[code]) continue;
			done[code] = 1;
			int left = 0;
			for (int q = 0; q < nb; q++) { int cq = h->enc[h->plain[li[q]] - 'A'] - 'A'; left += !given[cq] && !done[cq]; }
			if (stub_over(g)) { FAIL("solved too early"); break; }
			stub_tap(g, bx[b] + bw / 2, by[b] + bw / 2, rnd(3) == 0);
			if (rnd(5) == 0) { /* a wrong guess first */
				int w = (p + 1 + rnd(25)) % 26, wcode = h->enc[w] - 'A';
				if (!given[wcode]) {
					stub_tap(g, kx[w], ky[w], 0);
					mistakes++;
					if (stub_over(g)) FAIL("a wrong letter solved it");
					stub_tap(g, bx[b] + bw / 2, by[b] + bw / 2, 0);
					if (rnd(2)) { stub_tap(g, kx[26], ky[26], 0); erases++; if (strncmp(stub_say(g), "erased", 6)) FAIL("erase: say '%s'", stub_say(g)); stub_tap(g, bx[b] + bw / 2, by[b] + bw / 2, 0); }
				}
			}
			stub_tap(g, kx[p], ky[p], rnd(3) == 0);
			(void)left;
		}
		/* letters moved away by a wrong guess may need putting back */
		for (int pass = 0; pass < 3 && !stub_over(g); pass++)
			for (int b = 0; b < nb && !stub_over(g); b++) {
				int p = h->plain[li[b]] - 'A', code = h->enc[p] - 'A';
				if (given[code]) continue;
				stub_tap(g, bx[b] + bw / 2, by[b] + bw / 2, 0);
				stub_tap(g, kx[p], ky[p], 0);
			}
		if (!stub_over(g) || !strstr(stub_result(g), "Solved")) FAIL("not solved (say '%s')", stub_say(g));
		else {
			int author = 0;
			for (int i = 0; i < stub_ntexts(); i++) author |= !strncmp(stub_text(i)->text, "- ", 2);
			if (!author) FAIL("author not shown");
			solved++;
		}
		finish(g, "cryptogram");
	}
	printf("  cryptogram %-6s %4d games: %d different quotes, up to %d characters, boxes >= %d px; each code letter decodes to the quote, "
	       "no letter stands for itself; %.1f letters given per game; %d solved by taps (%d wrong guesses, %d erases)\n",
	       LV[level], games, distinct, maxlen, minbw, games ? (double)givens / games : 0, solved, mistakes, erases);
}

/* many starts (no play): quotes/proverbs don't come back within 100 of them and nearly all get played; made-up
 * sentences are well formed and almost never repeat */
typedef struct { char plain[121]; char enc[26]; int q; } CryptoHead3;
static void test_cryptogram_variety(int starts) {
	static int last_at[2048];
	static unsigned made[20000];
	int nfixed = 0, nmade = 0, dmade = 0, dfixed = 0, mingap = 1 << 30, maxq = -1;
	char sample[3][121] = { "", "", "" };
	for (int i = 0; i < 2048; i++) last_at[i] = -1;
	for (int gi = 0; gi < starts; gi++) {
		GameCtx *g = new_game(&GAME_cryptogram, gi % 3);
		const CryptoHead3 *h = stub_game_state(g);
		int len = (int)strlen(h->plain);
		if (h->q >= 0) {
			if (h->q >= 2048) { FAIL("quote index %d", h->q); finish(g, "cryptogram"); continue; }
			if (last_at[h->q] >= 0 && nfixed - last_at[h->q] < mingap) mingap = nfixed - last_at[h->q];
			if (last_at[h->q] < 0) dfixed++;
			last_at[h->q] = nfixed++;
			if (h->q > maxq) maxq = h->q;
		} else {
			int bad = len < 10 || len > 120 || (h->plain[len - 1] != '.' && h->plain[len - 1] != '?') || h->plain[0] == ' ' || strstr(h->plain, "  ") || strchr(h->plain, '{');
			if (bad) FAIL("odd made-up sentence '%s'", h->plain);
			unsigned hash = 0;
			for (int i = 0; i < len; i++) hash = hash * 31 + (unsigned char)h->plain[i];
			int seen = 0;
			for (int i = 0; i < dmade && !seen; i++) seen = made[i] == hash;
			if (!seen && dmade < 20000) made[dmade++] = hash;
			if (nmade < 3) snprintf(sample[nmade], sizeof sample[0], "%s", h->plain);
			nmade++;
		}
		finish(g, "cryptogram");
	}
	if (nmade < starts / 3 || nfixed < starts / 3) FAIL("mix: %d quotes, %d made-up sentences", nfixed, nmade);
	if (dmade < nmade * 98 / 100) FAIL("only %d different made-up sentences in %d", dmade, nmade);
	if (mingap < 100) FAIL("a quote came back after only %d others", mingap);
	if (nfixed >= 2 * (maxq + 1) && dfixed != maxq + 1) FAIL("only %d of %d quotes played in %d", dfixed, maxq + 1, nfixed);
	/* the bag lives in the store (the game keeps no memory of its own, so this is what an app restart keeps): it is
	 * there, holds this round's texts as hashes, and a damaged one is ignored */
	GameCtx *g = new_game(&GAME_cryptogram, 0);
	struct { unsigned magic, nused, nlast, lpos, last[128], used[1024]; } bag;
	if (g_load(g, "texts", &bag, sizeof bag) != (int)sizeof bag || !bag.nused || bag.nused > (unsigned)maxq + 1 || bag.nlast != 128)
		FAIL("cryptogram: the text bag is not in the store as expected");
	finish(g, "cryptogram");
	for (int k = 0; k < 2; k++) {
		g = stub_new(&GAME_cryptogram, 0, 777 + (unsigned)k, AX, AY, AW, AH);
		stub_store_put(g, "texts", "garbage!", k ? 8 : 0);
		stub_free(g);
		for (int i = 0; i < 6; i++) { /* a few starts, so that some take a quote */
			g = new_game(&GAME_cryptogram, 0);
			const CryptoHead3 *h = stub_game_state(g);
			if (strlen(h->plain) < 5 || h->q >= 2048) FAIL("cryptogram: odd start after a damaged bag");
			finish(g, "cryptogram");
		}
	}
	printf("  cryptogram %d starts: %d quotes/proverbs (%d different of %d, none back within %d), %d made-up sentences (%d different), e.g. "
	       "'%s' / '%s' / '%s'\n", starts, nfixed, dfixed, maxq + 1, mingap == 1 << 30 ? 0 : mingap, nmade, dmade, sample[0], sample[1], sample[2]);
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
#define RUN(name, def, call) if ((!only || !strcmp(only, name)) && &def) call
			/* word search: on the first area, at least one puzzle of every theme on every level */
			int ws = (quick ? 30 : 200) / div;
			if (a == 0 && ws < NTHEMES) ws = NTHEMES;
			RUN("wordsearch", GAME_wordsearch, test_wordsearch(lv, ws));
			RUN("maze", GAME_maze, test_maze(lv, (quick ? 20 : 120) / div));
			RUN("nonogram", GAME_nonogram, test_nonogram(lv, (quick ? 30 : 200) / div));
			if (a == 0) RUN("nonogram", GAME_nonogram, test_nonogram_variety(lv, (quick ? 300 : 1000) / (lv == 2 ? 4 : 1)));
			RUN("cryptogram", GAME_cryptogram, test_cryptogram(lv, (quick ? 60 : 400) / div));
		}
		if (a == 0) RUN("cryptogram", GAME_cryptogram, test_cryptogram_variety(quick ? 1500 : 6000));
	}
	AX = AREAS[0][0]; AY = AREAS[0][1]; AW = AREAS[0][2]; AH = AREAS[0][3];
	RUN("wordsearch", GAME_wordsearch, test_ws_bag());
	RUN("wordsearch", GAME_wordsearch, test_ws_resume());
	printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
	return failures != 0;
}

/* Host tests for Hangman, Minesweeper, Sudoku, Memory, Word Search, Code Breaker and Sliding Puzzle (agent B).
 * Plays each game many times on every level through the stub engine (stub_b.c), reading the board back from the
 * logged drawing calls, and checks that games end and that the rules hold.
 * Build: gcc -O1 -fsanitize=address,undefined -I device/app device/app/games/tests/stub_b.c \
 *        device/app/games/tests/test_b.c device/app/games/{dict_words,hangman,minesweeper,sudoku,memory,wordsearch,codebreaker,sliding}.c -lm
 * Run: ./a.out [quick|full] [game] */
#include "games.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- stub interface (stub_b.c) --- */
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
int stub_timer_pending(GameCtx *g);
void stub_set_busy(GameCtx *g, int b);
void stub_set_auto_timer(GameCtx *g, int on);
void *stub_game_state(GameCtx *g);
void stub_store_put(GameCtx *g, const char *key, const void *buf, size_t n);
GameCtx *stub_new(const GameDef *def, int level, unsigned long long seed, int ax, int ay, int aw, int ah);
void stub_free(GameCtx *g);
void stub_start(GameCtx *g);
void stub_fire_timer(GameCtx *g);
void stub_stroke(GameCtx *g, const short *xs, const short *ys, int n, int tap, int finger);
void stub_tap(GameCtx *g, int x, int y, int finger);
void stub_line(GameCtx *g, int x0, int y0, int x1, int y1, int npts);

extern const GameDef GAME_hangman, GAME_minesweeper, GAME_sudoku, GAME_memory, GAME_wordsearch, GAME_codebreaker, GAME_sliding;

static int AX = 40, AY = 200, AW = 1780, AH = 2040;
static unsigned long long trng = 88172645463325252ULL;
static int rnd(int n) { trng ^= trng << 13; trng ^= trng >> 7; trng ^= trng << 17; return (int)((trng >> 11) % (unsigned long long)n); }
static unsigned long long seed_ctr = 1000;
static const char *LV[3] = { "easy", "medium", "hard" };

static int failures;
#define FAIL(...) do { failures++; if (failures <= 40) { printf("  FAIL [%s] ", __func__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int count_type(int type) { int n = 0; for (int i = 0; i < stub_ncalls(); i++) n += stub_call(i)->type == type; return n; }
static int draw_calls(void) { int n = 0; for (int i = 0; i < stub_ncalls(); i++) n += stub_call(i)->type != C_SPEED; return n; }
static const Call *first_of(int type) { for (int i = 0; i < stub_ncalls(); i++) if (stub_call(i)->type == type) return stub_call(i); return 0; }

static void finish(GameCtx *g, const char *name) {
	if (stub_violations(g)) FAIL("%s: %ld engine-rule violations, first: %s", name, stub_violations(g), stub_first_violation(g));
	stub_free(g);
}

/* tapping while the engine is busy must not draw anything */
static void busy_probe(GameCtx *g, int x, int y, const char *name) {
	stub_set_busy(g, 1);
	stub_tap(g, x, y, 0);
	if (draw_calls() || stub_over(g)) FAIL("%s: drew while busy", name);
	stub_set_busy(g, 0);
}

static GameCtx *new_game(const GameDef *d, int level, unsigned long long *seed) {
	*seed = seed_ctr++;
	GameCtx *g = stub_new(d, level, *seed, AX, AY, AW, AH);
	stub_start(g);
	return g;
}

/* ======================= Hangman ======================= */
static void test_hangman(int level, int games) {
	int wins = 0, losses = 0, minlen = 99, maxlen = 0, replay_wins = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_hangman, level, &seed);
		int kx[26], ky[26], have = 0, ktop = 1 << 30;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1 && t->text[0] >= 'A' && t->text[0] <= 'Z') {
				int k = t->text[0] - 'A';
				kx[k] = t->a[0] + t->a[2] / 2; ky[k] = t->a[1] + t->a[3] / 2;
				if (t->a[1] < ktop) ktop = t->a[1];
				have++;
				if (t->a[2] < 90 || t->a[3] < 90) FAIL("key %c is only %dx%d", t->text[0], t->a[2], t->a[3]);
			}
		}
		if (have != 26) { FAIL("found %d keys", have); finish(g, "hangman"); continue; }
		int wordlen = count_type(C_LINE) - 5;
		char order[27];
		const char *freq = "EAIOTNSRLHDCUMPGBYFWKVXZJQ";
		memcpy(order, freq, 27);
		if (gi % 2) for (int i = 25; i > 0; i--) { int j = rnd(i + 1); char t = order[i]; order[i] = order[j]; order[j] = t; }
		else for (int i = 0; i < 4; i++) { int a = rnd(26), b = rnd(26); char t = order[a]; order[a] = order[b]; order[b] = t; }
		int misses = 0, taps = 0;
		for (int i = 0; i < 26 && !stub_over(g); i++) {
			int k = order[i] - 'A';
			if (rnd(6) == 0) busy_probe(g, kx[k], ky[k], "hangman");
			if (rnd(5) == 0) { stub_tap(g, AX + 5, AY + 5, 0); if (draw_calls()) FAIL("junk tap drew"); }
			if (i && rnd(5) == 0) { int j = order[rnd(i)] - 'A'; stub_tap(g, kx[j], ky[j], 1); if (draw_calls()) FAIL("repeated letter drew"); }
			stub_tap(g, kx[k], ky[k], rnd(2));
			taps++;
			if (!draw_calls()) FAIL("letter tap drew nothing");
			misses += count_type(C_CROSS);
		}
		const char *r = stub_result(g), *p;
		char word[32] = "";
		int won = 0;
		if (!stub_over(g)) FAIL("not over after %d letters", taps);
		else if ((p = strstr(r, "You got it: "))) { sscanf(p + 12, "%31[A-Z]", word); wins++; won = 1; if (misses >= 6) FAIL("won with %d misses", misses); }
		else if ((p = strstr(r, "the word was "))) { sscanf(p + 13, "%31[A-Z]", word); losses++; if (misses != 6) FAIL("lost with %d misses", misses); }
		else FAIL("odd result '%s'", r);
		int len = (int)strlen(word);
		if (len != wordlen) FAIL("word %s has %d letters, %d blanks drawn", word, len, wordlen);
		int lo = level == 0 ? 3 : level == 1 ? 5 : 7, hi = level == 0 ? 4 : level == 1 ? 6 : 15;
		if (len < lo || len > hi) FAIL("%s word '%s' has the wrong length", LV[level], word);
		if (len < minlen) minlen = len;
		if (len > maxlen) maxlen = len;
		/* the blanks must now show the whole word */
		char shown[32] = "";
		int xs[32], ns = 0;
		for (int i = 0; i < stub_ntexts() && ns < 31; i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1 && t->a[1] < ktop - 10) {
				int j = ns++;
				while (j > 0 && xs[j - 1] > t->a[0]) { xs[j] = xs[j - 1]; shown[j] = shown[j - 1]; j--; }
				xs[j] = t->a[0]; shown[j] = t->text[0];
			}
		}
		shown[ns] = 0;
		if (strcmp(shown, word)) FAIL("blanks show '%s', word '%s'", shown, word);
		finish(g, "hangman");
		if (!len || won) continue;
		/* after a lost game, a new one played knowing its word (read from the game's state; a replay with the same seed
		 * would get a different word, as words don't come back soon): two wrong letters, then its letters must win */
		GameCtx *h = stub_new(&GAME_hangman, level, seed + 7777777ULL, AX, AY, AW, AH);
		stub_start(h);
		snprintf(word, sizeof word, "%s", (const char *)stub_game_state(h));
		len = (int)strlen(word);
		int wrong = 0, rm = 0;
		for (int k = 0; k < 26 && wrong < 2; k++)
			if (!strchr(word, 'A' + k)) { stub_tap(h, kx[k], ky[k], 0); rm += count_type(C_CROSS); wrong++; }
		for (int i = 0; i < len && !stub_over(h); i++) stub_tap(h, kx[word[i] - 'A'], ky[word[i] - 'A'], 0);
		char want[64];
		snprintf(want, sizeof want, "You got it: %s! (2 misses)", word);
		if (rm != 2 || !stub_over(h) || strcmp(stub_result(h), want)) FAIL("replay with the word gave '%s'", stub_result(h));
		else replay_wins++;
		finish(h, "hangman replay");
	}
	printf("  hangman %-6s %4d games: %d won, %d lost (fixed letter orders), word length %d-%d; %d lost games won on replay with the word known\n",
	       LV[level], games, wins, losses, minlen, maxlen, replay_wins);
}

/* many starts (no play): every word has the level's length and only letters, no word comes back within 256 games,
 * and the number of different words shows the pool is in the thousands */
static int cmp_word(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void test_hangman_variety(int level, int starts) {
	static char w[4000][16], sorted[4000][16];
	if (starts > 4000) starts = 4000;
	int lo = level == 0 ? 3 : level == 1 ? 5 : 7, hi = level == 0 ? 4 : level == 1 ? 6 : 15, repeats = 0;
	long sumlen = 0;
	for (int i = 0; i < starts; i++) {
		GameCtx *g = stub_new(&GAME_hangman, level, 900000 + (unsigned long long)level * 10000 + (unsigned long long)i, AX, AY, AW, AH);
		stub_start(g);
		snprintf(w[i], sizeof w[i], "%s", (const char *)stub_game_state(g));
		stub_free(g);
		int len = (int)strlen(w[i]);
		sumlen += len;
		if (len < lo || len > hi) FAIL("%s word '%s' has the wrong length", LV[level], w[i]);
		for (int k = 0; k < len; k++) if (w[i][k] < 'A' || w[i][k] > 'Z') { FAIL("odd word '%s'", w[i]); break; }
		for (int j = i - 1; j >= 0 && j >= i - 255; j--) if (!strcmp(w[i], w[j])) { repeats++; break; }
	}
	memcpy(sorted, w, sizeof sorted);
	qsort(sorted, (size_t)starts, sizeof sorted[0], cmp_word);
	int distinct = starts > 0;
	for (int i = 1; i < starts; i++) distinct += strcmp(sorted[i], sorted[i - 1]) != 0;
	if (repeats) FAIL("%s: %d words came back within 256 games", LV[level], repeats);
	if (starts >= 2000 && distinct < 1000) FAIL("%s: only %d different words in %d games", LV[level], distinct, starts);
	/* the history is kept on the "Kindle" (the stub's store, which outlives the games like files outlive the app),
	 * as hashes of the words: the last word is its newest entry. A damaged record is ignored. */
	GameCtx *g = stub_new(&GAME_hangman, level, 4242, AX, AY, AW, AH);
	struct { unsigned magic, n, pos, h[256]; } rec;
	unsigned hw = 2166136261u;
	for (const char *p = w[starts - 1]; *p; p++) { hw ^= (unsigned char)*p; hw *= 16777619u; }
	if (g_load(g, "recent", &rec, sizeof rec) != (int)sizeof rec || rec.n != 256 || rec.h[(rec.pos + 255) % 256] != hw)
		FAIL("%s: the word history is not in the store as expected", LV[level]);
	stub_store_put(g, "recent", "junk", 4);
	stub_start(g);
	int len = (int)strlen((const char *)stub_game_state(g));
	if (len < lo || len > hi) FAIL("%s: no proper word after a damaged history", LV[level]);
	stub_free(g);
	printf("  hangman %-6s %4d starts: %d different words (avg length %.1f), none repeated within 256 games (history in the store)\n",
	       LV[level], starts, distinct, starts ? (double)sumlen / starts : 0.0);
}

/* ======================= Minesweeper ======================= */
static void test_minesweeper(int level, int games) {
	static const int N[3] = { 9, 12, 16 }, MINES[3] = { 10, 24, 45 };
	int n = N[level], nm = MINES[level], losses = 0, wins = 0, replays = 0, flagged_replays = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_minesweeper, level, &seed);
		const Call *b = first_of(C_BOX);
		if (!b) { FAIL("no board"); finish(g, "minesweeper"); continue; }
		int gx = b->a[0], gy = b->a[1], cell = b->a[2] / n;
		if (cell * n != b->a[2] || cell < 90) FAIL("board %d px for %d cells", b->a[2], n);
#define CX(c) (gx + (c) * cell + cell / 2)
#define CY(r) (gy + (r) * cell + cell / 2)
		int r0 = rnd(n), c0 = rnd(n);
		if (rnd(3) == 0) busy_probe(g, CX(c0), CY(r0), "minesweeper");
		stub_tap(g, CX(c0), CY(r0), rnd(2));
		if (stub_over(g)) { FAIL("first click ended the game: %s", stub_result(g)); finish(g, "minesweeper"); continue; }
		if (count_type(C_CLEAR) < 1) FAIL("first click opened nothing");
		int steps = 0;
		while (!stub_over(g) && steps++ < 100000) stub_tap(g, CX(rnd(n)), CY(rnd(n)), 0);
		if (!stub_over(g)) { FAIL("random taps never ended the game"); finish(g, "minesweeper"); continue; }
		if (strncmp(stub_result(g), "Boom", 4)) { wins++; finish(g, "minesweeper"); continue; }
		losses++;
		unsigned char mine[16][16] = { { 0 } };
		int dots = 0;
		for (int i = 0; i < stub_ncalls(); i++) {
			const Call *c = stub_call(i);
			if (c->type != C_DOT) continue;
			int mc = (c->a[0] - gx) / cell, mr = (c->a[1] - gy) / cell;
			if (mr >= 0 && mr < n && mc >= 0 && mc < n && !mine[mr][mc]) { mine[mr][mc] = 1; dots++; }
		}
		if (dots != nm) FAIL("loss showed %d mines, expected %d", dots, nm);
		for (int dr = -1; dr <= 1; dr++)
			for (int dc = -1; dc <= 1; dc++)
				if (r0 + dr >= 0 && r0 + dr < n && c0 + dc >= 0 && c0 + dc < n && mine[r0 + dr][c0 + dc]) FAIL("mine next to the first click");
		finish(g, "minesweeper");
		/* replay the same seed and first click, then open every safe square (sometimes flagging all mines first) */
		GameCtx *h = stub_new(&GAME_minesweeper, level, seed, AX, AY, AW, AH);
		stub_start(h);
		stub_tap(h, CX(c0), CY(r0), 0);
		unsigned char opened[16][16] = { { 0 } };
		int nopen = 0, flags = gi % 2;
#define TRACK_OPEN() \
	for (int i = 0; i < stub_ncalls(); i++) { \
		const Call *c = stub_call(i); \
		if (c->type != C_CLEAR) continue; \
		int oc = (c->a[0] - gx) / cell, orr = (c->a[1] - gy) / cell; \
		if (orr >= 0 && orr < n && oc >= 0 && oc < n && !opened[orr][oc]) { opened[orr][oc] = 1; nopen++; } \
	}
		TRACK_OPEN();
		if (flags) {
			flagged_replays++;
			for (int r = 0; r < n; r++)
				for (int c = 0; c < n; c++)
					if (mine[r][c]) {
						stub_line(h, CX(c) - cell / 4, CY(r) - cell / 4, CX(c) + cell / 4, CY(r) + cell / 4, 6);
						if (!count_type(C_LINE)) FAIL("flag stroke drew no flag");
						/* the second stroke of an X must not lift it */
						stub_line(h, CX(c) + cell / 4, CY(r) - cell / 4, CX(c) - cell / 4, CY(r) + cell / 4, 5);
						if (draw_calls() || stub_over(h)) FAIL("drawing on a flag did something");
						if (rnd(4) == 0) { /* a tap lifts it (without opening); plant it again */
							stub_tap(h, CX(c), CY(r), 0);
							if (count_type(C_BOX) != 1 || count_type(C_CLEAR) || stub_over(h)) FAIL("tapping a flag did not just lift it");
							stub_line(h, CX(c) - cell / 4, CY(r), CX(c) + cell / 4, CY(r), 4);
							if (!count_type(C_LINE)) FAIL("re-flagging drew no flag");
						}
					}
		}
		for (int k = 0; k < n * n * 3 && !stub_over(h); k++) {
			int r = rnd(n), c = rnd(n);
			if (mine[r][c] ? !flags : opened[r][c] && !flags) continue; /* flagged: also tap flags (lift) and numbers (open around) */
			if (mine[r][c]) { /* lift the flag and plant it again */
				stub_tap(h, CX(c), CY(r), 0);
				if (stub_over(h)) FAIL("tapping a flagged mine exploded");
				stub_line(h, CX(c), CY(r) - cell / 4, CX(c), CY(r) + cell / 4, 3);
				continue;
			}
			stub_tap(h, CX(c), CY(r), 0);
			TRACK_OPEN();
		}
		for (int r = 0; r < n && !stub_over(h); r++)
			for (int c = 0; c < n && !stub_over(h); c++)
				if (!mine[r][c] && !opened[r][c]) { stub_tap(h, CX(c), CY(r), 0); TRACK_OPEN(); }
		if (!stub_over(h) || strncmp(stub_result(h), "All clear", 9)) FAIL("replay: opening every safe square gave '%s'", stub_result(h));
		else if (nopen != n * n - nm) FAIL("replay won with %d of %d squares open", nopen, n * n - nm);
		replays++;
		finish(h, "minesweeper replay");
#undef CX
#undef CY
	}
	printf("  minesweeper %-6s %4d games: %d lost / %d won by random taps; %d replays cleared the board (%d with every mine flagged)\n",
	       LV[level], games, losses, wins, replays, flagged_replays);
}

/* ======================= Sudoku ======================= */
static int sd_ok(const int *b, int i, int v) {
	int r = i / 9, c = i % 9;
	for (int k = 0; k < 9; k++) {
		if (b[r * 9 + k] == v || b[k * 9 + c] == v) return 0;
		if (b[(r / 3 * 3 + k / 3) * 9 + c / 3 * 3 + k % 3] == v) return 0;
	}
	return 1;
}
static int sd_count(int *b, int limit, int *out) {
	int i = 0;
	while (i < 81 && b[i]) i++;
	if (i == 81) { if (out) memcpy(out, b, 81 * sizeof *b); return 1; }
	int n = 0;
	for (int v = 1; v <= 9 && n < limit; v++)
		if (sd_ok(b, i, v)) { b[i] = v; n += sd_count(b, limit - n, out); b[i] = 0; }
	return n;
}

static void test_sudoku(int level, int games) {
	int minc = 99, maxc = 0, solved = 0;
	double tmax = 0, tsum = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed = seed_ctr++;
		GameCtx *g = stub_new(&GAME_sudoku, level, seed, AX, AY, AW, AH);
		clock_t t0 = clock();
		stub_start(g);
		double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
		tsum += dt;
		if (dt > tmax) tmax = dt;
		const Call *f = 0;
		for (int i = 0; i < stub_ncalls(); i++) if (stub_call(i)->type == C_FRAME && stub_call(i)->a[2] == stub_call(i)->a[3] && stub_call(i)->a[2] > 800) f = stub_call(i);
		if (!f) { FAIL("no grid frame"); finish(g, "sudoku"); continue; }
		int gx = f->a[0], gy = f->a[1], size = f->a[2], cell = size / 9;
		int giv[81] = { 0 }, clues = 0, kx[10], ky = 0, keys = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (t->a[1] >= gy + size) { /* the pad */
				int k = !strcmp(t->text, "Erase") ? 9 : t->text[0] - '1';
				if (k >= 0 && k <= 9) { kx[k] = t->a[0] + t->a[2] / 2; ky = t->a[1] + t->a[3] / 2; keys++; }
				if (t->a[2] < 90 || t->a[3] < 90) FAIL("pad key %s is %dx%d", t->text, t->a[2], t->a[3]);
				continue;
			}
			int c = (t->a[0] - gx) / cell, r = (t->a[1] - gy) / cell;
			if (!t->a[5] || t->a[0] != gx + c * cell || t->a[1] != gy + r * cell) FAIL("given %s drawn off its square", t->text);
			giv[r * 9 + c] = t->text[0] - '0';
			clues++;
		}
		if (keys != 10) FAIL("pad has %d keys", keys);
		if (clues < minc) minc = clues;
		if (clues > maxc) maxc = clues;
		int sol[81], tmp[81];
		memcpy(tmp, giv, sizeof tmp);
		int ns = sd_count(tmp, 2, sol);
		if (ns != 1) { FAIL("puzzle has %d solutions", ns); finish(g, "sudoku"); continue; }
		/* play: a few bad taps, then every square (sometimes a clashing digit first) */
		stub_tap(g, kx[3], ky, 0);
		if (draw_calls()) FAIL("digit with no square selected drew");
		int order[81], ne = 0;
		for (int i = 0; i < 81; i++) if (!giv[i]) order[ne++] = i;
		for (int i = ne - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
		for (int i = 0; i < 81; i++) if (giv[i]) { stub_tap(g, gx + (i % 9) * cell + cell / 2, gy + (i / 9) * cell + cell / 2, 0); if (draw_calls()) FAIL("tapping a given drew"); break; }
		for (int e = 0; e < ne; e++) {
			int i = order[e], x = gx + (i % 9) * cell + cell / 2, y = gy + (i / 9) * cell + cell / 2;
			if (rnd(8) == 0) busy_probe(g, x, y, "sudoku");
			stub_tap(g, x, y, rnd(2));
			if (!draw_calls()) FAIL("selecting a square drew nothing");
			if (rnd(3) == 0) {
				int bad = 0;
				for (int k = 0; k < 9 && !bad; k++) if (giv[i / 9 * 9 + k] && giv[i / 9 * 9 + k] != sol[i]) bad = giv[i / 9 * 9 + k];
				if (bad) {
					stub_tap(g, kx[bad - 1], ky, 0);
					int marked = 0;
					for (int c = 0; c < stub_ncalls(); c++) marked |= stub_call(c)->type == C_BOX && stub_call(c)->a[4] < 255;
					if (!marked) FAIL("a clashing digit was not marked");
					if (stub_over(g)) FAIL("over with a wrong digit");
					if (rnd(2)) stub_tap(g, kx[9], ky, 0);
				}
			}
			if (stub_over(g)) FAIL("over before the grid was full");
			stub_tap(g, kx[sol[i] - 1], ky, 0);
		}
		if (!stub_over(g) || strncmp(stub_result(g), "Solved", 6)) FAIL("full correct grid gave '%s'", stub_result(g));
		else solved++;
		int scr[81] = { 0 };
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (t->a[1] >= gy + size) continue;
			scr[(t->a[1] - gy) / cell * 9 + (t->a[0] - gx) / cell] = t->text[0] - '0';
		}
		if (memcmp(scr, sol, sizeof scr)) FAIL("final screen does not show the solution");
		finish(g, "sudoku");
	}
	printf("  sudoku %-6s %4d games: all unique, clues %d-%d, %d solved by tapping; generation avg %.0f ms, max %.0f ms (host -O1+ASan)\n",
	       LV[level], games, minc, maxc, solved, tsum * 1000 / games, tmax * 1000);
}

/* ======================= Memory ======================= */
typedef struct { int x, y, w, h; } Box;

/* a fingerprint of the symbol drawn after a card is turned: the calls after its g_clear up to the next g_speed */
static void face_sig(const Box *bx, char *out, int outsz) {
	int cx = bx->x + bx->w / 2, cy = bx->y + bx->h / 2, i = 0, len = 0;
	out[0] = 0;
	while (i < stub_ncalls() && stub_call(i)->type != C_CLEAR) i++;
	for (i++; i < stub_ncalls() && stub_call(i)->type != C_SPEED; i++) {
		const Call *c = stub_call(i);
		int v[5];
		switch (c->type) {
		case C_LINE: v[0] = c->a[0] - cx; v[1] = c->a[1] - cy; v[2] = c->a[2] - cx; v[3] = c->a[3] - cy; v[4] = c->a[4]; break;
		case C_POLY: v[0] = c->a[0]; v[1] = c->a[1] - cx; v[2] = c->a[2] - cy; v[3] = c->a[3] - cx; v[4] = c->a[4] - cy; break;
		default: v[0] = c->a[0] - cx; v[1] = c->a[1] - cy; v[2] = c->a[2]; v[3] = c->a[3]; v[4] = c->a[4]; break;
		}
		len += snprintf(out + len, (size_t)(outsz - len), "%d:%d,%d,%d,%d,%d;", c->type, v[0], v[1], v[2], v[3], v[4]);
		if (len > outsz - 40) break;
	}
}

static int card_at_tap(const Box *cards, int k, GameCtx *g, char sig[][400]) {
	stub_tap(g, cards[k].x + cards[k].w / 2, cards[k].y + cards[k].h / 2, rnd(2));
	face_sig(&cards[k], sig[k], 400);
	return sig[k][0] != 0;
}

static void test_memory(int level, int games) {
	static const int NC[3] = { 12, 16, 20 };
	int done_games = 0, total_moves = 0, random_games = 0, random_moves = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_memory, level, &seed);
		Box cards[20];
		int nc = 0;
		for (int i = 0; i < stub_ncalls() && nc < 20; i++)
			if (stub_call(i)->type == C_FRAME && stub_call(i)->a[4] == 4) { const Call *c = stub_call(i); cards[nc++] = (Box){ c->a[0], c->a[1], c->a[2], c->a[3] }; }
		if (nc != NC[level]) { FAIL("%d cards", nc); finish(g, "memory"); continue; }
		for (int i = 0; i < nc; i++) if (cards[i].w < 100 || cards[i].h < 100) FAIL("card %dx%d", cards[i].w, cards[i].h);
		if (gi % 4 == 3) { /* random player */
			int taps = 0;
			while (!stub_over(g) && taps < 200000) { stub_tap(g, AX + rnd(AW), AY + rnd(AH), 0); taps++; }
			if (!stub_over(g)) FAIL("random play never finished");
			else { random_games++; int mv = 0; sscanf(strstr(stub_result(g), " in ") ? strstr(stub_result(g), " in ") + 4 : "0", "%d", &mv); random_moves += mv; }
			finish(g, "memory");
			continue;
		}
		static char sig[20][400];
		int done[20] = { 0 }, moves = 0, waited = 0;
		for (int i = 0; i < nc; i++) sig[i][0] = 0;
		while (!stub_over(g) && moves < 200) {
			int a = -1, b = -1;
			for (int i = 0; i < nc && a < 0; i++)
				for (int j = i + 1; j < nc; j++)
					if (!done[i] && !done[j] && sig[i][0] && !strcmp(sig[i], sig[j])) { a = i; b = j; break; }
			if (a >= 0) {
				card_at_tap(cards, a, g, sig);
				card_at_tap(cards, b, g, sig);
				done[a] = done[b] = 1;
				moves++;
				continue;
			}
			int unk[20], nu = 0;
			for (int i = 0; i < nc; i++) if (!sig[i][0]) unk[nu++] = i;
			if (nu == 0) { FAIL("no unknown card left but pairs remain"); break; }
			a = unk[rnd(nu)];
			if (rnd(5) == 0) busy_probe(g, cards[a].x + cards[a].w / 2, cards[a].y + cards[a].h / 2, "memory");
			if (!card_at_tap(cards, a, g, sig)) FAIL("turning a card drew no symbol");
			for (int i = 0; i < nc; i++) if (i != a && !done[i] && sig[i][0] && !strcmp(sig[i], sig[a])) b = i;
			if (b < 0) {
				if (nu < 2) { FAIL("card %d has no match", a); break; }
				do b = unk[rnd(nu)]; while (b == a);
				if (!waited && rnd(2)) stub_set_auto_timer(g, 0);
				card_at_tap(cards, b, g, sig);
				if (!strcmp(sig[a], sig[b])) { done[a] = done[b] = 1; moves++; continue; }
				moves++;
				if (stub_timer_pending(g)) { /* while the mismatched pair is showing, taps are ignored */
					waited = 1;
					int other = -1;
					for (int i = 0; i < nc; i++) if (i != a && i != b && !done[i]) other = i;
					if (other >= 0) {
						stub_tap(g, cards[other].x + cards[other].w / 2, cards[other].y + cards[other].h / 2, 0);
						if (draw_calls()) FAIL("a tap during the flip-back wait drew");
					}
					stub_fire_timer(g);
					if (count_type(C_BOX) != 2) FAIL("flip-back drew %d card backs", count_type(C_BOX));
					stub_set_auto_timer(g, 1);
				}
			} else {
				card_at_tap(cards, b, g, sig);
				done[a] = done[b] = 1;
				moves++;
			}
		}
		if (!stub_over(g)) { FAIL("perfect-memory play did not finish (%d moves)", moves); finish(g, "memory"); continue; }
		int mv = -1;
		const char *p = strstr(stub_result(g), " in ");
		if (p) sscanf(p + 4, "%d", &mv);
		if (mv != moves) FAIL("result '%s' but %d moves made", stub_result(g), moves);
		for (int i = 0; i < nc; i++) {
			int same = 0;
			for (int j = 0; j < nc; j++) same += !strcmp(sig[i], sig[j]);
			if (same != 2) FAIL("symbol of card %d appears %d times", i, same);
		}
		done_games++;
		total_moves += moves;
		finish(g, "memory");
	}
	printf("  memory %-6s %4d games: %d by a perfect-memory player (avg %.1f moves), %d by random taps (avg %.0f moves)\n", LV[level], games,
	       done_games, done_games ? (double)total_moves / done_games : 0.0, random_games, random_games ? (double)random_moves / random_games : 0.0);
}

/* ======================= Word Search ======================= */
/* Superseded by test_c.c (Word Search was reworked to 15/20/25 grids). */
__attribute__((unused)) static void test_wordsearch(int level, int games) {
	static const int N[3] = { 8, 10, 12 }, NW[3] = { 6, 8, 10 };
	int n = N[level], finished = 0, junk = 0, diag = 0, back = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_wordsearch, level, &seed);
		char grid[12][12];
		char words[12][48];
		int nl = 0, nw = 0, gx = 1 << 30, gy = 1 << 30, cell = 0;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1) { nl++; if (t->a[0] < gx) gx = t->a[0]; if (t->a[1] < gy) gy = t->a[1]; cell = t->a[2]; }
		}
		if (nl != n * n) { FAIL("%d letters for a %dx%d grid", nl, n, n); finish(g, "wordsearch"); continue; }
		if (cell < 90) FAIL("cells are %d px", cell);
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (strlen(t->text) == 1) grid[(t->a[1] - gy) / cell][(t->a[0] - gx) / cell] = t->text[0];
			else if (nw < 12) snprintf(words[nw++], sizeof words[0], "%s", t->text);
		}
		if (nw != NW[level]) FAIL("%d words listed", nw);
		static const int D[8][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { -1, 1 }, { 0, -1 }, { -1, 0 }, { -1, -1 }, { 1, -1 } };
		int pr[12], pc[12], pd[12], ok = 1;
		for (int w = 0; w < nw; w++) {
			int len = (int)strlen(words[w]), hit = 0;
			for (int d = 0; d < (level == 2 ? 8 : 2) && !hit; d++)
				for (int r = 0; r < n && !hit; r++)
					for (int c = 0; c < n && !hit; c++) {
						int k = 0;
						while (k < len) {
							int rr = r + D[d][0] * k, cc = c + D[d][1] * k;
							if (rr < 0 || rr >= n || cc < 0 || cc >= n || grid[rr][cc] != words[w][k]) break;
							k++;
						}
						if (k == len) { hit = 1; pr[w] = r; pc[w] = c; pd[w] = d; }
					}
			if (!hit) { FAIL("'%s' is not in the grid (%s)", words[w], LV[level]); ok = 0; }
			else { diag += pd[w] == 2 || pd[w] == 3 || pd[w] >= 6; back += pd[w] >= 4; }
		}
		if (!ok) { finish(g, "wordsearch"); continue; }
		int order[12];
		for (int w = 0; w < nw; w++) order[w] = w;
		for (int i = nw - 1; i > 0; i--) { int j = rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
		for (int e = 0; e < nw; e++) {
			int w = order[e], len = (int)strlen(words[w]);
			if (rnd(3) == 0) { /* junk: a tap, or a stroke under the list */
				junk++;
				if (rnd(2)) stub_tap(g, gx + rnd(n * cell), gy + rnd(n * cell), 0);
				else stub_line(g, AX + 30 + rnd(AW / 2), AY + AH - 30, AX + AW / 2 + rnd(AW / 2 - 30), AY + AH - 30 - rnd(60), 12);
				if (!strncmp(stub_say(g), "found", 5) || stub_over(g)) FAIL("junk input found a word");
			}
			int j = cell / 5;
			int x0 = gx + pc[w] * cell + cell / 2 + rnd(2 * j + 1) - j, y0 = gy + pr[w] * cell + cell / 2 + rnd(2 * j + 1) - j;
			int x1 = gx + (pc[w] + D[pd[w]][1] * (len - 1)) * cell + cell / 2 + rnd(2 * j + 1) - j;
			int y1 = gy + (pr[w] + D[pd[w]][0] * (len - 1)) * cell + cell / 2 + rnd(2 * j + 1) - j;
			if (rnd(2)) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
			if (stub_over(g)) FAIL("over before every word was found");
			stub_line(g, x0, y0, x1, y1, 2 + rnd(60));
			int capsule = 0;
			for (int i = 0; i < stub_ncalls(); i++) capsule |= stub_call(i)->type == C_POLY && stub_call(i)->a[0] == 19;
			char want[64];
			snprintf(want, sizeof want, "found %s ", words[w]);
			if (!capsule || (!stub_over(g) && strncmp(stub_say(g), want, strlen(want)))) FAIL("line over '%s' was not accepted (say: %s)", words[w], stub_say(g));
		}
		if (!stub_over(g) || !strstr(stub_result(g), "You found all")) FAIL("all words found but result '%s'", stub_result(g));
		else finished++;
		/* the letters must still all be on screen (the ink clean-up redraws them) */
		int left = 0;
		for (int i = 0; i < stub_ntexts(); i++) left += strlen(stub_text(i)->text) == 1;
		if (left != n * n) FAIL("%d letters left on screen after clean-ups", left);
		finish(g, "wordsearch");
	}
	printf("  wordsearch %-6s %4d games: every listed word is in the grid; %d finished by drawing lines (%d junk strokes ignored); diagonal %d, backwards %d\n",
	       LV[level], games, finished, junk, diag, back);
}

/* ======================= Code Breaker ======================= */
static void cb_keys(int *kx, int *ky) {
	static const char *L[8] = { "1", "2", "3", "4", "5", "6", "Erase", "Enter" };
	for (int k = 0; k < 8; k++) {
		ky[k] = -1;
		for (int i = 0; i < stub_ntexts(); i++) {
			const Call *t = stub_text(i);
			if (!strcmp(t->text, L[k]) && t->a[1] + t->a[3] / 2 > ky[k]) { kx[k] = t->a[0] + t->a[2] / 2; ky[k] = t->a[1] + t->a[3] / 2; }
		}
	}
}

static void cb_feedback(const int *code, const int *gs, int *black, int *white) {
	int cg[7] = { 0 }, cc[7] = { 0 };
	*black = *white = 0;
	for (int j = 0; j < 4; j++) { *black += gs[j] == code[j]; cg[gs[j]]++; cc[code[j]]++; }
	for (int d = 1; d <= 6; d++) *white += cg[d] < cc[d] ? cg[d] : cc[d];
	*white -= *black;
}

static void test_codebreaker(int level, int games) {
	int cracked = 0, repeats = 0, checked_rows = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_codebreaker, level, &seed);
		int kx[8], ky[8];
		cb_keys(kx, ky);
		stub_tap(g, kx[7], ky[7], 0);
		if (draw_calls()) FAIL("Enter with no digits drew");
		stub_tap(g, kx[6], ky[6], 0);
		if (draw_calls()) FAIL("Erase with no digits drew");
		for (int r = 0; r < 10 && !stub_over(g); r++) {
			int used[7] = { 0 };
			for (int j = 0; j < 4; j++) {
				int d;
				do d = 1 + rnd(6); while (level == 0 && used[d]);
				used[d] = 1;
				if (rnd(5) == 0) busy_probe(g, kx[d - 1], ky[d - 1], "codebreaker");
				stub_tap(g, kx[d - 1], ky[d - 1], 0);
				if (rnd(6) == 0) { stub_tap(g, kx[6], ky[6], 0); stub_tap(g, kx[d - 1], ky[d - 1], 0); }
			}
			stub_tap(g, kx[0], ky[0], 0);
			if (draw_calls()) FAIL("a fifth digit drew");
			stub_tap(g, kx[7], ky[7], 0);
		}
		if (!stub_over(g)) { FAIL("not over after 10 guesses"); finish(g, "codebreaker"); continue; }
		int code[4] = { 0 };
		const char *p = strstr(stub_result(g), "the code was ");
		if (!p) { if (!strstr(stub_result(g), "Cracked")) FAIL("odd result %s", stub_result(g)); finish(g, "codebreaker"); continue; }
		for (int j = 0; j < 4; j++) code[j] = p[13 + j] - '0';
		int seen[7] = { 0 }, rep = 0;
		for (int j = 0; j < 4; j++) { if (code[j] < 1 || code[j] > 6) FAIL("bad code digit"); else { rep |= seen[code[j]]; seen[code[j]] = 1; } }
		if (rep && level < 2) FAIL("%s code %d%d%d%d repeats a digit", LV[level], code[0], code[1], code[2], code[3]);
		repeats += rep;
		finish(g, "codebreaker");
		/* replay with the code known: check the dots and rings on each row, then crack it */
		GameCtx *h = stub_new(&GAME_codebreaker, level, seed, AX, AY, AW, AH);
		stub_start(h);
		int rows = rnd(10);
		for (int r = 0; r < rows; r++) {
			int gs[4], used[7] = { 0 };
			for (int j = 0; j < 4; j++) { do gs[j] = 1 + rnd(6); while (level == 0 && used[gs[j]]); used[gs[j]] = 1; stub_tap(h, kx[gs[j] - 1], ky[gs[j] - 1], 0); }
			stub_tap(h, kx[7], ky[7], 0);
			int b, w;
			cb_feedback(code, gs, &b, &w);
			if (b == 4) break;
			if (count_type(C_DOT) != b || count_type(C_CIRCLE) != w) FAIL("row %d: drew %d dots %d rings, expected %d %d", r, count_type(C_DOT), count_type(C_CIRCLE), b, w);
			checked_rows++;
		}
		if (level == 0 && !stub_over(h)) { /* Easy refuses a repeated digit */
			stub_tap(h, kx[code[0] - 1], ky[code[0] - 1], 0);
			stub_tap(h, kx[code[0] - 1], ky[code[0] - 1], 0);
			if (draw_calls()) FAIL("easy accepted a repeated digit");
			stub_tap(h, kx[6], ky[6], 0);
		}
		if (!stub_over(h)) {
			for (int j = 0; j < 4; j++) stub_tap(h, kx[code[j] - 1], ky[code[j] - 1], 0);
			stub_tap(h, kx[7], ky[7], 0);
		}
		if (!stub_over(h) || !strstr(stub_result(h), "Cracked")) FAIL("entering the code gave '%s'", stub_result(h));
		else cracked++;
		finish(h, "codebreaker replay");
	}
	printf("  codebreaker %-6s %4d games: %d cracked on replay, %d feedback rows checked, %d codes with repeats\n", LV[level], games, cracked, checked_rows, repeats);
}

/* ======================= Sliding Puzzle ======================= */
static int sp_n, sp_path[600], sp_len, sp_wn;
static long sp_nodes;
static int sp_h(const int *b) {
	int h = 0;
	for (int i = 0; i < sp_n * sp_n; i++)
		if (b[i]) { int t = b[i] - 1; h += abs(t / sp_n - i / sp_n) + abs(t % sp_n - i % sp_n); }
	return h;
}
static int sp_search(int *b, int gap, int gcost, int bound, int prev, int *next_bound) {
	int h = sp_h(b), f = 2 * gcost + sp_wn * h;
	if (f > bound) { if (f < *next_bound) *next_bound = f; return 0; }
	if (h == 0) { sp_len = gcost; return 1; }
	if (++sp_nodes > 50000000 || gcost >= 599) return 0;
	static const int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
	int r = gap / sp_n, c = gap % sp_n;
	for (int d = 0; d < 4; d++) {
		int rr = r + DR[d], cc = c + DC[d], t = rr * sp_n + cc;
		if (rr < 0 || rr >= sp_n || cc < 0 || cc >= sp_n || t == prev) continue;
		b[gap] = b[t]; b[t] = 0;
		sp_path[gcost] = t;
		int found = sp_search(b, t, gcost + 1, bound, gap, next_bound);
		b[t] = b[gap]; b[gap] = 0;
		if (found) return 1;
	}
	return 0;
}
/* weighted IDA*: f = 2g + w*h (w = 2: optimal; 3+: faster, longer solutions); fills sp_path with the tiles to tap */
static int sp_solve(const int *start, int n, int w) {
	int b[16];
	memcpy(b, start, sizeof b);
	sp_n = n; sp_wn = w; sp_nodes = 0;
	int gap = 0;
	while (b[gap]) gap++;
	int bound = w * sp_h(b);
	for (;;) {
		int nb = 1 << 30;
		sp_len = 0;
		if (sp_search(b, gap, 0, bound, -1, &nb)) return sp_len;
		if (nb == 1 << 30 || sp_nodes > 50000000) return -1;
		bound = nb;
	}
}

static int sp_read(int n, int x, int y, int cell, int *b) {
	int seen = 0;
	for (int i = 0; i < n * n; i++) b[i] = 0;
	for (int i = 0; i < stub_ntexts(); i++) {
		const Call *t = stub_text(i);
		int c = (t->a[0] - x) / cell, r = (t->a[1] - y) / cell, v = atoi(t->text);
		if (r < 0 || r >= n || c < 0 || c >= n || v < 1 || v >= n * n || b[r * n + c]) return -1;
		b[r * n + c] = v;
		seen |= 1 << v;
	}
	return seen == ((1 << (n * n)) - 2) ? 0 : -1;
}

static int sp_solvable(const int *b, int n) {
	int inv = 0, gap = 0;
	for (int i = 0; i < n * n; i++) {
		if (!b[i]) { gap = i; continue; }
		for (int j = i + 1; j < n * n; j++) if (b[j] && b[j] < b[i]) inv++;
	}
	if (n % 2) return inv % 2 == 0;
	return (inv + (n - 1 - gap / n)) % 2 == 0;
}

static void test_sliding(int level, int games) {
	static const int N[3] = { 3, 4, 4 };
	int n = N[level], solved = 0, slides = 0, total = 0;
	double tmax = 0;
	for (int gi = 0; gi < games; gi++) {
		unsigned long long seed;
		GameCtx *g = new_game(&GAME_sliding, level, &seed);
		const Call *f = first_of(C_FRAME);
		int x = f->a[0] + 8, y = f->a[1] + 8, cell = (f->a[2] - 16) / n, b[16], sim[16];
		if (cell < 100) FAIL("tiles are %d px", cell);
		if (sp_read(n, x, y, cell, b)) { FAIL("could not read the board"); finish(g, "sliding"); continue; }
		if (!sp_solvable(b, n)) FAIL("unsolvable start");
		int ok = 0;
		for (int i = 0; i < n * n - 1; i++) ok += b[i] == i + 1;
		if (ok == n * n - 1) FAIL("starts solved");
		memcpy(sim, b, sizeof sim);
		int moves = 0;
		for (int k = 0; k < 8; k++) { /* random taps: line slides or ignored taps; the screen must follow the rules */
			int r = rnd(n), c = rnd(n), gap = 0;
			while (sim[gap]) gap++;
			int gr = gap / n, gc = gap % n;
			if (rnd(4) == 0) busy_probe(g, x + c * cell + cell / 2, y + r * cell + cell / 2, "sliding");
			stub_tap(g, x + c * cell + cell / 2, y + r * cell + cell / 2, rnd(2));
			if (stub_over(g)) break;
			if ((r == gr) != (c == gc)) {
				int step = r == gr ? (c < gc ? -1 : 1) : (r < gr ? -n : n), i = r * n + c;
				while (gap != i) { sim[gap] = sim[gap + step]; sim[gap + step] = 0; gap += step; }
				moves++;
				slides++;
			} else if (draw_calls()) FAIL("ignored tap drew");
			if (sp_read(n, x, y, cell, b) || memcmp(b, sim, sizeof(int) * (size_t)(n * n))) FAIL("screen does not match the board after a tap");
		}
		if (stub_over(g)) { solved++; finish(g, "sliding"); continue; }
		clock_t t0 = clock();
		int len = sp_solve(sim, n, n == 3 ? 2 : level == 2 ? 4 : 3);
		double dt = (double)(clock() - t0) / CLOCKS_PER_SEC;
		if (dt > tmax) tmax = dt;
		if (len < 0) { FAIL("test solver gave up"); finish(g, "sliding"); continue; }
		for (int k = 0; k < len; k++) {
			int t = sp_path[k];
			if (stub_over(g)) { FAIL("over before the last move"); break; }
			stub_tap(g, x + (t % n) * cell + cell / 2, y + (t / n) * cell + cell / 2, 0);
			moves++;
		}
		total += len;
		int mv = -1;
		if (stub_over(g)) sscanf(stub_result(g), "Solved in %d", &mv);
		if (!stub_over(g) || mv != moves) FAIL("after the solution: over=%d result '%s', %d moves made", stub_over(g), stub_result(g), moves);
		else solved++;
		finish(g, "sliding");
	}
	printf("  sliding %-6s %4d games: all solvable, %d solved by tapping (avg solution %d moves, test solver max %.2f s), %d line slides checked\n",
	       LV[level], games, solved, games ? total / games : 0, tmax, slides);
}

int main(int argc, char **argv) {
	int quick = argc > 1 && !strcmp(argv[1], "quick");
	const char *only = argc > 2 ? argv[2] : 0; /* e.g. ./a.out quick sudoku, or ./a.out full sudoku */
	setvbuf(stdout, 0, _IONBF, 0);
	static const int AREAS[3][4] = { { 40, 200, 1780, 2040 }, { 0, 140, 1860, 2200 }, { 60, 240, 1740, 1800 } };
	for (int a = 0; a < (quick ? 1 : 3); a++) {
		AX = AREAS[a][0]; AY = AREAS[a][1]; AW = AREAS[a][2]; AH = AREAS[a][3];
		int div = a == 0 ? 1 : 4;
		printf("play area %d,%d %dx%d\n", AX, AY, AW, AH);
		for (int lv = 0; lv < 3; lv++) {
#define RUN(name, call) if (!only || !strcmp(only, name)) call
			RUN("hangman", test_hangman(lv, (quick ? 40 : 300) / div));
			if (a == 0) RUN("hangman", test_hangman_variety(lv, quick ? 600 : 3000));
			RUN("minesweeper", test_minesweeper(lv, (quick ? 30 : 200) / div));
			RUN("sudoku", test_sudoku(lv, (quick ? 5 : 40) / div));
			RUN("memory", test_memory(lv, (quick ? 20 : 120) / div));
			RUN("codebreaker", test_codebreaker(lv, (quick ? 30 : 200) / div));
			RUN("sliding", test_sliding(lv, (quick ? 5 : 30) / div));
		}
	}
	printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
	return failures != 0;
}

/* Hangman: the Kindle picks a word; tap letters on the keypad. Six misses and the stick figure is complete.
 * Easy = 3-4 letter words, Medium = 5-6, Hard = 7 or more. The words are the common words of the shared word list
 * (dict_words.h via dict_pick.h: base forms only, so no plurals or -ed/-ing forms, minus a few that are no fun in a
 * family game) plus the hand-picked list below (which also has the 9-11 letter words): about 1,900 Easy, 4,500
 * Medium and 4,800 Hard words. A word doesn't come back until 256 other words have been played, across restarts. */
#include "../games.h"
#include "bag.h"
#include "dict_pick.h"
#include <stdio.h>
#include <string.h>

/* One string, words separated by single spaces (lowercase, kid-friendly). */
static const char WORDS[] =
	/* 3-4 letters */
	"cat dog sun hat cup bus pig cow egg ant bee owl fox hen bat box car bed red map pen jam fan log mud nut toy "
	"van web zip yak bag bun cap fig gum jet kit leg mop net pot rug sea sky tea top wig ball bear bell bird boat "
	"book cake corn desk drum duck fish frog game gift goat hand kite lamp leaf lion milk moon nest park pear rain "
	"ring road rock rose sand seal ship shoe snow sock star swan tent tree wave wolf worm yarn bike coat door farm "
	"fork girl hill king lake lime mint nose palm rope sail seed song tail toad town wind wing "
	/* 5-6 letters */
	"apple beach bread brush candy chair cloud clown crown daisy dance dream eagle earth fairy field flute ghost "
	"grape grass horse house jelly juice koala lemon light mango melon money mouse music ocean otter panda paper "
	"party peach pizza plant puppy queen river robot salad sheep shell skate smile snake spoon storm sugar table "
	"tiger toast train truck whale wheel zebra animal banana basket bottle bridge bucket butter button camera "
	"candle carrot castle cheese cherry circle cookie dragon flower forest garden guitar hammer island jacket "
	"jungle kitten ladder lizard monkey orange pencil pirate planet rabbit rocket school spider summer tomato "
	"turtle wagon window winter yellow "
	/* 7+ letters */
	"airplane alligator backpack balloon bicycle blanket buffalo butterfly calendar camping caterpillar chicken "
	"chocolate computer cupcake dinosaur dolphin elephant feather firework flamingo football giraffe goldfish "
	"hamburger hedgehog helicopter jellyfish kangaroo ladybug lemonade library lighthouse magician mermaid "
	"monster mountain mushroom notebook octopus pancake peacock penguin pineapple popcorn pumpkin raccoon rainbow "
	"sandwich scissors seahorse snowman spaceship squirrel starfish strawberry submarine sunflower teacher "
	"telescope tractor treasure trumpet umbrella unicorn vacation volcano waterfall watermelon whistle birthday "
	"blueberry carousel cucumber envelope fountain gorilla hospital keyboard kingdom lollipop marshmallow "
	"necklace ostrich painting pajamas playground princess sailboat skeleton sneakers snowflake spaghetti "
	"stadium sweater thunder toothbrush tornado triangle vegetable weather";

#define MAXMISS 6
#define KCOLS 9
#define M 20

typedef struct {
	char word[16];           /* first field: read by the host test */
	int len, misses, found;
	unsigned char used[26];
	int kx, ky, kw, kh;      /* keypad: left edge, top, key size */
	int bx, by, bw, bh;      /* blanks: first slot left edge, top, slot size */
	int gx, gy, gs;          /* gallows square */
	char msg[96];
} S;

/* The word pool of each level, built on first use: (length << 16 | index) for a dictionary word, or the offset in
 * WORDS of a hand-picked word that the dictionary part doesn't already have. */
#define MAXPOOL (DICT_N7 + DICT_N8 + 600)
static unsigned pool[3][MAXPOOL];
static int npool[3], built[3];
static const int LO[3] = { 3, 5, 7 }, HI[3] = { 4, 6, 15 };

static int dict_pick(const char *w, int len) { /* 1 if this word is in the dictionary part of the pool */
	int i = len <= DICT_MAXLEN ? dict_find(w, len) : -1;
	return i >= 0 && dict_common(len, i) && !dict_inflected(w, len) && !dict_skipped(w);
}

static void build_pool(int lvl) {
	char w[16];
	int n = 0;
	for (int len = LO[lvl]; len <= HI[lvl] && len <= DICT_MAXLEN; len++)
		for (int i = 0; i < dict_count(len); i++) {
			if (dict_pickable(len, i, w) && n < MAXPOOL) pool[lvl][n++] = (unsigned)len << 16 | (unsigned)i;
		}
	for (const char *p = WORDS; *p;) {
		const char *e = strchr(p, ' ');
		int len = e ? (int)(e - p) : (int)strlen(p);
		if (len >= LO[lvl] && len <= HI[lvl] && len < (int)sizeof w) {
			memcpy(w, p, (size_t)len);
			w[len] = 0;
			if (!dict_pick(w, len) && n < MAXPOOL) pool[lvl][n++] = (unsigned)(p - WORDS);
		}
		if (!e) break;
		p = e + 1;
	}
	npool[lvl] = n;
	built[lvl] = 1;
}

static void entry_word(unsigned e, char *out) { /* the pool entry, upper case */
	int len = (int)(e >> 16);
	if (len) dict_word(len, (int)(e & 0xffff), out);
	else {
		const char *p = WORDS + e;
		for (len = 0; p[len] && p[len] != ' ' && len < 15; len++) out[len] = p[len];
		out[len] = 0;
	}
	for (char *c = out; *c; c++) *c = (char)(*c - 'a' + 'A');
}

/* a word not among the last 256 played (all levels; kept on the Kindle as hashes of the words) */
static void pick_word(GameCtx *g, int lvl, char *out) {
	static Recent r;
	if (!built[lvl]) build_pool(lvl);
	recent_load(g, "recent", &r);
	entry_word(pool[lvl][g_rand(g, npool[lvl])], out);
	for (int tries = 0; tries < 40 && recent_seen(&r, bag_hash(out), RECENT_MAX); tries++) entry_word(pool[lvl][g_rand(g, npool[lvl])], out);
	recent_add(g, "recent", &r, bag_hash(out));
}

static void key_box(S *s, int k, int *x, int *y) {
	int row = k / KCOLS, col = k % KCOLS;
	*x = s->kx + col * s->kw + (row == 2 ? s->kw / 2 : 0); /* last row has 8 keys: centre it */
	*y = s->ky + row * s->kh;
}

static int key_at(S *s, int px, int py) {
	for (int k = 0; k < 26; k++) {
		int x, y;
		key_box(s, k, &x, &y);
		if (px >= x && px < x + s->kw && py >= y && py < y + s->kh) return k;
	}
	return -1;
}

/* gallows / figure coordinates are in a 0..100 square */
#define GX(v) (s->gx + (v) * s->gs / 100)
#define GY(v) (s->gy + (v) * s->gs / 100)

static void draw_part(GameCtx *g, S *s, int part) {
	int w = s->gs / 110 + 4;
	switch (part) {
	case 0: g_circle(g, GX(68), GY(28), s->gs * 9 / 100, w); break;          /* head */
	case 1: g_line(g, GX(68), GY(37), GX(68), GY(62), w); break;              /* body */
	case 2: g_line(g, GX(68), GY(43), GX(56), GY(55), w); break;              /* left arm */
	case 3: g_line(g, GX(68), GY(43), GX(80), GY(55), w); break;              /* right arm */
	case 4: g_line(g, GX(68), GY(62), GX(58), GY(80), w); break;              /* left leg */
	case 5: g_line(g, GX(68), GY(62), GX(78), GY(80), w); break;              /* right leg */
	}
}

static void print_letter(GameCtx *g, S *s, int i, int bold) {
	char t[2] = { s->word[i], 0 };
	g_print(g, s->bx + i * s->bw, s->by, s->bw, s->bh - 16, s->bh * 62 / 100, bold, t);
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	int lvl = g_level(g);
	pick_word(g, lvl, s->word);
	s->len = (int)strlen(s->word);

	/* keypad at the bottom: 9 + 9 + 8 keys */
	s->kw = (aw - 2 * M) / KCOLS;
	if (s->kw > 200) s->kw = 200;
	s->kh = s->kw < 180 ? s->kw : 180;
	s->kx = ax + (aw - KCOLS * s->kw) / 2;
	s->ky = ay + ah - M - 3 * s->kh;
	/* blanks above the keypad */
	s->bw = (aw - 2 * M) / s->len;
	if (s->bw > 170) s->bw = 170;
	s->bh = 170;
	s->bx = ax + (aw - s->len * s->bw) / 2;
	s->by = s->ky - 70 - s->bh;
	/* gallows in the space left at the top */
	int top = ay + M, room = s->by - 50 - top;
	s->gs = room < aw * 7 / 10 ? room : aw * 7 / 10;
	s->gx = ax + aw / 2 - s->gs * 44 / 100; /* the drawing spans 8..80: centre it */
	s->gy = top + (room - s->gs) / 2;

	g_speed(g, 8);
	int w = s->gs / 100 + 4;
	g_line(g, GX(8), GY(96), GX(62), GY(96), w);  /* base */
	g_line(g, GX(22), GY(96), GX(22), GY(4), w);  /* pole */
	g_line(g, GX(22), GY(4), GX(68), GY(4), w);   /* beam */
	g_line(g, GX(22), GY(18), GX(36), GY(4), w);  /* brace */
	g_line(g, GX(68), GY(4), GX(68), GY(19), w - 2); /* rope */
	for (int i = 0; i < s->len; i++)
		g_line(g, s->bx + i * s->bw + s->bw / 8, s->by + s->bh - 6, s->bx + (i + 1) * s->bw - s->bw / 8, s->by + s->bh - 6, 5);
	g_speed(g, 10);
	for (int k = 0; k < 26; k++) {
		int x, y;
		char t[2] = { (char)('A' + k), 0 };
		key_box(s, k, &x, &y);
		g_frame(g, x + 6, y + 6, s->kw - 12, s->kh - 12, 3);
		g_print(g, x + 6, y + 6, s->kw - 12, s->kh - 12, s->kh * 45 / 100, 1, t);
	}
	g_remark(g, "Guess my word - tap a letter.");
	g_say(g, "%d letters - tap a letter below", s->len);
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (g_busy(g)) { g_say(g, "one moment - still drawing"); return; }
	int k = key_at(s, st->cx, st->cy);
	if (k < 0) { g_say(g, "tap a letter on the keypad"); return; }
	if (s->used[k]) { g_say(g, "you already tried %c", 'A' + k); return; }
	s->used[k] = 1;
	int x, y, hits = 0;
	key_box(s, k, &x, &y);
	for (int i = 0; i < s->len; i++) if (s->word[i] == 'A' + k) hits++;
	if (hits) {
		g_speed(g, 5);
		g_line(g, x + 18, y + s->kh - 18, x + s->kw - 18, y + 18, 5); /* crossed out with a slash */
		g_speed(g, 3);
		for (int i = 0; i < s->len; i++) if (s->word[i] == 'A' + k) print_letter(g, s, i, 1);
		s->found += hits;
		if (s->found == s->len) {
			snprintf(s->msg, sizeof s->msg, "You got it: %s! (%d miss%s)", s->word, s->misses, s->misses == 1 ? "" : "es");
			g_over(g, s->msg);
			return;
		}
		g_say(g, "yes, %d %c%s - %d miss%s left", hits, 'A' + k, hits > 1 ? "s" : "", MAXMISS - s->misses,
		      MAXMISS - s->misses == 1 ? "" : "es");
		return;
	}
	g_speed(g, 5);
	g_cross(g, x + s->kw / 2, y + s->kh / 2, s->kh * 32 / 100, 5);
	g_speed(g, 2.5);
	draw_part(g, s, s->misses);
	s->misses++;
	if (s->misses >= MAXMISS) {
		/* show the letters they missed (regular weight, theirs are bold) */
		g_speed(g, 4);
		for (int i = 0; i < s->len; i++) {
			int c = s->word[i] - 'A';
			if (!s->used[c]) print_letter(g, s, i, 0);
		}
		snprintf(s->msg, sizeof s->msg, "Out of guesses - the word was %s.", s->word);
		g_over(g, s->msg);
		return;
	}
	g_say(g, "no %c - %d miss%s left", 'A' + k, MAXMISS - s->misses, MAXMISS - s->misses == 1 ? "" : "es");
}

const GameDef GAME_hangman = { "hangman", "Hangman", "Guess my word, one letter at a time", sizeof(S), 0, 1, start, stroke, 0 };

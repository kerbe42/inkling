/* Criss-Cross: a fill-in crossword. A themed word list is laid out across and down with shared letters (a greedy
 * layout: the longest word first, then each word attached where its letters match, never touching another word side
 * by side). The grid starts empty (Easy shows one letter of each word) and the words are listed by length: tap a
 * square, then letters on the keypad. Easy 8 words of 4-6 letters, Medium 12 of up to 8, Hard 16 of up to 13 (when no
 * word length is unique, one letter of a longest word is shown). The layout is checked to have only one way to fit
 * the words in. Two puzzles in five use one of the hand-made themes below (each once before any comes back, across
 * restarts); the others a theme made from the shared word list by a rule ("words starting with ST", "words ending in
 * -LE", "words with OO in them"...: about 120 rules, each with hundreds of words to draw from). */
#include "../games.h"
#include "bag.h"
#include "dict_pick.h"
#include <stdio.h>
#include <string.h>

#define WG 34      /* work grid for building the layout */
#define GMAX 15    /* largest finished grid side */
#define MAXW 18    /* words in a puzzle */
#define WLEN 14    /* longest word + 1 */
#define MAXCAND 48
#define M 20
#define KCOLS 14
#define K_ERASE 26

typedef struct { const char *name, *words; } Theme;

static const Theme THEMES[] = {
	{ "Animals",
	  "LION TIGER ZEBRA HORSE MOUSE CAMEL OTTER PANDA BEAVER DONKEY MONKEY RABBIT TURTLE WALRUS BADGER "
	  "JAGUAR GIRAFFE LEOPARD HAMSTER PENGUIN DOLPHIN CHEETAH GORILLA ANTELOPE ELEPHANT KANGAROO "
	  "HEDGEHOG SQUIRREL PORCUPINE ARMADILLO CROCODILE ALLIGATOR CHIMPANZEE RHINOCEROS HIPPOPOTAMUS "
	  "MOLE WOLF BEAR DEER GOAT" },
	{ "Fruit",
	  "PEAR PLUM LIME KIWI FIGS MANGO LEMON GRAPE PEACH MELON GUAVA APPLE BANANA CHERRY ORANGE PAPAYA "
	  "QUINCE DAMSON APRICOT AVOCADO COCONUT TANGERINE PINEAPPLE BLUEBERRY RASPBERRY STRAWBERRY "
	  "GOOSEBERRY BLACKBERRY CRANBERRY GRAPEFRUIT POMEGRANATE MULBERRY NECTARINE PERSIMMON CLEMENTINE "
	  "DATES OLIVE" },
	{ "Vegetables",
	  "LEEK KALE BEET CORN OKRA PEAS ONION BEANS CHARD SWEDE CARROT TURNIP RADISH CELERY POTATO TOMATO "
	  "GARLIC SQUASH PEPPER PARSNIP SPINACH LETTUCE CABBAGE PUMPKIN SHALLOT BROCCOLI CUCUMBER ZUCCHINI "
	  "BEETROOT CHICKPEA ASPARAGUS ARTICHOKE AUBERGINE WATERCRESS CAULIFLOWER" },
	{ "Space",
	  "STAR MOON MARS COMET ORBIT EARTH VENUS PLUTO LUNAR SOLAR SATURN URANUS GALAXY PLANET METEOR "
	  "NEBULA ROCKET QUASAR PULSAR CRATER COSMOS NEPTUNE JUPITER MERCURY ECLIPSE GRAVITY ASTEROID "
	  "TELESCOPE ASTRONAUT SATELLITE SPACESHIP SUPERNOVA OBSERVATORY CONSTELLATION" },
	{ "Ocean",
	  "WAVE TIDE REEF CRAB SEAL SAND SHARK WHALE CORAL SQUID SHELL SHRIMP OYSTER MUSSEL ANCHOR LOBSTER "
	  "SEAWEED DOLPHIN OCTOPUS ANEMONE CURRENT STARFISH SEAHORSE STINGRAY BARNACLE PLANKTON SAILBOAT "
	  "JELLYFISH SUBMARINE LIGHTHOUSE SHIPWRECK SWORDFISH" },
	{ "Weather",
	  "RAIN SNOW HAIL WIND MIST STORM CLOUD SLEET FROST SUNNY BREEZE ICICLE DRIZZLE THUNDER TORNADO "
	  "RAINBOW MONSOON TEMPEST CYCLONE TYPHOON DROUGHT BLIZZARD HUMIDITY FORECAST SUNSHINE OVERCAST "
	  "RAINDROP HEATWAVE HURRICANE LIGHTNING BAROMETER SNOWFLAKE THERMOMETER" },
	{ "Kitchen",
	  "BOWL FORK OVEN SINK KNIFE SPOON PLATE GRILL STOVE LADLE WHISK TONGS APRON KETTLE PEELER GRATER "
	  "TEAPOT FRIDGE TOASTER BLENDER SPATULA SKILLET CUTLERY FREEZER SAUCEPAN COLANDER CUPBOARD "
	  "CORKSCREW MICROWAVE CHOPSTICKS DISHWASHER" },
	{ "Music",
	  "DRUM HARP LUTE TUBA OBOE FLUTE PIANO CELLO BANJO CHORD TEMPO VIOLIN GUITAR MELODY RHYTHM "
	  "TRUMPET BASSOON UKULELE HARMONY CLARINET TROMBONE MANDOLIN BAGPIPES SYMPHONY SAXOPHONE "
	  "XYLOPHONE ACCORDION HARMONICA ORCHESTRA CONDUCTOR METRONOME TAMBOURINE" },
	{ "Sports",
	  "GOLF POLO JUDO RUGBY TENNIS HOCKEY SOCCER ROWING BOXING KARATE DIVING SKIING CYCLING ARCHERY "
	  "FENCING SAILING SURFING CRICKET SKATING BOWLING BASEBALL FOOTBALL SWIMMING MARATHON BADMINTON "
	  "WRESTLING TRIATHLON VOLLEYBALL BASKETBALL GYMNASTICS" },
	{ "Birds",
	  "DOVE CROW HAWK WREN DUCK SWAN KITE ROBIN EAGLE STORK HERON RAVEN FINCH GOOSE PIGEON PARROT "
	  "TOUCAN FALCON CONDOR OSPREY PELICAN OSTRICH PEACOCK SPARROW SWALLOW BLACKBIRD FLAMINGO "
	  "CORMORANT ALBATROSS KINGFISHER WOODPECKER HUMMINGBIRD NIGHTINGALE" },
	{ "Garden",
	  "ROSE LILY IRIS PINE TULIP DAISY LILAC POPPY ASTER MAPLE BIRCH CEDAR ORCHID VIOLET DAHLIA CROCUS "
	  "WILLOW SPRUCE POPLAR HEATHER JASMINE MAGNOLIA BLUEBELL DAFFODIL LAVENDER GERANIUM HYACINTH "
	  "SNOWDROP SUNFLOWER CARNATION BUTTERCUP HONEYSUCKLE" },
	{ "Jobs",
	  "CHEF COOK PILOT NURSE BAKER JUDGE ACTOR DOCTOR FARMER LAWYER SAILOR TAILOR WAITER DENTIST "
	  "TEACHER PLUMBER SURGEON BUTCHER CASHIER PAINTER GARDENER ENGINEER MECHANIC ARCHITECT SCIENTIST "
	  "LIBRARIAN CARPENTER ASTRONAUT JOURNALIST PHARMACIST ELECTRICIAN FIREFIGHTER VETERINARIAN" },
	{ "Transport",
	  "SHIP BOAT TAXI TRAM BIKE KAYAK CANOE TRAIN TRUCK FERRY YACHT WAGON GLIDER BICYCLE SCOOTER "
	  "TRACTOR BALLOON CARAVAN AIRPLANE SAILBOAT MOTORBIKE AMBULANCE LIMOUSINE CATAMARAN STEAMBOAT "
	  "SUBMARINE HELICOPTER SNOWMOBILE SKATEBOARD HOVERCRAFT" },
	{ "Body",
	  "HAND FOOT KNEE NOSE LIPS NECK CHIN HEART ELBOW ANKLE THUMB WRIST TOOTH LUNGS LIVER SPINE CHEEK "
	  "TONGUE FINGER MUSCLE KIDNEY EYEBROW EYELASH STOMACH KNUCKLE KNEECAP SHOULDER SKELETON FOREHEAD "
	  "BACKBONE CHEEKBONE COLLARBONE FINGERNAIL" },
	{ "House",
	  "DOOR ROOF WALL SOFA LAMP ATTIC PORCH TABLE CHAIR SHELF STAIRS CELLAR CLOSET CARPET MIRROR "
	  "WINDOW GARAGE CURTAIN CHIMNEY BALCONY HALLWAY BEDROOM BATHROOM BOOKCASE WARDROBE BASEMENT "
	  "DOORBELL FIREPLACE STAIRCASE CHANDELIER GREENHOUSE" },
	{ "Countries",
	  "PERU CUBA CHAD MALI FIJI CHILE CHINA EGYPT INDIA ITALY JAPAN KENYA NEPAL SPAIN CANADA FRANCE "
	  "GREECE MEXICO NORWAY BRAZIL SWEDEN TURKEY GERMANY IRELAND JAMAICA ICELAND PORTUGAL ARGENTINA "
	  "AUSTRALIA INDONESIA MADAGASCAR SWITZERLAND NETHERLANDS PHILIPPINES" },
	{ "Clothes",
	  "COAT SOCK SHOE BELT VEST SCARF SKIRT DRESS GLOVE JEANS BOOTS SHIRT BLOUSE JACKET SANDAL NECKTIE "
	  "SLIPPER SWEATER UNIFORM MITTENS TROUSERS OVERALLS RAINCOAT CARDIGAN SWIMSUIT WAISTCOAT "
	  "TRACKSUIT DUNGAREES TURTLENECK" },
	{ "Sweets",
	  "CAKE TART FUDGE CANDY TOFFEE MUFFIN COOKIE WAFFLE SORBET GELATO TRIFLE ECLAIR CUSTARD PUDDING "
	  "BROWNIE PANCAKE CRUMBLE CUPCAKE CARAMEL PRALINE MERINGUE DOUGHNUT LOLLIPOP MARZIPAN CROISSANT "
	  "CHOCOLATE CHEESECAKE SHORTBREAD GINGERBREAD MARSHMALLOW BUTTERSCOTCH" },
};
#define NTHEMES ((int)(sizeof THEMES / sizeof THEMES[0]))

/* Word themes made from the shared word list (common base-form words, 4-8 letters): "s:" starts with, "e:" ends
 * with, "c:" contains, "d" a double letter, "f" the same first and last letter, "m" any word. A rule is used only
 * when the level has enough words for it. */
static const char *const RULES[] = {
	"s:a", "s:b", "s:c", "s:d", "s:e", "s:f", "s:g", "s:h", "s:i", "s:j", "s:k", "s:l", "s:m", "s:n", "s:o", "s:p",
	"s:q", "s:r", "s:s", "s:t", "s:u", "s:v", "s:w", "s:y", "s:bl", "s:br", "s:ch", "s:cl", "s:cr", "s:dr", "s:fl",
	"s:fr", "s:gl", "s:gr", "s:pl", "s:pr", "s:sc", "s:sh", "s:sl", "s:sn", "s:sp", "s:st", "s:sw", "s:th", "s:tr",
	"s:wh", "s:un", "s:re", "s:in", "s:con", "s:dis", "s:pro", "s:over", "s:out", "e:er", "e:le", "e:ly", "e:y",
	"e:ing", "e:tion", "e:ness", "e:ment", "e:ful", "e:less", "e:ous", "e:age", "e:ate", "e:ish", "e:ive", "e:ant",
	"e:ent", "e:ight", "e:ack", "e:ock", "e:ow", "e:all", "e:ell", "e:ain", "e:ice", "e:ink", "e:ound", "e:ure",
	"e:ble", "e:dle", "e:tle", "e:ry", "e:ty", "e:ic", "e:al", "e:et", "e:on", "e:en", "e:est", "e:ist", "e:ade",
	"e:ine", "e:ope", "e:ide", "c:oo", "c:ee", "c:ea", "c:ai", "c:ou", "c:oa", "c:ie", "c:qu", "c:ph", "c:gh", "c:ck",
	"c:z", "c:x", "c:j", "c:k", "c:v", "c:w", "c:sh", "c:ch", "c:th", "c:ar", "c:or", "c:ur", "d", "f", "m",
};
#define NRULES ((int)(sizeof RULES / sizeof RULES[0]))

typedef struct { char w[WLEN]; int len, r, c, dir; } Slot; /* dir 0 = across, 1 = down */

typedef struct {
	int theme, rows, cols, nw;     /* theme: index into THEMES, or -1 - RULES index for a word-list theme */
	char tname[48];                /* the theme as shown */
	Slot slot[MAXW];
	char sol[GMAX][GMAX];          /* 'A'..'Z', 0 = no square */
	char cur[GMAX][GMAX];          /* the player's letters (givens included), 0 = empty */
	unsigned char given[GMAX][GMAX], cdir[GMAX][GMAX]; /* cdir bit 0: in an across word, bit 1: in a down word */
	unsigned char crossed[MAXW];
	int order[MAXW];               /* list order: by length, then alphabetical */
	int ex[MAXW], ey[MAXW], ew, eh, epx; /* list entry boxes */
	int lx, ly, labw, nlabels, laby[MAXW], lablen[MAXW];
	int gx, gy, cell;
	int kx, ky, kw, kh;
	int selr, selc, dir, ready, over;
	/* layout building */
	char work[WG][WG];
	unsigned char wdir[WG][WG];
	int br0, bc0, br1, bc1;        /* bounding box of the placed words */
	char cand[MAXCAND][WLEN];
	int ncand, nplaced;
	/* uniqueness check */
	char sg[GMAX][GMAX];
	unsigned char used[MAXW];
	int nsols;
	long nodes;
	char msg[200];
} S;

/* ---------------- building the layout ---------------- */

static int load_theme(S *s, int t, int lo, int hi) {
	const char *p = THEMES[t].words;
	s->ncand = 0;
	while (*p) {
		while (*p == ' ') p++;
		int n = 0;
		while (p[n] && p[n] != ' ') n++;
		if (n >= lo && n <= hi && n < WLEN && s->ncand < MAXCAND) {
			memcpy(s->cand[s->ncand], p, (size_t)n);
			s->cand[s->ncand][n] = 0;
			s->ncand++;
		}
		p += n;
	}
	return s->ncand;
}

/* can w go at (r,c) in direction d? On success *score rates it (more crossings, a tighter box). */
static int fits(S *s, const char *w, int len, int r, int c, int d, int maxd, int *score) {
	int dr = d, dc = !d, er = r + dr * (len - 1), ec = c + dc * (len - 1), cross = 0;
	if (r < 1 || c < 1 || er > WG - 2 || ec > WG - 2) return 0;
	if (s->work[r - dr][c - dc] || s->work[er + dr][ec + dc]) return 0;
	for (int k = 0; k < len; k++) {
		int rr = r + dr * k, cc = c + dc * k;
		char ch = s->work[rr][cc];
		if (ch) {
			if (ch != w[k] || (s->wdir[rr][cc] & (1 << d))) return 0;
			cross++;
		} else if (d == 0 ? (s->work[rr - 1][cc] || s->work[rr + 1][cc]) : (s->work[rr][cc - 1] || s->work[rr][cc + 1])) return 0;
	}
	if (!cross || cross == len) return 0;
	int r0 = r < s->br0 ? r : s->br0, c0 = c < s->bc0 ? c : s->bc0, r1 = er > s->br1 ? er : s->br1, c1 = ec > s->bc1 ? ec : s->bc1;
	int h = r1 - r0 + 1, wd = c1 - c0 + 1;
	if (h > maxd || wd > maxd) return 0;
	int grow = h * wd - (s->br1 - s->br0 + 1) * (s->bc1 - s->bc0 + 1);
	*score = cross * 100 - grow - 4 * (h > wd ? h - wd : wd - h);
	return 1;
}

static void put(S *s, const char *w, int len, int r, int c, int d) {
	int dr = d, dc = !d;
	for (int k = 0; k < len; k++) {
		s->work[r + dr * k][c + dc * k] = w[k];
		s->wdir[r + dr * k][c + dc * k] |= (unsigned char)(1 << d);
	}
	if (s->nplaced == 0) { s->br0 = r; s->bc0 = c; s->br1 = r + dr * (len - 1); s->bc1 = c + dc * (len - 1); }
	if (r < s->br0) s->br0 = r;
	if (c < s->bc0) s->bc0 = c;
	if (r + dr * (len - 1) > s->br1) s->br1 = r + dr * (len - 1);
	if (c + dc * (len - 1) > s->bc1) s->bc1 = c + dc * (len - 1);
	Slot *sl = &s->slot[s->nplaced++];
	strcpy(sl->w, w);
	sl->len = len; sl->r = r; sl->c = c; sl->dir = d;
}

/* attach w at its best spot; returns 1 if placed */
static int attach(GameCtx *g, S *s, const char *w, int maxd) {
	int len = (int)strlen(w), best = -1000000, br = 0, bc = 0, bd = 0;
	for (int i = s->br0; i <= s->br1; i++)
		for (int j = s->bc0; j <= s->bc1; j++) {
			if (!s->work[i][j]) continue;
			for (int k = 0; k < len; k++) {
				if (w[k] != s->work[i][j]) continue;
				for (int d = 0; d < 2; d++) {
					int sc, r = i - d * k, c = j - (!d) * k;
					if (!fits(s, w, len, r, c, d, maxd, &sc)) continue;
					sc += g_rand(g, 30);
					if (sc > best) { best = sc; br = r; bc = c; bd = d; }
				}
			}
		}
	if (best == -1000000) return 0;
	put(s, w, len, br, bc, bd);
	return 1;
}

/* count the ways to fit the words into the slots (stops at 2) */
static void count_fills(S *s, int k) {
	if (s->nsols >= 2) return;
	if (++s->nodes > 300000) { s->nsols = 2; return; } /* too hard to tell: treat as not unique */
	if (k == s->nw) { s->nsols++; return; }
	const Slot *sl = &s->slot[k];
	int dr = sl->dir, dc = !sl->dir;
	for (int i = 0; i < s->nw; i++) {
		if (s->used[i] || s->slot[i].len != sl->len) continue;
		const char *w = s->slot[i].w;
		int ok = 1;
		for (int p = 0; p < sl->len && ok; p++) {
			char ch = s->sg[sl->r + dr * p][sl->c + dc * p];
			ok = !ch || ch == w[p];
		}
		if (!ok) continue;
		unsigned short fresh = 0;
		for (int p = 0; p < sl->len; p++) {
			char *ch = &s->sg[sl->r + dr * p][sl->c + dc * p];
			if (!*ch) { *ch = w[p]; fresh |= (unsigned short)(1 << p); }
		}
		s->used[i] = 1;
		count_fills(s, k + 1);
		s->used[i] = 0;
		for (int p = 0; p < sl->len; p++)
			if (fresh >> p & 1) s->sg[sl->r + dr * p][sl->c + dc * p] = 0;
	}
}

static void shuffle(GameCtx *g, S *s) {
	for (int i = s->ncand - 1; i > 0; i--) {
		int j = g_rand(g, i + 1);
		char t[WLEN];
		memcpy(t, s->cand[i], WLEN); memcpy(s->cand[i], s->cand[j], WLEN); memcpy(s->cand[j], t, WLEN);
	}
}

/* ---------------- themes: the hand-made ones and the word-list rules ---------------- */

/* the word-list words: common base-form words of 4-8 letters, (length << 16 | index), built on first use */
#define MAXDW 16384
static unsigned dw[MAXDW];
static int ndw = -1;

static void build_dw(void) {
	char w[DICT_MAXLEN + 1];
	ndw = 0;
	for (int len = 4; len <= DICT_MAXLEN; len++)
		for (int i = 0; i < dict_count(len) && ndw < MAXDW; i++)
			if (dict_pickable(len, i, w)) dw[ndw++] = (unsigned)len << 16 | (unsigned)i;
}

static int rule_match(const char *rule, const char *w, int len) {
	const char *a = rule[1] == ':' ? rule + 2 : "";
	int n = (int)strlen(a);
	switch (rule[0]) {
	case 's': return len >= n + 2 && !strncmp(w, a, (size_t)n);
	case 'e': return len >= n + 2 && !strcmp(w + len - n, a);
	case 'c': return strstr(w, a) != 0;
	case 'd': for (int k = 0; k + 1 < len; k++) if (w[k] == w[k + 1]) return 1; return 0;
	case 'f': return w[0] == w[len - 1];
	default: return 1;
	}
}

static void rule_name(const char *rule, char *out, size_t cap) {
	char a[8];
	int n = 0;
	for (const char *p = rule + 2; rule[1] == ':' && *p && n < 7; p++) a[n++] = (char)(*p - 'a' + 'A');
	a[n] = 0;
	switch (rule[0]) {
	case 's': snprintf(out, cap, "words starting with %s", a); break;
	case 'e': snprintf(out, cap, n > 1 ? "words ending in -%s" : "words ending in %s", a); break;
	case 'c': if (n > 1) snprintf(out, cap, "words with %s in them", a);
	          else snprintf(out, cap, "words with a%s %s", strchr("AEFHILMNORSX", a[0]) ? "n" : "", a);
	          break;
	case 'd': snprintf(out, cap, "words with a double letter"); break;
	case 'f': snprintf(out, cap, "words that start and end alike"); break;
	default: snprintf(out, cap, "mixed words"); break;
	}
}

/* Fill the candidates with up to MAXCAND random words (lo..hi letters) that follow the rule; returns how many follow
 * it in all. */
static int load_rule(GameCtx *g, S *s, int r, int lo, int hi) {
	char w[DICT_MAXLEN + 1];
	int count = 0;
	if (ndw < 0) build_dw();
	s->ncand = 0;
	for (int k = 0; k < ndw; k++) {
		int len = (int)(dw[k] >> 16);
		if (len < lo || len > hi) continue;
		dict_word(len, (int)(dw[k] & 0xffff), w);
		if (!rule_match(RULES[r], w, len)) continue;
		int slot = count < MAXCAND ? count : g_rand(g, count + 1); /* reservoir sampling */
		count++;
		if (slot >= MAXCAND) continue;
		for (int i = 0; i <= len; i++) s->cand[slot][i] = (char)(w[i] ? w[i] - 'a' + 'A' : 0);
		if (s->ncand < MAXCAND) s->ncand++;
	}
	return count;
}

/* Which themes were played lately is kept on the Kindle (bag.h, by name): each hand-made theme once before any comes
 * back (and none within 6 of its last time), and no word-list rule again within 40 rules. */
static uint32_t theme_hash[NTHEMES];

/* pick a theme for a puzzle of N words of up to hi letters: two in five hand-made, the rest word-list rules */
static void pick_theme(GameCtx *g, S *s, int N, int hi) {
	if (g_rand(g, 5) >= 2) {
		static Recent rr;
		recent_load(g, "rules", &rr);
		for (int tries = 0; tries < 40; tries++) {
			int r = g_rand(g, NRULES);
			if (recent_seen(&rr, bag_hash(RULES[r]), 40) && tries < 30) continue;
			if (load_rule(g, s, r, 4, hi < DICT_MAXLEN ? hi : DICT_MAXLEN) < N + 8) continue;
			s->theme = -1 - r;
			rule_name(RULES[r], s->tname, sizeof s->tname);
			return;
		}
	}
	if (!theme_hash[0])
		for (int t = 0; t < NTHEMES; t++) theme_hash[t] = bag_hash(THEMES[t].name);
	s->theme = bag_peek(g, "themes", theme_hash, NTHEMES, 6);
	snprintf(s->tname, sizeof s->tname, "%s", THEMES[s->theme].name);
}

static void note_theme(GameCtx *g, S *s) { /* the puzzle is built: remember its theme */
	if (s->theme >= 0) bag_take(g, "themes", theme_hash[s->theme]);
	else {
		static Recent rr;
		recent_load(g, "rules", &rr);
		recent_add(g, "rules", &rr, bag_hash(RULES[-1 - s->theme]));
	}
}

static void build(GameCtx *g, S *s, int lvl) {
	int N = lvl == 0 ? 8 : lvl == 1 ? 12 : 16, hi = lvl == 0 ? 6 : lvl == 1 ? 8 : 13, maxd = lvl == 0 ? 10 : lvl == 1 ? 12 : 14;
	int longmin = lvl == 1 ? 7 : 9, reqlong = lvl == 0 ? 0 : lvl == 1 ? 2 : 3;
	for (int attempt = 0; attempt < 400; attempt++) {
		if (attempt % 8 == 0) pick_theme(g, s, N, hi);
		if (s->theme >= 0) load_theme(s, s->theme, 4, hi);
		else load_rule(g, s, -1 - s->theme, 4, hi < DICT_MAXLEN ? hi : DICT_MAXLEN);
		int lmin = s->theme < 0 && lvl == 2 ? 8 : longmin; /* the word list stops at 8 letters */
		shuffle(g, s);
		/* pick the words: the long ones the level asks for, then any */
		int pick[MAXCAND], np = 0, taken[MAXCAND] = { 0 };
		for (int i = 0; i < s->ncand && np < reqlong; i++)
			if ((int)strlen(s->cand[i]) >= lmin) { pick[np++] = i; taken[i] = 1; }
		for (int i = 0; i < s->ncand && np < N; i++)
			if (!taken[i]) { pick[np++] = i; taken[i] = 1; }
		if (np < N) continue;
		/* longest first */
		for (int a = 0; a < np; a++)
			for (int b = a + 1; b < np; b++)
				if (strlen(s->cand[pick[b]]) > strlen(s->cand[pick[a]])) { int t = pick[a]; pick[a] = pick[b]; pick[b] = t; }
		memset(s->work, 0, sizeof s->work);
		memset(s->wdir, 0, sizeof s->wdir);
		s->nplaced = 0;
		int len0 = (int)strlen(s->cand[pick[0]]);
		put(s, s->cand[pick[0]], len0, WG / 2, (WG - len0) / 2, 0);
		unsigned char done[MAXCAND] = { 0 };
		done[0] = 1;
		for (int pass = 0; pass < 3; pass++)
			for (int a = 1; a < np; a++)
				if (!done[a] && attach(g, s, s->cand[pick[a]], maxd)) done[a] = 1;
		/* replacements for words that would not fit */
		for (int i = 0; i < s->ncand && s->nplaced < N; i++) {
			if (taken[i]) continue;
			taken[i] = 1;
			attach(g, s, s->cand[i], maxd);
		}
		if (s->nplaced < N && attempt < 399) continue;
		/* move the layout into the finished grid */
		s->nw = s->nplaced;
		s->rows = s->br1 - s->br0 + 1;
		s->cols = s->bc1 - s->bc0 + 1;
		memset(s->sol, 0, sizeof s->sol);
		memset(s->cdir, 0, sizeof s->cdir);
		for (int i = 0; i < s->rows; i++)
			for (int j = 0; j < s->cols; j++) {
				s->sol[i][j] = s->work[s->br0 + i][s->bc0 + j];
				s->cdir[i][j] = s->wdir[s->br0 + i][s->bc0 + j];
			}
		for (int k = 0; k < s->nw; k++) { s->slot[k].r -= s->br0; s->slot[k].c -= s->bc0; }
		/* Easy: one letter of each word is shown (a square not shared with another word when there is one) */
		memset(s->given, 0, sizeof s->given);
		if (lvl == 0)
			for (int k = 0; k < s->nw; k++) {
				const Slot *sl = &s->slot[k];
				int cells[WLEN], nc = 0, dr = sl->dir, dc = !sl->dir;
				for (int p = 0; p < sl->len; p++)
					if (s->cdir[sl->r + dr * p][sl->c + dc * p] != 3) cells[nc++] = p;
				int p = nc ? cells[g_rand(g, nc)] : g_rand(g, sl->len);
				s->given[sl->r + dr * p][sl->c + dc * p] = 1;
			}
		/* Medium/Hard: when no word length is unique there is no obvious first word: show one letter of a longest one */
		if (lvl > 0) {
			int cnt[WLEN] = { 0 }, uniq = 0, lk = 0;
			for (int k = 0; k < s->nw; k++) cnt[s->slot[k].len]++;
			for (int l = 0; l < WLEN; l++) uniq |= cnt[l] == 1;
			for (int k = 1; k < s->nw; k++) if (s->slot[k].len > s->slot[lk].len) lk = k;
			const Slot *sl = &s->slot[lk];
			int pick = 1; /* the first square not shared with another word, after the first letter */
			for (int p = sl->len - 1; p > 0; p--)
				if (s->cdir[sl->r + sl->dir * p][sl->c + !sl->dir * p] != 3) pick = p;
			if (!uniq) s->given[sl->r + sl->dir * pick][sl->c + !sl->dir * pick] = 1;
		}
		/* only one way to fill it in? */
		for (int i = 0; i < s->rows; i++)
			for (int j = 0; j < s->cols; j++) s->sg[i][j] = s->given[i][j] ? s->sol[i][j] : 0;
		memset(s->used, 0, sizeof s->used);
		s->nsols = 0;
		s->nodes = 0;
		count_fills(s, 0);
		if (s->nsols == 1 || attempt >= 300) return;
	}
}

/* ---------------- layout and drawing ---------------- */

static void cell_xy(const S *s, int r, int c, int *x, int *y) { *x = s->gx + c * s->cell; *y = s->gy + r * s->cell; }

static void draw_cell(GameCtx *g, S *s, int r, int c) {
	int x, y, n = s->cell;
	cell_xy(s, r, c, &x, &y);
	g_clear(g, x + 4, y + 4, n - 8, n - 8);
	if (r == s->selr && c == s->selc) {
		g_frame(g, x + 8, y + 8, n - 16, n - 16, 5);
		int m = n / 10 + 3;
		if (s->dir == 0) {
			int xy[6] = { x + n - 14 - m, y + n / 2 - m, x + n - 14, y + n / 2, x + n - 14 - m, y + n / 2 + m };
			g_poly(g, xy, 3, 3);
		} else {
			int xy[6] = { x + n / 2 - m, y + n - 14 - m, x + n / 2, y + n - 14, x + n / 2 + m, y + n - 14 - m };
			g_poly(g, xy, 3, 3);
		}
	}
	if (s->cur[r][c]) {
		char t[2] = { s->cur[r][c], 0 };
		g_print(g, x + 14, y + 14, n - 28, n - 28, (n - 28) * 80 / 100, s->given[r][c], t);
	}
}

static void key_box(const S *s, int k, int *x, int *y, int *w, int *h) {
	if (k == K_ERASE) { *x = s->kx + 13 * s->kw; *y = s->ky; *w = s->kw; *h = 2 * s->kh; return; }
	*x = s->kx + (k % 13) * s->kw;
	*y = s->ky + (k / 13) * s->kh;
	*w = s->kw; *h = s->kh;
}

static void draw_key(GameCtx *g, S *s, int k) {
	int x, y, w, h;
	key_box(s, k, &x, &y, &w, &h);
	g_frame(g, x + 5, y + 5, w - 10, h - 10, 3);
	if (k == K_ERASE) g_print(g, x + 8, y + 8, w - 16, h - 16, (w - 16) * 28 / 100, 1, "Erase");
	else {
		char t[2] = { (char)('A' + k), 0 };
		g_print(g, x + 8, y + 8, w - 16, h - 16, (h - 16) * 55 / 100, 1, t);
	}
}

static void draw_entry(GameCtx *g, S *s, int k) {
	g_print(g, s->ex[k], s->ey[k], s->ew, s->eh - 6, s->epx, 1, s->slot[k].w);
	if (s->crossed[k]) {
		int tw = s->slot[k].len * s->epx * 66 / 100 + 16, cx = s->ex[k] + s->ew / 2, cy = s->ey[k] + (s->eh - 6) / 2;
		if (tw > s->ew) tw = s->ew;
		g_line(g, cx - tw / 2, cy, cx + tw / 2, cy, 4);
	}
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah, lvl = g_level(g);
	g_area(g, &ax, &ay, &aw, &ah);
	build(g, s, lvl);
	note_theme(g, s);
	for (int i = 0; i < s->rows; i++)
		for (int j = 0; j < s->cols; j++) s->cur[i][j] = s->given[i][j] ? s->sol[i][j] : 0;
	s->selr = s->selc = -1;

	/* keypad at the bottom: A-M, N-Z, and a tall Erase key */
	s->kw = (aw - 2 * M) / KCOLS;
	if (s->kw > 140) s->kw = 140;
	s->kh = s->kw < 115 ? s->kw : 115;
	s->kx = ax + (aw - KCOLS * s->kw) / 2;
	s->ky = ay + ah - M - 2 * s->kh;
	/* the word list above it, one line (or more) per length */
	int maxlen = 0;
	for (int k = 0; k < s->nw; k++) {
		s->order[k] = k;
		if (s->slot[k].len > maxlen) maxlen = s->slot[k].len;
	}
	for (int a = 0; a < s->nw; a++)
		for (int b = a + 1; b < s->nw; b++) {
			const Slot *p = &s->slot[s->order[a]], *q = &s->slot[s->order[b]];
			if (q->len < p->len || (q->len == p->len && strcmp(q->w, p->w) < 0)) { int t = s->order[a]; s->order[a] = s->order[b]; s->order[b] = t; }
		}
	s->epx = 34;
	s->eh = 50;
	s->ew = maxlen * s->epx * 66 / 100 + 40;
	if (s->ew < 200) s->ew = 200;
	s->labw = 190;
	int per = (aw - 2 * M - s->labw) / s->ew;
	if (per < 1) per = 1;
	int lines = 0;
	for (int a = 0; a < s->nw;) {
		int b = a;
		while (b < s->nw && s->slot[s->order[b]].len == s->slot[s->order[a]].len) b++;
		lines += (b - a + per - 1) / per;
		a = b;
	}
	s->lx = ax + M;
	s->ly = s->ky - 30 - lines * s->eh;
	s->nlabels = 0;
	int line = 0;
	for (int a = 0; a < s->nw;) {
		int b = a, len = s->slot[s->order[a]].len;
		while (b < s->nw && s->slot[s->order[b]].len == len) b++;
		s->laby[s->nlabels] = s->ly + line * s->eh;
		s->lablen[s->nlabels++] = len;
		for (int i = a; i < b; i++) {
			int k = s->order[i];
			s->ex[k] = s->lx + s->labw + ((i - a) % per) * s->ew;
			s->ey[k] = s->ly + (line + (i - a) / per) * s->eh;
		}
		line += (b - a + per - 1) / per;
		a = b;
	}
	/* the grid in the space left at the top */
	int gtop = ay + M, gh = s->ly - 30 - gtop;
	s->cell = (aw - 2 * M) / s->cols;
	if (s->cell > gh / s->rows) s->cell = gh / s->rows;
	if (s->cell > 140) s->cell = 140;
	s->gx = ax + (aw - s->cols * s->cell) / 2;
	s->gy = gtop + (gh - s->rows * s->cell) / 2;

	g_speed(g, 12);
	for (int i = 0; i < s->rows; i++)
		for (int j = 0; j < s->cols; j++)
			if (s->sol[i][j]) {
				int x, y;
				cell_xy(s, i, j, &x, &y);
				g_frame(g, x, y, s->cell, s->cell, 3);
			}
	g_speed(g, 8);
	for (int i = 0; i < s->rows; i++)
		for (int j = 0; j < s->cols; j++)
			if (s->given[i][j]) {
				int x, y;
				cell_xy(s, i, j, &x, &y);
				char t[2] = { s->sol[i][j], 0 };
				g_print(g, x + 14, y + 14, s->cell - 28, s->cell - 28, (s->cell - 28) * 80 / 100, 1, t);
			}
	g_speed(g, 20);
	for (int i = 0; i < s->nlabels; i++) {
		char t[16];
		snprintf(t, sizeof t, "%d letters", s->lablen[i] % 100);
		g_print(g, s->lx, s->laby[i], s->labw - 10, s->eh - 6, s->epx * 85 / 100, 0, t);
	}
	for (int k = 0; k < s->nw; k++) draw_entry(g, s, k);
	g_speed(g, 25);
	for (int k = 0; k <= K_ERASE; k++) draw_key(g, s, k);
	snprintf(s->msg, sizeof s->msg, "Theme: %s - fit all %d words into the grid.", s->tname, s->nw);
	g_remark(g, s->msg);
	g_say(g, "tap a square, then letters below");
}

/* ---------------- play ---------------- */

static int in_grid(const S *s, int r, int c) { return r >= 0 && c >= 0 && r < s->rows && c < s->cols && s->sol[r][c]; }

static int slot_has(const Slot *sl, int r, int c) {
	if (sl->dir == 0) return r == sl->r && c >= sl->c && c < sl->c + sl->len;
	return c == sl->c && r >= sl->r && r < sl->r + sl->len;
}

static int slot_correct(const S *s, int k) {
	const Slot *sl = &s->slot[k];
	for (int p = 0; p < sl->len; p++)
		if (s->cur[sl->r + sl->dir * p][sl->c + !sl->dir * p] != sl->w[p]) return 0;
	return 1;
}

/* every square right, or (if the layout allows another fill) every slot holds a different listed word */
static int solved(const S *s) {
	int all = 1;
	for (int i = 0; i < s->rows && all; i++)
		for (int j = 0; j < s->cols && all; j++)
			if (s->sol[i][j] && s->cur[i][j] != s->sol[i][j]) all = 0;
	if (all) return 1;
	unsigned char used[MAXW] = { 0 };
	for (int k = 0; k < s->nw; k++) {
		const Slot *sl = &s->slot[k];
		char w[WLEN];
		for (int p = 0; p < sl->len; p++) {
			w[p] = s->cur[sl->r + sl->dir * p][sl->c + !sl->dir * p];
			if (!w[p]) return 0;
		}
		w[sl->len] = 0;
		int hit = -1;
		for (int i = 0; i < s->nw && hit < 0; i++)
			if (!used[i] && !strcmp(s->slot[i].w, w)) hit = i;
		if (hit < 0) return 0;
		used[hit] = 1;
	}
	return 1;
}

static void update_words(GameCtx *g, S *s, int r, int c) {
	for (int k = 0; k < s->nw; k++) {
		if (!slot_has(&s->slot[k], r, c)) continue;
		int ok = slot_correct(s, k);
		if (ok == s->crossed[k]) continue;
		s->crossed[k] = (unsigned char)ok;
		g_speed(g, ok ? 4 : 30);
		if (!ok) g_clear(g, s->ex[k], s->ey[k], s->ew, s->eh - 6);
		draw_entry(g, s, k);
	}
}

static void select_cell(GameCtx *g, S *s, int r, int c) {
	int pr = s->selr, pc = s->selc;
	s->selr = r; s->selc = c;
	g_speed(g, 30);
	if (pr >= 0 && (pr != r || pc != c)) draw_cell(g, s, pr, pc);
	if (r >= 0) draw_cell(g, s, r, c);
}

/* the next (step = +1) or previous (-1) square of the current word that the player fills in, or 0 */
static int step_cell(const S *s, int step, int *r, int *c) {
	int dr = s->dir * step, dc = !s->dir * step, rr = s->selr + dr, cc = s->selc + dc;
	while (in_grid(s, rr, cc) && s->given[rr][cc]) { rr += dr; cc += dc; }
	if (!in_grid(s, rr, cc)) return 0;
	*r = rr; *c = cc;
	return 1;
}

static void type_letter(GameCtx *g, S *s, char ch) {
	int r = s->selr, c = s->selc, nr, nc;
	s->cur[r][c] = ch;
	g_speed(g, 8);
	draw_cell(g, s, r, c);
	update_words(g, s, r, c);
	if (solved(s)) {
		select_cell(g, s, -1, -1);
		if (s->theme >= 0) snprintf(s->msg, sizeof s->msg, "Solved! All %d %s words are in place.", s->nw, s->tname);
		else snprintf(s->msg, sizeof s->msg, "Solved! All %d %s are in place.", s->nw, s->tname);
		s->over = 1;
		g_over(g, s->msg);
		return;
	}
	if (step_cell(s, 1, &nr, &nc)) select_cell(g, s, nr, nc);
	g_say(g, "%c", ch);
}

static void erase(GameCtx *g, S *s) {
	int r = s->selr, c = s->selc;
	if (!s->cur[r][c]) {
		if (!step_cell(s, -1, &r, &c)) { g_say(g, "nothing to erase here"); return; }
		select_cell(g, s, r, c);
	}
	s->cur[r][c] = 0;
	g_speed(g, 30);
	draw_cell(g, s, r, c);
	update_words(g, s, r, c);
	g_say(g, "erased");
}

static int key_at(const S *s, int px, int py) {
	for (int k = 0; k <= K_ERASE; k++) {
		int x, y, w, h;
		key_box(s, k, &x, &y, &w, &h);
		if (px >= x && px < x + w && py >= y && py < y + h) return k;
	}
	return -1;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->over) return;
	if (!s->ready) {
		if (g_busy(g)) { g_say(g, "one moment - still drawing the puzzle"); return; }
		s->ready = 1;
	}
	if (!st->tap && (st->x1 - st->x0 > 60 || st->y1 - st->y0 > 60)) { g_say(g, "tap a square, then a letter key"); return; }
	int k = key_at(s, st->cx, st->cy);
	if (k >= 0) {
		if (s->selr < 0) { g_say(g, "tap a square in the grid first"); return; }
		if (k == K_ERASE) erase(g, s);
		else type_letter(g, s, (char)('A' + k));
		return;
	}
	if (st->cx >= s->gx && st->cy >= s->gy) {
		int c = (st->cx - s->gx) / s->cell, r = (st->cy - s->gy) / s->cell;
		if (in_grid(s, r, c)) {
			if (s->given[r][c]) { g_say(g, "that letter is given - tap an empty square"); return; }
			if (r == s->selr && c == s->selc) {
				if (s->cdir[r][c] == 3) { s->dir = !s->dir; select_cell(g, s, r, c); }
				g_say(g, "writing %s", s->dir ? "down" : "across");
				return;
			}
			if (s->cdir[r][c] != 3) s->dir = s->cdir[r][c] == 2;
			select_cell(g, s, r, c);
			g_say(g, s->cdir[r][c] == 3 ? "writing %s - tap the square again to turn" : "writing %s", s->dir ? "down" : "across");
			return;
		}
	}
	g_say(g, "tap a square in the grid, then a letter key");
}

const GameDef GAME_crisscross = { "crisscross", "Criss-Cross", "Fit the themed words into the crossword grid", sizeof(S), 0, 1, start, stroke, 0 };

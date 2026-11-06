/* Game engine: the Games menu, input routing, queued pen-style drawing and timers for the local games
 * (one file per game in games/, listed in games/registry.c), plus the AI games, which run as a conversation with a game prompt. */
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "games.h"
#include "hershey_font.h"
#include "scribe.h"

/* Games' own records (g_save) and saved games live here, root-only like the rest of SECURE_DIR. */
#define GAMES_DIR SECURE_DIR "/games"

extern const GameDef *const GAMES[];
extern const int NGAMES_ALL;

struct GameCtx {
	App *a;
	const GameDef *def;
	void *state;
	int level, over;
	double speed;
	unsigned rng;
	int timer_pending, timer_ms;
	double timer_at;
	int ax, ay, aw, ah; /* play area */
	int ry, rh;         /* remark strip */
};

static GameCtx ctx;
static int menu_level = 1;
static int menu_tab = 0; /* 0 board & puzzle games, 1 AI games */

/* ---------- AI games: a conversation with a game prompt ---------- */
typedef struct {
	const char *name, *desc, *intro, *prompt;
} AiGame;

static const AiGame AIGAMES[] = {
	{ "Guess My Drawing", "Draw something, the AI guesses", "Draw something, then pause - I'll guess!",
	  "Game: Guess my drawing. The writer draws something; you guess what it is in one short, playful line, like a "
	  "friend playing a drawing game. If unsure, give your best two guesses. If they write yes/correct, celebrate "
	  "briefly and invite the next drawing; if no, guess again. Keep the score in `memo` and mention it now and then." },
	{ "20 Questions", "The AI thinks of something - you ask", "Write 'start' and pause, then ask yes/no questions.",
	  "Game: 20 questions. At the start (the writer writes start, or there is no game yet in memo), secretly choose "
	  "an everyday thing suitable for all ages (an object, animal or place), store it in `memo` with the question "
	  "count, and invite the first question. Then answer each yes/no question with Yes, No or Sometimes plus a tiny "
	  "playful hint, and count ('Question 5 of 20'). If they guess it, celebrate; after 20, reveal it. Never reveal "
	  "the answer early. Always carry the secret and count forward in `memo`." },
	{ "Riddles", "Solve riddles, keep score", "Write 'start' and pause for your first riddle.",
	  "Game: riddles. Ask one riddle suitable for all ages and keep its answer and the score in `memo`. When the "
	  "writer answers, say whether they got it (give the answer if not), update the score, then ask the next riddle." },
	{ "Story Builder", "Write a story together", "Write the first line of a story and pause.",
	  "Game: story builder. Take turns writing a story with the writer: they write a sentence or two, you continue "
	  "with one or two vivid sentences that leave room for them. Keep it imaginative and suitable for all ages. "
	  "Keep a one-line summary of the plot so far in `memo`." },
	{ "I Draw, You Guess", "The AI sketches - you guess what it is", "Write 'start' and pause - I'll draw something.",
	  "Game: I draw, you guess. Pick a simple, recognisable everyday thing (an animal, object, vehicle or place) and "
	  "describe it in `illustrate` so the illustrator draws it - no letters or text in the picture - but don't name it in "
	  "`reply`: just ask what it is; keep the answer and the score in `memo`. When "
	  "the writer guesses, say whether they're right (a hint if not; reveal it after 3 tries), then draw the next one." },
	{ "Trivia Quiz", "Multiple-choice questions, keep score", "Write 'start' (or a topic) and pause.",
	  "Game: trivia quiz. Ask one multiple-choice question at a time (A to D), general knowledge suitable for all ages "
	  "or on the writer's chosen topic; keep the right answer and the score in `memo`. When they answer, say whether "
	  "they're right with a one-line fun fact, update the score, then ask the next question. Facts must be accurate." },
	{ "Would You Rather", "Fun choices, the AI reacts", "Write 'start' and pause.",
	  "Game: would you rather. Ask a fun, all-ages would-you-rather question. When the writer answers, react "
	  "playfully to their choice, say which you'd pick and why, then ask a new one." },
	{ "Two Truths and a Lie", "Spot the false fact", "Write 'start' (or a topic) and pause.",
	  "Game: two truths and a lie. Give three short numbered statements about a fun topic (animals, space, history, "
	  "food...), two true and one false; keep which is false and the score in `memo`. When the writer guesses, reveal "
	  "the lie with a fun explanation, update the score and play again. The true facts must be accurate." },
	{ "Word Chain", "Each word starts with the last letter", "Write any word and pause.",
	  "Game: word chain. Take turns: each word must start with the last letter of the previous word, with no repeats "
	  "(keep the used words in `memo`). Check the writer's word; if it breaks the rule, explain kindly and let them try "
	  "again. Then give your word. Use common words." },
	{ "Unscramble", "Untangle the scrambled word", "Write 'start' and pause.",
	  "Game: unscramble. Give one scrambled word (letters separated by spaces) with a small hint, sized to how the "
	  "writer is doing; keep the answer and score in `memo`. Check their answer, celebrate or give another hint, then "
	  "give the next one." },
	{ "Math Challenge", "Mental maths that gets harder", "Write 'start' and pause.",
	  "Game: math challenge. Ask one mental-maths question at a time, starting easy and getting harder as they get "
	  "answers right (keep the level and score in `memo`). Check their handwritten answer, explain briefly if it's "
	  "wrong, then ask the next one." },
	{ "Mystery Detective", "Solve a little whodunit", "Write 'start' and pause.",
	  "Game: mystery detective. Run a short, all-ages whodunit: set the scene with a small mystery and three suspects; "
	  "keep the truth and the clues in `memo`. The writer asks questions or investigates; answer in character and reveal "
	  "clues fairly. When they accuse someone, reveal whether they solved it and how." },
	{ "Dungeon Quest", "A fantasy adventure - you choose", "Write 'start' and pause. Try a fantasy voice!",
	  "Game: dungeon quest. Be the game master of a light-hearted fantasy adventure for all ages. Describe the scene in "
	  "two or three vivid sentences and end with a choice; the writer writes what they do. Keep their health, items and "
	  "location in `memo`, roll dice when it adds suspense, and keep the story moving." },
	{ "Rhyme Duel", "Take turns writing a silly poem", "Write a first line and pause.",
	  "Game: rhyme duel. Take turns writing lines of a silly poem; each line should rhyme with or follow the rhythm of "
	  "the one before. Cheer their line in a few words, then add yours. After eight lines, give the poem a title and "
	  "start a new one." },
	{ "Categories", "A letter, three categories, go!", "Write 'start' and pause.",
	  "Game: categories. Give a letter and three categories (for example an animal, a food, a place). The writer writes "
	  "one word for each starting with that letter; score one point per good answer (keep the total in `memo`), share "
	  "your own answers, then give a new letter." },
	{ "Reverse 20 Questions", "You think of something - the AI asks", "Think of something, write 'ready' and pause.",
	  "Game: reverse 20 questions. The writer thinks of something; ask one yes/no question at a time to work out what it "
	  "is, keeping what you've learned and the question count in `memo`, and guess when you're fairly sure (at most 20 "
	  "questions). Be playful about it." },
};
#define NAIGAMES ((int)(sizeof AIGAMES / sizeof AIGAMES[0]))

/* ---------- drawing helpers (queued ops) ---------- */
static int dt_for(double len, double speed) { return (int)(len / (1.8 * speed)); }

static void add_stroke(GameCtx *g, const int *xy, int n, int width, int first_dt) {
	if (n < 1) return;
	App *a = g->a;
	/* subdivide long segments so lines grow smoothly instead of popping in */
	int cap = n * 8 + 8, m = 0;
	int16_t *pts = malloc((size_t)cap * 2 * sizeof *pts);
	uint16_t *dts = malloc((size_t)cap * sizeof *dts);
	pts[0] = (int16_t)xy[0]; pts[1] = (int16_t)xy[1]; dts[0] = (uint16_t)first_dt; m = 1;
	for (int i = 1; i < n; i++) {
		double dx = xy[2 * i] - xy[2 * i - 2], dy = xy[2 * i + 1] - xy[2 * i - 1], len = sqrt(dx * dx + dy * dy);
		int parts = (int)(len / 40) + 1;
		if (m + parts >= cap) { cap = (m + parts) * 2; pts = realloc(pts, (size_t)cap * 2 * sizeof *pts); dts = realloc(dts, (size_t)cap * sizeof *dts); }
		for (int p = 1; p <= parts; p++) {
			pts[2 * m] = (int16_t)(xy[2 * i - 2] + dx * p / parts);
			pts[2 * m + 1] = (int16_t)(xy[2 * i - 1] + dy * p / parts);
			dts[m] = (uint16_t)dt_for(len / parts, g->speed);
			m++;
		}
	}
	Op *o = op_new(a, OP_STROKE);
	o->a = width;
	o->n = m;
	o->xy = pts;
	o->dt = dts;
}

void g_speed(GameCtx *g, double f) { g->speed = f > 0.1 ? f : 4; }
void g_line(GameCtx *g, int x0, int y0, int x1, int y1, int w) { int xy[4] = { x0, y0, x1, y1 }; add_stroke(g, xy, 2, w, 30); }
void g_poly(GameCtx *g, const int *xy, int n, int w) { add_stroke(g, xy, n, w, 30); }
void g_circle(GameCtx *g, int cx, int cy, int r, int w) {
	int xy[2 * 49];
	for (int i = 0; i <= 48; i++) {
		double t = -M_PI / 2 + 2 * M_PI * i / 48;
		xy[2 * i] = cx + (int)lround(r * cos(t));
		xy[2 * i + 1] = cy + (int)lround(r * sin(t));
	}
	add_stroke(g, xy, 49, w, 30);
}
void g_cross(GameCtx *g, int cx, int cy, int r, int w) {
	g_line(g, cx - r, cy - r, cx + r, cy + r, w);
	g_line(g, cx + r, cy - r, cx - r, cy + r, w);
}
void g_frame(GameCtx *g, int x, int y, int w, int h, int lw) {
	int xy[10] = { x, y, x + w, y, x + w, y + h, x, y + h, x, y };
	add_stroke(g, xy, 5, lw, 30);
}
void g_dot(GameCtx *g, int cx, int cy, int r) {
	Op *o = op_new(g->a, OP_DISC);
	o->a = cx; o->b = cy; o->c = r;
}
void g_box(GameCtx *g, int x, int y, int w, int h, int gray) {
	Op *o = op_new(g->a, OP_FILL);
	o->a = x; o->b = y; o->c = w; o->d = h; o->style = gray;
}
void g_clear(GameCtx *g, int x, int y, int w, int h) { g_box(g, x, y, w, h, 255); }
void g_pause(GameCtx *g, int ms) { op_new(g->a, OP_PAUSE)->a = ms; }

int g_write_width(GameCtx *g, int cap, const char *t) {
	const HFont *F = &HFONTS[g->a->hfont];
	double s = (double)cap / F->cap, w = 0;
	for (const unsigned char *c = (const unsigned char *)t; *c; c++) {
		const HGlyph *gl = &F->g[(*c < 32 || *c > 126 ? '?' : *c) - 32];
		w += (gl->right - gl->left) * s;
	}
	return (int)w;
}

void g_write(GameCtx *g, int x, int base, int cap, const char *t) {
	const HFont *F = &HFONTS[g->a->hfont];
	double s = (double)cap / F->cap, cx = x;
	int first = 1;
	for (const unsigned char *c = (const unsigned char *)t; *c; c++) {
		const HGlyph *gl = &F->g[(*c < 32 || *c > 126 ? '?' : *c) - 32];
		const signed char *v = F->pts + 2 * gl->off;
		double ox = cx - gl->left * s;
		int k = 0;
		while (k < gl->n) {
			int st = k;
			while (k < gl->n && v[2 * k] != -128) k++;
			int cnt = k - st;
			if (cnt > 0) {
				int *xy = malloc((size_t)cnt * 2 * sizeof *xy);
				for (int m = 0; m < cnt; m++) {
					xy[2 * m] = (int)lround(ox + v[2 * (st + m)] * s);
					xy[2 * m + 1] = (int)lround(base + (v[2 * (st + m) + 1] - 9) * s);
				}
				add_stroke(g, xy, cnt, cap >= 50 ? 4 : 3, first ? 60 : 25);
				free(xy);
				first = 0;
			}
			k++;
		}
		cx += (gl->right - gl->left) * s;
	}
}

void g_write_center(GameCtx *g, int cx, int base, int cap, const char *t) { g_write(g, cx - g_write_width(g, cap, t) / 2, base, cap, t); }

void g_print(GameCtx *g, int x, int y, int w, int h, int px, int bold, const char *t) {
	Op *o = op_new(g->a, OP_TEXT);
	o->a = x; o->b = y; o->c = w; o->d = h; o->size = px;
	o->style = (bold ? TXT_BOLD : TXT_REGULAR) | 0x200 | 0x400;
	o->s = strdup(t);
}

/* ---------- state, talk, timers ---------- */
void *g_state(GameCtx *g) { return g->state; }
int g_level(GameCtx *g) { return g->level; }
int g_rand(GameCtx *g, int n) {
	g->rng ^= g->rng << 13; g->rng ^= g->rng >> 17; g->rng ^= g->rng << 5;
	return n > 0 ? (int)(g->rng % (unsigned)n) : 0;
}
void g_area(GameCtx *g, int *x, int *y, int *w, int *h) { *x = g->ax; *y = g->ay; *w = g->aw; *h = g->ah; }
int g_busy(GameCtx *g) { return g->a->nops > 0; }

/* ---------- persistent storage: GAMES_DIR/<game id>.<key>.dat ---------- */
#define REC_MAX 65536

static int write_all(int fd, const void *p, size_t n) {
	const char *c = p;
	while (n) {
		ssize_t w = write(fd, c, n);
		if (w <= 0) return 0;
		c += w; n -= (size_t)w;
	}
	return 1;
}

/* Write a file safely: a temporary file, flushed, then renamed over the old one. Two parts so a header can go first. */
static int store_write(const char *path, const void *a, size_t na, const void *b, size_t nb) {
	char tmp[300];
	mkdir(SECURE_DIR, 0700);
	mkdir(GAMES_DIR, 0700);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return -1;
	int ok = write_all(fd, a, na) && write_all(fd, b, nb);
	if (fsync(fd)) ok = 0;
	close(fd);
	if (!ok || rename(tmp, path)) { unlink(tmp); return -1; }
	return 0;
}

static int rec_path(GameCtx *g, const char *key, char *p, size_t n) {
	size_t len = key ? strlen(key) : 0;
	if (!g->def || len < 1 || len > 31) return 0;
	for (size_t i = 0; i < len; i++)
		if (!((key[i] >= 'a' && key[i] <= 'z') || (key[i] >= '0' && key[i] <= '9') || key[i] == '_' || key[i] == '-')) return 0;
	snprintf(p, n, GAMES_DIR "/%s.%s.dat", g->def->id, key);
	return 1;
}

int g_load(GameCtx *g, const char *key, void *buf, size_t n) {
	char p[256];
	if (!rec_path(g, key, p, sizeof p)) { logf_("game: bad record key"); return 0; }
	int fd = open(p, O_RDONLY);
	if (fd < 0) return 0;
	size_t got = 0;
	while (got < n) {
		ssize_t r = read(fd, (char *)buf + got, n - got);
		if (r <= 0) break;
		got += (size_t)r;
	}
	close(fd);
	return (int)got;
}

void g_save(GameCtx *g, const char *key, const void *buf, size_t n) {
	char p[256];
	if (!rec_path(g, key, p, sizeof p) || n > REC_MAX) { logf_("game: bad record %s", key ? key : "?"); return; }
	if (store_write(p, buf, n, "", 0)) logf_("game: could not save %s", p);
}

void g_say(GameCtx *g, const char *fmt, ...) {
	char b[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	ui_set_status(g->a, "%s: %s", g->def->name, b);
}

void g_remark(GameCtx *g, const char *t) {
	double sp = g->speed;
	g_clear(g, g->ax, g->ry + 6, g->aw, g->rh - 6);
	g->speed = 2.5;
	int cap = 52;
	while (cap > 30 && g_write_width(g, cap, t) > g->aw - 40) cap -= 4;
	g_write_center(g, g->ax + g->aw / 2, g->ry + g->rh / 2 + cap / 2, cap, t);
	g->speed = sp;
}

void g_after(GameCtx *g, int ms) { g->timer_pending = 1; g->timer_ms = ms; g->timer_at = 0; }

/* ---------- saved games (GameDef.resume): GAMES_DIR/<id>.saved = a header, then the state bytes ---------- */
#define SAVE_MAGIC "SCRGSAV1"
typedef struct {
	char magic[8];
	char id[24];
	int32_t level, version;
	uint32_t state_size;
	char progress[48]; /* the game's progress() text when it was saved */
} SaveHdr;

static void save_path(const GameDef *d, char *p, size_t n) { snprintf(p, n, GAMES_DIR "/%s.saved", d->id); }

static void save_drop(const GameDef *d) {
	char p[256];
	if (!d || !d->resume) return;
	save_path(d, p, sizeof p);
	unlink(p);
}

/* d's saved game, checked against the game as it is built now (id, version, state size, level, file size); a save
 * that doesn't match is deleted. state may be NULL to check the header only. Returns 1 if there is a usable save. */
static int save_read(const GameDef *d, SaveHdr *h, void *state) {
	char p[256];
	struct stat st;
	if (!d || !d->resume) return 0;
	save_path(d, p, sizeof p);
	int fd = open(p, O_RDONLY);
	if (fd < 0) return 0;
	int ok = !fstat(fd, &st) && st.st_size == (off_t)(sizeof *h + d->state_size) && read(fd, h, sizeof *h) == (ssize_t)sizeof *h &&
		 !memcmp(h->magic, SAVE_MAGIC, sizeof h->magic) && !h->id[sizeof h->id - 1] && !strcmp(h->id, d->id) &&
		 h->version == d->save_version && h->state_size == d->state_size && h->level >= 0 && h->level <= 2;
	if (ok && state) ok = read(fd, state, d->state_size) == (ssize_t)d->state_size;
	close(fd);
	if (!ok) {
		logf_("game: saved %s does not match this version - dropped", d->id);
		unlink(p);
		return 0;
	}
	h->progress[sizeof h->progress - 1] = 0;
	for (char *c = h->progress; *c; c++) if (*c < 32 || *c > 126) *c = '?';
	return 1;
}

/* Leaving the running game (Close, Games, Exit, power key): keep it if it's unfinished, forget it if it's over. */
static void save_current_q(int quiet) {
	const GameDef *d = ctx.def;
	if (!d || !d->resume || !ctx.state || strlen(d->id) >= sizeof ((SaveHdr *)0)->id) return;
	if (ctx.over) { save_drop(d); return; }
	SaveHdr h;
	char p[256];
	memset(&h, 0, sizeof h);
	memcpy(h.magic, SAVE_MAGIC, sizeof h.magic);
	snprintf(h.id, sizeof h.id, "%s", d->id);
	h.level = ctx.level;
	h.version = d->save_version;
	h.state_size = (uint32_t)d->state_size;
	if (d->progress) d->progress(&ctx, h.progress, (int)sizeof h.progress);
	save_path(d, p, sizeof p);
	if (store_write(p, &h, sizeof h, ctx.state, d->state_size)) logf_("game: could not save %s", p);
	else if (!quiet) logf_("game: saved %s (level %d, %s)", d->id, ctx.level, h.progress);
}

static void save_current(void) { save_current_q(0); }

void g_over(GameCtx *g, const char *result) {
	g->over = 1;
	g->timer_pending = 0;
	save_drop(g->def); /* finished: nothing to continue */
	g_remark(g, result);
	ui_set_status(g->a, "%s: %s - tap the board to play again, or Close to stop", g->def->name, result);
}

/* ---------- menu ---------- */
typedef struct {
	int x, y, w, h, kind, idx; /* kind 0 local game, 1 AI game, 2 level, 3 tab, 4 continue, 5 new, 6 back */
} MHit;
static MHit mh[64];
static int nmh;
static int choice = -1; /* a local game with a saved game: the Continue / New puzzle screen is showing */

static void fill(App *a, int x, int y, int w, int h, uint8_t gray) {
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, gray);
}

static void choice_button(App *a, int x, int y, int w, int h, const char *title, const char *sub, int dark, int kind) {
	fill(a, x, y, w, h, 0x00);
	if (!dark) fill(a, x + 6, y + 6, w - 12, h - 12, 0xFF);
	int inv = dark ? 0x800 : 0;
	if (sub) {
		text_draw(a, title, x, y + h / 2 - 95, w, 90, 64, TXT_BOLD | 0x400 | inv);
		text_draw(a, sub, x, y + h / 2 + 15, w, 60, 38, TXT_REGULAR | 0x400 | inv);
	} else text_draw(a, title, x, y, w, h, 46, TXT_BOLD | 0x400 | inv);
	mh[nmh++] = (MHit){ x, y, w, h, kind, choice };
	hits_note(kind == 4 ? "choice:continue" : kind == 5 ? "choice:new" : "choice:back", x, y, w, h);
}

/* A game with a saved game was tapped: Continue it, or start a new one (which replaces it)? */
static void draw_choice(App *a, const DispRefresh *how) {
	static const char *lv[3] = { "Easy", "Medium", "Hard" };
	const GameDef *d = GAMES[choice];
	SaveHdr h;
	char sub[96];
	int L = 60, W = a->W - 2 * L, y = a->cy + 36, bh = 260;
	if (!save_read(d, &h, NULL)) { choice = -1; return; } /* (gone since the tap: the caller draws the menu) */
	nmh = 0;
	hits_reset();
	fill(a, a->cx, a->cy, a->cw, a->ch, 0xFF);
	text_draw(a, d->name, L, y, W, 80, 64, TXT_BOLD);
	text_draw(a, "You have an unfinished puzzle. Carry on with it, or start a new one?", L, y + 110, W, 50, 36, TXT_REGULAR);
	y = a->cy + 300;
	snprintf(sub, sizeof sub, "%s%s%s", d->levels ? lv[h.level] : "", d->levels && h.progress[0] ? " - " : "", h.progress);
	choice_button(a, L, y, W, bh, "Continue", sub[0] ? sub : NULL, 1, 4);
	y += bh + 60;
	snprintf(sub, sizeof sub, "%s%sthe saved one is thrown away", d->levels ? lv[menu_level] : "", d->levels ? " - " : "");
	choice_button(a, L, y, W, bh, "New puzzle", sub, 0, 5);
	y += bh + 60;
	choice_button(a, (a->W - 600) / 2, y, 600, 150, "Back to the games", NULL, 0, 6);
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, how);
}

static void draw_menu(App *a, const DispRefresh *how) {
	if (choice >= 0) draw_choice(a, how);
	if (choice >= 0) return;
	nmh = 0;
	hits_reset();
	fill(a, a->cx, a->cy, a->cw, a->ch, 0xFF);
	int L = 60, y = a->cy + 36;
	text_draw(a, "Games", L, y, 600, 80, 64, TXT_BOLD);
	const char *lv[3] = { "Easy", "Medium", "Hard" };
	for (int i = 0; i < 3 && menu_tab == 0; i++) {
		int w = 200, x = a->W - L - (3 - i) * (w + 16) + 16;
		int sel = i == menu_level;
		fill(a, x, y, w, 90, 0x00);
		if (!sel) fill(a, x + 5, y + 5, w - 10, 80, 0xFF);
		DispText t = { .left = x, .top = y + 24, .right = x + w, .bottom = y + 90, .px = 36,
			       .style = sel ? TXT_BOLD : TXT_REGULAR, .centered = true, .inverted = sel };
		disp_text(&a->disp, &a->font_ui, lv[i], &t, NULL);
		mh[nmh++] = (MHit){ x, y, w, 90, 2, i };
		hits_note(lv[i], x, y, w, 90);
	}
	y += 120;
	/* tabs */
	int nlocal = 0;
	for (int i = 0; i < NGAMES_ALL; i++) if (GAMES[i]) nlocal++;
	char t0[48], t1[48];
	snprintf(t0, sizeof t0, "Board & puzzles (%d)", nlocal);
	snprintf(t1, sizeof t1, "AI games (%d)", NAIGAMES);
	const char *tabs[2] = { t0, t1 };
	int tabw = (a->W - 2 * L - 18) / 2;
	for (int i = 0; i < 2; i++) {
		int x = L + i * (tabw + 18), sel = i == menu_tab;
		fill(a, x, y, tabw, 100, 0x00);
		if (!sel) fill(a, x + 5, y + 5, tabw - 10, 90, 0xFF);
		text_draw(a, tabs[i], x, y, tabw, 100, 42, TXT_BOLD | 0x400 | (sel ? 0x800 : 0));
		mh[nmh++] = (MHit){ x, y, tabw, 100, 3, i };
		hits_note(i ? "tab:ai" : "tab:local", x, y, tabw, 100);
	}
	y += 124;
	text_draw(a, menu_tab ? "These games use your AI: write on the page and pause, like a conversation. A fantasy voice "
				"makes them more fun (Settings)." : "These games play themselves on the Kindle - instant, offline. Pick a "
				"difficulty above.", L, y, a->W - 2 * L, 44, 30, TXT_ITALIC);
	y += 64;
	int cols = 2, gap = 18, tw = (a->W - 2 * L - gap) / cols, th = 150, n = 0;
	for (int i = 0; i < NGAMES_ALL + NAIGAMES; i++) {
		const char *name, *desc;
		int kind, idx;
		if (i < NGAMES_ALL) { if (!GAMES[i] || menu_tab != 0) continue; name = GAMES[i]->name; desc = GAMES[i]->desc; kind = 0; idx = i; }
		else { if (menu_tab != 1) continue; idx = i - NGAMES_ALL; name = AIGAMES[idx].name; desc = AIGAMES[idx].desc; kind = 1; }
		int x = L + (n % cols) * (tw + gap), ty = y + (n / cols) * (th + gap);
		if (ty + th > a->cy + a->ch - 10) break;
		SaveHdr sh;
		char sdesc[96];
		int saved = !kind && save_read(GAMES[idx], &sh, NULL), bw = saved ? 124 : 64; /* badge: SAVED or AI */
		if (saved) {
			snprintf(sdesc, sizeof sdesc, "Saved puzzle: %s%s%s", GAMES[idx]->levels ? lv[sh.level] : "",
				 GAMES[idx]->levels && sh.progress[0] ? ", " : "", sh.progress);
			desc = sdesc;
		}
		fill(a, x, ty, tw, th, 0x00);
		fill(a, x + 4, ty + 4, tw - 8, th - 8, 0xFF);
		if (kind || saved) fill(a, x + tw - 26 - bw, ty + 18, bw, 34, 0x00);
		text_draw(a, name, x + 28, ty + 22, tw - 76 - bw, 56, 42, TXT_BOLD);
		text_draw(a, desc, x + 28, ty + 86, tw - 56, 44, 30, TXT_REGULAR);
		if (kind || saved) {
			DispText t = { .left = x + tw - 26 - bw, .top = ty + 21, .right = x + tw - 26, .bottom = ty + 52, .px = 24,
				       .style = TXT_BOLD, .centered = true, .inverted = true };
			disp_text(&a->disp, &a->font_ui, saved ? "SAVED" : "AI", &t, NULL);
		}
		mh[nmh++] = (MHit){ x, ty, tw, th, kind, idx };
		char hl[72];
		snprintf(hl, sizeof hl, "%s:%s", kind ? "ai" : "game", kind ? name : GAMES[idx]->id);
		hits_note(hl, x, ty, tw, th);
		n++;
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, how);
}

/* Leaving a running game: save it (if the game can resume and isn't finished) and free it. */
static void leave_game(App *a) {
	if (a->in_games != 2) return;
	save_current();
	free(ctx.state);
	memset(&ctx, 0, sizeof ctx);
}

void games_open_menu(App *a) {
	leave_game(a);
	if (a->in_games == 0) page_save(a);
	reply_free(a);
	a->in_games = 1;
	a->game_prompt = NULL;
	a->autosend_at = 0;
	choice = -1;
	draw_menu(a, &a->flash);
	ui_set_status(a, "games - tap one to play, or Close to go back to your page");
}

void games_close(App *a) {
	leave_game(a);
	reply_free(a);
	a->in_games = 0;
	choice = -1;
	page_load(a, a->page);
	ui_set_status(a, "back to your notebook - page %d of %d", a->page, a->last_page);
}

/* The app is exiting (Exit, power key, signal): keep an unfinished game for next time. */
void games_exit(App *a) { leave_game(a); }

/* A fresh context for game d at this level: clears the canvas, draws the remark strip. */
static void setup_game(App *a, const GameDef *d, int level) {
	free(ctx.state);
	memset(&ctx, 0, sizeof ctx);
	ctx.a = a;
	ctx.def = d;
	ctx.state = calloc(1, d->state_size ? d->state_size : 1);
	ctx.level = level;
	ctx.speed = 4;
	ctx.rng = (unsigned)time(NULL) * 2654435761u | 1u;
	ctx.rh = 220;
	ctx.ax = a->cx + 40; ctx.ay = a->cy + 30; ctx.aw = a->cw - 80; ctx.ah = a->ch - 30 - ctx.rh - 20;
	ctx.ry = a->cy + a->ch - ctx.rh;
	a->in_games = 2;
	reply_free(a);
	clear_canvas(a);
	g_box(&ctx, a->cx + 40, ctx.ry, a->cw - 80, 3, 0x00); /* remark strip separator */
	ui_set_status(a, "%s - %s", d->name, d->levels ? (const char *[]){ "easy", "medium", "hard" }[ctx.level] : "have fun");
}

static void start_game(App *a, int idx) {
	const GameDef *d = GAMES[idx];
	save_drop(d); /* a new game replaces the saved one */
	setup_game(a, d, menu_level);
	d->start(&ctx);
	logf_("game: %s (level %d)", d->id, ctx.level);
}

/* Continue the saved game: restore the state bytes and let the game redraw itself. */
static void resume_game(App *a, int idx) {
	const GameDef *d = GAMES[idx];
	SaveHdr h;
	void *st = calloc(1, d->state_size ? d->state_size : 1);
	if (!save_read(d, &h, st)) { free(st); start_game(a, idx); return; }
	setup_game(a, d, h.level);
	memcpy(ctx.state, st, d->state_size);
	free(st);
	d->resume(&ctx);
	logf_("game: %s resumed (level %d, %s)", d->id, ctx.level, h.progress);
}

static void start_ai_game(App *a, int idx) {
	free(ctx.state);
	memset(&ctx, 0, sizeof ctx);
	a->in_games = 0;
	a->game_prompt = AIGAMES[idx].prompt;
	/* a fresh notebook page for the game; the conversation continues normally from here */
	pending_clear(a);
	a->page = a->last_page + 1;
	a->last_page = a->page;
	clear_canvas(a);
	state_save(a);
	brain_new_page(a);
	for (int i = 0; i < a->nmodes; i++)
		if (!strcmp(a->mode_id[i], "talk")) a->mode = i;
	ui_draw_bar(a);
	ui_set_status(a, "%s: %s", AIGAMES[idx].name, AIGAMES[idx].intro);
	logf_("AI game: %s", AIGAMES[idx].name);
}

void games_menu_tap(App *a, int x, int y) {
	for (int i = 0; i < nmh; i++) {
		MHit *h = &mh[i];
		if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
		if (h->kind == 2) { menu_level = h->idx; draw_menu(a, &a->gc16); return; }
		if (h->kind == 3) { menu_tab = h->idx; draw_menu(a, &a->flash); return; }
		if (h->kind >= 4) { /* the Continue / New puzzle screen */
			choice = -1;
			if (h->kind == 4) resume_game(a, h->idx);
			else if (h->kind == 5) start_game(a, h->idx);
			else {
				draw_menu(a, &a->flash);
				ui_set_status(a, "games - tap one to play, or Close to go back to your page");
			}
			return;
		}
		if (h->kind == 1) {
			if (a->provider == PROV_NONE) { ui_set_status(a, "AI games need an API key (see config.txt)"); return; }
			start_ai_game(a, h->idx);
			return;
		}
		SaveHdr sh;
		if (save_read(GAMES[h->idx], &sh, NULL)) { /* a saved game: ask first */
			choice = h->idx;
			draw_menu(a, &a->flash);
			ui_set_status(a, "%s: continue your saved puzzle, or start a new one", GAMES[h->idx]->name);
			return;
		}
		start_game(a, h->idx);
		return;
	}
}

/* A finished pen stroke or tap on the canvas while a game is running. */
void games_stroke(App *a, const Stroke *s, int finger) {
	if (a->in_games != 2 || !ctx.def) return;
	if (ctx.over) { /* tap to play again */
		int idx = 0;
		for (int i = 0; i < NGAMES_ALL; i++) if (GAMES[i] == ctx.def) idx = i;
		menu_level = ctx.level;
		start_game(a, idx);
		return;
	}
	if (s->n <= 0) return;
	short *xs = malloc((size_t)s->n * sizeof *xs), *ys = malloc((size_t)s->n * sizeof *ys);
	GStroke gs = { 0 };
	gs.n = s->n; gs.x = xs; gs.y = ys; gs.finger = finger;
	gs.x0 = gs.x1 = s->pts[0].x; gs.y0 = gs.y1 = s->pts[0].y;
	long sx = 0, sy = 0;
	for (int i = 0; i < s->n; i++) {
		xs[i] = s->pts[i].x; ys[i] = s->pts[i].y;
		sx += xs[i]; sy += ys[i];
		if (xs[i] < gs.x0) gs.x0 = xs[i];
		if (xs[i] > gs.x1) gs.x1 = xs[i];
		if (ys[i] < gs.y0) gs.y0 = ys[i];
		if (ys[i] > gs.y1) gs.y1 = ys[i];
		if (i) gs.len += abs(xs[i] - xs[i - 1]) + abs(ys[i] - ys[i - 1]);
	}
	gs.cx = (int)(sx / s->n); gs.cy = (int)(sy / s->n);
	gs.tap = gs.len < 30 && gs.x1 - gs.x0 < 30 && gs.y1 - gs.y0 < 30;
	ctx.def->stroke(&ctx, &gs);
	if (ctx.def && ctx.def->resume) save_current_q(1); /* after every move, so a crash or a flat battery loses nothing */
	free(xs); free(ys);
}

int games_wants_ink(App *a) { return a->in_games == 2 && ctx.def && ctx.def->ink && !ctx.over; }

/* Called every loop: fires timers once the queued drawing has finished. */
void games_tick(App *a, double now) {
	if (a->in_games != 2 || !ctx.timer_pending || a->nops) return;
	if (ctx.timer_at == 0) { ctx.timer_at = now + ctx.timer_ms; return; }
	if (now < ctx.timer_at) return;
	ctx.timer_pending = 0;
	if (ctx.def->timer) ctx.def->timer(&ctx);
}

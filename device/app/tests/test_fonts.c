/* Host test for the handwriting fonts (hershey_font.h, ofl_fonts.h) and the stroke layout in layout.c, which is
 * compiled in directly so the checks go through the same code the Kindle runs.
 *  - every glyph of every font: ASCII 32..126 present, coordinates in range, pen-ups well formed, sane widths, the
 *    dotless-i stroke first;
 *  - accents: marks (acute, tilde, diaeresis, cedilla...) land above (or below) their base letter and over it;
 *  - a preview sheet of every font (pangram + accents), written as a PGM to argv[1] (run_font_tests.sh turns it
 *    into dist/fonts-preview.png).
 * Usage: test_fonts [preview.pgm] */
#include "../layout.c"

#include <stdio.h>

static App app;
static int fails;

Op *op_new(App *a, OpType t) {
	static int cap;
	if (a->nops == cap) {
		cap = cap ? cap * 2 : 1024;
		a->ops = realloc(a->ops, (size_t)cap * sizeof *a->ops);
	}
	Op *o = &a->ops[a->nops++];
	memset(o, 0, sizeof *o);
	o->t = t;
	return o;
}

int text_measure(App *a, const char *s, int w, int px, int style, int *lines) {
	if (lines) *lines = 1;
	return px * 3 / 2;
}

static void clear_ops(void) {
	for (int i = 0; i < app.nops; i++) {
		free(app.ops[i].xy);
		free(app.ops[i].dt);
		free(app.ops[i].s);
	}
	app.nops = 0;
}

#define FAIL(...) do { printf("  FAIL %s: ", F->id); printf(__VA_ARGS__); printf("\n"); fails++; } while (0)

/* ---------- glyph checks ---------- */
static void check_font(int fi) {
	const HFont *F = &HFONTS[fi];
	if (F->cap < 10 || F->cap > 120) FAIL("cap %d out of range", F->cap);
	if (F->slant < 0 || F->slant > 60) FAIL("slant %d out of range", F->slant);
	if (strcmp(F->group, "cursive") && strcmp(F->group, "print") && strcmp(F->group, "italic") && strcmp(F->group, "fancy"))
		FAIL("unknown group %s", F->group);
	for (int c = 32; c <= 126; c++) {
		const HGlyph *g = &F->g[c - 32];
		const signed char *v = F->pts + 2 * g->off;
		int w = g->right - g->left, pts = 0, prev_up = 1;
		if (g->right <= g->left && c != ' ') FAIL("'%c' has no width (%d..%d)", c, g->left, g->right);
		if (c == ' ' && (w < F->cap / 5 || w > F->cap * 6 / 5)) FAIL("space width %d (cap %d)", w, F->cap);
		if (c != ' ' && w > 3 * F->cap) FAIL("'%c' too wide: %d (cap %d)", c, w, F->cap);
		for (int k = 0; k < g->n; k++) {
			int x = v[2 * k], y = v[2 * k + 1];
			if (x == -128 || y == -128) {
				if (x != y) FAIL("'%c' half pen-up at %d", c, k);
				if (prev_up) FAIL("'%c' empty stroke at %d", c, k);
				prev_up = 1;
				continue;
			}
			prev_up = 0;
			pts++;
			if (y < 9 - 2.6 * F->cap || y > 9 + 1.8 * F->cap) FAIL("'%c' point y=%d far outside the line (cap %d)", c, y, F->cap);
			if (x < g->left - 2 * F->cap || x > g->right + 2 * F->cap) FAIL("'%c' point x=%d far outside %d..%d", c, x, g->left, g->right);
		}
		if (g->n && prev_up) FAIL("'%c' ends with a pen-up", c);
		if (c != ' ' && pts == 0) FAIL("'%c' missing (no strokes)", c);
	}
	/* dotless i and j (written under accents): the 'i' / 'j' without the dot strokes, which come last */
	for (int d = 0; d < 2; d++) {
		const HGlyph *full = &F->g[(d ? 'j' : 'i') - 32], *dl = &F->g[d ? HG_DOTLESS_J : HG_DOTLESS_I];
		double tf = glyph_top(F, full), td = glyph_top(F, dl);
		if (dl->n == 0 || dl->n >= full->n) FAIL("dotless %c: %d points vs %d", d ? 'j' : 'i', dl->n, full->n);
		if (td < tf + F->cap / 8.0) FAIL("dotless %c: top %.0f not below the dot (%.0f)", d ? 'j' : 'i', td, tf);
		if (dl->off != full->off) FAIL("dotless %c: not the first strokes of the full letter", d ? 'j' : 'i');
		if (9 - td < F->cap / 5.0) FAIL("dotless %c: stem only %.0f high (cap %d)", d ? 'j' : 'i', 9 - td, F->cap);
	}
}

/* ---------- accent checks (through layout_written) ---------- */
typedef struct { int x0, y0, x1, y1; } Box;
static Box box_of(int from, int to) {
	Box b = { 1 << 30, 1 << 30, -(1 << 30), -(1 << 30) };
	for (int i = from; i < to; i++) {
		if (app.ops[i].t != OP_STROKE) continue;
		for (int m = 0; m < app.ops[i].n; m++) {
			int x = app.ops[i].xy[2 * m], y = app.ops[i].xy[2 * m + 1];
			if (x < b.x0) b.x0 = x;
			if (x > b.x1) b.x1 = x;
			if (y < b.y0) b.y0 = y;
			if (y > b.y1) b.y1 = y;
		}
	}
	return b;
}

static int strokes_of(const char *t) {
	clear_ops();
	app.ink_bottom = app.cy;
	layout_written(&app, t);
	int n = 0;
	for (int i = 0; i < app.nops; i++) n += app.ops[i].t == OP_STROKE;
	return n;
}

/* Writes the base letter alone, then with the accent; the accent's extra strokes must sit where 'where' says. */
static void check_accent(int fi, const char *base, const char *accented, int below, const char *what) {
	const HFont *F = &HFONTS[fi];
	app.hfont = fi;
	int nb = strokes_of(base);
	Box bb = box_of(0, app.nops);
	int na = strokes_of(accented);
	if (na <= nb && strcmp(base, "i")) { FAIL("%s: no accent strokes", what); return; }
	/* the accent = the strokes after the base letter's (for i: the dot is replaced, so take the last stroke) */
	int seen = 0, first_mark = app.nops;
	int keep = !strcmp(base, "i") ? na - 1 : nb;
	for (int i = 0; i < app.nops; i++)
		if (app.ops[i].t == OP_STROKE && seen++ == keep) { first_mark = i; break; }
	Box mb = box_of(first_mark, app.nops);
	double px = (double)app.script_cap; /* cap in pixels */
	double mcx = (mb.x0 + mb.x1) / 2.0;
	int top = bb.y0;
	if (!strcmp(base, "i")) top = box_of(0, first_mark).y0; /* the stem's top (dot dropped) */
	if (!below) {
		if (mb.y1 > top + 0.08 * px) FAIL("%s: accent bottom %d not above the letter top %d", what, mb.y1, top);
		if (mb.y1 < top - 0.75 * px) FAIL("%s: accent floats %d px above the letter", what, top - mb.y1);
	} else if (mb.y1 < app.ops[0].xy[1] - 1000) FAIL("%s: cedilla misplaced", what);
	if (mcx < bb.x0 - 0.12 * px || mcx > bb.x1 + 0.12 * px)
		FAIL("%s: accent centre x %.0f outside the letter %d..%d", what, mcx, bb.x0, bb.x1);
}

/* ---------- preview sheet ---------- */
#define PW 3720
#define COLW 1860
static unsigned char *sheet;
static int sheet_h;

static void dot(int x, int y) {
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++) {
			int X = x + dx, Y = y + dy;
			if (X >= 0 && X < PW && Y >= 0 && Y < sheet_h) sheet[(size_t)Y * PW + X] = 0;
		}
}

static void raster(int ox) {
	for (int i = 0; i < app.nops; i++) {
		Op *o = &app.ops[i];
		if (o->t != OP_STROKE) continue;
		for (int m = 0; m < o->n; m++) {
			int x0 = o->xy[2 * (m ? m - 1 : 0)] + ox, y0 = o->xy[2 * (m ? m - 1 : 0) + 1];
			int x1 = o->xy[2 * m] + ox, y1 = o->xy[2 * m + 1];
			int steps = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
			for (int s = 0; s <= steps; s++) dot(x0 + (steps ? (x1 - x0) * s / steps : 0), y0 + (steps ? (y1 - y0) * s / steps : 0));
		}
	}
}

static int find_font(const char *id) {
	for (int i = 0; i < NHFONTS; i++)
		if (!strcmp(HFONTS[i].id, id)) return i;
	return 0;
}

static void preview(const char *path) {
	int per_col = (NHFONTS + 1) / 2, col_y[2] = { 10, 10 };
	sheet_h = per_col * 900 + 100; /* cropped to what was used below */
	sheet = malloc((size_t)PW * sheet_h);
	memset(sheet, 255, (size_t)PW * sheet_h);
	app.cw = COLW;
	app.ch = 100000;
	for (int f = 0; f < NHFONTS; f++) {
		int col = f < per_col ? 0 : 1;
		char label[160];
		snprintf(label, sizeof label, "%d. %s (%s) - %s", f + 1, HFONTS[f].label, HFONTS[f].group, HFONTS[f].id);
		for (int x = col * COLW + 40; x < (col + 1) * COLW - 40; x++) sheet[(size_t)col_y[col] * PW + x] = 200;
		clear_ops();
		app.hfont = find_font("print");
		app.script_cap = 24;
		app.ink_bottom = col_y[col] - 20;
		layout_written(&app, label);
		raster(col * COLW);
		clear_ops();
		app.hfont = f;
		app.script_cap = 46;
		app.ink_bottom -= 10;
		layout_written(&app, "The quick brown fox jumps over the lazy dog.\nH\xC3\xA9llo, \xC2\xBFqu\xC3\xA9 tal? 12 x 14 = 168"
					   "  \xC3\xA7" "a, \xC3\xBC" "ber, ma\xC3\xB1" "ana, \xC3\xAD" "tem, \xC5\x81\xC3\xB3" "d\xC5\xBA");
		raster(col * COLW);
		col_y[col] = app.ink_bottom + 10;
	}
	app.script_cap = 60;
	int h = col_y[0] > col_y[1] ? col_y[0] : col_y[1];
	if (h < sheet_h) sheet_h = h;
	FILE *fp = fopen(path, "wb");
	if (!fp) { perror(path); fails++; return; }
	fprintf(fp, "P5\n%d %d\n255\n", PW, sheet_h);
	fwrite(sheet, 1, (size_t)PW * sheet_h, fp);
	fclose(fp);
	free(sheet);
}

int main(int argc, char **argv) {
	app.cx = 0;
	app.cy = 0;
	app.cw = 1860;
	app.ch = 100000;
	app.script_cap = 60;
	for (int f = 0; f < NHFONTS; f++) {
		check_font(f);
		const HFont *F = &HFONTS[f];
		for (int g = 0; g < f; g++)
			if (!strcmp(HFONTS[g].id, F->id)) FAIL("duplicate id");
		check_accent(f, "e", "\xC3\xA9", 0, "e acute");
		check_accent(f, "E", "\xC3\x89", 0, "E acute");
		check_accent(f, "n", "\xC3\xB1", 0, "n tilde");
		check_accent(f, "N", "\xC3\x91", 0, "N tilde");
		check_accent(f, "u", "\xC3\xBC", 0, "u diaeresis");
		check_accent(f, "a", "\xC3\xA0", 0, "a grave");
		check_accent(f, "o", "\xC3\xB4", 0, "o circumflex");
		check_accent(f, "i", "\xC3\xAD", 0, "i acute");
		check_accent(f, "c", "\xC3\xA7", 1, "c cedilla");
		app.hfont = f;
		if (strokes_of("\xC2\xBF") == 0) FAIL("inverted question mark not drawn");
	}
	if (argc > 1) preview(argv[1]);
	clear_ops();
	printf("%d fonts checked: %s\n", NHFONTS, fails ? "FAILED" : "ok");
	return fails != 0;
}

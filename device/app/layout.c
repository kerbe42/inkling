/* Reply layout on the device: script replies become animated Hershey strokes, typeset replies become text blocks
 * (Kindle fonts drawn by display.c), tables and rules. Both start below the lowest ink and roll onto a new page when
 * they don't fit. Timing/size parameters follow the server's layout (docs/server-handoff.md). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hershey_font.h"
#include "scribe.h"

#define MARGIN 80
#define GAP 40
#define CAP_PX ((double)(a->script_cap > 0 ? a->script_cap : 60))
#define CAP_UNITS ((double)HFONTS[a->hfont].cap)
#define PEN_W 4
#define SPEED 1.8 /* px per ms */

static int page_bottom(App *a) { return a->cy + a->ch - 30; }

static void new_page_op(App *a, int *y, int top_pad) {
	op_new(a, OP_NEWPAGE)->a = 0; /* 0 = next page */
	*y = a->cy + GAP + top_pad;
}

/* ---------- script (Hershey), with accents drawn on top of the base letters ---------- */
#include "diacritics.h"
#define RF 0x200 /* reply font (typed font family, CJK aware) */

static const HGlyph *glyph_f(const HFont *f, unsigned char c) {
	if (c < 32 || c > 126) c = '?';
	return &f->g[c - 32];
}

/* One drawable character: up to 2 ASCII base glyphs plus up to 2 marks. */
typedef struct {
	char base[3];
	unsigned char m1, m2;
} WChar;

static unsigned next_cp(const unsigned char **p) {
	const unsigned char *s = *p;
	unsigned cp;
	if (s[0] < 0x80) { cp = s[0]; *p = s + 1; }
	else if ((s[0] & 0xE0) == 0xC0 && s[1]) { cp = (unsigned)(s[0] & 0x1F) << 6 | (s[1] & 0x3F); *p = s + 2; }
	else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) { cp = (unsigned)(s[0] & 0x0F) << 12 | (unsigned)(s[1] & 0x3F) << 6 | (s[2] & 0x3F); *p = s + 3; }
	else if ((s[0] & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) { cp = 0x10000; *p = s + 4; }
	else { cp = 0xFFFD; *p = s + 1; }
	return cp;
}

/* Maps a code point to something the stroke fonts can draw; returns 0 if it can't be handwritten. */
static int wchar_of(unsigned cp, WChar *w) {
	memset(w, 0, sizeof *w);
	if (cp >= 32 && cp < 127) { w->base[0] = (char)cp; return 1; }
	switch (cp) {
	case 0x2018: case 0x2019: case 0x201A: case 0x2032: w->base[0] = '\''; return 1;
	case 0x201C: case 0x201D: case 0x201E: w->base[0] = '"'; return 1;
	case 0x2013: case 0x2014: case 0x2212: w->base[0] = '-'; return 1;
	case 0x2026: strcpy(w->base, ".."); w->m1 = M_NONE; return 1; /* close enough to an ellipsis */
	case 0xA0: w->base[0] = ' '; return 1;
	}
	for (int i = 0; i < NDIACRITICS; i++)
		if (DIACRITICS[i].cp == cp) {
			snprintf(w->base, sizeof w->base, "%s", DIACRITICS[i].base);
			w->m1 = DIACRITICS[i].m1;
			w->m2 = DIACRITICS[i].m2;
			return 1;
		}
	return 0;
}

static int line_handwritable(const char *line) {
	const unsigned char *p = (const unsigned char *)line;
	WChar w;
	while (*p)
		if (!wchar_of(next_cp(&p), &w)) return 0;
	return 1;
}

static double wchar_width(const HFont *F, const WChar *w, double s) {
	double t = 0;
	for (const char *b = w->base; *b; b++) {
		const HGlyph *g = glyph_f(F, (unsigned char)*b);
		t += (g->right - g->left) * s;
	}
	return t;
}

/* Queue one stroke given in font units relative to (ox, base). */
static void unit_stroke(App *a, double ox, int base, double s, const double *u, int n, int first_dt) {
	Op *o = op_new(a, OP_STROKE);
	o->a = PEN_W;
	o->n = n;
	o->xy = malloc((size_t)n * 2 * sizeof *o->xy);
	o->dt = malloc((size_t)n * sizeof *o->dt);
	for (int m = 0; m < n; m++) {
		o->xy[2 * m] = (int16_t)lround(ox + u[2 * m] * s);
		o->xy[2 * m + 1] = (int16_t)lround(base + (u[2 * m + 1] - 9) * s);
		if (m == 0) o->dt[m] = (uint16_t)first_dt;
		else {
			double dx = o->xy[2 * m] - o->xy[2 * m - 2], dy = o->xy[2 * m + 1] - o->xy[2 * m - 1];
			o->dt[m] = (uint16_t)(sqrt(dx * dx + dy * dy) / SPEED + 4);
		}
	}
}

/* Draws a mark over/under a glyph, all relative to ox: cx = the middle of the advance, tx = the middle of the glyph's
 * top, top = y of the glyph top, right = its right edge (font units). Marks are drawn in Hershey units (cap about
 * 21): k = the font's units per Hershey unit (its cap / 21); slant = the font's slant (x shift per unit of height). */
static void draw_mark(App *a, int mark, double ox, int base, double s, double cx, double tx, double top, double right,
		      double k, double slant) {
	s *= k; /* from here on everything is in Hershey units */
	cx /= k;
	tx /= k;
	right /= k;
	top = 9 + (top - 9) / k;
	double y = top - 4; /* marks sit above the glyph top, shifted along the slant */
	double x = tx + slant * (top - y);
	double u[16];
	switch (mark) {
	case M_ACUTE: { double v[] = { x - 1.4, y + 1.6, x + 1.6, y - 1.8 }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_GRAVE: { double v[] = { x - 1.6, y - 1.8, x + 1.4, y + 1.6 }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_CIRC: { double v[] = { x - 2.2, y + 1.2, x, y - 1.6, x + 2.2, y + 1.2 }; unit_stroke(a, ox, base, s, v, 3, 30); break; }
	case M_CARON: { double v[] = { x - 2.2, y - 1.6, x, y + 1.2, x + 2.2, y - 1.6 }; unit_stroke(a, ox, base, s, v, 3, 30); break; }
	case M_MACRON: { double v[] = { x - 2.4, y, x + 2.4, y }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_BREVE: { double v[] = { x - 2.2, y - 1.4, x - 1.2, y + 0.8, x + 1.2, y + 0.8, x + 2.2, y - 1.4 }; unit_stroke(a, ox, base, s, v, 4, 30); break; }
	case M_TILDE: { double v[] = { x - 2.8, y + 0.6, x - 1.2, y - 1.0, x + 1.2, y + 0.8, x + 2.8, y - 0.8 }; unit_stroke(a, ox, base, s, v, 4, 30); break; }
	case M_DOT: { double v[] = { x - 0.4, y, x + 0.4, y }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_DIAER: {
		double v1[] = { x - 2.0, y, x - 1.2, y }, v2[] = { x + 1.2, y, x + 2.0, y };
		unit_stroke(a, ox, base, s, v1, 2, 30);
		unit_stroke(a, ox, base, s, v2, 2, 20);
		break;
	}
	case M_DACUTE: {
		double v1[] = { x - 2.6, y + 1.6, x - 0.6, y - 1.8 }, v2[] = { x + 0.6, y + 1.6, x + 2.6, y - 1.8 };
		unit_stroke(a, ox, base, s, v1, 2, 30);
		unit_stroke(a, ox, base, s, v2, 2, 20);
		break;
	}
	case M_RING: {
		for (int i = 0; i <= 7; i++) { double t = 2 * M_PI * i / 7; u[2 * i] = x + 1.3 * cos(t); u[2 * i + 1] = y + 1.3 * sin(t); }
		unit_stroke(a, ox, base, s, u, 8, 30);
		break;
	}
	case M_CEDIL: { double v[] = { cx, 9, cx + 1.0, 10.6, cx, 12.2, cx - 1.2, 12.0 }; unit_stroke(a, ox, base, s, v, 4, 30); break; }
	case M_OGONEK: { double v[] = { right - 1.5, 9, right - 3.0, 11.0, right - 2.2, 12.6, right - 0.8, 12.2 }; unit_stroke(a, ox, base, s, v, 4, 30); break; }
	case M_SLASH: { double v[] = { cx - 3.6, 9.6, cx + 3.6 + 1.5 * slant / 0.22, top - 0.6 }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_STROKE: { double my = (top + 9) / 2; double v[] = { cx - 2.6, my + 1.8, cx + 2.6 + slant / 0.22, my - 1.8 }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	case M_BAR: { double v[] = { right - 4.5, top + 3.5, right + 0.5, top + 3.5 }; unit_stroke(a, ox, base, s, v, 2, 30); break; }
	default: break;
	}
}

/* Top (smallest y) of a glyph's points in font units. */
static double glyph_top(const HFont *F, const HGlyph *g) {
	double t = 9;
	const signed char *v = F->pts + 2 * g->off;
	for (int k = 0; k < g->n; k++)
		if (v[2 * k] != -128 && v[2 * k + 1] < t) t = v[2 * k + 1];
	return t;
}

/* The middle (x) of a glyph's top part: where an accent goes. top = glyph_top(). */
static double glyph_top_mid(const HFont *F, const HGlyph *g, double top) {
	double band = top + 0.3 * (9 - top), lo = 1e9, hi = -1e9;
	const signed char *v = F->pts + 2 * g->off;
	for (int k = 0; k < g->n; k++)
		if (v[2 * k] != -128 && v[2 * k + 1] <= band) {
			if (v[2 * k] < lo) lo = v[2 * k];
			if (v[2 * k] > hi) hi = v[2 * k];
		}
	return lo <= hi ? (lo + hi) / 2 : (g->left + g->right) / 2.0;
}

/* How far the font's descenders reach below the baseline, in font units (at least 12 Hershey units). */
static double font_desc(const HFont *F) {
	double d = 12.0 * F->cap / 21;
	for (const char *c = "gjpqy"; *c; c++) {
		const HGlyph *g = glyph_f(F, (unsigned char)*c);
		const signed char *v = F->pts + 2 * g->off;
		for (int k = 0; k < g->n; k++)
			if (v[2 * k] != -128 && v[2 * k + 1] - 9 > d) d = v[2 * k + 1] - 9;
	}
	return d;
}

/* Draws one base glyph (rotated 180 degrees for M_ROT, e.g. an inverted question mark). */
static void draw_glyph(App *a, const HFont *F, const HGlyph *g, double ox, int base, double s, int rot, int first_dt) {
	const signed char *v = F->pts + 2 * g->off;
	double top = glyph_top(F, g), mid_x = (g->left + g->right) / 2.0, mid_y = (top + 9) / 2.0;
	int k = 0, first = 1;
	while (k < g->n) {
		int start = k;
		while (k < g->n && v[2 * k] != -128) k++;
		int cnt = k - start;
		if (cnt >= 1) {
			double *u = malloc((size_t)cnt * 2 * sizeof *u);
			for (int m = 0; m < cnt; m++) {
				double x = v[2 * (start + m)], y = v[2 * (start + m) + 1];
				if (rot) { x = 2 * mid_x - x; y = 2 * mid_y - y + 3; } /* upside down, dropped slightly */
				u[2 * m] = x;
				u[2 * m + 1] = y;
			}
			unit_stroke(a, ox, base, s, u, cnt, first ? first_dt : 40);
			free(u);
			first = 0;
		}
		k++;
	}
}

static int page_bottom_px(App *a) { return a->cy + a->ch - 30; }

/* Lays out handwriting starting with the first baseline at *base; updates *base to the last baseline. */
static void script_text(App *a, const char *t, int *base, int *first_stroke) {
	const HFont *F = &HFONTS[a->hfont];
	double s = CAP_PX / CAP_UNITS;
	int line_h = (int)(CAP_PX * 1.8);
	int left = a->cx + MARGIN, right = a->cx + a->cw - MARGIN;
	double k = F->cap / 21.0, slant = F->slant / 100.0, desc = font_desc(F); /* k: font units per Hershey unit */
	double space = (F->g[0].right - F->g[0].left) * s;
	double x = left;
	int first_on_line = 1;
	const unsigned char *p = (const unsigned char *)t;
	while (*p) {
		if (*p == ' ' || *p == '\t') { p++; continue; }
		/* measure the word */
		const unsigned char *q = p;
		double ww = 0;
		while (*q && *q != ' ' && *q != '\t') {
			WChar w;
			const unsigned char *r = q;
			if (wchar_of(next_cp(&r), &w)) ww += wchar_width(F, &w, s);
			q = r;
		}
		if (!first_on_line && x + space + ww > right) {
			*base += line_h;
			if (*base + (int)(desc * s) > page_bottom_px(a)) { op_new(a, OP_NEWPAGE)->a = 0; *base = a->cy + GAP + (int)CAP_PX; }
			x = left;
			first_on_line = 1;
		}
		if (!first_on_line) x += space;
		int word_start = 1;
		while (p < q) {
			WChar w;
			if (!wchar_of(next_cp(&p), &w)) continue;
			double start_x = x, top = 9, cxu = 0, txu = 0, rightu = 0;
			for (const char *b = w.base; *b; b++) {
				const HGlyph *g = glyph_f(F, (unsigned char)*b);
				double ox = x - g->left * s;
				int dt = *first_stroke ? 250 : first_on_line && word_start ? 220 : word_start ? 90 : 40;
				int above = w.m1 > M_NONE && w.m1 <= M_CARON; /* a mark above the letter */
				if ((*b == 'i' || *b == 'j') && !b[1] && (w.m1 == M_NODOT || above))
					g = &F->g[*b == 'i' ? HG_DOTLESS_I : HG_DOTLESS_J]; /* no dot under an accent */
				draw_glyph(a, F, g, ox, *base, s, w.m1 == M_ROT, dt);
				*first_stroke = 0;
				word_start = 0;
				top = glyph_top(F, g);
				txu = glyph_top_mid(F, g, top);
				cxu = (g->left + g->right) / 2.0;
				rightu = g->right;
				x += (g->right - g->left) * s;
			}
			/* marks apply to the last base glyph */
			const HGlyph *lg = glyph_f(F, (unsigned char)w.base[strlen(w.base) - 1]);
			double ox = (x - (lg->right - lg->left) * s) - lg->left * s;
			if (w.m1 && w.m1 != M_ROT && w.m1 != M_NODOT) draw_mark(a, w.m1, ox, *base, s, cxu, txu, top, rightu, k, slant);
			if (w.m2)
				draw_mark(a, w.m2, ox, *base, s, cxu, txu, w.m1 == M_DIAER || w.m1 == M_DOT ? top - 3.5 * k : top, rightu, k,
					  slant);
			(void)start_x;
		}
		first_on_line = 0;
	}
}

/* Text the stroke fonts can't write (Japanese, Chinese, Korean, Cyrillic...): typeset into the framebuffer without
 * showing it, then revealed left to right, line by line, as if being written. */
static void reveal_text(App *a, const char *t, int *y) {
	int x = a->cx + MARGIN, w = a->cw - 2 * MARGIN, px = 46;
	int h = text_measure(a, t, w, px, TXT_REGULAR | RF, NULL);
	if (*y + h > page_bottom_px(a) && *y > a->cy + GAP) { op_new(a, OP_NEWPAGE)->a = 0; *y = a->cy + GAP; }
	Op *o = op_new(a, OP_TEXT);
	o->a = x; o->b = *y; o->c = w; o->d = h; o->size = px;
	o->style = TXT_REGULAR | RF | 0x1000; /* 0x1000: draw without refreshing */
	o->s = strdup(t);
	Op *r = op_new(a, OP_REVEAL);
	r->a = x; r->b = *y; r->c = w; r->d = h; r->size = px * 13 / 10;
	*y += h;
}

/* Handwritten reply: handwriting where the stroke fonts can write it, a written-out reveal elsewhere. */
static void layout_written(App *a, const char *text) {
	double s = CAP_PX / CAP_UNITS, desc = font_desc(&HFONTS[a->hfont]);
	int y = a->ink_bottom + GAP; /* top of the next block */
	int first = 1, nstart = a->nops;
	char *t = strdup(text);
	char *save = NULL;
	for (char *line = strtok_r(t, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		while (*line == ' ' || *line == '\r') line++;
		if (!*line) continue;
		if (line_handwritable(line)) {
			int base = y + (int)CAP_PX;
			if (base + (int)(desc * s) > page_bottom_px(a)) { op_new(a, OP_NEWPAGE)->a = 0; base = a->cy + GAP + (int)CAP_PX; }
			script_text(a, line, &base, &first);
			y = base + (int)(desc * s) + (int)(CAP_PX * 0.5);
		} else {
			reveal_text(a, line, &y);
			y += (int)(CAP_PX * 0.5);
			first = 0;
		}
	}
	free(t);
	/* keep long replies to about 40 s of writing */
	double total = 0;
	for (int i = nstart; i < a->nops; i++)
		if (a->ops[i].t == OP_STROKE)
			for (int m = 0; m < a->ops[i].n; m++) total += a->ops[i].dt[m];
	if (total > 40000) {
		double f = 40000 / total;
		for (int i = nstart; i < a->nops; i++)
			if (a->ops[i].t == OP_STROKE)
				for (int m = 0; m < a->ops[i].n; m++) a->ops[i].dt[m] = (uint16_t)(a->ops[i].dt[m] * f);
	}
	a->ink_bottom = y;
}

/* ---------- typeset ---------- */
#define BODY_PX 38
#define FMT 0x100

static void add_text(App *a, const char *s, int x, int y, int w, int h, int px, int style) {
	Op *o = op_new(a, OP_TEXT);
	o->a = x; o->b = y; o->c = w; o->d = h;
	o->size = px;
	o->style = style | RF;
	o->s = strdup(s);
}

static void add_rule(App *a, int x, int y, int w, int h) {
	Op *o = op_new(a, OP_RULE);
	o->a = x; o->b = y; o->c = w; o->d = h;
}

/* Places a block of height h at *y, rolling to a new page if needed. */
static void place(App *a, int *y, int h) {
	if (*y + h > page_bottom(a) && *y > a->cy + GAP) new_page_op(a, y, 0);
}

static int is_table_sep(const char *l) {
	for (; *l; l++)
		if (!strchr("|-: \t", *l)) return 0;
	return 1;
}

static int split_cells(char *line, char **cells, int max) {
	char *p = line;
	while (*p == ' ') p++;
	if (*p == '|') p++;
	int n = 0;
	while (*p && n < max) {
		char *bar = strchr(p, '|');
		if (bar) *bar = 0;
		while (*p == ' ') p++;
		char *e = p + strlen(p);
		while (e > p && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
		if (bar || *p) cells[n++] = p;
		if (!bar) break;
		p = bar + 1;
		if (!*p) break;
	}
	return n;
}

static void layout_table(App *a, char **rows, int nrows, int x, int w, int *y) {
	char *cells[16][12];
	int ncell[16] = { 0 }, ncol = 0, nr = 0;
	for (int r = 0; r < nrows && nr < 16; r++) {
		if (is_table_sep(rows[r])) continue;
		ncell[nr] = split_cells(rows[r], cells[nr], 12);
		if (ncell[nr] > ncol) ncol = ncell[nr];
		nr++;
	}
	if (!ncol) return;
	int maxlen[12] = { 0 }, sum = 0;
	for (int r = 0; r < nr; r++)
		for (int c = 0; c < ncell[r]; c++) {
			int l = (int)strlen(cells[r][c]);
			if (l > maxlen[c]) maxlen[c] = l;
		}
	for (int c = 0; c < ncol; c++) { if (maxlen[c] < 4) maxlen[c] = 4; sum += maxlen[c]; }
	int cw[12], cx[13];
	cx[0] = x;
	for (int c = 0; c < ncol; c++) { cw[c] = w * maxlen[c] / sum; cx[c + 1] = cx[c] + cw[c]; }
	cx[ncol] = x + w;
	int px = 32, pad = 12;
	for (int r = 0; r < nr; r++) {
		int rh = 0;
		for (int c = 0; c < ncell[r]; c++) {
			int h = text_measure(a, cells[r][c], cx[c + 1] - cx[c] - 2 * pad, px, (r == 0 ? TXT_BOLD : TXT_REGULAR | FMT) | RF, NULL);
			if (h > rh) rh = h;
		}
		rh += 2 * pad;
		place(a, y, rh);
		add_rule(a, x, *y, w, r == 0 ? 3 : 2);
		for (int c = 0; c <= ncol; c++) add_rule(a, cx[c] - (c == ncol ? 2 : 0), *y, 2, rh);
		for (int c = 0; c < ncell[r]; c++)
			add_text(a, cells[r][c], cx[c] + pad, *y + pad, cx[c + 1] - cx[c] - 2 * pad, rh - 2 * pad, px,
				 r == 0 ? TXT_BOLD : TXT_REGULAR | FMT);
		*y += rh;
	}
	add_rule(a, x, *y, w, 3);
	*y += 3;
}

static void strip_backticks(char *s) {
	char *o = s;
	for (; *s; s++) if (*s != '`') *o++ = *s;
	*o = 0;
}

static void layout_typeset(App *a, const char *text) {
	char *t = strdup(text);
	strip_backticks(t);
	int x = a->cx + MARGIN, w = a->cw - 2 * MARGIN;
	int y = a->ink_bottom + GAP;
	if (y + 80 > page_bottom(a)) new_page_op(a, &y, 0);
	char *lines[512];
	int n = 0;
	for (char *p = strtok(t, "\n"); p && n < 512; p = strtok(NULL, "\n")) lines[n++] = p;
	/* strtok skips empty lines; paragraph spacing comes from block gaps instead */
	for (int i = 0; i < n; i++) {
		char *l = lines[i];
		while (*l == '\r') l++;
		size_t ll = strlen(l);
		while (ll && (l[ll - 1] == '\r' || l[ll - 1] == ' ')) l[--ll] = 0;
		int indent = 0;
		while (l[indent] == ' ') indent++;
		char *s = l + indent;
		if (!*s) continue;
		if (*s == '|') {
			int j = i;
			while (j < n && lines[j][strspn(lines[j], " ")] == '|') j++;
			layout_table(a, lines + i, j - i, x, w, &y);
			y += 18;
			i = j - 1;
			continue;
		}
		if (!strncmp(s, "---", 3) && strspn(s, "-") == strlen(s)) {
			place(a, &y, 30);
			add_rule(a, x, y + 12, w, 3);
			y += 30;
			continue;
		}
		int px = BODY_PX, style = TXT_REGULAR | FMT, tx = x, tw = w;
		const char *bullet = NULL;
		char num[16];
		if (s[0] == '#') {
			int lv = (int)strspn(s, "#");
			s += lv;
			while (*s == ' ') s++;
			px = lv == 1 ? 54 : lv == 2 ? 46 : 42;
			style = TXT_BOLD;
			y += 10;
		} else if ((s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
			bullet = "\xE2\x80\xA2"; /* bullet */
			s += 2;
		} else if (s[0] >= '0' && s[0] <= '9') {
			size_t d = strspn(s, "0123456789");
			if ((s[d] == '.' || s[d] == ')') && s[d + 1] == ' ' && d < 4) {
				snprintf(num, sizeof num, "%.*s.", (int)d, s);
				bullet = num;
				s += d + 2;
			}
		} else if (s[0] == '>' ) {
			s++;
			while (*s == ' ') s++;
			style = TXT_ITALIC | FMT;
			tx += 40;
			tw -= 40;
		}
		int ind = indent / 2 * 50;
		if (bullet) { tx += ind + 60; tw -= ind + 60; }
		int h = text_measure(a, s, tw, px, style | RF, NULL);
		if (h < px) h = px + px / 4;
		place(a, &y, h);
		if (bullet) add_text(a, bullet, x + ind + 8, y, 60, h, px, TXT_REGULAR);
		add_text(a, s, tx, y, tw, h, px, style);
		y += h + (px >= 42 ? 16 : 12);
	}
	a->ink_bottom = y;
	free(t);
}

/* The model's sketch: strokes on a 100x100 grid, drawn in a square below the ink, animated like a pen. */
/* A drawing (1000 x 1000 grid) as pen strokes in a square below the ink, the points joined by Catmull-Rom curves so
 * it reads as smooth line art rather than straight segments. */
#define DRAW_SUB 6 /* curve points per segment */
static void layout_drawing(App *a, Reply *r) {
	int size = a->cw - 2 * MARGIN;
	if (size > 1000) size = 1000;
	int y = a->ink_bottom + GAP;
	if (y + size > page_bottom_px(a) && y > a->cy + GAP) { op_new(a, OP_NEWPAGE)->a = 0; y = a->cy + GAP; }
	if (y + size > page_bottom_px(a)) size = page_bottom_px(a) - y;
	if (size < 200) return;
	int x0 = a->cx + (a->cw - size) / 2, p = 0;
	for (int i = 0; i < r->nstrokes; i++) {
		int n = r->slen[i];
		const float *q = r->dpts + 2 * p;
		p += n;
		int m = n < 3 ? n : (n - 1) * DRAW_SUB + 1;
		Op *o = op_new(a, OP_STROKE);
		o->a = 5;
		o->n = m;
		o->xy = malloc((size_t)m * 2 * sizeof *o->xy);
		o->dt = malloc((size_t)m * sizeof *o->dt);
		for (int k = 0; k < m; k++) {
			double fx, fy;
			if (n < 3) { fx = q[2 * k]; fy = q[2 * k + 1]; }
			else {
				int s = k / DRAW_SUB;
				double t = (double)(k % DRAW_SUB) / DRAW_SUB;
				if (s >= n - 1) { s = n - 2; t = 1; }
				int i0 = s > 0 ? s - 1 : 0, i3 = s + 2 < n ? s + 2 : n - 1;
				double t2 = t * t, t3 = t2 * t;
				for (int c = 0; c < 2; c++) {
					double p0 = q[2 * i0 + c], p1 = q[2 * s + c], p2 = q[2 * (s + 1) + c], p3 = q[2 * i3 + c];
					double v = 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t3);
					if (c == 0) fx = v; else fy = v;
				}
			}
			if (fx < 0) fx = 0;
			if (fx > 1000) fx = 1000;
			if (fy < 0) fy = 0;
			if (fy > 1000) fy = 1000;
			o->xy[2 * k] = (int16_t)(x0 + fx * size / 1000);
			o->xy[2 * k + 1] = (int16_t)(y + fy * size / 1000);
			if (k == 0) o->dt[k] = (uint16_t)(i == 0 ? 250 : 90);
			else {
				double dx = o->xy[2 * k] - o->xy[2 * k - 2], dy = o->xy[2 * k + 1] - o->xy[2 * k - 1];
				o->dt[k] = (uint16_t)(sqrt(dx * dx + dy * dy) / 2.6 + 1);
			}
		}
	}
	a->ink_bottom = y + size;
}

void layout_picture(App *a, Reply *r) {
	if (r->nstrokes > 0) layout_drawing(a, r);
}

void layout_reply(App *a, Reply *r) {
	if (r->nstrokes > 0) layout_drawing(a, r);
	int typed = !strcmp(r->style, "typeset") || (!strcmp(r->mode, "translate") && a->translate_typed);
	if (!strcmp(r->mode, "translate") && !a->translate_typed) typed = 0; /* translations are written out */
	if (typed) layout_typeset(a, r->reply);
	else layout_written(a, r->reply);
}

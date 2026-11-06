/* Fonts for typeset replies: the chosen Kindle font family (settings: typed_font), plus Japanese / Chinese / Korean
 * fonts loaded on demand when a block of text needs them (display.c takes one font per style, with no per-glyph
 * fallback). The UI keeps its own fonts (Amazon Ember) in a->font_ui. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "scribe.h"

typedef struct {
	const char *id, *label;
	const char *files[4]; /* regular, bold, italic, bold italic (without extension); a family with fewer faces lists
			       * only those, and the missing styles use the regular face */
} Family;

static const Family FAMILIES[] = {
	{ "bookerly", "Bookerly", { "Bookerly-Regular", "Bookerly-Bold", "Bookerly-Italic", "Bookerly-BoldItalic" } },
	{ "sans", "Sans", { "Amazon-Ember-Regular", "Amazon-Ember-Bold", "Amazon-Ember-RegularItalic", "Amazon-Ember-BoldItalic" } },
	{ "palatino", "Palatino", { "Palatino-Regular", "Palatino-Bold", "Palatino-Italic", "Palatino-BoldItalic" } },
	{ "baskerville", "Baskerville", { "Baskerville-Regular", "Baskerville-Bold", "Baskerville-Italic", "Baskerville-BoldItalic" } },
	{ "futura", "Futura", { "Futura-Medium", "Futura-Bold", "Futura-MediumOblique", "Futura-BoldOblique" } },
	{ "dyslexic", "Dyslexic", { "OpenDyslexic-Regular", "OpenDyslexic-Bold", "OpenDyslexic-Italic", "OpenDyslexic-BoldItalic" } },
	/* more families the Kindle ships, named by look in the app */
	{ "slab", "Slab serif", { "Caecilia_LT_65_Medium", "Caecilia_LT_75_Bold", "Caecilia_LT_66_Medium_Italic", "Caecilia_LT_76_Bold_Italic" } },
	{ "classicsans", "Classic sans", { "Helvetica_LT_65_Medium", "Helvetica_LT_75_Bold", "Helvetica_LT_66_Medium_Italic", "Helvetica_LT_76_Bold_Italic" } },
	{ "modernserif", "Modern serif", { "AmazonEmberSerif_Rg", "AmazonEmberSerif_Bd", "AmazonEmberSerif_RgIt", "AmazonEmberSerif_BdIt" } },
	{ "display", "Display serif", { "BookerlyDisplay-Regular", "BookerlyDisplay-Bold", "BookerlyDisplay-Italic", "BookerlyDisplay-BoldItalic" } },
	/* The Scribe's handwriting-look fonts (Notewright-Single, Cadia-Single...) are single-line stroke fonts: filled
	 * by the text renderer they come out as blobs, so they are not offered here. */
};
#define NFAM ((int)(sizeof FAMILIES / sizeof FAMILIES[0]))

/* CJK: 0 Japanese, 1 Chinese, 2 Korean (regular, bold) */
static const char *CJK[3][2] = { { "TBGothicMed_213", "TBGothicBold_213" }, { "STHeitiMedium", "STHeitiBold" },
				 { "NotoSansKR-Regular", "NotoSansKR-Black" } };

static int find_font(const char *stem, char *path, size_t n) {
	const char *ext[] = { ".ttf", ".otf", ".ttc" };
	for (int i = 0; i < 3; i++) {
		snprintf(path, n, FONT_DIR "%s%s", stem, ext[i]);
		if (access(path, R_OK) == 0) return 1;
	}
	return 0;
}

static void load_set(DispFonts *cfg, const char *const files[4], int nfiles) {
	const int styles[4] = { TXT_REGULAR, TXT_BOLD, TXT_ITALIC, TXT_BOLD_ITALIC };
	char path[200];
	for (int i = 0; i < 4; i++) {
		/* a missing style uses the bold face (for bold italic) or the regular one */
		const char *stem = i < nfiles && files[i] ? files[i] : i == 3 && nfiles > 1 && files[1] ? files[1] : files[0];
		if (find_font(stem, path, sizeof path)) {
			if (disp_font_add(cfg, path, styles[i]) < 0) logf_("font load failed: %s", path);
		} else logf_("font missing: %s", stem);
	}
}

const char *typed_font_label(int i) { return i >= 0 && i < NFAM ? FAMILIES[i].label : NULL; }
const char *typed_font_id(int i) { return i >= 0 && i < NFAM ? FAMILIES[i].id : NULL; }

/* 1 if the family's regular face is on this Kindle (checked once per family). */
int typed_font_available(int i) {
	static signed char known[NFAM];
	if (i < 0 || i >= NFAM) return 0;
	if (!known[i]) {
		char path[200];
		known[i] = find_font(FAMILIES[i].files[0], path, sizeof path) ? 1 : -1;
	}
	return known[i] > 0;
}

void reply_fonts_load(App *a) {
	if (a->reply_fonts_loaded) disp_font_free(&a->font_reply);
	memset(&a->font_reply, 0, sizeof a->font_reply);
	int f = 0;
	for (int i = 0; i < NFAM; i++)
		if (!strcmp(FAMILIES[i].id, a->tfont)) f = i;
	if (!typed_font_available(f)) f = 0; /* not on this Kindle: Bookerly */
	load_set(&a->font_reply, FAMILIES[f].files, 4);
	a->reply_fonts_loaded = 1;
	logf_("typed font: %s", FAMILIES[f].label);
}

/* Which script a string needs: -1 none (Latin etc.), 0 Japanese, 1 Chinese, 2 Korean. */
static int cjk_kind(const char *s) {
	int han = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p;) {
		unsigned cp;
		if (*p < 0x80) { p++; continue; }
		if ((*p & 0xE0) == 0xC0 && p[1]) { cp = (unsigned)(*p & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
		else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = (unsigned)(*p & 0x0F) << 12 | (unsigned)(p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
		else { p++; while ((*p & 0xC0) == 0x80) p++; continue; }
		if (cp >= 0x3040 && cp <= 0x30FF) return 0;
		if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F)) return 2;
		if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)) han = 1;
	}
	return han ? 1 : -1;
}

/* The OpenType config to render s with: UI fonts, the reply family, or a CJK font. */
const DispFonts *font_for(App *a, const char *s, int style) {
	if (!(style & 0x200)) return &a->font_ui;
	int k = cjk_kind(s);
	if (k < 0) return &a->font_reply;
	if (!a->cjk_loaded[k]) {
		memset(&a->font_cjk[k], 0, sizeof a->font_cjk[k]);
		load_set(&a->font_cjk[k], CJK[k], 2);
		a->cjk_loaded[k] = 1;
		logf_("loaded %s font", k == 0 ? "Japanese" : k == 1 ? "Chinese" : "Korean");
	}
	return &a->font_cjk[k];
}

/* Regular face of a family, for the typed-font previews on the settings page (loaded once, on demand). */
static DispFonts prev_cfg[NFAM];
static int prev_loaded[NFAM];
const DispFonts *font_preview(App *a, int family) {
	if (family < 0 || family >= NFAM) return &a->font_ui;
	if (!prev_loaded[family]) {
		memset(&prev_cfg[family], 0, sizeof prev_cfg[family]);
		load_set(&prev_cfg[family], FAMILIES[family].files, 1);
		prev_loaded[family] = 1;
	}
	return &prev_cfg[family];
}

void fonts_free(App *a) {
	for (int i = 0; i < NFAM; i++)
		if (prev_loaded[i]) disp_font_free(&prev_cfg[i]);
	if (a->reply_fonts_loaded) disp_font_free(&a->font_reply);
	for (int k = 0; k < 3; k++)
		if (a->cjk_loaded[k]) disp_font_free(&a->font_cjk[k]);
}

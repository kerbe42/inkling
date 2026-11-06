/* Settings: a home list of categories (each row shows the current choices); each category opens its own page with
 * large options (finger or pen), short explanations, font previews, and Back (or flick right). Every setting is a
 * config.txt key, so the page, the file on the USB storage and the app agree. settings_apply() is also used when
 * config.txt is read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hershey_font.h"
#include "scribe.h"

const char *typed_font_label(int i);
const char *typed_font_id(int i);
int typed_font_available(int i);
const DispFonts *font_preview(App *a, int family);

#define MAXOPT 18
typedef struct {
	int cat, per_row;
	const char *label, *help, *key;
	int n;
	const char *val[MAXOPT], *text[MAXOPT];
} Setting;

static const char *CATS[] = { "Voices", "Replies", "Handwriting & fonts", "Translate", "Math", "Pen & timing", "AI & keys", "Email" };
#define CAT_AI 6
#define CAT_EMAIL 7
#define NCATS ((int)(sizeof CATS / sizeof CATS[0]))

static const Setting SET[] = {
	{ 0, 4, "Everyday voices", "How replies sound in Chat and the AI games. Pick any one, everyday or fantasy.", "reply_voice", 18,
	  { "friendly", "mentor", "playful", "poetic", "mysterious", "concise", "storyteller", "philosopher", "comedian",
	    "pirate", "shakespeare", "detective", "coach", "zen", "grandparent", "starship", "victorian", "cheerleader" },
	  { "Friendly", "Mentor", "Playful", "Poetic", "Mysterious diary", "Concise", "Storyteller", "Philosopher",
	    "Comedian", "Pirate", "Shakespeare", "Detective", "Coach", "Zen", "Grandparent", "Starship AI", "Victorian",
	    "Cheerleader" } },
	{ 1, 3, "Reply length", NULL, "reply_length", 3, { "short", "medium", "long" }, { "Short", "Medium", "Long" } },
	{ 1, 3, "Reply language", NULL, "reply_language", 2, { "match", "english" }, { "Same as mine", "Always English" } },
	{ 1, 2, "Type up", "What happens to your handwriting when Type up turns it into text.", "typeup", 2,
	  { "replace", "below" }, { "Becomes the text", "Stays, text below" } },
	{ 0, 4, "Fantasy voices", NULL,
	  "reply_voice", 12,
	  { "evil_wizard", "barbarian", "elf", "dwarf", "dragon", "fairy", "knight", "witch", "goblin", "oracle", "bard", "ghost" },
	  { "Evil wizard", "Barbarian", "Wise elf", "Grumpy dwarf", "Ancient dragon", "Fairy", "Noble knight", "Cackling witch",
	    "Goblin", "Oracle", "Bard", "Friendly ghost" } },
	/* handwriting styles: one row per group of HFONTS (val[0] = the group), each style previewed in its own hand */
	{ 2, 5, "Cursive (joined-up)", "Written replies, drawn stroke by stroke - each sample in its own style. Typed fonts are at the bottom.",
	  "handwriting_font", 0, { "cursive" }, { 0 } },
	{ 2, 5, "Italic & calligraphy", NULL, "handwriting_font", 0, { "italic" }, { 0 } },
	{ 2, 5, "Print", NULL, "handwriting_font", 0, { "print" }, { 0 } },
	{ 2, 5, "Blackletter & decorative", NULL, "handwriting_font", 0, { "fancy" }, { 0 } },
	{ 2, 3, "Handwriting size", NULL, "handwriting_size", 3, { "small", "medium", "large" }, { "Small", "Medium", "Large" } },
	{ 2, 4, "Writing speed", NULL, "writing_speed", 4, { "0.6", "1", "2", "10" }, { "Slow", "Normal", "Fast", "Instant" } },
	{ 2, 5, "Typed font", NULL, "typed_font", 0,
	  { 0 }, { 0 } },
	{ 3, 3, "Translate into", "Translate mode writes back in this language.", "translate_to", 12,
	  { "English", "Spanish", "French", "German", "Italian", "Portuguese", "Dutch", "Russian", "Japanese", "Chinese",
	    "Korean", "Polish" },
	  { "English", "Spanish", "French", "German", "Italian", "Portuguese", "Dutch", "Russian", "Japanese", "Chinese",
	    "Korean", "Polish" } },
	{ 3, 2, "Translation appears", "Handwritten: accents drawn in; Japanese, Chinese, Korean and Russian are written out line by line.",
	  "translate_style", 2, { "written", "typed" }, { "Handwritten", "Typed" } },
	{ 4, 2, "Math help", "Hints first points to your first mistake and nudges you on; full solution shows every step.",
	  "math_help", 2, { "hints", "full" }, { "Hints first", "Full solution" } },
	{ 5, 3, "Pen", NULL, "ink", 3, { "thin", "normal", "thick" }, { "Thin", "Normal", "Thick" } },
	{ 5, 4, "Auto-reply after a pause of", "Lift the pen and wait this long; Off means only the Send button replies.",
	  "pause_seconds", 4, { "1.5", "2.5", "4", "0" }, { "1.5 s", "2.5 s", "4 s", "Off" } },
	{ 5, 2, "Scratch out to erase", "Scribble back and forth over writing to rub it out. The Erase button and the pen's eraser end always work.",
	  "scratch_erase", 2, { "on", "off" }, { "On", "Off" } },
	{ 6, 1, "GitHub token (updates)", "Only while the app's GitHub repository is private: a fine-grained token with read-only Contents.",
	  "key:github_token", -1, { 0 }, { 0 } },
	{ 7, 4, "Email account", "Send > Email this page sends from this account, with an app password (not your normal password).",
	  "email_provider", 7, { "gmail", "outlook", "icloud", "yahoo", "fastmail", "custom", "resend" },
	  { "Gmail", "Outlook", "iCloud", "Yahoo", "Fastmail", "Other server", "Resend" } },
	{ 7, 1, "Your email address", NULL, "email_address", -1, { 0 }, { 0 } },
	{ 7, 1, "App password", "Gmail: Google Account > Security > 2-Step Verification > App passwords (16 letters).",
	  "key:email_password", -1, { 0 }, { 0 } },
	{ 7, 1, "Server (Other server)", "For example smtp.example.com", "smtp_server", -1, { 0 }, { 0 } },
	{ 7, 1, "Port (Other server)", "465 for SSL, 587 for STARTTLS", "smtp_port", -1, { 0 }, { 0 } },
	{ 7, 1, "Resend key", "From resend.com (API keys, sending access).", "key:resend_api_key", -1, { 0 }, { 0 } },
	{ 7, 1, "Send pages to", "Leave empty to send them to yourself.", "email_to", -1, { 0 }, { 0 } },
};
#define NSET ((int)(sizeof SET / sizeof SET[0]))

#define LEFT 70
#define TOP (BAR_H + 40)
#define CHIP_H 112
#define PREVIEW_H 150
#define GAP 22

enum { HIT_CAT, HIT_BACK, HIT_CHIP, HIT_FIELD, HIT_PROV, HIT_MODEL, HIT_REGION, HIT_AIKEY, HIT_TEST };
static void draw(App *a, const DispRefresh *how);
typedef struct {
	int kind, x, y, w, h, set, opt;
} Hit;
static Hit hits[200];
static int nhits;

static int options(const Setting *s, const char **val, const char **text) {
	if (!strcmp(s->key, "handwriting_font")) { /* the styles of one group (s->val[0]), or all of them */
		int n = 0;
		for (int i = 0; i < NHFONTS && n < MAXOPT; i++)
			if (!s->val[0] || !strcmp(HFONTS[i].group, s->val[0])) { val[n] = HFONTS[i].id; text[n] = HFONTS[i].label; n++; }
		return n;
	}
	if (!strcmp(s->key, "typed_font")) {
		int n = 0;
		while (n < MAXOPT && typed_font_id(n)) { val[n] = typed_font_id(n); text[n] = typed_font_label(n); n++; }
		return n;
	}
	for (int i = 0; i < s->n; i++) { val[i] = s->val[i]; text[i] = s->text[i]; }
	return s->n;
}

/* Maps a config key/value onto the app. Returns 1 if the key is a setting. */
int settings_apply(App *a, const char *k, const char *v) {
	if (!strcmp(k, "reply_voice")) snprintf(a->voice, sizeof a->voice, "%s", v);
	else if (!strcmp(k, "reply_length")) snprintf(a->length, sizeof a->length, "%s", v);
	else if (!strcmp(k, "math_help")) snprintf(a->math_help, sizeof a->math_help, "%s", v);
	else if (!strcmp(k, "reply_language")) snprintf(a->language, sizeof a->language, "%s", v);
	else if (!strcmp(k, "translate_to")) snprintf(a->translate_to, sizeof a->translate_to, "%s", v);
	else if (!strcmp(k, "translate_style")) a->translate_typed = !strcmp(v, "typed");
	else if (!strcmp(k, "typeup")) a->typeup_below = !strcmp(v, "below");
	else if (!strcmp(k, "email_provider")) snprintf(a->email_provider, sizeof a->email_provider, "%s", v);
	else if (!strcmp(k, "email_address")) snprintf(a->email_address, sizeof a->email_address, "%s", v);
	else if (!strcmp(k, "email_to")) snprintf(a->email_to, sizeof a->email_to, "%s", v);
	else if (!strcmp(k, "email_from")) snprintf(a->email_from, sizeof a->email_from, "%s", v);
	else if (!strcmp(k, "smtp_server")) snprintf(a->smtp_server, sizeof a->smtp_server, "%s", v);
	else if (!strcmp(k, "smtp_port")) a->smtp_port = atoi(v);
	else if (!strcmp(k, "scratch_erase")) a->scratch_erase = strcmp(v, "off") != 0;
	else if (!strcmp(k, "handwriting_font")) {
		a->hfont = 0;
		for (int i = 0; i < NHFONTS; i++)
			if (!strcmp(HFONTS[i].id, v)) a->hfont = i;
	} else if (!strcmp(k, "typed_font")) {
		snprintf(a->tfont, sizeof a->tfont, "%s", v);
		if (a->disp.fb && a->reply_fonts_loaded) reply_fonts_load(a); /* live change */
	} else if (!strcmp(k, "handwriting_size")) {
		snprintf(a->hsize, sizeof a->hsize, "%s", v);
		a->script_cap = !strcmp(v, "small") ? 46 : !strcmp(v, "large") ? 76 : 60;
	} else if (!strcmp(k, "ink")) {
		snprintf(a->ink, sizeof a->ink, "%s", v);
	} else if (!strcmp(k, "writing_speed")) a->anim_speed = atof(v) > 0.1 ? atof(v) : 1.0;
	else if (!strcmp(k, "pause_seconds")) {
		double ps = atof(v);
		a->pause_ms = ps <= 0 ? 0 : ps < 0.8 ? 800 : ps > 30 ? 30000 : (int)(ps * 1000);
		for (int i = 0; i < MAX_MODES; i++) a->autosend_ms[i] = a->pause_ms;
	} else return 0;
	return 1;
}

static const char *nearest(const char **val, int n, double cur, double scale, int skip_zero) {
	double best = 1e18;
	const char *pick = val[0];
	for (int i = 0; i < n; i++) {
		if (skip_zero && atof(val[i]) == 0) continue;
		double d = atof(val[i]) * scale - cur;
		if (d < 0) d = -d;
		if (d < best) { best = d; pick = val[i]; }
	}
	return pick;
}

static const char *current(App *a, const Setting *s, const char **val, int n) {
	const char *k = s->key;
	if (!strcmp(k, "reply_voice")) return a->voice;
	if (!strcmp(k, "reply_length")) return a->length;
	if (!strcmp(k, "math_help")) return a->math_help;
	if (!strcmp(k, "reply_language")) return a->language;
	if (!strcmp(k, "translate_to")) return a->translate_to;
	if (!strcmp(k, "translate_style")) return a->translate_typed ? "typed" : "written";
	if (!strcmp(k, "typeup")) return a->typeup_below ? "below" : "replace";
	if (!strcmp(k, "email_provider")) return a->email_provider;
	if (!strcmp(k, "scratch_erase")) return a->scratch_erase ? "on" : "off";
	if (!strcmp(k, "handwriting_font")) return HFONTS[a->hfont].id;
	if (!strcmp(k, "typed_font")) return a->tfont;
	if (!strcmp(k, "handwriting_size")) return a->hsize;
	if (!strcmp(k, "ink")) return a->ink;
	if (!strcmp(k, "writing_speed")) return nearest(val, n, a->anim_speed, 1, 0);
	if (!strcmp(k, "pause_seconds")) return a->pause_ms == 0 ? "0" : nearest(val, n, a->pause_ms, 1000, 1);
	if (!strcmp(k, "provider")) return a->provider == PROV_OPENAI ? "openai" : "anthropic";
	if (!strcmp(k, "anthropic_model")) return brain_model(a, provider_find("anthropic"));
	if (!strcmp(k, "openai_model")) return brain_model(a, provider_find("openai"));
	return "";
}

/* The label of a setting's current value, e.g. "Friendly". */
static const char *current_label(App *a, const char *key) {
	const char *cur = "";
	for (int r = 0; r < NSET; r++) {
		if (strcmp(SET[r].key, key)) continue;
		const char *val[MAXOPT], *text[MAXOPT];
		int n = options(&SET[r], val, text);
		cur = current(a, &SET[r], val, n);
		for (int i = 0; i < n; i++)
			if (!strcmp(cur, val[i])) return text[i];
	}
	return cur;
}

static void summary(App *a, int cat, char *out, size_t n) {
	switch (cat) {
	case 0: snprintf(out, n, "Using: %s - 18 everyday and 12 fantasy voices", current_label(a, "reply_voice")); break;
	case 2: snprintf(out, n, "%s handwriting (%s, %s), typed in %s - %d styles", current_label(a, "handwriting_font"),
			 current_label(a, "handwriting_size"), current_label(a, "writing_speed"), current_label(a, "typed_font"), NHFONTS); break;
	case 3: snprintf(out, n, "Translate mode writes back in %s (%s)", a->translate_to, a->translate_typed ? "typed" : "handwritten"); break;
	case 4: snprintf(out, n, "%s", !strcmp(a->math_help, "full") ? "Full worked solutions" : "Hints first, then the answer if you ask"); break;
	case 5:
		if (a->pause_ms) snprintf(out, n, "%s pen, auto-reply after %s", current_label(a, "ink"), current_label(a, "pause_seconds"));
		else snprintf(out, n, "%s pen, auto-reply off (Send button only)", current_label(a, "ink"));
		if (a->scratch_erase) snprintf(out + strlen(out), n - strlen(out), ", scratch out to erase");
		break;
	case 7:
		if (a->email_address[0]) snprintf(out, n, "%s: %s", current_label(a, "email_provider"), a->email_address);
		else snprintf(out, n, "Not set up - email a page to yourself from Send");
		break;
	case 6: {
		const Provider *p = provider_by_prov(a->provider);
		if (!p) { snprintf(out, n, "No AI key yet - add one here"); break; }
		const char *ml = provider_model_label(p, brain_model(a, p));
		snprintf(out, n, "%s, %s", p->short_name, ml ? ml : brain_model(a, p));
		break;
	}
	case 1: snprintf(out, n, "%s replies, %s; Type up: %s", current_label(a, "reply_length"),
			 !strcmp(a->language, "english") ? "always in English" : "in your language",
			 a->typeup_below ? "handwriting stays" : "handwriting becomes the text"); break;
	default: out[0] = 0;
	}
}

static void fill(App *a, int x, int y, int w, int h, uint8_t g) {
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, g);
}

static void label_in(App *a, const DispFonts *font, const char *text, int x, int y, int w, int h, int px, int bold,
		     int inverted) {
	DispText t = { .left = x, .top = y + (h - px) / 2 - px / 8, .right = x + w, .bottom = y + h, .px = px,
		       .style = bold ? TXT_BOLD : TXT_REGULAR, .centered = true, .inverted = inverted };
	disp_text(&a->disp, font, text, &t, NULL);
}

static int usable(App *a, const Setting *s, int i) {
	if (!strcmp(s->key, "provider")) return brain_has_key(provider_at(i));
	if (!strcmp(s->key, "typed_font")) return typed_font_available(i);
	return 1;
}

/* A sample word in a Hershey font, centred in the box (for the handwriting font previews). */
static void hershey_sample(App *a, int f, const char *word, int x, int y, int w, int cap, uint8_t gray) {
	const HFont *F = &HFONTS[f];
	double s = (double)cap / F->cap, width = 0;
	for (const char *c = word; *c; c++) width += (F->g[*c - 32].right - F->g[*c - 32].left) * s;
	if (width > w - 24) { s *= (w - 24) / width; width = w - 24; } /* wide styles: shrink to fit the chip */
	double cx = x + (w - width) / 2;
	Dirty d = { 0, 0, -1, -1 };
	for (const char *c = word; *c; c++) {
		const HGlyph *g = &F->g[*c - 32];
		const signed char *v = F->pts + 2 * g->off;
		double ox = cx - g->left * s;
		int have = 0, lx = 0, ly = 0;
		for (int k = 0; k < g->n; k++) {
			if (v[2 * k] == -128) { have = 0; continue; }
			int px = (int)(ox + v[2 * k] * s), py = (int)(y + (v[2 * k + 1] - 9) * s);
			if (have) stamp_line(a, lx, ly, px, py, 1, gray, &d);
			lx = px; ly = py; have = 1;
		}
		cx += (g->right - g->left) * s;
	}
}

/* Field rows (n == -1): a value typed on the on-screen keyboard. "key:<name>" values live in the key store and are
 * only ever shown masked. */
static void field_value(App *a, const char *key, char *b, size_t n) {
	if (!strncmp(key, "key:", 4)) { key_masked(key + 4, b, n); return; }
	if (!strcmp(key, "smtp_port")) { if (a->smtp_port) snprintf(b, n, "%d", a->smtp_port); else b[0] = 0; return; }
	snprintf(b, n, "%s", !strcmp(key, "email_address") ? a->email_address : !strcmp(key, "email_to") ? a->email_to
			     : !strcmp(key, "email_from") ? a->email_from : !strcmp(key, "smtp_server") ? a->smtp_server : "");
}

/* Email rows that don't apply to the chosen account stay hidden. */
static int visible(App *a, const Setting *s) {
	if (s->cat != CAT_EMAIL) return 1;
	int resend = !strcmp(a->email_provider, "resend"), custom = !strcmp(a->email_provider, "custom");
	if (!strcmp(s->key, "key:email_password")) return !resend;
	if (!strcmp(s->key, "key:resend_api_key")) return resend;
	if (!strcmp(s->key, "smtp_server") || !strcmp(s->key, "smtp_port")) return custom;
	return 1;
}

static int draw_field(App *a, int r, int y) {
	const Setting *s = &SET[r];
	int h0 = y;
	text_draw(a, s->label, LEFT, y, a->W - 2 * LEFT, 56, 42, TXT_BOLD);
	y += 60;
	if (s->help) { text_draw(a, s->help, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC); y += 48; }
	y += 8;
	char v[200];
	field_value(a, s->key, v, sizeof v);
	int w = a->W - 2 * LEFT, h = 104;
	ui_rrect(a, LEFT, y, w, h, 26, 0x99);
	ui_rrect(a, LEFT + 4, y + 4, w - 8, h - 8, 23, 0xFF);
	text_draw(a, v[0] ? v : "not set", LEFT + 30, y + (h - 38) / 2 - 6, w - 300, 50, 38, v[0] ? TXT_REGULAR : TXT_ITALIC);
	label_in(a, &a->font_ui, v[0] ? "Change" : "Enter", LEFT + w - 240, y, 220, h, 38, 1, 0);
	if (nhits < 200) hits[nhits++] = (Hit){ HIT_FIELD, LEFT, y, w, h, r, 0 };
	char hl[72];
	snprintf(hl, sizeof hl, "field:%s", s->key);
	hits_note(hl, LEFT, y, w, h);
	return y + h - h0 + 46;
}

/* the keyboard closed: save what was typed */
/* (brain_keys_changed() is in brain.c: it re-reads the key store and picks the provider again) */

static void field_done(App *a, const char *key, const char *text) {
	if (text) {
		if (!strncmp(key, "key:", 4)) {
			key_set(key + 4, text);
			brain_keys_changed(a);
			logf_("setting %s: %s", key + 4, text[0] ? "saved" : "removed");
		} else {
			config_set(key, text);
			settings_apply(a, key, text);
			logf_("setting %s = %s", key, text);
		}
		ui_set_status(a, text[0] ? "saved" : "cleared");
	} else ui_set_status(a, "not changed");
	draw(a, &a->gc16);
}

/* ---- AI & keys: drawn from the provider table. Tap a service to use it (or, without a key yet, to see where its
 * key goes); its key (masked, Change, Test), models and, for Bedrock, the region follow. */
static char ai_focus[16]; /* the service whose details are shown */

static const Provider *ai_shown(App *a) {
	const Provider *p = ai_focus[0] ? provider_find(ai_focus) : NULL;
	if (!p) p = provider_by_prov(a->provider);
	return p ? p : provider_at(0);
}

static void chip(App *a, int x, int y, int w, int h, const char *label, const char *sub, int sel, int enabled) {
	fill(a, x, y, w, h, enabled ? 0x00 : 0xAA);
	if (!sel) fill(a, x + 5, y + 5, w - 10, h - 10, 0xFF);
	if (sub) {
		label_in(a, &a->font_ui, label, x, y + 10, w, h / 2, 36, sel, sel);
		label_in(a, &a->font_ui, sub, x, y + h / 2 - 4, w, h / 2, 26, 0, sel);
	} else label_in(a, &a->font_ui, label, x, y, w, h, 36, sel, sel);
}

static int section_label(App *a, const char *label, const char *help, int y) {
	text_draw(a, label, LEFT, y, a->W - 2 * LEFT, 56, 42, TXT_BOLD);
	y += 60;
	if (help) { text_draw(a, help, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC); y += 48; }
	return y + 8;
}

static int draw_ai(App *a, int y) {
	const Provider *cur = provider_by_prov(a->provider), *show = ai_shown(a);
	y = section_label(a, "Which AI answers", "Each service needs your own key. Tap one to use it.", y);
	int n = providers_count(), per = 4, cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per, ch = CHIP_H;
	for (int i = 0; i < n; i++) {
		const Provider *p = provider_at(i);
		int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (ch + GAP), has = brain_has_key(p);
		chip(a, x, cy, cw, ch, p->short_name, has ? (p == show && p != cur ? "shown below" : NULL) : "no key yet", p == cur, 1);
		if (p == show && p != cur) fill(a, x + 5, cy + ch - 12, cw - 10, 7, 0x00); /* underline: details below */
		if (nhits < 200) hits[nhits++] = (Hit){ HIT_PROV, x, cy, cw, ch, i, 0 };
		char hl[48]; snprintf(hl, sizeof hl, "ai:%s", p->id); hits_note(hl, x, cy, cw, ch);
	}
	y += ((n + per - 1) / per) * (ch + GAP) + 30;
	/* the shown service's key */
	char lab[96], help[200], v[160];
	snprintf(lab, sizeof lab, "%s key", show->label);
	snprintf(help, sizeof help, "Get one at %s, or copy a file %s into the inkling folder.", show->key_where, show->key_file);
	y = section_label(a, lab, help, y);
	key_masked(show->key_name, v, sizeof v);
	int w = a->W - 2 * LEFT, h = 104, tw = 260;
	ui_rrect(a, LEFT, y, w - tw - GAP, h, 26, 0x99);
	ui_rrect(a, LEFT + 4, y + 4, w - tw - GAP - 8, h - 8, 23, 0xFF);
	text_draw(a, v[0] ? v : "not set", LEFT + 30, y + (h - 38) / 2 - 6, w - tw - 340, 50, 38, v[0] ? TXT_REGULAR : TXT_ITALIC);
	label_in(a, &a->font_ui, v[0] ? "Change" : "Enter", LEFT + w - tw - GAP - 240, y, 220, h, 38, 1, 0);
	if (nhits < 200) hits[nhits++] = (Hit){ HIT_AIKEY, LEFT, y, w - tw - GAP, h, 0, 0 };
	hits_note("ai:key", LEFT, y, w - tw - GAP, h);
	ui_rrect(a, LEFT + w - tw, y, tw, h, 26, v[0] ? 0x2A : 0xCC);
	label_in(a, &a->font_ui, "Test key", LEFT + w - tw, y, tw, h, 38, 1, 1);
	if (nhits < 200) hits[nhits++] = (Hit){ HIT_TEST, LEFT + w - tw, y, tw, h, 0, 0 };
	hits_note("ai:test", LEFT + w - tw, y, tw, h);
	y += h + 46;
	/* its models */
	snprintf(lab, sizeof lab, "%s model", show->short_name);
	y = section_label(a, lab, NULL, y);
	const char *m = brain_model(a, show);
	per = show->nmodels > 4 ? 3 : 2;
	cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per;
	for (int i = 0; i < show->nmodels; i++) {
		int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (ch + GAP);
		chip(a, x, cy, cw, ch, show->models[i].label, NULL, !strcmp(m, show->models[i].id), 1);
		if (nhits < 200) hits[nhits++] = (Hit){ HIT_MODEL, x, cy, cw, ch, i, 0 };
		char hl[96]; snprintf(hl, sizeof hl, "model:%s", show->models[i].id); hits_note(hl, x, cy, cw, ch);
	}
	y += ((show->nmodels + per - 1) / per) * (ch + GAP) + 30;
	if (show->needs_region) {
		y = section_label(a, "Region", "Where your requests are handled.", y);
		per = 4;
		cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per;
		int rh = 90;
		const char *r = a->bedrock_region[0] ? a->bedrock_region : show->default_region;
		for (int i = 0; i < show->nregions; i++) {
			int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (rh + GAP);
			fill(a, x, cy, cw, rh, 0x00);
			int sel = !strcmp(r, show->regions[i].id);
			if (!sel) fill(a, x + 5, cy + 5, cw - 10, rh - 10, 0xFF);
			label_in(a, &a->font_ui, show->regions[i].label, x, cy, cw, rh, 30, sel, sel);
			if (nhits < 200) hits[nhits++] = (Hit){ HIT_REGION, x, cy, cw, rh, i, 0 };
			char hl[64]; snprintf(hl, sizeof hl, "region:%s", show->regions[i].id); hits_note(hl, x, cy, cw, rh);
		}
		y += ((show->nregions + per - 1) / per) * (rh + GAP) + 30;
	}
	return y;
}

static void ai_key_done(App *a, const char *key, const char *text) {
	const Provider *p = ai_shown(a);
	if (text) {
		key_set(p->key_name, text);
		brain_keys_changed(a);
		logf_("setting %s: %s", p->key_name, text[0] ? "saved" : "removed");
		if (text[0]) {
			if (a->provider == PROV_NONE || !brain_has_key(provider_by_prov(a->provider))) { brain_config_apply(a, "provider", p->id); config_set("provider", p->id); }
			app_key_check(a, p->id); /* says in the status line whether it works */
		} else ui_set_status(a, "%s key removed", p->short_name);
	} else ui_set_status(a, "not changed");
	(void)key;
	draw(a, &a->gc16);
}

/* Draws one setting at y; returns the height used. */
static int draw_setting(App *a, int r, int y) {
	const Setting *s = &SET[r];
	const char *val[MAXOPT], *text[MAXOPT];
	int n = options(s, val, text);
	int per = s->per_row, h0 = y;
	int compact = s->cat == 2; /* Handwriting & fonts: 30 styles + 10 fonts on one screen */
	text_draw(a, s->label, LEFT, y, a->W - 2 * LEFT, compact ? 48 : 56, compact ? 36 : 42, TXT_BOLD);
	y += compact ? 50 : 60;
	if (s->help) {
		text_draw(a, s->help, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
		y += 48;
	}
	y += 8;
	int is_hw = !strcmp(s->key, "handwriting_font"), is_tf = !strcmp(s->key, "typed_font");
	int ch = is_hw ? (compact ? 104 : PREVIEW_H) : compact ? 96 : CHIP_H;
	int cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per;
	const char *cur = current(a, s, val, n);
	for (int i = 0; i < n; i++) {
		int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (ch + GAP);
		int sel = !strcmp(cur, val[i]), ok = usable(a, s, i);
		fill(a, x, cy, cw, ch, ok ? 0x00 : 0xAA);
		if (!sel) fill(a, x + 5, cy + 5, cw - 10, ch - 10, 0xFF);
		if (is_hw) {
			int f = 0;
			while (f < NHFONTS - 1 && strcmp(HFONTS[f].id, val[i])) f++;
			if (compact) {
				hershey_sample(a, f, "Hello", x, cy + 58, cw, 38, sel ? 0xFF : 0x00);
				label_in(a, &a->font_ui, text[i], x, cy + 66, cw, 32, 24, 0, sel);
			} else {
				hershey_sample(a, f, "Hello", x, cy + 88, cw, 46, sel ? 0xFF : 0x00);
				label_in(a, &a->font_ui, text[i], x, cy + 104, cw, 40, 28, 0, sel);
			}
		} else if (is_tf) {
			if (ok) label_in(a, font_preview(a, i), text[i], x, cy, cw, ch, compact ? 36 : 40, 0, sel);
			else label_in(a, &a->font_ui, "(not on this Kindle)", x, cy, cw, ch, 26, 0, 0);
		} else {
			label_in(a, &a->font_ui, ok ? text[i] : "(no key yet)", x, cy, cw, ch, 38, sel, sel);
		}
		if (nhits < 200) hits[nhits++] = (Hit){ HIT_CHIP, x, cy, cw, ch, r, i };
		char hl[72];
		snprintf(hl, sizeof hl, "%s=%s", s->key, val[i]);
		hits_note(hl, x, cy, cw, ch);
	}
	y += ((n + per - 1) / per) * (ch + GAP);
	return y - h0 + (compact ? 18 : 46);
}

static void draw(App *a, const DispRefresh *how) {
	if (kb_active()) { kb_draw(a); return; }
	nhits = 0;
	hits_reset();
	fill(a, a->cx, a->cy, a->cw, a->ch, 0xFF);
	if (a->settings_page < 0) {
		text_draw(a, "Settings", LEFT, TOP, 900, 84, 64, TXT_BOLD);
		text_draw(a, "Tap a section. Changes are saved straight away (also in inkling/config.txt).", LEFT,
			  TOP + 92, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
		int y = TOP + 170, rowh = 216; /* 9 sections: the last ends at y = 2324 */
		for (int c = 0; c < NCATS; c++) {
			char sum[200];
			summary(a, c, sum, sizeof sum);
			fill(a, LEFT, y, a->W - 2 * LEFT, 3, 0x00);
			text_draw(a, CATS[c], LEFT + 10, y + 30, a->W - 2 * LEFT - 120, 66, 50, TXT_BOLD);
			text_draw(a, sum, LEFT + 10, y + 108, a->W - 2 * LEFT - 140, 50, 34, TXT_REGULAR);
			text_draw(a, ">", a->W - LEFT - 70, y + 60, 60, 80, 64, TXT_BOLD);
			hits[nhits++] = (Hit){ HIT_CAT, LEFT, y, a->W - 2 * LEFT, rowh, c, 0 };
			hits_note(CATS[c], LEFT, y, a->W - 2 * LEFT, rowh);
			y += rowh;
		}
		fill(a, LEFT, y, a->W - 2 * LEFT, 3, 0x00);
	} else {
		int bw = 230;
		fill(a, LEFT, TOP, bw, 96, 0x00);
		fill(a, LEFT + 5, TOP + 5, bw - 10, 86, 0xFF);
		label_in(a, &a->font_ui, "< Back", LEFT, TOP, bw, 96, 38, 1, 0);
		hits[nhits++] = (Hit){ HIT_BACK, LEFT, TOP, bw, 96, 0, 0 };
		hits_note("< Back", LEFT, TOP, bw, 96);
		text_draw(a, CATS[a->settings_page], LEFT + bw + 40, TOP + 12, a->W - LEFT * 2 - bw - 40, 80, 60, TXT_BOLD);
		int y = TOP + 150;
		if (a->settings_page == CAT_AI) y = draw_ai(a, y); /* the services first, then the GitHub token row */
		for (int r = 0; r < NSET; r++)
			if (SET[r].cat == a->settings_page && visible(a, &SET[r])) y += SET[r].n == -1 ? draw_field(a, r, y) : draw_setting(a, r, y);
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, how);
}

void settings_open(App *a) {
	a->in_settings = 1;
	a->settings_page = -1;
	a->autosend_at = 0;
	draw(a, &a->flash);
	ui_set_status(a, "settings - tap Close to go back to your page");
}

/* Flick: right = back to the list; left does nothing on a section page. */
/* A flick on a section: left = the next section, right = back to the list of sections. */
void settings_flip(App *a, int dir) {
	if (kb_active()) return;
	if (dir < 0 && a->settings_page >= 0) { a->settings_page = -1; draw(a, &a->flash); }
	else if (dir > 0 && a->settings_page < NCATS - 1) { a->settings_page++; draw(a, &a->flash); }
}

void settings_close(App *a) {
	a->in_settings = 0; /* (before kb_cancel, whose callback would redraw the settings page) */
	if (kb_active()) kb_cancel(a);
	page_load(a, a->page);
	ui_set_status(a, "settings saved - %s", brain_provider_name(a));
	if (a->npending && a->autosend_ms[a->mode] > 0) a->autosend_at = mono_ms() + a->autosend_ms[a->mode];
}

void settings_tap(App *a, int x, int y) {
	if (kb_active()) { kb_tap(a, x, y); return; }
	for (int h = 0; h < nhits; h++) {
		Hit *t = &hits[h];
		if (x < t->x - 6 || x > t->x + t->w + 6 || y < t->y - 8 || y > t->y + t->h + 8) continue;
		if (t->kind == HIT_PROV || t->kind == HIT_MODEL || t->kind == HIT_REGION || t->kind == HIT_AIKEY || t->kind == HIT_TEST) {
			const Provider *p = t->kind == HIT_PROV ? provider_at(t->set) : ai_shown(a);
			if (!p) return;
			if (t->kind == HIT_PROV) {
				snprintf(ai_focus, sizeof ai_focus, "%s", p->id);
				if (brain_has_key(p)) {
					brain_config_apply(a, "provider", p->id);
					config_set("provider", p->id);
					ui_set_status(a, "%s answers now", p->short_name);
					logf_("setting provider = %s", p->id);
				} else ui_set_status(a, "add a %s key below to use it", p->short_name);
			} else if (t->kind == HIT_MODEL) {
				brain_config_apply(a, p->model_key, p->models[t->set].id);
				config_set(p->model_key, p->models[t->set].id);
				ui_set_status(a, "%s model: %s", p->short_name, p->models[t->set].label);
				logf_("setting %s = %s", p->model_key, p->models[t->set].id);
			} else if (t->kind == HIT_REGION) {
				brain_config_apply(a, "bedrock_region", p->regions[t->set].id);
				config_set("bedrock_region", p->regions[t->set].id);
				ui_set_status(a, "region: %s", p->regions[t->set].label);
			} else if (t->kind == HIT_AIKEY) {
				char title[96], hint[200];
				snprintf(title, sizeof title, "%s key", p->label);
				snprintf(hint, sizeof hint, "From %s. Typing a long key is slow: a file %s in the inkling folder works too.", p->key_where, p->key_file);
				kb_open(a, title, hint, p->key_name, "", 1, ai_key_done);
				return;
			} else { /* Test key */
				if (!brain_has_key(p)) { ui_set_status(a, "no %s key to test yet", p->short_name); return; }
				app_key_check(a, p->id);
				return;
			}
			draw(a, &a->gc16);
			return;
		}
		if (t->kind == HIT_FIELD) {
			const Setting *f = &SET[t->set];
			char v[200] = "";
			if (strncmp(f->key, "key:", 4)) field_value(a, f->key, v, sizeof v); /* a key is typed afresh, never shown */
			kb_open(a, f->label, f->help, f->key, v, !strncmp(f->key, "key:", 4), field_done);
			return;
		}
		if (t->kind == HIT_CAT) { a->settings_page = t->set; draw(a, &a->flash); return; }
		if (t->kind == HIT_BACK) { a->settings_page = -1; draw(a, &a->flash); return; }
		const Setting *s = &SET[t->set];
		const char *val[MAXOPT], *text[MAXOPT];
		options(s, val, text);
		if (!usable(a, s, t->opt)) {
			ui_set_status(a, !strcmp(s->key, "typed_font") ? "that font isn't on this Kindle"
								    : "add an API key for that AI first (see the note below)");
			return;
		}
		const char *v = val[t->opt];
		if (!brain_config_apply(a, s->key, v)) settings_apply(a, s->key, v);
		config_set(s->key, v);
		draw(a, &a->gc16);
		ui_set_status(a, "%s: %s", s->label, text[t->opt]);
		logf_("setting %s = %s", s->key, v);
		return;
	}
}

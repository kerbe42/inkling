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

static const char *CATS[] = { "Voices", "Replies", "Handwriting & fonts", "Translate", "Math", "Pen & timing", "AI", "Email" };
#define CAT_AI 6 /* two tabs: AI services (keys, self-hosted servers, the main AI) and Each feature (its own AI and tuning) */
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

enum { HIT_CAT, HIT_BACK, HIT_CHIP, HIT_FIELD, HIT_PROV, HIT_MODEL, HIT_REGION, HIT_AIKEY, HIT_TEST,
       /* Settings > AI: Each feature, self-hosted servers */
       HIT_ADV_OK, HIT_ADV_ROW, HIT_ADV_SVC, HIT_ADV_MODEL, HIT_ADV_EFFORT, HIT_ADV_RESET, HIT_ADV_RESET_ALL,
       HIT_SRV_FIELD, HIT_SRV_API, HIT_SRV_TEST, HIT_SRV_PICK, HIT_SRV_WAIT, HIT_SRV_SETUP, HIT_ADV_INSTR, HIT_ADV_TAB,
       HIT_ADV_TEMP, HIT_ADV_MAXTOK, HIT_SRV_JSON, HIT_AI_TAB, HIT_ADD_SERVER, HIT_SRV_NAME };
#define ADV_SERVER FEAT_COUNT
#define ADV_ALL (FEAT_COUNT + 1) /* instructions for every feature */
static int adv_page = -1;   /* Settings > AI: -1 the tabs, a FEAT_* page, ADV_SERVER (a self-hosted server) or ADV_ALL */
static int ai_tab;          /* Settings > AI: 0 AI services, 1 Each feature */
static int adv_armed = -1;  /* a reset tapped once (its page, or -2 for every feature): a second tap within 4 s does it */
static double adv_armed_t;
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
		if (!p) { snprintf(out, n, "No AI set up yet - add a key or a self-hosted server here"); break; }
		const char *ml = provider_model_label(p, brain_model(a, p));
		char list[120] = "";
		int k = 0;
		for (int f = 0; f < FEAT_COUNT; f++)
			if (brain_feature_custom(a, f))
				snprintf(list + strlen(list), sizeof list - strlen(list), "%s%s", k++ ? ", " : "", feature_at(f)->label);
		snprintf(out, n, "%s, %s; %s%s", p->short_name, ml ? ml : brain_model(a, p), k ? "own choices for " : "same AI for every feature",
			 list);
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
			if (!brain_config_apply(a, key, text)) settings_apply(a, key, text);
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
	y = section_label(a, "Which AI answers", "Set up each AI you use (a key, or your own server). The dark one answers unless a "
					     "feature chooses another.", y);
	int n = providers_count(), per = 4, cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per, ch = CHIP_H, k = 0, add = -1;
	for (int i = 0; i <= n; i++) {
		const Provider *p = i < n ? provider_at(i) : NULL;
		if (p && p->custom && p->slot > 0 && !provider_custom_slot(p->slot)->url[0]) { /* not set up: one "+ server" chip */
			if (add < 0) add = p->slot;
			continue;
		}
		if (!p && add < 0) break;
		int x = LEFT + (k % per) * (cw + GAP), cy = y + (k / per) * (ch + GAP);
		k++;
		if (!p) {
			chip(a, x, cy, cw, ch, "+ Server", "another one", 0, 1);
			if (nhits < 200) hits[nhits++] = (Hit){ HIT_ADD_SERVER, x, cy, cw, ch, add, 0 };
			hits_note("ai:add-server", x, cy, cw, ch);
			break;
		}
		int has = brain_usable(p);
		chip(a, x, cy, cw, ch, p->short_name, has ? (p == show && p != cur ? "shown below" : NULL) : p->custom ? "not set up" : "no key yet",
		     p == cur, 1);
		if (p == show && p != cur) fill(a, x + 5, cy + ch - 12, cw - 10, 7, 0x00); /* underline: details below */
		if (nhits < 200) hits[nhits++] = (Hit){ HIT_PROV, x, cy, cw, ch, i, 0 };
		char hl[48]; snprintf(hl, sizeof hl, "ai:%s", p->id); hits_note(hl, x, cy, cw, ch);
	}
	y += ((k + per - 1) / per) * (ch + GAP) + 30;
	if (show->custom) { /* a self-hosted server: its settings are a page of their own, with their warnings */
		const CustomServer *cs = provider_custom_slot(show->slot);
		char lab[200];
		if (cs->url[0]) snprintf(lab, sizeof lab, "%s, model %s.", cs->url, cs->model[0] ? cs->model : "not chosen yet");
		else snprintf(lab, sizeof lab, "Your own AI server: Ollama, LM Studio, llama.cpp, vLLM, or any OpenAI- or Anthropic-compatible API.");
		y = section_label(a, cs->name, lab, y);
		int w = a->W - 2 * LEFT, h = 110;
		ui_rrect(a, LEFT, y, w, h, 26, 0x2A);
		snprintf(lab, sizeof lab, "%s: address, key and model >", cs->url[0] ? "Server settings" : "Set it up");
		label_in(a, &a->font_ui, lab, LEFT, y, w, h, 38, 1, 1);
		if (nhits < 200) hits[nhits++] = (Hit){ HIT_SRV_SETUP, LEFT, y, w, h, show->slot, 0 };
		hits_note("ai:server-setup", LEFT, y, w, h);
		return y + h + 46;
	}
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

/* ---- Settings > AI, Each feature: which of the AIs set up under AI services a feature uses, its model and tuning, and
 * the self-hosted servers' pages. A feature's page is behind a one-time warning (ai_advanced = understood); every page
 * repeats it briefly, and each feature's page says what its current choices mean ("What to expect",
 * brain_route_notes). ---- */

static const char ADV_GATE[] =
	"Inkling's defaults are chosen and tested for each feature on the Kindle: quick models for chat, careful ones for "
	"maths, the most capable for drawings. Changing them can:\n"
	"\xE2\x80\xA2 make replies slower: a bigger model or more thinking can take several times as long\n"
	"\xE2\x80\xA2 make mistakes more likely: a small model can misread handwriting or get maths wrong\n"
	"\xE2\x80\xA2 cost more on your AI account\n"
	"\xE2\x80\xA2 stop a feature working: some models and servers can't read images, so they can't read your page\n"
	"You can put any feature back to its default at any time.";
static const char ADV_WARN[] =
	"Defaults are tuned for speed and accuracy. Other choices can be slower, less accurate or costlier, or stop a "
	"feature working.";
static const char SRV_WARN[] =
	"Inkling can't check your server's speed, accuracy or privacy, and your pages go to it. Its model must read images "
	"to read your handwriting; small local models often misread it and slip in maths. http:// isn't encrypted: use it "
	"only on your own network.";

static const int WAITS[] = { 60, 170, 300, 600 };
static const char *WAIT_LABEL[] = { "1 min", "3 min", "5 min", "10 min" };
static const char *APIS[] = { "openai", "anthropic", "responses" };
static const char *API_LABEL[] = { "OpenAI-compatible", "Anthropic-compatible", "OpenAI Responses" };
static const char *API_SUB[] = { "most servers", "Messages API", "Responses API" };
static const char *EFFORTS[] = { "", "low", "medium", "high" };

static void hit(int kind, int x, int y, int w, int h, int set, const char *label) {
	if (nhits < 200) hits[nhits++] = (Hit){ kind, x, y, w, h, set, 0 };
	hits_note(label, x, y, w, h);
}

/* A choice box: dark when chosen, grey border when it can't be chosen; an optional second line. */
static void adv_chip(App *a, int x, int y, int w, int h, const char *label, const char *sub, int sel, int enabled) {
	fill(a, x, y, w, h, enabled ? 0x00 : 0xAA);
	if (!sel) fill(a, x + 5, y + 5, w - 10, h - 10, 0xFF);
	if (sub) {
		label_in(a, &a->font_ui, label, x + 8, y + 8, w - 16, h / 2, 32, sel, sel);
		label_in(a, &a->font_ui, sub, x + 8, y + h / 2 - 4, w - 16, h / 2, 25, 0, sel);
	} else label_in(a, &a->font_ui, label, x + 8, y, w - 16, h, 32, sel, sel);
}

/* A framed note with a warning sign at y; returns the y below it (gap included). */
static int warn_box(App *a, const char *title, const char *text, int y) {
	int w = a->W - 2 * LEFT, pad = 30, iw = 90, px = 30;
	int th = title ? 58 : 0, bh = text_measure(a, text, w - 2 * pad - iw, px, TXT_REGULAR, NULL);
	int h = pad + th + bh + pad;
	ui_rrect(a, LEFT, y, w, h, 26, 0x00);
	ui_rrect(a, LEFT + 6, y + 6, w - 12, h - 12, 22, 0xFF);
	ui_rrect(a, LEFT + pad, y + pad, 60, 60, 30, 0x00);
	label_in(a, &a->font_ui, "!", LEFT + pad, y + pad, 60, 60, 44, 1, 1);
	if (title) text_draw(a, title, LEFT + pad + iw, y + pad - 2, w - 2 * pad - iw, 50, 40, TXT_BOLD);
	text_draw(a, text, LEFT + pad + iw, y + pad + th, w - 2 * pad - iw, bh, px, TXT_REGULAR);
	return y + h + 36;
}

static int wide_button(App *a, const char *label, int kind, int set, const char *hl, int y, int dark) {
	int w = a->W - 2 * LEFT, h = 110;
	if (dark) ui_rrect(a, LEFT, y, w, h, 26, 0x2A);
	else {
		ui_rrect(a, LEFT, y, w, h, 26, 0x00);
		ui_rrect(a, LEFT + 5, y + 5, w - 10, h - 10, 22, 0xFF);
	}
	label_in(a, &a->font_ui, label, LEFT, y, w, h, 38, 1, dark);
	hit(kind, LEFT, y, w, h, set, hl);
	return y + h + 40;
}

/* A model's name without the chip's remarks: "Opus 5.5 - best, slower" -> "Opus 5.5", "Haiku 5.5 only" -> "Haiku 5.5" */
static void model_short(const Provider *p, const char *id, char *b, size_t n) {
	const char *ml = provider_model_label(p, id);
	snprintf(b, n, "%s", ml ? ml : id);
	char *d = ml ? strstr(b, " - ") : NULL;
	if (d) *d = 0;
	size_t l = strlen(b);
	if (ml && l > 5 && !strcmp(b + l - 5, " only")) b[l - 5] = 0;
}

/* "Claude Haiku 5.5" / "Self-hosted qwen3-vl:8b" for a route */
static void route_name(const Route *r, char *b, size_t n) {
	if (!r->p) { snprintf(b, n, "no AI set up"); return; }
	char m[80];
	model_short(r->p, r->model, m, sizeof m);
	snprintf(b, n, "%s %s", r->p->short_name, m);
}

static void adv_row_summary(App *a, int f, char *b, size_t n) {
	Route r;
	brain_route(a, f, &r);
	char rn[120];
	route_name(&r, rn, sizeof rn);
	const char *ins = r.instr ? " + your instructions" : "";
	if (r.fallback) snprintf(b, n, "Chosen service isn't ready - %s for now%s", rn, ins);
	else if (brain_feature_custom(a, f) && r.custom)
		snprintf(b, n, "Custom: %s%s%s%s", rn, r.effort_set ? ", thinking " : "", r.effort_set ? r.effort : "", ins);
	else snprintf(b, n, "Default: %s%s", rn, ins);
}

/* The writer's instructions in a framed box (two lines shown), Add/Change */
static int instr_field(App *a, const char *label, const char *help, const char *value, int set, int y) {
	y = section_label(a, label, help, y);
	int w = a->W - 2 * LEFT, h = 128;
	ui_rrect(a, LEFT, y, w, h, 26, 0x99);
	ui_rrect(a, LEFT + 4, y + 4, w - 8, h - 8, 23, 0xFF);
	char shown[AI_INSTR_MAX + 8];
	if (strlen(value) > 150) snprintf(shown, sizeof shown, "%.147s...", value);
	else snprintf(shown, sizeof shown, "%s", value[0] ? value : "none");
	text_draw(a, shown, LEFT + 30, y + 18, w - 290, h - 40, 32, value[0] ? TXT_REGULAR : TXT_ITALIC);
	label_in(a, &a->font_ui, value[0] ? "Change" : "Add", LEFT + w - 230, y, 210, h, 36, 1, 0);
	hit(HIT_ADV_INSTR, LEFT, y, w, h, set, "adv:instr");
	return y + h + 40;
}

/* A small reset button at the right of the page title; the first tap arms it (dark, "Tap again"). */
static void header_reset(App *a, int set, const char *label) {
	int bw = 320, bh = 96, x = a->W - LEFT - bw, armed = adv_armed == set;
	fill(a, x, TOP, bw, bh, 0x00);
	if (!armed) fill(a, x + 5, TOP + 5, bw - 10, bh - 10, 0xFF);
	label_in(a, &a->font_ui, armed ? "Tap again" : label, x, TOP, bw, bh, 36, 1, armed);
	hit(HIT_ADV_RESET, x, TOP, bw, bh, set, "adv:reset");
}

static int draw_adv_all(App *a, int y) {
	text_draw(a, "Added to the instructions of every feature (Chat, Type up, Math, Translate, AI games, Drawings and Email), "
		     "before each feature's own.", LEFT, y, a->W - 2 * LEFT, 84, 30, TXT_ITALIC);
	y += 110;
	y = instr_field(a, "Your instructions", "For example: \"I'm 12, keep explanations simple\" or \"use British spelling\".",
			a->all_instr, ADV_ALL, y);
	y = warn_box(a, "What to expect",
		     "\xE2\x80\xA2 Long or conflicting instructions can make replies longer, slower or less reliable, on every feature.\n"
		     "\xE2\x80\xA2 They can't change how replies are laid out on the page or switch off Inkling's own rules.\n"
		     "\xE2\x80\xA2 Something like \"answer in French\" here changes Type up and Translate too: put it on one "
		     "feature's page if that isn't what you want.", y);
	if (a->all_instr[0]) header_reset(a, ADV_ALL, "Remove");
	return y;
}

static int draw_adv_gate(App *a, int y) {
	y = warn_box(a, "Before you change these", ADV_GATE, y);
	y = wide_button(a, "I understand - show the settings", HIT_ADV_OK, 0, "adv:ok", y, 1);
	text_draw(a, "Not sure? Leave them as they are: the defaults work well.", LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
	return y + 60;
}

static int draw_adv_list(App *a, int y) {
	text_draw(a, ADV_WARN, LEFT, y, a->W - 2 * LEFT, 84, 30, TXT_ITALIC);
	y += 100;
	int rowh = 160, w = a->W - 2 * LEFT;
	for (int k = 0; k <= FEAT_COUNT; k++) { /* the features, then instructions for all of them */
		char sum[200];
		const char *title;
		if (k == FEAT_COUNT) {
			title = "Instructions for every feature";
			if (a->all_instr[0]) snprintf(sum, sizeof sum, "\"%.150s\"", a->all_instr);
			else snprintf(sum, sizeof sum, "None - add your own, e.g. \"keep it simple, I'm 12\"");
		} else {
			title = feature_at(k)->label;
			adv_row_summary(a, k, sum, sizeof sum);
		}
		fill(a, LEFT, y, w, 3, 0x00);
		text_draw(a, title, LEFT + 10, y + 22, w - 120, 60, 44, TXT_BOLD);
		int all = k == FEAT_COUNT, bold = (!all && brain_feature_custom(a, k)) || (all && a->all_instr[0]);
		text_draw(a, sum, LEFT + 10, y + 88, w - 140, 40, 31, bold ? TXT_BOLD : TXT_REGULAR);
		text_draw(a, ">", a->W - LEFT - 70, y + 40, 60, 80, 64, TXT_BOLD);
		char hl[48];
		snprintf(hl, sizeof hl, "adv:row:%s", all ? "all" : feature_at(k)->id);
		hit(HIT_ADV_ROW, LEFT, y, w, rowh, all ? ADV_ALL : k, hl);
		y += rowh;
	}
	fill(a, LEFT, y, w, 3, 0x00);
	y += 40;
	int any = a->all_instr[0] != 0;
	for (int f = 0; f < FEAT_COUNT; f++) any |= brain_feature_custom(a, f);
	if (any)
		y = wide_button(a, adv_armed == -2 ? "Tap again: every feature back to its default, instructions too"
						   : "Put every feature back to its default", HIT_ADV_RESET_ALL, 0, "adv:reset-all", y, adv_armed == -2);
	return y;
}

/* ---- a feature's page: two tabs (Service & model; Tuning: thinking, temperature, reply length, instructions), each
 * with "What to expect" ---- */
static int adv_tab;         /* 0 Service & model, 1 Tuning */
static int adv_slot;        /* the server page shows this own server (0 the shared Self-hosted one, 1 + FEAT_*) */
static int adv_return = -1; /* the server page's Back: the feature page it came from, or -1 the list */

static const char *TEMPS[] = { "", "0", "0.5", "1" };
static const char *TEMP_LABEL[] = { "Default", "0", "0.5", "1" };
static const char *TEMP_SUB[] = { "model's own", "predictable", "", "varied" };
static const int MAXT[] = { 0, 2000, 4000, 8000, 32000 };
static const char *MAXT_LABEL[] = { "Default", "2,000", "4,000", "8,000", "32,000" };
static const char *JSONS[] = { "auto", "schema", "json", "prompt" };
static const char *JSON_LABEL[] = { "Auto", "Strict schema", "JSON only", "In the prompt" };
static const char *JSON_SUB[] = { "finds out", "best", "", "most servers" };

/* The service choices on a feature's page: Default, then the AIs set up in the AI services tab (the built-in services,
 * the first self-hosted server always, the others once they have an address). */
static int svc_options(int f, const Provider **o) {
	int n = 0;
	(void)f;
	o[n++] = NULL;
	for (int i = 0; i < providers_count(); i++) {
		const Provider *p = provider_at(i);
		if (p->custom && p->slot > 0 && !provider_custom_slot(p->slot)->url[0]) continue;
		o[n++] = p;
	}
	return n;
}

/* config key of a self-hosted server's setting: custom_<field>, custom2_<field>, custom3_<field> */
static void slot_key(int slot, const char *field, char *out, size_t n) {
	if (slot > 0 && slot < NSERVERS) snprintf(out, n, "custom%d_%s", slot + 1, field);
	else snprintf(out, n, "custom_%s", field);
}

/* The chips a feature's page lists for the model of its service (the server's models for an own server). */
static int model_options(App *a, int f, const Provider *p, const char *rec, const char **ids, char labels[][80]) {
	int nm = 0;
	ids[nm] = ""; snprintf(labels[nm], 80, "Default"); nm++;
	if (p->custom) {
		const char **sm;
		int ns = brain_server_models(p->slot, &sm);
		for (int i = 0; i < ns && nm < 7; i++)
			if (strcmp(sm[i], rec)) { ids[nm] = sm[i]; snprintf(labels[nm], 80, "%s", sm[i]); nm++; }
	} else
		for (int i = 0; i < p->nmodels && nm < 7; i++)
			if (strcmp(p->models[i].id, "fast")) { ids[nm] = p->models[i].id; snprintf(labels[nm], 80, "%s", p->models[i].label); nm++; }
	(void)a; (void)f;
	return nm;
}

/* What Default would pick for the model on the feature's current service */
static void default_route(App *a, int f, Route *d) {
	char keep[64];
	snprintf(keep, sizeof keep, "%s", a->feat_model[f]);
	a->feat_model[f][0] = 0;
	brain_route(a, f, d);
	snprintf(a->feat_model[f], sizeof a->feat_model[f], "%s", keep);
}

static int chip_row(App *a, const char *label, const char *help, int n, const char **labels, const char **subs, int selected,
		    int enabled, int kind, const char *hl_prefix, const char **hl_ids, int y) {
	y = section_label(a, label, help, y);
	int per = n, cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per, ch = 100;
	for (int i = 0; i < n; i++) {
		int x = LEFT + i * (cw + GAP);
		adv_chip(a, x, y, cw, ch, labels[i], subs && subs[i] && subs[i][0] ? subs[i] : NULL, i == selected, enabled);
		char hl[64];
		snprintf(hl, sizeof hl, "%s%s", hl_prefix, hl_ids[i]);
		hit(kind, x, y, cw, ch, i, hl);
	}
	return y + ch + GAP + 26;
}

static int expect_box(App *a, int f, int y) {
	char notes[1200], help[200];
	brain_route_notes(a, f, notes, sizeof notes);
	if (!notes[0]) {
		snprintf(help, sizeof help, "These are Inkling's defaults for %s, tuned for speed and accuracy.", feature_at(f)->label);
		text_draw(a, help, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
		return y + 70;
	}
	char bul[1400] = "";
	for (char *l = notes, *e; l && *l; l = e ? e + 1 : NULL) { /* each note as a bullet */
		e = strchr(l, '\n');
		snprintf(bul + strlen(bul), sizeof bul - strlen(bul), "%s\xE2\x80\xA2 %.*s", bul[0] ? "\n" : "", e ? (int)(e - l) : (int)strlen(l), l);
	}
	return warn_box(a, "What to expect", bul, y);
}

static int draw_adv_feature(App *a, int f, int y) {
	const Feature *ft = feature_at(f);
	Route r;
	brain_route(a, f, &r);
	const Provider *main_p = provider_by_prov(a->provider), *p = r.p;
	if (brain_feature_custom(a, f)) header_reset(a, f, "Use defaults");
	/* the tabs */
	int tw = (a->W - 2 * LEFT - GAP) / 2;
	adv_chip(a, LEFT, y, tw, 96, "Service & model", NULL, adv_tab == 0, 1);
	hit(HIT_ADV_TAB, LEFT, y, tw, 96, 0, "adv:tab:service");
	adv_chip(a, LEFT + tw + GAP, y, tw, 96, "Tuning & instructions", NULL, adv_tab == 1, 1);
	hit(HIT_ADV_TAB, LEFT + tw + GAP, y, tw, 96, 1, "adv:tab:tuning");
	y += 96 + 30;
	text_draw(a, ft->what, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
	y += 60;
	char help[220], lab[120];
	if (adv_tab == 0) {
		/* the service */
		snprintf(help, sizeof help, "Any AI set up in the AI services tab. Default: the one that answers everything (now %s).",
			 main_p ? main_p->short_name : "none");
		y = section_label(a, "AI service", help, y);
		const Provider *o[16];
		int n = svc_options(f, o), per = 4, cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per, ch = 100;
		const char *svc = a->feat_service[f];
		for (int i = 0; i < n; i++) {
			int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (ch + GAP);
			const Provider *q = o[i];
			char hl[48];
			if (!q) {
				adv_chip(a, x, cy, cw, ch, "Default", main_p ? main_p->short_name : "none", !svc[0], 1);
				hit(HIT_ADV_SVC, x, cy, cw, ch, i, "adv:svc:default");
				continue;
			}
			int ok = brain_usable(q);
			adv_chip(a, x, cy, cw, ch, q->short_name, ok ? NULL : q->custom ? "set up" : "no key yet", svc[0] && !strcmp(svc, q->id),
				 ok || q->custom);
			snprintf(hl, sizeof hl, "adv:svc:%s", q->id);
			hit(HIT_ADV_SVC, x, cy, cw, ch, i, hl);
		}
		y += ((n + per - 1) / per) * (ch + GAP) + 24;
		const Provider *chosen = svc[0] ? provider_find(svc) : NULL;
		if (chosen && chosen->custom) { /* its address, key and model (the same server settings as in AI services) */
			const CustomServer *cs = provider_custom_slot(chosen->slot);
			snprintf(lab, sizeof lab, "%s: server settings%s >", cs->name, cs->url[0] && cs->model[0] ? "" : " (not set up yet)");
			int w = a->W - 2 * LEFT, h = 104;
			ui_rrect(a, LEFT, y, w, h, 26, 0x2A);
			label_in(a, &a->font_ui, lab, LEFT, y, w, h, 36, 1, 1);
			hit(HIT_SRV_SETUP, LEFT, y, w, h, chosen->slot, "adv:server-settings");
			y += h + 36;
		}
		if (!p) return y;
		/* the model */
		Route d;
		default_route(a, f, &d);
		snprintf(lab, sizeof lab, "Model (%s)", p->short_name);
		y = section_label(a, lab, NULL, y);
		const char *ids[16];
		char labels[16][80];
		int nm = model_options(a, f, p, d.model, ids, labels);
		const char *cur_m = a->feat_model[f];
		int listed = !cur_m[0];
		for (int i = 1; i < nm; i++) listed |= !strcmp(ids[i], cur_m);
		ids[nm] = NULL;
		snprintf(labels[nm], sizeof labels[nm], "%s", listed ? "Other..." : cur_m);
		nm++;
		per = 3;
		cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per;
		for (int i = 0; i < nm; i++) {
			int x = LEFT + (i % per) * (cw + GAP), cy = y + (i / per) * (ch + GAP);
			int sel = i == 0 ? !cur_m[0] : ids[i] ? !strcmp(cur_m, ids[i]) : !listed;
			char subb[80];
			const char *sub = NULL;
			if (i == 0) { model_short(p, d.model, subb, sizeof subb); sub = subb[0] ? subb : "not chosen"; }
			else if (!ids[i]) sub = listed ? "type a name" : "typed name";
			adv_chip(a, x, cy, cw, ch, labels[i], sub, sel, 1);
			char hl[96];
			snprintf(hl, sizeof hl, "adv:model:%s", i == 0 ? "default" : ids[i] ? ids[i] : "other");
			hit(HIT_ADV_MODEL, x, cy, cw, ch, i == 0 ? -1 : ids[i] ? i : -2, hl);
		}
		y += ((nm + per - 1) / per) * (ch + GAP) + 26;
		return expect_box(a, f, y);
	}
	/* Tuning: thinking, temperature, reply length, instructions */
	int takes = !p || p->api != API_BEDROCK;
	Route d;
	default_route(a, f, &d);
	{
		const char *labels[4] = { "Default", "Low", "Medium", "High" }, *subs[4], *ids[4] = { "default", "low", "medium", "high" };
		subs[0] = p && (p->prov == PROV_ANTHROPIC || p->prov == PROV_GEMINI) ? brain_default_effort(f) : "model decides";
		subs[1] = "quicker"; subs[2] = ""; subs[3] = "slower";
		int sel = 0;
		for (int i = 1; i < 4; i++) if (!strcmp(a->feat_effort[f], EFFORTS[i])) sel = i;
		y = chip_row(a, "Thinking", takes ? "How much the model thinks before it answers." : "Bedrock doesn't take a thinking level.",
			     4, labels, subs, sel, takes, HIT_ADV_EFFORT, "adv:effort:", ids, y);
	}
	{
		const char *ids[4] = { "default", "0", "0.5", "1" };
		int sel = 0;
		for (int i = 1; i < 4; i++) if (a->feat_temp[f][0] && atof(a->feat_temp[f]) == atof(TEMPS[i])) sel = i;
		if (a->feat_temp[f][0] && !sel) sel = -1; /* a value set in config.txt: none of the chips */
		y = chip_row(a, "Temperature", "Low: steadier, more predictable replies. High: more varied ones.", 4, TEMP_LABEL, TEMP_SUB,
			     sel, 1, HIT_ADV_TEMP, "adv:temp:", ids, y);
	}
	{
		const char *ids[5] = { "default", "2000", "4000", "8000", "32000" }, *subs[5] = { "16,000", "", "", "", "" };
		int sel = 0;
		for (int i = 1; i < 5; i++) if (a->feat_maxtok[f] == MAXT[i]) sel = i;
		if (a->feat_maxtok[f] && !sel) sel = -1;
		y = chip_row(a, "Longest reply (tokens)", "Lower costs less but can cut long replies off; drawings need the most.", 5,
			     MAXT_LABEL, subs, sel, 1, HIT_ADV_MAXTOK, "adv:maxtok:", ids, y);
	}
	snprintf(lab, sizeof lab, "Your instructions for %s", ft->label);
	snprintf(help, sizeof help, "Added to Inkling's own%s, e.g. \"%s\".", a->all_instr[0] ? ", after those for every feature" : "",
		 f == FEAT_DRAW ? "draw in a cartoon style" : f == FEAT_MATH ? "I'm learning algebra, explain each step" :
		 f == FEAT_TRANSLATE ? "use British spelling" : f == FEAT_EMAIL ? "start the subject with [Notes]" : "keep it simple");
	y = instr_field(a, lab, help, a->feat_instr[f], f, y);
	return expect_box(a, f, y);
}

/* A value row: label, help, a framed value with Change/Enter, and optionally a button on the right. */
static int srv_field(App *a, const char *label, const char *help, const char *value, int which, const char *hl, int y,
		     const char *button, int can_test) {
	y = section_label(a, label, help, y);
	int w = a->W - 2 * LEFT, h = 100, tw = button ? 360 : 0, fw = w - (button ? tw + GAP : 0);
	ui_rrect(a, LEFT, y, fw, h, 26, 0x99);
	ui_rrect(a, LEFT + 4, y + 4, fw - 8, h - 8, 23, 0xFF);
	text_draw(a, value[0] ? value : "not set", LEFT + 30, y + (h - 34) / 2 - 6, fw - 280, 50, 34, value[0] ? TXT_REGULAR : TXT_ITALIC);
	label_in(a, &a->font_ui, value[0] ? "Change" : "Enter", LEFT + fw - 230, y, 210, h, 36, 1, 0);
	hit(HIT_SRV_FIELD, LEFT, y, fw, h, which, hl);
	if (button) {
		ui_rrect(a, LEFT + w - tw, y, tw, h, 26, can_test ? 0x2A : 0xCC);
		label_in(a, &a->font_ui, button, LEFT + w - tw, y, tw, h, 34, 1, 1);
		hit(HIT_SRV_TEST, LEFT + w - tw, y, tw, h, 0, "srv:test");
	}
	return y + h + 34;
}

static int draw_adv_server(App *a, int y) {
	int slot = adv_slot;
	const CustomServer *cs = provider_custom_slot(slot);
	const Provider *p = provider_slot(slot);
	char warn[600];
	{ /* rename it, at the right of the title */
		int bw = 260, bh = 96, x = a->W - LEFT - bw;
		fill(a, x, TOP, bw, bh, 0x00);
		fill(a, x + 5, TOP + 5, bw - 10, bh - 10, 0xFF);
		label_in(a, &a->font_ui, "Rename", x, TOP, bw, bh, 36, 1, 0);
		hit(HIT_SRV_NAME, x, TOP, bw, bh, slot, "srv:name");
	}
	y = warn_box(a, NULL, SRV_WARN, y);
	y = srv_field(a, "Address", "The API's base address, e.g. http://10.0.0.20:11434/v1 (Ollama) or https://api.openai.com/v1",
		      cs->url, 0, "srv:url", y, NULL, 0);
	{
		const char *ids[3] = { "openai", "anthropic", "responses" };
		int sel = 0;
		for (int i = 0; i < 3; i++) if (!strcmp(cs->api, APIS[i])) sel = i;
		y = chip_row(a, "Kind of API", NULL, 3, API_LABEL, API_SUB, sel, 1, HIT_SRV_API, "srv:api:", ids, y);
	}
	char v[160];
	key_masked(p->key_name, v, sizeof v);
	y = srv_field(a, "Key", "Only if the server asks for one (a cloud API always does).", v, 1, "srv:key", y, "Test & list models",
		      cs->url[0] != 0);
	y = srv_field(a, "Model", "Must read images (Drawings excepted). Tap Test to list the server's models and pick one.", cs->model, 2,
		      "srv:model", y, NULL, 0);
	const char **sm;
	int ns = brain_server_models(slot, &sm), rh = 84, per = 3, cw = (a->W - 2 * LEFT - (per - 1) * GAP) / per;
	if (ns > 6) ns = 6;
	for (int i = 0; i < ns; i++) {
		int x = LEFT + (i % per) * (cw + GAP), cy = y - 10 + (i / per) * (rh + GAP);
		fill(a, x, cy, cw, rh, 0x00);
		int sel = !strcmp(cs->model, sm[i]);
		if (!sel) fill(a, x + 4, cy + 4, cw - 8, rh - 8, 0xFF);
		label_in(a, &a->font_ui, sm[i], x + 8, cy, cw - 16, rh, 28, sel, sel);
		char hl[96];
		snprintf(hl, sizeof hl, "srv:pick:%s", sm[i]);
		hit(HIT_SRV_PICK, x, cy, cw, rh, i, hl);
	}
	if (ns) y += ((ns + per - 1) / per) * (rh + GAP) + 16;
	{
		const char *ids[4] = { "60", "170", "300", "600" }, *subs[4] = { "", "default", "", "" };
		int best = 0;
		for (int k = 1; k < 4; k++)
			if (abs(WAITS[k] - cs->timeout) < abs(WAITS[best] - cs->timeout)) best = k;
		y = chip_row(a, "Wait for an answer up to", NULL, 4, WAIT_LABEL, subs, best, 1, HIT_SRV_WAIT, "srv:wait:", ids, y);
	}
	{
		int sel = 0;
		for (int i = 0; i < 4; i++) if (!strcmp(cs->json, JSONS[i])) sel = i;
		y = chip_row(a, "Reply format", "How replies are kept to the form Inkling reads. Auto tries strict, then falls back.", 4,
			     JSON_LABEL, JSON_SUB, sel, 1, HIT_SRV_JSON, "srv:json:", JSONS, y);
	}
	snprintf(warn, sizeof warn, "Use it for everything: tap %s under Which AI answers. For one feature: Each feature > the feature.%s",
		 cs->name, slot ? " Clear the address to remove it." : "");
	text_draw(a, warn, LEFT, y, a->W - 2 * LEFT, 44, 30, TXT_ITALIC);
	return y + 60;
}

/* Settings > AI: the two tabs, or one of their pages (a server; a feature or every feature, behind the one-time
 * warning) */
static int draw_adv(App *a, int y) {
	if (adv_page == ADV_SERVER) return draw_adv_server(a, y);
	if (adv_page == ADV_ALL || (adv_page >= 0 && adv_page < FEAT_COUNT)) {
		if (!a->ai_advanced_ok) return draw_adv_gate(a, y);
		return adv_page == ADV_ALL ? draw_adv_all(a, y) : draw_adv_feature(a, adv_page, y);
	}
	int tw = (a->W - 2 * LEFT - GAP) / 2;
	adv_chip(a, LEFT, y, tw, 96, "AI services", NULL, ai_tab == 0, 1);
	hit(HIT_AI_TAB, LEFT, y, tw, 96, 0, "ai:tab:services");
	adv_chip(a, LEFT + tw + GAP, y, tw, 96, "Each feature", NULL, ai_tab == 1, 1);
	hit(HIT_AI_TAB, LEFT + tw + GAP, y, tw, 96, 1, "ai:tab:features");
	y += 96 + 34;
	return ai_tab ? draw_adv_list(a, y) : draw_ai(a, y);
}

static void adv_set(App *a, int f, const char *what, const char *v) {
	char k[40];
	snprintf(k, sizeof k, "ai_%s%s", feature_at(f)->id, what);
	if (!v[0] && strcmp(what, "_instructions")) v = "default"; /* (empty instructions are just empty) */
	brain_config_apply(a, k, v);
	config_set(k, v);
	logf_("setting %s = %s", k, v);
}

/* After a change on a feature's page: name the effect in the status line (the first note), or confirm the default. */
static void adv_status(App *a, int f) {
	char notes[1200];
	brain_route_notes(a, f, notes, sizeof notes);
	char *nl = strchr(notes, '\n');
	if (nl) *nl = 0;
	Route r;
	brain_route(a, f, &r);
	char rn[120];
	route_name(&r, rn, sizeof rn);
	if (notes[0]) ui_set_status(a, "%s: %s - %s", feature_at(f)->label, rn, notes);
	else ui_set_status(a, "%s: %s (default)", feature_at(f)->label, rn);
}

static void adv_model_done(App *a, const char *key, const char *text) {
	int f = adv_page;
	(void)key;
	if (f < 0 || f >= FEAT_COUNT) return;
	if (text && text[0]) {
		Route r;
		brain_route(a, f, &r);
		char k[40];
		snprintf(k, sizeof k, "ai_%s_model", feature_at(f)->id);
		brain_config_apply(a, k, text);
		if (strcmp(a->feat_model[f], text)) ui_set_status(a, "not a model name: letters, digits and . _ : / @ + - only");
		else {
			if (!a->feat_service[f][0] && r.p) adv_set(a, f, "", r.p->id); /* a model belongs to its service */
			adv_set(a, f, "_model", text);
			adv_status(a, f);
		}
	} else ui_set_status(a, "not changed");
	draw(a, &a->gc16);
}

static void srv_field_done(App *a, const char *key, const char *text) {
	if (!text) { ui_set_status(a, "not changed"); draw(a, &a->gc16); return; }
	const CustomServer *cs = provider_custom_slot(adv_slot);
	const Provider *p = provider_slot(adv_slot);
	if (!strncmp(key, "key:", 4)) {
		key_set(key + 4, text);
		brain_keys_changed(a);
		logf_("setting %s: %s", key + 4, text[0] ? "saved" : "removed");
		ui_set_status(a, text[0] ? "key saved" : "key removed");
		if (cs->url[0]) app_key_check(a, p->id);
	} else {
		size_t kl = strlen(key);
		int url = kl > 4 && !strcmp(key + kl - 4, "_url");
		char before[200];
		snprintf(before, sizeof before, "%s", url ? cs->url : cs->model);
		brain_config_apply(a, key, text);
		const char *now = url ? cs->url : cs->model;
		if (text[0] && !strcmp(now, before) && strcmp(text, before))
			ui_set_status(a, url ? "not an address: start with http:// or https://, no spaces, no user:password@"
					     : "not a model name: letters, digits and . _ : / @ + - only");
		else {
			config_set(key, now);
			logf_("setting %s = %s", key, now);
			if (url && now[0] && !strncmp(now, "http://", 7))
				ui_set_status(a, "saved - http:// isn't encrypted: use it only on your own network");
			else ui_set_status(a, now[0] ? "saved" : "cleared");
			if (url && now[0]) app_key_check(a, p->id); /* fetch its models */
		}
	}
	draw(a, &a->gc16);
}

static void adv_instr_done(App *a, const char *key, const char *text) {
	if (text) {
		brain_config_apply(a, key, text);
		int f = adv_page;
		const char *kept = f >= 0 && f < FEAT_COUNT ? a->feat_instr[f] : a->all_instr;
		config_set(key, kept);
		logf_("setting %s: %zu characters", key, strlen(kept));
		if (!kept[0]) ui_set_status(a, "instructions removed");
		else ui_set_status(a, "saved - instructions can make replies longer or slower; see What to expect");
	} else ui_set_status(a, "not changed");
	draw(a, &a->gc16);
}

static void srv_name_done(App *a, const char *key, const char *text) {
	if (text) {
		brain_config_apply(a, key, text);
		config_set(key, provider_custom_slot(adv_slot)->name);
		logf_("setting %s = %s", key, provider_custom_slot(adv_slot)->name);
		ui_set_status(a, "renamed: %s", provider_custom_slot(adv_slot)->name);
	} else ui_set_status(a, "not changed");
	draw(a, &a->gc16);
}

static void srv_set(App *a, const char *field, const char *v) {
	char k[48];
	slot_key(adv_slot, field, k, sizeof k);
	brain_config_apply(a, k, v);
	config_set(k, v);
	logf_("setting %s = %s", k, v);
}

/* 1 if the tap was on a control of Settings > AI's feature or server pages */
static int adv_tap(App *a, Hit *t) {
	const CustomServer *cs = provider_custom_slot(adv_slot);
	const Provider *sp = provider_slot(adv_slot);
	int f = adv_page;
	switch (t->kind) {
	case HIT_SRV_SETUP: /* a server's page: from AI services, or from a feature's page (Back returns there) */
	case HIT_ADD_SERVER:
		adv_return = f >= 0 && f < FEAT_COUNT ? f : -1;
		adv_slot = t->set;
		adv_page = ADV_SERVER;
		if (t->kind == HIT_ADD_SERVER) ui_set_status(a, "a new self-hosted server: its address, key (if it needs one) and model");
		draw(a, &a->flash);
		return 1;
	case HIT_AI_TAB:
		ai_tab = t->set;
		draw(a, &a->flash);
		return 1;
	case HIT_SRV_NAME: {
		char k[48];
		slot_key(adv_slot, "name", k, sizeof k);
		kb_open(a, "Server name", "What it's called on the Kindle, e.g. Home Ollama or Work.", k, cs->name, 0, srv_name_done);
		return 1;
	}
	case HIT_ADV_OK:
		a->ai_advanced_ok = 1;
		config_set("ai_advanced", "understood");
		draw(a, &a->flash);
		ui_set_status(a, "each feature can have its own AI, model and tuning");
		return 1;
	case HIT_ADV_ROW:
		adv_page = t->set;
		adv_tab = 0;
		if (t->set == ADV_SERVER) { adv_slot = 0; adv_return = -1; }
		draw(a, &a->flash);
		return 1;
	case HIT_ADV_TAB:
		adv_tab = t->set;
		draw(a, &a->flash);
		return 1;
	case HIT_ADV_RESET_ALL:
	case HIT_ADV_RESET: {
		int code = t->kind == HIT_ADV_RESET_ALL ? -2 : t->set;
		if (adv_armed != code || mono_ms() - adv_armed_t > 4000) { /* first tap: arm (typed instructions are at stake) */
			adv_armed = code;
			adv_armed_t = mono_ms();
			ui_set_status(a, code == -2 ? "tap again to put every feature back to its default (your instructions too)"
				      : code == ADV_ALL ? "tap again to remove your instructions for every feature"
							: "tap again to put %s back to its default (its instructions too; server settings are kept)",
				      code >= 0 && code < FEAT_COUNT ? feature_at(code)->label : "");
			draw(a, &a->gc16);
			return 1;
		}
		adv_armed = -1;
		if (code == -2 || code == ADV_ALL) {
			brain_config_apply(a, "ai_instructions", "");
			config_set("ai_instructions", "");
		}
		for (int k = 0; k < FEAT_COUNT; k++)
			if ((code == -2 || code == k) && brain_feature_custom(a, k)) {
				adv_set(a, k, "", ""); adv_set(a, k, "_model", ""); adv_set(a, k, "_effort", "");
				adv_set(a, k, "_temperature", ""); adv_set(a, k, "_max_tokens", ""); adv_set(a, k, "_instructions", "");
			}
		if (code >= 0 && code < FEAT_COUNT) adv_status(a, code);
		else ui_set_status(a, code == -2 ? "every feature is back on Inkling's defaults" : "instructions for every feature removed");
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_ADV_INSTR: {
		char key[48], title[96];
		int fi = t->set;
		if (fi >= 0 && fi < FEAT_COUNT) {
			snprintf(key, sizeof key, "ai_%s_instructions", feature_at(fi)->id);
			snprintf(title, sizeof title, "Your instructions for %s", feature_at(fi)->label);
		} else {
			snprintf(key, sizeof key, "ai_instructions");
			snprintf(title, sizeof title, "Your instructions for every feature");
		}
		kb_open(a, title, "Up to 360 characters, added to Inkling's own. They can't change the page layout. Clear + Save removes them.",
			key, fi >= 0 && fi < FEAT_COUNT ? a->feat_instr[fi] : a->all_instr, 0, adv_instr_done);
		return 1;
	}
	case HIT_ADV_SVC: {
		if (f < 0 || f >= FEAT_COUNT) return 1;
		const Provider *o[16];
		int n = svc_options(f, o);
		if (t->set < 0 || t->set >= n) return 1;
		const Provider *q = o[t->set];
		if (q && !q->custom && !brain_usable(q)) { ui_set_status(a, "add a %s key in AI & keys first", q->short_name); return 1; }
		adv_set(a, f, "", q ? q->id : "");
		adv_set(a, f, "_model", ""); /* the model list is the new service's */
		if (q && q->custom && !brain_usable(q)) { /* not set up yet: its settings, straight away */
			adv_return = f;
			adv_slot = q->slot;
			adv_page = ADV_SERVER;
			ui_set_status(a, "set up %s: address, key (if it needs one) and model - %s uses it once it's ready", q->short_name,
				      feature_at(f)->label);
			draw(a, &a->flash);
			return 1;
		}
		adv_status(a, f);
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_ADV_MODEL: {
		if (f < 0 || f >= FEAT_COUNT) return 1;
		Route r;
		brain_route(a, f, &r);
		if (!r.p) return 1;
		if (t->set == -2) {
			char title[96], hint[200];
			snprintf(title, sizeof title, "Model for %s (%s)", feature_at(f)->label, r.p->short_name);
			snprintf(hint, sizeof hint, "Exactly as %s names it%s%s. A wrong name makes replies fail.", r.p->short_name,
				 r.p->nmodels ? ", e.g. " : "", r.p->nmodels ? r.p->models[r.p->nmodels - 1].id : "");
			kb_open(a, title, hint, "adv_model", a->feat_model[f], 0, adv_model_done);
			return 1;
		}
		const char *id = "";
		if (t->set >= 0) { /* the chip's model, listed again as the page listed it */
			Route d;
			default_route(a, f, &d);
			const char *ids[16];
			char labels[16][80];
			int nm = model_options(a, f, r.p, d.model, ids, labels);
			if (t->set >= nm) return 1;
			id = ids[t->set];
		}
		if (id[0] && !a->feat_service[f][0]) adv_set(a, f, "", r.p->id); /* a model belongs to its service */
		adv_set(a, f, "_model", id);
		adv_status(a, f);
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_ADV_EFFORT: {
		if (f < 0 || f >= FEAT_COUNT) return 1;
		Route r;
		brain_route(a, f, &r);
		if (r.p && r.p->api == API_BEDROCK) { ui_set_status(a, "Bedrock doesn't take a thinking level"); return 1; }
		adv_set(a, f, "_effort", EFFORTS[t->set]);
		adv_status(a, f);
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_ADV_TEMP:
		if (f < 0 || f >= FEAT_COUNT) return 1;
		adv_set(a, f, "_temperature", TEMPS[t->set]);
		adv_status(a, f);
		draw(a, &a->gc16);
		return 1;
	case HIT_ADV_MAXTOK: {
		if (f < 0 || f >= FEAT_COUNT) return 1;
		char v[16] = "";
		if (MAXT[t->set]) snprintf(v, sizeof v, "%d", MAXT[t->set]);
		adv_set(a, f, "_max_tokens", v);
		adv_status(a, f);
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_SRV_FIELD: {
		char k[48], kk[56];
		if (t->set == 0) {
			slot_key(adv_slot, "url", k, sizeof k);
			kb_open(a, "Server address", "Base address of its API, e.g. http://10.0.0.20:11434/v1. http:// isn't encrypted.", k,
				cs->url[0] ? cs->url : "http://", 0, srv_field_done);
		} else if (t->set == 1) {
			snprintf(kk, sizeof kk, "key:%s", sp->key_name);
			kb_open(a, "Server key", "Only if the server asks for one. Kept in protected storage.", kk, "", 1, srv_field_done);
		} else {
			slot_key(adv_slot, "model", k, sizeof k);
			kb_open(a, "Model", "Exactly as the server names it, e.g. qwen3-vl:8b. It must read images.", k, cs->model, 0,
				srv_field_done);
		}
		return 1;
	}
	case HIT_SRV_API:
		srv_set(a, "api", APIS[t->set]);
		ui_set_status(a, "kind of API: %s", API_LABEL[t->set]);
		draw(a, &a->gc16);
		return 1;
	case HIT_SRV_JSON:
		srv_set(a, "json", JSONS[t->set]);
		ui_set_status(a, "reply format: %s", JSON_LABEL[t->set]);
		draw(a, &a->gc16);
		return 1;
	case HIT_SRV_TEST:
		if (!cs->url[0]) { ui_set_status(a, "enter the server's address first"); return 1; }
		app_key_check(a, sp->id);
		return 1;
	case HIT_SRV_PICK: {
		const char **sm;
		int ns = brain_server_models(adv_slot, &sm);
		if (t->set >= ns) return 1;
		char id[64];
		snprintf(id, sizeof id, "%s", sm[t->set]);
		srv_set(a, "model", id);
		ui_set_status(a, "model: %s - it must read images to read your page", id);
		draw(a, &a->gc16);
		return 1;
	}
	case HIT_SRV_WAIT: {
		char v[16];
		snprintf(v, sizeof v, "%d", WAITS[t->set]);
		srv_set(a, "timeout", v);
		ui_set_status(a, "waits up to %s for an answer", WAIT_LABEL[t->set]);
		draw(a, &a->gc16);
		return 1;
	}
	}
	return 0;
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
		const char *title = CATS[a->settings_page];
		if (a->settings_page == CAT_AI && adv_page >= 0)
			title = adv_page == ADV_ALL ? "Every feature" : adv_page != ADV_SERVER ? feature_at(adv_page)->label
				: provider_custom_slot(adv_slot)->name;
		text_draw(a, title, LEFT + bw + 40, TOP + 12, a->W - LEFT * 2 - bw - 40, 80, 60, TXT_BOLD);
		int y = TOP + 150;
		if (a->settings_page == CAT_AI) y = draw_adv(a, y); /* the two tabs, or one of their pages */
		for (int r = 0; r < NSET; r++)
			if (SET[r].cat == a->settings_page && visible(a, &SET[r])) y += SET[r].n == -1 ? draw_field(a, r, y) : draw_setting(a, r, y);
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, how);
}

void settings_open(App *a) {
	a->in_settings = 1;
	a->settings_page = -1;
	adv_page = adv_armed = adv_return = -1;
	adv_slot = adv_tab = ai_tab = 0;
	a->autosend_at = 0;
	draw(a, &a->flash);
	ui_set_status(a, "settings - tap Close to go back to your page");
}

/* Flick: right = back to the list; left does nothing on a section page. */
/* A flick on a section: left = the next section, right = back to the list of sections. */
void settings_flip(App *a, int dir) {
	if (kb_active()) return;
	if (dir < 0 && a->settings_page == CAT_AI && adv_page == ADV_SERVER && adv_return >= 0) { /* to its feature */
		adv_page = adv_return;
		adv_return = -1;
		draw(a, &a->flash);
	} else if (dir < 0 && a->settings_page == CAT_AI && adv_page >= 0) { adv_page = -1; draw(a, &a->flash); } /* to the tabs */
	else if (dir < 0 && a->settings_page >= 0) { a->settings_page = -1; draw(a, &a->flash); }
	else if (dir > 0 && a->settings_page < NCATS - 1) { a->settings_page++; draw(a, &a->flash); }
}

void settings_refresh(App *a) {
	if (a->in_settings && !kb_active()) draw(a, &a->gc16);
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
		if (t->kind != HIT_ADV_RESET && t->kind != HIT_ADV_RESET_ALL) adv_armed = -1; /* any other tap disarms */
		if (t->kind >= HIT_ADV_OK) { adv_tap(a, t); return; }
		if (t->kind == HIT_PROV || t->kind == HIT_MODEL || t->kind == HIT_REGION || t->kind == HIT_AIKEY || t->kind == HIT_TEST) {
			const Provider *p = t->kind == HIT_PROV ? provider_at(t->set) : ai_shown(a);
			if (!p) return;
			if (t->kind == HIT_PROV) {
				snprintf(ai_focus, sizeof ai_focus, "%s", p->id);
				if (brain_usable(p)) {
					brain_config_apply(a, "provider", p->id);
					config_set("provider", p->id);
					if (p->custom) ui_set_status(a, "%s answers now - its speed and accuracy are up to it", p->short_name);
					else ui_set_status(a, "%s answers now", p->short_name);
					logf_("setting provider = %s", p->id);
				} else if (p->custom) ui_set_status(a, "set up your own server first (button below)");
				else ui_set_status(a, "add a %s key below to use it", p->short_name);
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
		if (t->kind == HIT_CAT) { a->settings_page = t->set; adv_page = -1; draw(a, &a->flash); return; }
		if (t->kind == HIT_BACK) {
			if (a->settings_page == CAT_AI && adv_page == ADV_SERVER && adv_return >= 0) { adv_page = adv_return; adv_return = -1; }
			else if (a->settings_page == CAT_AI && adv_page >= 0) adv_page = -1; /* a page of the AI tabs: back to them */
			else a->settings_page = -1;
			draw(a, &a->flash);
			return;
		}
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

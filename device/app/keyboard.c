/* On-screen keyboard (inside Settings): for API keys, the email address and the app password. Big keys for pen or
 * finger; secrets are shown while typing (you can hide them) and never written to the log. */
#include <stdio.h>
#include <string.h>

#include "scribe.h"

#define KW 160 /* key size and spacing */
#define KH 138
#define KG 12
#define TOPY (BAR_H + 40)

enum { K_CHAR, K_SHIFT, K_BACK, K_SPACE, K_CANCEL, K_SAVE, K_SHOW, K_CLEAR };
typedef struct { int x, y, w, h, kind; char c; } Key;

static struct {
	int active, secret, show, shift, caps;
	char title[80], hint[240], key[48], text[320];
	void (*done)(App *a, const char *key, const char *text);
} kb;
static Key keys[80];
static int nkeys;
static int field_y, field_h;

int kb_active(void) { return kb.active; }

static const char *ROWS[5] = { "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm", "_.@+/=:~" };

static void layout(App *a) {
	nkeys = 0;
	int x0 = (a->W - (10 * KW + 9 * KG)) / 2, y = field_y + field_h + 60;
	for (int r = 0; r < 5; r++, y += KH + KG) {
		const char *row = ROWS[r];
		int n = (int)strlen(row), x = x0;
		if (r == 3) { keys[nkeys++] = (Key){ x, y, KW + KW / 2, KH, K_SHIFT, 0 }; x += KW + KW / 2 + KG; }
		for (int i = 0; i < n; i++, x += KW + KG) keys[nkeys++] = (Key){ x, y, KW, KH, K_CHAR, row[i] };
		if (r == 3) keys[nkeys++] = (Key){ x, y, x0 + 10 * KW + 9 * KG - x, KH, K_BACK, 0 };
		if (r == 4) keys[nkeys++] = (Key){ x, y, x0 + 10 * KW + 9 * KG - x, KH, K_SPACE, 0 };
	}
	int bw = (10 * KW + 9 * KG - 3 * KG) / 4, x = x0;
	keys[nkeys++] = (Key){ x, y + 20, bw, KH, K_CANCEL, 0 }; x += bw + KG;
	keys[nkeys++] = (Key){ x, y + 20, bw, KH, K_CLEAR, 0 }; x += bw + KG;
	keys[nkeys++] = (Key){ x, y + 20, bw, KH, K_SHOW, 0 }; x += bw + KG;
	keys[nkeys++] = (Key){ x, y + 20, bw, KH, K_SAVE, 0 };
}

static void key_label(const Key *k, char *b, size_t n) {
	switch (k->kind) {
	case K_CHAR: snprintf(b, n, "%c", (kb.shift || kb.caps) && k->c >= 'a' && k->c <= 'z' ? k->c - 32 : k->c); break;
	case K_SHIFT: snprintf(b, n, "%s", kb.caps ? "CAPS" : "Shift"); break;
	case K_BACK: snprintf(b, n, "Delete"); break;
	case K_SPACE: snprintf(b, n, "space"); break;
	case K_CANCEL: snprintf(b, n, "Cancel"); break;
	case K_CLEAR: snprintf(b, n, "Clear"); break;
	case K_SHOW: snprintf(b, n, "%s", kb.show ? "Hide" : "Show"); break;
	default: snprintf(b, n, "Save"); break;
	}
}

static void draw_key(App *a, const Key *k, int dark) {
	int on = dark || (k->kind == K_SHIFT && (kb.shift || kb.caps)) || k->kind == K_SAVE;
	ui_rrect(a, k->x, k->y, k->w, k->h, 22, on ? 0x2A : 0xE6);
	char l[16];
	key_label(k, l, sizeof l);
	text_draw(a, l, k->x, k->y, k->w, k->h, k->kind == K_CHAR ? 52 : 38, TXT_BOLD | 0x400 | (on ? 0x800 : 0));
}

static void draw_field(App *a, int refresh) {
	int x = 70, w = a->W - 140;
	ui_rrect(a, x, field_y, w, field_h, 26, 0x99);
	ui_rrect(a, x + 4, field_y + 4, w - 8, field_h - 8, 23, 0xFF);
	char shown[340];
	size_t n = strlen(kb.text);
	if (kb.secret && !kb.show) { size_t i; for (i = 0; i < n && i < sizeof shown - 2; i++) shown[i] = '*'; shown[i] = 0; }
	else snprintf(shown, sizeof shown, "%s", kb.text);
	strncat(shown, "|", sizeof shown - strlen(shown) - 1); /* the cursor */
	text_draw(a, shown, x + 30, field_y + 24, w - 60, field_h - 40, 40, TXT_REGULAR);
	char cnt[32];
	snprintf(cnt, sizeof cnt, "%zu", n);
	text_draw(a, cnt, x + w - 160, field_y + field_h + 6, 140, 40, 28, TXT_ITALIC);
	if (refresh) disp_refresh(&a->disp, (uint32_t)field_y, (uint32_t)x, (uint32_t)w, (uint32_t)(field_h + 50), &a->gl16);
}

void kb_draw(App *a) {
	DispRect rc = { a->cx, a->cy, a->cw, a->ch };
	disp_fill_rect(&a->disp, &rc, 0xFF);
	hits_reset();
	text_draw(a, kb.title, 70, TOPY, a->W - 140, 80, 56, TXT_BOLD);
	int y = TOPY + 90;
	if (kb.hint[0]) { text_draw(a, kb.hint, 70, y, a->W - 140, 90, 30, TXT_ITALIC); y += 100; }
	field_y = y + 10;
	field_h = 260;
	layout(a);
	draw_field(a, 0);
	for (int i = 0; i < nkeys; i++) {
		draw_key(a, &keys[i], 0);
		char hl[24], l[16];
		key_label(&keys[i], l, sizeof l);
		snprintf(hl, sizeof hl, "kb:%s", keys[i].kind == K_CHAR ? (char[2]){ keys[i].c, 0 } : l);
		hits_note(hl, keys[i].x, keys[i].y, keys[i].w, keys[i].h);
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, &a->gc16);
}

void kb_open(App *a, const char *title, const char *hint, const char *key, const char *initial, int secret,
	     void (*done)(App *a, const char *key, const char *text)) {
	memset(&kb, 0, sizeof kb);
	kb.active = 1;
	kb.secret = secret;
	kb.show = 1; /* see what you type; Hide is one tap away */
	snprintf(kb.title, sizeof kb.title, "%s", title);
	snprintf(kb.hint, sizeof kb.hint, "%s", hint ? hint : "");
	snprintf(kb.key, sizeof kb.key, "%s", key);
	snprintf(kb.text, sizeof kb.text, "%s", initial ? initial : "");
	kb.done = done;
	kb_draw(a);
	ui_set_status(a, "type, then Save");
}

static void finish(App *a, int save) {
	char text[320], key[48];
	snprintf(text, sizeof text, "%s", kb.text);
	snprintf(key, sizeof key, "%s", kb.key);
	void (*done)(App *, const char *, const char *) = kb.done;
	memset(&kb, 0, sizeof kb); /* the typed secret doesn't linger */
	if (done) done(a, key, save ? text : NULL);
	memset(text, 0, sizeof text);
}

void kb_cancel(App *a) { if (kb.active) finish(a, 0); }

void kb_tap(App *a, int x, int y) {
	for (int i = 0; i < nkeys; i++) {
		Key *k = &keys[i];
		if (x < k->x - KG / 2 || x >= k->x + k->w + KG / 2 || y < k->y - KG / 2 || y >= k->y + k->h + KG / 2) continue;
		size_t n = strlen(kb.text);
		int relabel = 0;
		switch (k->kind) {
		case K_CHAR:
			if (n + 1 < sizeof kb.text) {
				kb.text[n] = (kb.shift || kb.caps) && k->c >= 'a' && k->c <= 'z' ? (char)(k->c - 32) : k->c;
				kb.text[n + 1] = 0;
			}
			if (kb.shift) { kb.shift = 0; relabel = 1; }
			break;
		case K_SHIFT:
			if (kb.caps) kb.caps = 0;
			else if (kb.shift) { kb.shift = 0; kb.caps = 1; } /* twice: caps lock */
			else kb.shift = 1;
			relabel = 1;
			break;
		case K_BACK: if (n) kb.text[n - 1] = 0; break;
		case K_SPACE: if (n + 1 < sizeof kb.text) { kb.text[n] = ' '; kb.text[n + 1] = 0; } break;
		case K_CLEAR: kb.text[0] = 0; break;
		case K_SHOW: kb.show = !kb.show; relabel = 1; break;
		case K_CANCEL: finish(a, 0); return;
		case K_SAVE: finish(a, 1); return;
		}
		if (relabel) { /* letters change case, or the Show/Hide label */
			for (int j = 0; j < nkeys; j++) draw_key(a, &keys[j], 0);
			int top = keys[0].y, bot = keys[nkeys - 1].y + KH;
			disp_refresh(&a->disp, (uint32_t)top, 0, (uint32_t)a->W, (uint32_t)(bot - top), &a->gl16);
		} else { /* quick feedback on the key */
			draw_key(a, k, 1);
			disp_refresh(&a->disp, (uint32_t)k->y, (uint32_t)k->x, (uint32_t)k->w, (uint32_t)k->h, &a->fast);
			draw_key(a, k, 0);
			disp_refresh(&a->disp, (uint32_t)k->y, (uint32_t)k->x, (uint32_t)k->w, (uint32_t)k->h, &a->gl16);
		}
		draw_field(a, 1);
		return;
	}
}

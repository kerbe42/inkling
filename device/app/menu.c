/* The Menu panel (the Menu button): big tiles for Page options, Settings and Exit. Modes, Translate included, are
 * picked from the mode button on the bar. */
#include <stdio.h>
#include <string.h>

#include "scribe.h"

enum { MN_PAGE, MN_SETTINGS, MN_EXIT, MN_CLOSE, MN_COUNT };
static const char *TITLE[MN_COUNT] = { "Page", "Settings", "Exit", "Close menu" };
static const char *SUB[MN_COUNT] = { "New page, clear or delete this page, forget the conversation, start over",
				     "Voices, handwriting, fonts, translation, AI model",
				     "Back to the Kindle home screen (your page is saved)", "Back to your page" };

typedef struct {
	int x, y, w, h, id;
} MHit;
static MHit mh[MN_COUNT];

static void fill(App *a, int x, int y, int w, int h, uint8_t g) {
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, g);
}

void menu_open(App *a) {
	page_save(a);
	a->in_menu = 1;
	a->autosend_at = 0;
	fill(a, a->cx, a->cy, a->cw, a->ch, 0xFF);
	hits_reset();
	int L = 70, gap = 30, tw = (a->W - 2 * L - gap) / 2, th = 360, y0 = a->cy + 60;
	for (int i = 0; i < MN_COUNT; i++) {
		int x = L + (i % 2) * (tw + gap), y = y0 + (i / 2) * (th + gap), w = tw, h = th;
		ui_rrect(a, x, y, w, h, 44, 0xB8);             /* soft rounded tiles */
		ui_rrect(a, x + 4, y + 4, w - 8, h - 8, 40, i == MN_CLOSE ? 0xEE : 0xFF);
		text_draw(a, TITLE[i], x + 50, y + 70, w - 100, 90, 72, TXT_BOLD);
		text_draw(a, SUB[i], x + 50, y + 180, w - 100, 140, 36, TXT_REGULAR);
		mh[i] = (MHit){ x, y, w, h, i };
		hits_note(TITLE[i], x, y, w, h);
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, &a->flash);
	ui_set_status(a, "menu - tap a tile, or Close to go back to your page");
}

void menu_close(App *a) {
	a->in_menu = 0;
	page_load(a, a->page);
	ui_set_status(a, "page %d of %d", a->page, a->last_page);
}

/* Returns 1 if a tile was hit. The actions reuse the same code paths as before the redesign. */
int menu_tap(App *a, int x, int y) {
	for (int i = 0; i < MN_COUNT; i++) {
		MHit *h = &mh[i];
		if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
		switch (h->id) {
		case MN_PAGE: app_action(a, 2); break;
		case MN_SETTINGS: app_action(a, 4); break;
		case MN_EXIT: app_action(a, 3); return 1; /* stays "in the menu": the exit must not save the menu as the page */
		default: menu_close(a); break;
		}
		a->in_menu = 0;
		ui_layout(a);
		ui_draw_bar(a);
		return 1;
	}
	return 0;
}

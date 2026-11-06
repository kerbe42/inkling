/* Page menu (the Page button): new page, clear or delete this page, forget the conversation, start over.
 * Destructive actions ask for confirmation. The text record (notebooks/<name>.md) is archived, never deleted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "scribe.h"

enum { PM_NEW, PM_CLEAR, PM_DELETE, PM_FORGET, PM_RESTART, PM_BACK, PM_COUNT };
static const char *TITLES[PM_COUNT] = { "New page", "Clear this page", "Delete this page", "Forget the conversation",
					"Start over", "Back to the page" };

typedef struct {
	int x, y, w, h, id;
} PHit;
static PHit ph[16];
static int nph, confirm_id = -1;

static void fill(App *a, int x, int y, int w, int h, uint8_t g) {
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, g);
}

static void button(App *a, int x, int y, int w, int h, const char *text, int dark, int id) {
	fill(a, x, y, w, h, 0x00);
	if (!dark) fill(a, x + 5, y + 5, w - 10, h - 10, 0xFF);
	text_draw(a, text, x, y, w, h, 46, TXT_BOLD | 0x400 | (dark ? 0x800 : 0));
	ph[nph++] = (PHit){ x, y, w, h, id };
	hits_note(text, x, y, w, h);
}

static void subtitle(App *a, int id, char *out, size_t n) {
	switch (id) {
	case PM_NEW: snprintf(out, n, "A blank page after page %d", a->last_page); break;
	case PM_CLEAR: snprintf(out, n, "Wipe page %d. The conversation is kept.", a->page); break;
	case PM_DELETE: snprintf(out, n, "Remove page %d and close the gap.", a->page); break;
	case PM_FORGET: snprintf(out, n, "The AI forgets everything written so far (who you are, what you said). Pages stay."); break;
	case PM_RESTART: snprintf(out, n, "Delete all %d pages and forget the conversation. The text record is kept.", a->last_page); break;
	default: snprintf(out, n, "Nothing changes."); break;
	}
}

static void draw(App *a) {
	nph = 0;
	hits_reset();
	int L = 70, W = a->W - 2 * L;
	fill(a, a->cx, a->cy, a->cw, a->ch, 0xFF);
	if (confirm_id < 0) {
		char t[64];
		snprintf(t, sizeof t, "Page %d of %d", a->page, a->last_page);
		text_draw(a, t, L, a->cy + 40, W, 84, 64, TXT_BOLD);
		int y = a->cy + 170, rowh = 250;
		for (int id = 0; id < PM_COUNT; id++) {
			char sub[200];
			subtitle(a, id, sub, sizeof sub);
			fill(a, L, y, W, 3, 0x00);
			text_draw(a, TITLES[id], L + 10, y + 44, W - 20, 66, 50, TXT_BOLD);
			text_draw(a, sub, L + 10, y + 124, W - 20, 90, 34, TXT_REGULAR);
			ph[nph++] = (PHit){ L, y, W, rowh, id };
			hits_note(TITLES[id], L, y, W, rowh);
			y += rowh;
		}
		fill(a, L, y, W, 3, 0x00);
	} else {
		char q[160], sub[200];
		if (confirm_id == PM_DELETE) snprintf(q, sizeof q, "Delete page %d?", a->page);
		else if (confirm_id == PM_FORGET) snprintf(q, sizeof q, "Forget the conversation?");
		else snprintf(q, sizeof q, "Start over?");
		subtitle(a, confirm_id, sub, sizeof sub);
		text_draw(a, q, L, a->cy + 420, W, 100, 76, TXT_BOLD | 0x400);
		text_draw(a, sub, L, a->cy + 560, W, 120, 38, TXT_REGULAR | 0x400);
		int bw = 640, gap = 80, x0 = (a->W - 2 * bw - gap) / 2, by = a->cy + 820;
		button(a, x0, by, bw, 170, confirm_id == PM_FORGET ? "Yes, forget" : "Yes, delete", 1, 100);
		button(a, x0 + bw + gap, by, bw, 170, "Cancel", 0, 101);
	}
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, &a->flash);
}

void pagemenu_open(App *a) {
	page_save(a);
	a->in_pagemenu = 1;
	a->autosend_at = 0;
	confirm_id = -1;
	draw(a);
	ui_set_status(a, "page menu - tap an option, or Close to go back");
}

void pagemenu_close(App *a) {
	a->in_pagemenu = 0;
	confirm_id = -1;
	page_load(a, a->page);
}

static void page_file(App *a, int n, char *p, size_t len) { snprintf(p, len, USB_DIR "/notebooks/%s/page-%04d.png", a->notebook, n); }

/* Shared with the Clear drop-down. Each leaves the page shown and the status set. */
void page_delete(App *a) {
	char p[256], q[256];
	page_save_wait();
	pending_clear(a);
	int n = a->page;
	page_file(a, n, p, sizeof p);
	unlink(p);
	for (int k = n + 1; k <= a->last_page; k++) {
		page_file(a, k, p, sizeof p);
		page_file(a, k - 1, q, sizeof q);
		rename(p, q);
	}
	if (a->last_page > 1) a->last_page--;
	if (a->page > a->last_page) a->page = a->last_page;
	page_load(a, a->page);
	state_save(a);
	ui_set_status(a, "page %d deleted - now on page %d of %d", n, a->page, a->last_page);
	logf_("page %d deleted", n);
}

void conversation_forget(App *a) {
	brain_forget(a, 0);
	a->game_prompt = NULL;
	ui_set_status(a, "conversation forgotten - the AI starts fresh (your pages are still here)");
}

void notebook_start_over(App *a) {
	char p[256];
	page_save_wait();
	pending_clear(a);
	for (int k = 1; k <= a->last_page; k++) {
		page_file(a, k, p, sizeof p);
		unlink(p);
	}
	brain_forget(a, 1);
	a->game_prompt = NULL;
	a->page = a->last_page = 1;
	clear_canvas(a);
	state_save(a);
	ui_set_status(a, "a fresh start - page 1, the AI remembers nothing");
	logf_("start over");
}

static void do_action(App *a, int id) {
	page_save_wait(); /* a page save in progress must not write a page back after it's cleared or moved */
	switch (id) {
	case PM_NEW:
		pagemenu_close(a);
		app_request_turn(a, 2);
		return;
	case PM_CLEAR:
		a->in_pagemenu = 0;
		confirm_id = -1;
		page_clear(a);
		ui_set_status(a, "page %d cleared", a->page);
		return;
	case PM_DELETE:
		a->in_pagemenu = 0;
		confirm_id = -1;
		page_delete(a);
		return;
	case PM_FORGET:
		pagemenu_close(a);
		conversation_forget(a);
		return;
	case PM_RESTART:
		a->in_pagemenu = 0;
		confirm_id = -1;
		notebook_start_over(a);
		return;
	default:
		pagemenu_close(a);
		ui_set_status(a, "page %d of %d", a->page, a->last_page);
	}
}

void pagemenu_tap(App *a, int x, int y) {
	for (int i = 0; i < nph; i++) {
		PHit *h = &ph[i];
		if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
		if (h->id == 100) { do_action(a, confirm_id); return; }
		if (h->id == 101) { confirm_id = -1; draw(a); return; }
		if (h->id == PM_DELETE || h->id == PM_FORGET || h->id == PM_RESTART) { confirm_id = h->id; draw(a); return; }
		do_action(a, h->id);
		return;
	}
}

/* The mode picker: a card that drops down from the mode button with Chat, Type up, Math, Translate and Games, each
 * with its icon and one line saying what it does. The page (or game) under the card is kept and put back when it
 * closes. From a game it leads back to the games list, or to the page in another mode. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scribe.h"

#define IC_CHAT 0
#define IC_DOC 1
#define IC_CLEAR 4
#define IC_GAMES 5
#define IC_SEND 6
#define IC_CHECK 10
#define IC_TRASH 11
#define IC_RESTART 12
#define IC_GEAR 13
#define IC_EXIT 14
#define IC_MAIL 15
#define ROW_GAMES -1
#define ROW_H 176

static unsigned char *under;
static int ux, uy, uw, uh;

static void keep_under(App *a) {
	free(under);
	under = malloc((size_t)uw * (size_t)uh);
	if (under)
		for (int y = 0; y < uh; y++)
			memcpy(under + (size_t)y * (size_t)uw, a->fb + (size_t)(a->pan_y + uy + y) * (size_t)a->stride + (size_t)ux, (size_t)uw);
}
static struct { int y, mode; } rows[8];
static int nrows;

static const char *describe(App *a, const char *id, char *buf, size_t n) {
	if (!strcmp(id, "talk")) return "Write anything - it writes back to you, like a diary that answers";
	if (!strcmp(id, "notes")) return "Your handwriting turns into neat typed text";
	if (!strcmp(id, "math")) return "Checks your working step by step";
	if (!strcmp(id, "translate")) { snprintf(buf, n, "Writes back what you wrote in %s", a->translate_to); return buf; }
	return "";
}

void modes_open(App *a) {
	nrows = 0;
	for (int i = 0; i < a->nmodes && nrows < 7; i++)
		if (strcmp(a->mode_id[i], "auto")) rows[nrows++].mode = i;
	rows[nrows++].mode = ROW_GAMES;
	ux = 16; uy = BAR_H - 4; uw = 980; uh = nrows * ROW_H + 40;
	keep_under(a);
	a->in_modes = 1;
	hits_reset();
	ui_rrect(a, ux, uy, uw, uh, 36, 0x99);              /* border */
	ui_rrect(a, ux + 4, uy + 4, uw - 8, uh - 8, 33, 0xFF); /* card */
	for (int k = 0; k < nrows; k++) {
		int m = rows[k].mode, y = uy + 20 + k * ROW_H, games = m == ROW_GAMES;
		int cur = games ? a->in_games != 0 : !a->in_games && m == a->mode;
		rows[k].y = y;
		if (cur) ui_rrect(a, ux + 16, y + 6, uw - 32, ROW_H - 12, 26, 0xE6);
		ui_icon(a, games ? IC_GAMES : ui_mode_icon(a->mode_id[m]), ux + 44, y + (ROW_H - 64) / 2, 64, 0x00);
		char buf[96];
		text_draw(a, games ? "Games" : a->mode_label[m], ux + 140, y + 30, uw - 260, 64, 48, TXT_BOLD);
		text_draw(a, games ? "Board, puzzle and word games, and games with the AI" : describe(a, a->mode_id[m], buf, sizeof buf),
			  ux + 140, y + 100, uw - 230, 50, 31, TXT_REGULAR);
		if (cur) ui_icon(a, IC_CHECK, ux + uw - 100, y + (ROW_H - 56) / 2, 56, 0x00);
		char hl[40];
		snprintf(hl, sizeof hl, "mode:%s", games ? "games" : a->mode_id[m]);
		hits_note(hl, ux, y, uw, ROW_H);
	}
	disp_refresh(&a->disp, (uint32_t)uy, (uint32_t)ux, (uint32_t)uw, (uint32_t)uh, &a->gl16);
	ui_set_status(a, a->in_games ? "back to the games, or pick a mode to go back to your page" : "pick a mode, or Games");
}

void modes_close(App *a) {
	if (!a->in_modes) return;
	a->in_modes = 0;
	if (under) {
		for (int y = 0; y < uh; y++)
			memcpy(a->fb + (size_t)(a->pan_y + uy + y) * (size_t)a->stride + (size_t)ux, under + (size_t)y * (size_t)uw, (size_t)uw);
		free(under);
		under = NULL;
	}
	disp_refresh(&a->disp, (uint32_t)uy, (uint32_t)ux, (uint32_t)uw, (uint32_t)uh, &a->gl16);
	ui_draw_bar(a);
}

/* ---- List drop-downs under Clear (2), Menu (3) and Send (4): rows with an icon, a title and one line. Rows marked
 * confirm (all of Clear's) need a second tap within 4 s; the first turns the row dark and says what the next does. */
typedef struct { int icon, confirm; char id[16], title[48], desc[160], again[64]; } DRow;
static DRow dr[8];
static int ndr, d_armed = -1;
static double d_armed_t;
static const char *D_PREFIX[5] = { "", "mode", "clear", "menu", "send" };

static void add_row(int icon, int confirm, const char *id, const char *title, const char *desc, const char *again) {
	DRow *r = &dr[ndr++];
	r->icon = icon; r->confirm = confirm;
	snprintf(r->id, sizeof r->id, "%s", id);
	snprintf(r->title, sizeof r->title, "%s", title);
	snprintf(r->desc, sizeof r->desc, "%s", desc);
	snprintf(r->again, sizeof r->again, "%s", again ? again : "");
}

static void d_row(App *a, int k, int refresh) {
	int y = uy + 20 + k * ROW_H, dark = k == d_armed;
	ui_rrect(a, ux + 16, y + 6, uw - 32, ROW_H - 12, 26, dark ? 0x2A : 0xFF);
	ui_icon(a, dr[k].icon, ux + 44, y + (ROW_H - 64) / 2, 64, dark ? 0xFF : 0x00);
	text_draw(a, dr[k].title, ux + 140, y + 30, uw - 200, 64, 48, TXT_BOLD | (dark ? 0x800 : 0));
	text_draw(a, dark ? dr[k].again : dr[k].desc, ux + 140, y + 100, uw - 200, 50, 31, TXT_REGULAR | (dark ? 0x800 : 0));
	if (refresh) disp_refresh(&a->disp, (uint32_t)y, (uint32_t)ux, (uint32_t)uw, ROW_H, &a->gl16);
}

static void d_open(App *a, int kind, int right, const char *status) {
	uw = 980; ux = right - uw; if (ux < 16) ux = 16;
	uy = BAR_H - 4; uh = ndr * ROW_H + 40;
	keep_under(a);
	a->in_modes = kind;
	d_armed = -1;
	hits_reset();
	ui_rrect(a, ux, uy, uw, uh, 36, 0x99);
	ui_rrect(a, ux + 4, uy + 4, uw - 8, uh - 8, 33, 0xFF);
	for (int k = 0; k < ndr; k++) {
		d_row(a, k, 0);
		char hl[40];
		snprintf(hl, sizeof hl, "%s:%s", D_PREFIX[kind], dr[k].id);
		hits_note(hl, ux, uy + 20 + k * ROW_H, uw, ROW_H);
	}
	disp_refresh(&a->disp, (uint32_t)uy, (uint32_t)ux, (uint32_t)uw, (uint32_t)uh, &a->gl16);
	ui_set_status(a, "%s", status);
}

void clearpick_open(App *a, int right) {
	char d[160];
	ndr = 0;
	add_row(IC_CLEAR, 1, "page", "Clear this page", "Wipes the writing on this page. The conversation is kept.", "Tap again to clear the page");
	add_row(IC_CHAT, 1, "forget", "Forget the conversation", "The AI forgets what's been said. Your pages stay.", "Tap again to forget");
	add_row(IC_DOC, 1, "both", "Clear page and forget", "A blank page and a fresh conversation.", "Tap again to clear and forget");
	snprintf(d, sizeof d, "Removes page %d and closes the gap.", a->page);
	add_row(IC_TRASH, 1, "delete", "Delete this page", d, "Tap again to delete it");
	snprintf(d, sizeof d, "Deletes all %d page%s and forgets everything (the text record is kept).", a->last_page, a->last_page == 1 ? "" : "s");
	add_row(IC_RESTART, 1, "startover", "Start over", d, "Tap again to start over");
	d_open(a, 2, right, "clear - tap an option twice");
}

void menupick_open(App *a, int right) {
	char d[160];
	ndr = 0;
	snprintf(d, sizeof d, "A blank page after page %d; the conversation goes on.", a->last_page);
	add_row(IC_DOC, 0, "newpage", "New page", d, NULL);
	add_row(IC_GEAR, 0, "settings", "Settings", "Voices, handwriting, fonts, translation, AI model", NULL);
	add_row(IC_RESTART, 0, "update", "Check for updates", update_row_desc(a), NULL);
	add_row(IC_EXIT, 0, "exit", "Exit", "Back to the Kindle home screen (your page is saved)", NULL);
	d_open(a, 3, right, "menu");
}

void sendpick_open(App *a, int right) {
	char d[200];
	ndr = 0;
	add_row(IC_SEND, 0, "now", "Send now", "Answer what's on the page right away (or read the page again)", NULL);
	if (!brain_email_missing(a)) snprintf(d, sizeof d, "Typed up and sent to %s, with a picture of the page", brain_email_to(a));
	else snprintf(d, sizeof d, "Set up first: Menu > Settings > Email (your account and an app password)");
	add_row(IC_MAIL, 0, "email", "Email this page", d, NULL);
	d_open(a, 4, right, "send");
}

/* Called from the main loop: an armed row goes back to normal after 4 s. */
void clearpick_tick(App *a, double now) {
	if (a->in_modes < 2 || d_armed < 0 || now - d_armed_t < 4000) return;
	int k = d_armed;
	d_armed = -1;
	d_row(a, k, 1);
	ui_set_status(a, "clear - tap an option twice");
}

int modes_tap(App *a, int x, int y) {
	if (x < ux || x >= ux + uw || y < uy || y >= uy + uh) return 0;
	if (a->in_modes >= 2) {
		for (int k = 0; k < ndr; k++) {
			int ry = uy + 20 + k * ROW_H;
			if (y < ry || y >= ry + ROW_H) continue;
			int kind = a->in_modes;
			if (dr[k].confirm && !(k == d_armed && mono_ms() - d_armed_t < 4000)) {
				int old = d_armed;
				d_armed = k;
				d_armed_t = mono_ms();
				if (old >= 0) d_row(a, old, 1);
				d_row(a, k, 1);
				ui_set_status(a, "%s - tap it again to confirm", dr[k].title);
				return 1;
			}
			char id[16];
			snprintf(id, sizeof id, "%s", dr[k].id);
			modes_close(a); /* the page is the page again before anything happens */
			app_dropdown_action(a, kind, id);
			return 1;
		}
		return 1;
	}
	for (int k = 0; k < nrows; k++)
		if (y >= rows[k].y && y < rows[k].y + ROW_H) {
			modes_close(a);
			if (rows[k].mode == ROW_GAMES) app_open_games(a);
			else app_set_mode(a, rows[k].mode);
			return 1;
		}
	return 1; /* on the card but between rows: keep it open */
}

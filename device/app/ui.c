/* Top bar: the mode button (a drop-down: Chat, Type up, Math, Translate), Clear, Games, Send, Menu, and a status
 * line. Soft rounded buttons with small drawn icons; the active or pressed one turns dark. */
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "scribe.h"

#define BTN_Y 18
#define BTN_H 96
#define RADIUS 30
#define STATUS_Y 122
#define STATUS_H 42

static void add_button(App *a, const char *text, int x, int w, int kind, int arg) {
	Button *b = &a->buttons[a->nbuttons++];
	snprintf(b->text, sizeof b->text, "%s", text);
	b->x = x; b->y = BTN_Y; b->w = w; b->h = BTN_H;
	b->kind = kind; b->arg = arg;
}

void ui_layout(App *a) {
	a->nbuttons = 0;
	int sub = a->in_menu || a->in_settings || a->in_games || a->in_pagemenu;
	add_button(a, "Mode", 16, 540, 9, 0); /* shows the current mode (or Games); opens the picker */
	int r = a->W - 16, gap = 18;
	add_button(a, sub ? "Close" : "Menu", r - 236, 236, 6, 0);
	r -= 236 + gap;
	add_button(a, "Send", r - 226, 226, 1, 0);
	r -= 226 + gap;
	add_button(a, "Clear", r - 226, 226, 8, 0); /* wipe the page (tap twice); the pen's eraser end and scratch-out rub out */
}

/* A filled rounded rectangle, straight into the framebuffer (no refresh). */
void ui_rrect(App *a, int x, int y, int w, int h, int r, uint8_t g) {
	if (r * 2 > h) r = h / 2;
	for (int yy = 0; yy < h; yy++) {
		int inset = 0, d = yy < r ? r - yy : yy >= h - r ? yy - (h - r - 1) : 0;
		if (d > 0) {
			int k = r - d;
			int t = 0;
			while ((t + 1) * (t + 1) <= r * r - (r - k) * (r - k)) t++; /* integer sqrt */
			inset = r - t;
		}
		if (w - 2 * inset <= 0) continue;
		memset(a->fb + (size_t)(a->pan_y + y + yy) * (size_t)a->stride + (size_t)(x + inset), g, (size_t)(w - 2 * inset));
	}
}

/* Small line icons in an s x s box (stroke radius 2). */
enum { IC_CHAT, IC_DOC, IC_MATH, IC_GLOBE, IC_CLEAR, IC_GAMES, IC_SEND, IC_MENU, IC_CLOSE, IC_CHEVRON, IC_CHECK, IC_TRASH, IC_RESTART, IC_GEAR, IC_EXIT, IC_MAIL };
/* Icons draw straight into the framebuffer: stamp() is clipped to the page and counts as page ink. */
static void dot(App *a, int x, int y, int r, uint8_t g) {
	for (int yy = y - r; yy <= y + r; yy++) {
		if (yy < 0 || yy >= a->H) continue;
		int l = x - r < 0 ? 0 : x - r, rr = x + r >= a->W ? a->W - 1 : x + r;
		if (rr >= l) memset(a->fb + (size_t)(a->pan_y + yy) * (size_t)a->stride + (size_t)l, g, (size_t)(rr - l + 1));
	}
}
static void L(App *a, double x0, double y0, double x1, double y1, uint8_t g, Dirty *d) {
	(void)d;
	double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
	int n = (int)len + 1;
	for (int k = 0; k <= n; k++) dot(a, (int)(x0 + dx * k / n + 0.5), (int)(y0 + dy * k / n + 0.5), 2, g);
}
static void ring(App *a, double cx, double cy, double rx, double ry, uint8_t g, Dirty *d) {
	double px = cx + rx, py = cy;
	for (int k = 1; k <= 32; k++) {
		double t = k * 6.2831853 / 32, x = cx + rx * cos(t), y = cy + ry * sin(t);
		L(a, px, py, x, y, g, d);
		px = x; py = y;
	}
}
void ui_icon(App *a, int kind, int x, int y, int s, uint8_t g) {
	Dirty d = { 0, 0, -1, -1 };
	double u = s / 12.0; /* the icons are drawn on a 12 x 12 grid */
#define P(px, py) x + (px) * u, y + (py) * u
	switch (kind) {
	case IC_CHAT: /* speech bubble */
		L(a, P(1, 2), P(11, 2), g, &d); L(a, P(11, 2), P(11, 8), g, &d); L(a, P(11, 8), P(6, 8), g, &d);
		L(a, P(6, 8), P(3, 11), g, &d); L(a, P(3, 11), P(3.5, 8), g, &d); L(a, P(3.5, 8), P(1, 8), g, &d); L(a, P(1, 8), P(1, 2), g, &d);
		break;
	case IC_DOC: /* page with lines */
		L(a, P(2, 1), P(8, 1), g, &d); L(a, P(8, 1), P(10, 3), g, &d); L(a, P(10, 3), P(10, 11), g, &d);
		L(a, P(10, 11), P(2, 11), g, &d); L(a, P(2, 11), P(2, 1), g, &d);
		for (int k = 0; k < 3; k++) L(a, P(4, 5 + 2 * k), P(8, 5 + 2 * k), g, &d);
		break;
	case IC_MATH: /* plus, minus, times, equals */
		L(a, P(3, 1.5), P(3, 5.5), g, &d); L(a, P(1, 3.5), P(5, 3.5), g, &d); L(a, P(7, 3.5), P(11, 3.5), g, &d);
		L(a, P(1.5, 7), P(4.5, 10), g, &d); L(a, P(4.5, 7), P(1.5, 10), g, &d);
		L(a, P(7, 7.5), P(11, 7.5), g, &d); L(a, P(7, 9.5), P(11, 9.5), g, &d);
		break;
	case IC_GLOBE:
		ring(a, P(6, 6), 5 * u, 5 * u, g, &d); ring(a, P(6, 6), 2.2 * u, 5 * u, g, &d); L(a, P(1, 6), P(11, 6), g, &d);
		break;
	case IC_CLEAR: /* eraser */
		L(a, P(1.5, 7.5), P(6.5, 2.5), g, &d); L(a, P(6.5, 2.5), P(10.5, 6.5), g, &d); L(a, P(10.5, 6.5), P(5.5, 11.5), g, &d);
		L(a, P(5.5, 11.5), P(1.5, 7.5), g, &d); L(a, P(4, 5), P(8, 9), g, &d); L(a, P(5.5, 11.5), P(11, 11.5), g, &d);
		break;
	case IC_GAMES: /* die */
		L(a, P(1.5, 1.5), P(10.5, 1.5), g, &d); L(a, P(10.5, 1.5), P(10.5, 10.5), g, &d); L(a, P(10.5, 10.5), P(1.5, 10.5), g, &d);
		L(a, P(1.5, 10.5), P(1.5, 1.5), g, &d);
		dot(a, (int)(x + 4 * u), (int)(y + 4 * u), 3, g); dot(a, (int)(x + 6 * u), (int)(y + 6 * u), 3, g);
		dot(a, (int)(x + 8 * u), (int)(y + 8 * u), 3, g);
		break;
	case IC_SEND: /* paper plane */
		L(a, P(1, 5.5), P(11, 1), g, &d); L(a, P(11, 1), P(7, 11), g, &d); L(a, P(7, 11), P(5, 7), g, &d);
		L(a, P(5, 7), P(1, 5.5), g, &d); L(a, P(5, 7), P(11, 1), g, &d);
		break;
	case IC_MENU:
		for (int k = 0; k < 3; k++) L(a, P(1.5, 3 + 3 * k), P(10.5, 3 + 3 * k), g, &d);
		break;
	case IC_CLOSE:
		L(a, P(2, 2), P(10, 10), g, &d); L(a, P(10, 2), P(2, 10), g, &d);
		break;
	case IC_CHEVRON:
		L(a, P(3, 4.5), P(6, 7.5), g, &d); L(a, P(6, 7.5), P(9, 4.5), g, &d);
		break;
	case IC_CHECK:
		L(a, P(2, 6.5), P(5, 9.5), g, &d); L(a, P(5, 9.5), P(10.5, 2.5), g, &d);
		break;
	case IC_TRASH: /* bin */
		L(a, P(1.5, 3), P(10.5, 3), g, &d); L(a, P(4.5, 3), P(5, 1.2), g, &d); L(a, P(5, 1.2), P(7, 1.2), g, &d);
		L(a, P(7, 1.2), P(7.5, 3), g, &d); L(a, P(2.5, 3), P(3.5, 11), g, &d); L(a, P(3.5, 11), P(8.5, 11), g, &d);
		L(a, P(8.5, 11), P(9.5, 3), g, &d); L(a, P(5, 5), P(5, 9), g, &d); L(a, P(7, 5), P(7, 9), g, &d);
		break;
	case IC_GEAR:
		ring(a, P(6, 6), 3.2 * u, 3.2 * u, g, &d); ring(a, P(6, 6), 1.2 * u, 1.2 * u, g, &d);
		for (int k = 0; k < 8; k++) {
			double t = k * 0.7853982, c = cos(t), sn = sin(t);
			L(a, x + (6 + 3.4 * c) * u, y + (6 + 3.4 * sn) * u, x + (6 + 5.2 * c) * u, y + (6 + 5.2 * sn) * u, g, &d);
		}
		break;
	case IC_EXIT: /* door with an arrow out */
		L(a, P(7, 1.5), P(1.5, 1.5), g, &d); L(a, P(1.5, 1.5), P(1.5, 10.5), g, &d); L(a, P(1.5, 10.5), P(7, 10.5), g, &d);
		L(a, P(4.5, 6), P(11, 6), g, &d); L(a, P(11, 6), P(8.5, 3.5), g, &d); L(a, P(11, 6), P(8.5, 8.5), g, &d);
		break;
	case IC_MAIL: /* envelope */
		L(a, P(1, 2.5), P(11, 2.5), g, &d); L(a, P(11, 2.5), P(11, 9.5), g, &d); L(a, P(11, 9.5), P(1, 9.5), g, &d);
		L(a, P(1, 9.5), P(1, 2.5), g, &d); L(a, P(1, 2.5), P(6, 6.5), g, &d); L(a, P(6, 6.5), P(11, 2.5), g, &d);
		break;
	case IC_RESTART: { /* circular arrow */
		double px = x + 6 * u + 4.5 * u * cos(-0.9), py = y + 6 * u + 4.5 * u * sin(-0.9);
		for (int k = 1; k <= 24; k++) {
			double t = -0.9 + k * 5.2 / 24, nx = x + 6 * u + 4.5 * u * cos(t), ny = y + 6 * u + 4.5 * u * sin(t);
			L(a, px, py, nx, ny, g, &d);
			px = nx; py = ny;
		}
		double ex = x + 6 * u + 4.5 * u * cos(-0.9), ey = y + 6 * u + 4.5 * u * sin(-0.9);
		L(a, ex, ey, ex - 3 * u, ey - 0.3 * u, g, &d); L(a, ex, ey, ex + 0.6 * u, ey + 2.8 * u, g, &d);
		break;
	}
	}
#undef P
}

int ui_mode_icon(const char *mode_id) {
	return !strcmp(mode_id, "notes") ? IC_DOC : !strcmp(mode_id, "math") ? IC_MATH : !strcmp(mode_id, "translate") ? IC_GLOBE : IC_CHAT;
}

static void text_in_box(App *a, const char *s, int x, int y, int w, int h, int px, int bold, int centered, int inverted) {
	DispText t = { .left = x, .top = y + (h - px) / 2 - px / 8, .right = x + w, .bottom = y + h, .px = px,
		       .style = bold ? TXT_BOLD : TXT_REGULAR, .centered = centered, .inverted = inverted };
	disp_text(&a->disp, &a->font_ui, s, &t, NULL);
}

/* Height in px of s wrapped to width w (style: font style in the low byte, 0x100 = Markdown-like bold/italic). */
int text_measure(App *a, const char *s, int w, int px, int style, int *lines) {
	DispText t = { .left = 0, .top = 0, .right = w, .bottom = a->H, .px = px, .style = style & 0xFF,
		       .formatted = (style & 0x100) != 0, .measure_only = true };
	DispTextFit fit;
	disp_text(NULL, font_for(a, s, style), s, &t, &fit);
	if (lines) *lines = fit.fitted; /* (lines that fit on a screen) */
	int line_h = px * 13 / 10;
	int h = fit.fitted * line_h;
	if (fit.height > h) h = fit.height;
	return h > 0 ? h : line_h;
}

/* Text at (x, y) wrapped to w; lines may run px below the box (h) before they are dropped. style: font style in the
 * low byte, 0x100 formatted (**bold**, *italic*), 0x200 reply fonts (font_for), 0x400 centred in the box (vertically
 * too), 0x800 white text (on a dark button). */
void text_draw(App *a, const char *s, int x, int y, int w, int h, int px, int style) {
	DispText t = { .left = x, .top = y, .right = x + w, .bottom = y + h + px < a->H ? y + h + px : a->H, .px = px,
		       .style = style & 0xFF, .formatted = (style & 0x100) != 0, .inverted = (style & 0x800) != 0 };
	if (style & 0x400) {
		t.centered = true;
		t.top = y + (h - px) / 2 - px / 8;
	}
	disp_text(&a->disp, font_for(a, s, style), s, &t, NULL);
}

static void fill(App *a, int x, int y, int w, int h, uint8_t g) {
	DispRect rc = { x, y, w, h };
	disp_fill_rect(&a->disp, &rc, g);
}

static void draw_button_as(App *a, const Button *b, int dark) {
	fill(a, b->x - 2, b->y - 2, b->w + 4, b->h + 4, 0xFF);
	ui_rrect(a, b->x, b->y, b->w, b->h, RADIUS, dark ? 0x2A : 0xE6);
	uint8_t ink = dark ? 0xFF : 0x00;
	int is = 44, iy = b->y + (b->h - is) / 2;
	const char *label = b->text;
	int icon = b->kind == 1 ? IC_SEND : b->kind == 5 ? IC_GAMES : b->kind == 8 ? IC_CLEAR : b->kind == 6 ? (strcmp(b->text, "Close") ? IC_MENU : IC_CLOSE) : -1;
	if (b->kind == 9) {
		label = a->in_games ? "Games" : a->mode_label[a->mode];
		icon = a->in_games ? IC_GAMES : ui_mode_icon(a->mode_id[a->mode]);
	}
	int tx = b->x + 30;
	if (icon >= 0) { ui_icon(a, icon, tx, iy, is, ink); tx += is + 22; }
	int tw = b->x + b->w - tx - (b->kind == 9 ? 70 : 18);
	text_in_box(a, label, tx, b->y, tw, b->h, 40, 1, 0, dark);
	if (b->kind == 9) ui_icon(a, IC_CHEVRON, b->x + b->w - 30 - 40, b->y + (b->h - 40) / 2, 40, ink);
}

static void draw_button(App *a, const Button *b) {
	int on = (b->kind == 9 && a->in_modes == 1) || (b->kind == 8 && a->in_modes == 2) || (b->kind == 6 && a->in_modes == 3) ||
		 (b->kind == 1 && a->in_modes == 4);
	draw_button_as(a, b, on);
}

static void draw_status_nr(App *a) {
	fill(a, 0, STATUS_Y, a->W, STATUS_H, 0xFF);
	/* Black, as it has always come out on the Kindle (a gray such as 0x44 would be softer, but this line refreshes with
	 * DU, which shows only black and white). */
	DispText t = { .left = 30, .top = STATUS_Y + (STATUS_H - 30) / 2 - 3, .right = a->W - 30, .bottom = STATUS_Y + STATUS_H,
		       .px = 30, .style = TXT_ITALIC, .fg = 0x00 };
	disp_text(&a->disp, &a->font_ui, a->status, &t, NULL);
}

int ui_redraws; /* counts bar redraws, so a tap knows whether its action already redrew the button */

void ui_draw_bar(App *a) {
	ui_redraws++;
	fill(a, 0, 0, a->W, BAR_H, 0xFF);
	for (int i = 0; i < a->nbuttons; i++) draw_button(a, &a->buttons[i]);
	draw_status_nr(a);
	fill(a, 0, BAR_H - 3, a->W, 2, 0xB0); /* a light rule, not a black line */
	disp_refresh(&a->disp, 0, 0, (uint32_t)a->W, BAR_H, &a->gl16); /* the soft grays need GL16 (DU is black/white) */
}

/* Redraws one button and refreshes just its box (fast waveform, no flash). */
void ui_draw_button(App *a, int i) {
	if (i < 0 || i >= a->nbuttons) return;
	ui_redraws++;
	Button *b = &a->buttons[i];
	draw_button(a, b);
	disp_refresh(&a->disp, (uint32_t)(b->y - 2), (uint32_t)(b->x - 2), (uint32_t)(b->w + 4), (uint32_t)(b->h + 4), &a->gl16); /* grays */
}

/* Instant touch feedback: invert the button's pixels and show it with the fastest waveform. */
/* Instant touch feedback: the button turns dark, shown with the fastest waveform. */
void ui_press(App *a, int i) {
	if (i < 0 || i >= a->nbuttons) return;
	Button *b = &a->buttons[i];
	draw_button_as(a, b, 1);
	disp_refresh(&a->disp, (uint32_t)(b->y - 2), (uint32_t)(b->x - 2), (uint32_t)(b->w + 4), (uint32_t)(b->h + 4), &a->fast);
}

void ui_draw_status(App *a) {
	draw_status_nr(a);
	disp_refresh(&a->disp, STATUS_Y, 0, (uint32_t)a->W, STATUS_H, &a->fast);
}

void ui_set_status(App *a, const char *fmt, ...) {
	char buf[sizeof a->status];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (!strcmp(buf, a->status)) return;
	snprintf(a->status, sizeof a->status, "%s", buf);
	logf_("status: %s", buf);
	ui_draw_status(a);
}

/* Developer hook (devdump): every tappable region on screen, with a label, for the end-to-end tests. Panels note
 * their regions as they draw them; the bar's come from the button list. */
#define MAXHITS 220
static struct { char label[72]; int x, y, w, h; } hl[MAXHITS];
static int nhl;
void hits_reset(void) { nhl = 0; }
void hits_note(const char *label, int x, int y, int w, int h) {
	if (nhl >= MAXHITS) return;
	snprintf(hl[nhl].label, sizeof hl[nhl].label, "%s", label);
	hl[nhl].x = x; hl[nhl].y = y; hl[nhl].w = w; hl[nhl].h = h;
	nhl++;
}
#ifdef INKLING_DEV
void hits_dump(App *a, const char *path) {
	FILE *f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "state menu=%d settings=%d settings_page=%d games=%d pagemenu=%d modes=%d page=%d last_page=%d mode=%s nops=%d\n",
		a->in_menu, a->in_settings, a->settings_page, a->in_games, a->in_pagemenu, a->in_modes, a->page, a->last_page, a->mode_id[a->mode], a->nops);
	for (int i = 0; i < a->nbuttons; i++)
		fprintf(f, "bar\t%s\t%d %d %d %d\n", a->buttons[i].text, a->buttons[i].x, a->buttons[i].y, a->buttons[i].w, a->buttons[i].h);
	if (a->in_menu || a->in_settings || a->in_games == 1 || a->in_pagemenu || a->in_modes)
		for (int i = 0; i < nhl; i++) fprintf(f, "panel\t%s\t%d %d %d %d\n", hl[i].label, hl[i].x, hl[i].y, hl[i].w, hl[i].h);
	fclose(f);
}
#endif

/* Returns the button for a press at (x, y), or -1. The whole bar height counts (the status line under the buttons
 * too), and a press in the gap beside a button still reaches it if it is within 40 px. */
int ui_hit(App *a, int x, int y) {
	if (y < 0 || y >= BAR_H) return -1;
	int best = -1, bestd = 41;
	for (int i = 0; i < a->nbuttons; i++) {
		const Button *b = &a->buttons[i];
		int d = x < b->x ? b->x - x : x >= b->x + b->w ? x - (b->x + b->w - 1) : 0;
		if (d < bestd) { bestd = d; best = i; }
	}
	return best;
}

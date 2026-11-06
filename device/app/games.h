/* Game engine API for Inkling. A game is a GameDef (see games/registry.c) with its own state struct; the
 * engine owns the screen, input and animation. All drawing calls are queued in order and played back as pen-style
 * animation (the board appears as if drawn by hand); nothing is drawn immediately. Coordinates are screen pixels
 * (portrait 1860x2480); g_area() gives the play area (the canvas minus the remark strip at the bottom).
 * Games must not include scribe.h: this header is the whole interface. */
#ifndef GAMES_H
#define GAMES_H

#include <stddef.h>

typedef struct {
	int n;               /* points in the stroke */
	const short *x, *y;  /* the points (screen px), n of each */
	int x0, y0, x1, y1;  /* bounding box */
	int cx, cy;          /* centroid (for a tap: where it was tapped) */
	int tap;             /* 1 = a tap (pen or finger barely moved) */
	int len;             /* path length in px */
	int finger;          /* 1 = finger (touch screen), 0 = pen */
} GStroke;

typedef struct GameCtx GameCtx;

typedef struct {
	const char *id, *name, *desc; /* desc: one short line for the menu */
	size_t state_size;            /* bytes of state; zeroed before start() */
	int ink;                      /* 1 = the player's pen strokes stay on the page; 0 = taps only, no ink */
	int levels;                   /* 1 = honours Easy/Medium/Hard */
	void (*start)(GameCtx *g);                    /* draw the board, set up state */
	void (*stroke)(GameCtx *g, const GStroke *s); /* the player finished a pen stroke or a tap on the canvas */
	void (*timer)(GameCtx *g);                    /* optional: g_after() fired (e.g. the computer's move) */
	/* Optional: save and resume. A game that sets resume() is saved when the player leaves it unfinished (Close,
	 * Games, Exit, the power key) and the menu then offers Continue or New. The engine saves the state bytes as they
	 * are, so the state must hold no pointers (keep names, not indexes into tables that may grow). On Continue it
	 * restores them and calls resume() instead of start(): redraw the board from the state (and call g_after() again
	 * if a timer was due). Bump save_version whenever the state's layout or meaning changes: a saved game with
	 * another version or size is dropped. */
	void (*resume)(GameCtx *g);
	void (*progress)(GameCtx *g, char *buf, int n); /* optional: e.g. "12 of 28 found", shown with Continue */
	int save_version;
} GameDef;

/* --- state and helpers --- */
void *g_state(GameCtx *g);   /* this game's state (state_size bytes) */
int g_level(GameCtx *g);     /* 0 easy, 1 medium, 2 hard */
int g_rand(GameCtx *g, int n); /* 0 .. n-1 */
void g_area(GameCtx *g, int *x, int *y, int *w, int *h); /* play area */
int g_busy(GameCtx *g);      /* 1 while queued drawing is still being animated */

/* --- small persistent storage: a few bytes per key, per game, kept on the Kindle across app restarts --- */
int g_load(GameCtx *g, const char *key, void *buf, size_t n);        /* bytes read into buf (at most n); 0 if none */
void g_save(GameCtx *g, const char *key, const void *buf, size_t n); /* replaces the record (written safely) */
/* key: 1-31 characters a-z 0-9 _ - ; records are meant to be small (at most 64 KB) and are not portable */

/* --- drawing (queued, animated in order) --- */
void g_speed(GameCtx *g, double factor);  /* pen speed for the following calls (1 = handwriting pace, 4 = default, 50 = instant) */
void g_line(GameCtx *g, int x0, int y0, int x1, int y1, int width);
void g_poly(GameCtx *g, const int *xy, int npoints, int width); /* xy = x0,y0,x1,y1,... */
void g_circle(GameCtx *g, int cx, int cy, int r, int width);
void g_cross(GameCtx *g, int cx, int cy, int r, int width);     /* an X */
void g_frame(GameCtx *g, int x, int y, int w, int h, int width); /* rectangle outline */
void g_dot(GameCtx *g, int cx, int cy, int r);                  /* filled black disc */
void g_box(GameCtx *g, int x, int y, int w, int h, int gray);   /* filled rectangle, gray 0 = black .. 255 = white */
void g_clear(GameCtx *g, int x, int y, int w, int h);           /* white rectangle */
void g_write(GameCtx *g, int x, int y_baseline, int cap, const char *text);      /* handwriting, left-aligned */
void g_write_center(GameCtx *g, int cx, int y_baseline, int cap, const char *text);
int g_write_width(GameCtx *g, int cap, const char *text);
void g_print(GameCtx *g, int x, int y, int w, int h, int px, int bold, const char *text); /* typeset, centred in the box */
void g_pause(GameCtx *g, int ms);         /* a pause in the animation */

/* --- talking to the player --- */
void g_say(GameCtx *g, const char *fmt, ...);  /* the status line in the top bar */
void g_remark(GameCtx *g, const char *text);   /* a handwritten remark in the strip at the bottom (replaces the last one) */
void g_after(GameCtx *g, int ms);              /* call timer() once, ms after the queued drawing has finished */
void g_over(GameCtx *g, const char *result);   /* game finished: shows result; the next tap starts a new game */

#endif

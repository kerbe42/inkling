/* Inkling's display layer: the Kindle Scribe's 8-bit gray framebuffer, its e-ink refresh ioctls (MediaTek hwtcon
 * driver), TrueType/OpenType text (stb_truetype) and PNG pictures (stb_image). Written for this app from the kernel's
 * interface definitions and the stb libraries' public APIs; see display.c for the provenance of every kernel fact. */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Font styles (a text's face). The numbers are stored in the app's ops, keep them. */
enum { TXT_REGULAR = 0, TXT_ITALIC = 1, TXT_BOLD = 2, TXT_BOLD_ITALIC = 3 };

/* Waveform modes of the hwtcon driver (enum WAVEFORM_MODE_ENUM in the kernel header, see display.c). */
enum {
	DISP_WFM_INIT = 0,
	DISP_WFM_DU = 1,    /* fast, black and white only */
	DISP_WFM_GC16 = 2,  /* 16 grays; a full (flashing) update clears ghosting */
	DISP_WFM_GL16 = 3,  /* 16 grays without the flash, for light backgrounds */
	DISP_WFM_REAGL = 4, /* GLR16 */
	DISP_WFM_A2 = 6,    /* fastest, 2 levels */
	DISP_WFM_DU4 = 7,
	DISP_WFM_AUTO = 257 /* the driver picks one from the region's content */
};

/* How to refresh a region: the waveform, and whether to flash (a full update; the panel goes black then redraws). */
typedef struct {
	int waveform;
	bool flash;
	bool wait; /* block until the panel has finished this update (up to the driver's 5 s timeout) */
} DispRefresh;

typedef struct {
	int fd;                  /* -1 for a memory framebuffer (host tests) */
	int width, height;       /* visible pixels */
	int stride;              /* bytes per row */
	int yoffset;             /* first visible row in the buffer (the fb's pan offset) */
	unsigned char *fb;       /* the whole buffer (mmapped), fbsize bytes */
	size_t fbsize;
	uint32_t marker;         /* last update marker sent */
	int last_rc;             /* last refresh ioctl's return value (errno-style on failure: -errno) */
	void (*on_refresh)(void *ctx, int top, int left, int width, int height, const DispRefresh *how); /* tests */
	void *ctx;
} Display;

typedef struct {
	int left, top, width, height;
} DispRect;

/* Open /dev/fb0 (or path): reads the screen geometry and maps the buffer. 0 or -1 (logged to stderr). */
int disp_open(Display *d, const char *path);
/* A framebuffer in memory, for host tests: refreshes are only reported to on_refresh. */
int disp_open_mem(Display *d, int width, int height, int stride);
void disp_close(Display *d);

/* Pointer to pixel (x, y) of the visible screen (no bounds check). */
static inline unsigned char *disp_px(const Display *d, int x, int y) {
	return d->fb + (size_t)(d->yoffset + y) * (size_t)d->stride + (size_t)x;
}

/* Fill a rectangle with a gray (0 black .. 255 white), clipped to the screen. */
void disp_fill_rect(Display *d, const DispRect *r, uint8_t gray);

/* Refresh a region of the panel (the kernel's rectangle order: top, left, width, height), clipped to the screen (the
 * values are taken as ints, so a negative top or left clips). A zero width or height means the whole screen.
 * Returns the ioctl's result (0 ok; nothing is sent for a region entirely off the screen). */
int disp_refresh(Display *d, uint32_t top, uint32_t left, uint32_t width, uint32_t height, const DispRefresh *how);
/* Wait for the panel to finish the last update sent (0 ok). */
int disp_wait(Display *d);

/* Draw a PNG at (x, y), clipped to the screen; transparent parts blend over what is there. Gray conversion of color
 * pictures is stb_image's. drawn (optional) gets the rectangle covered. 0 or -1. Files read here may come from the
 * USB storage (and the app runs as root), so they are size-checked before decoding: at most DISP_IMAGE_MAX_BYTES,
 * and at most DISP_IMAGE_MAX_SIDE pixels wide and high by the PNG header; a file must be a regular file, not a link. */
#define DISP_IMAGE_MAX_BYTES (16u << 20)
#define DISP_IMAGE_MAX_SIDE 4096
int disp_image_file(Display *d, const char *path, int x, int y, DispRect *drawn);
int disp_image_mem(Display *d, const unsigned char *data, size_t len, int x, int y, DispRect *drawn);

/* ---- text ---- */

typedef struct DispFace DispFace;
/* One font per style; a style without its own face uses the regular one. */
typedef struct {
	DispFace *face[4];
} DispFonts;

/* Load a .ttf/.otf/.ttc (first font of a collection) for a style, replacing that style's face. 0 or -1. */
int disp_font_add(DispFonts *f, const char *path, int style);
void disp_font_free(DispFonts *f);

/* Text in a box: wrapped to the box width (breaks at spaces, after hyphens and slashes, between CJK characters, at
 * newlines; inside a word only when it is longer than a line), lines spaced by the font's line height, lines that
 * would end below the box bottom dropped. */
typedef struct {
	int left, top, right, bottom; /* the box: right and bottom are exclusive edges */
	int px;                       /* size: the font's ascent + descent in pixels */
	int style;                    /* TXT_*; with formatted, the style outside markup */
	bool centered;                /* center each line horizontally in the box */
	bool formatted;               /* *italic*, **bold**, ***bold italic*** (also with _), markers not drawn */
	bool inverted;                /* ink in 255 - fg (white text on a dark fill) */
	bool measure_only;            /* lay out only: fills fit, draws nothing (d may be NULL) */
	uint8_t fg;                   /* ink gray (0 black); glyph edges blend with what is under them */
} DispText;

typedef struct {
	int lines;      /* lines the text needs at this width */
	int fitted;     /* of those, lines that fit above the box bottom */
	int rendered;   /* lines drawn (0 when measuring) */
	int width;      /* widest line, in pixels (to the ink of its last glyph) */
	int height;     /* height of the fitted lines (line pitch each, less half the line gap after the last) */
	bool truncated; /* some lines did not fit above the box bottom */
} DispTextFit;

/* Lays out and draws s. Returns the y below the last line, or -1 (no font, empty string). fit is optional. */
int disp_text(Display *d, const DispFonts *f, const char *s, const DispText *t, DispTextFit *fit);
/* Line height in pixels of the fonts' regular face at px. */
int disp_line_height(const DispFonts *f, int style, int px);

#endif

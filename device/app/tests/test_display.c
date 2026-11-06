/* Host tests for the display layer (display.c) on a memory framebuffer: fills, PNG blits, refresh regions, text
 * layout invariants, and test screens written as PNGs for a look by eye (dist/display-tests/). Fonts come from the
 * computer's free fonts (DejaVu, Noto, Lato, Liberation, Droid); checks that need a missing font are skipped.
 * Usage: test_display <outdir> */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "display.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#define W 1860
#define H 2480
#define STRIDE 1872

static int checks, failed;
#define CHECK(cond, ...)                                                                                               \
	do {                                                                                                           \
		checks++;                                                                                              \
		if (!(cond)) {                                                                                         \
			failed++;                                                                                      \
			printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                     \
			printf(__VA_ARGS__);                                                                           \
			printf("\n");                                                                                  \
		}                                                                                                      \
	} while (0)

static const char *outdir;

static void save(Display *d, const char *name) {
	char p[512];
	snprintf(p, sizeof p, "%s/%s.png", outdir, name);
	unsigned char *rows = malloc((size_t)W * H);
	for (int y = 0; y < H; y++) memcpy(rows + (size_t)y * W, disp_px(d, 0, y), W);
	if (!stbi_write_png(p, W, H, 1, rows, W)) printf("could not write %s\n", p);
	else printf("screen: %s\n", p);
	free(rows);
}

static void clear(Display *d, uint8_t g) { disp_fill_rect(d, &(DispRect){ 0, 0, W, H }, g); }

/* Ink bounding box (pixels differing from bg by more than 40) inside a rectangle; returns 0 when there is none. */
static int ink_box(Display *d, int x, int y, int w, int h, uint8_t bg, int *x0, int *y0, int *x1, int *y1) {
	*x0 = *y0 = 1 << 30;
	*x1 = *y1 = -1;
	for (int yy = y; yy < y + h; yy++)
		for (int xx = x; xx < x + w; xx++) {
			int v = *disp_px(d, xx, yy);
			if (abs(v - bg) > 40) {
				if (xx < *x0) *x0 = xx;
				if (xx > *x1) *x1 = xx;
				if (yy < *y0) *y0 = yy;
				if (yy > *y1) *y1 = yy;
			}
		}
	return *x1 >= 0;
}

static const char *first(const char *const *c) {
	for (; *c; c++)
		if (access(*c, R_OK) == 0) return *c;
	return NULL;
}

#define FD "/usr/share/fonts/truetype/"
static const char *SANS[4][4] = {
	{ FD "noto/NotoSans-Regular.ttf", FD "dejavu/DejaVuSans.ttf", FD "lato/Lato-Regular.ttf", NULL },
	{ FD "noto/NotoSans-Italic.ttf", FD "dejavu/DejaVuSans-Oblique.ttf", FD "lato/Lato-Italic.ttf", NULL },
	{ FD "noto/NotoSans-Bold.ttf", FD "dejavu/DejaVuSans-Bold.ttf", FD "lato/Lato-Bold.ttf", NULL },
	{ FD "noto/NotoSans-BoldItalic.ttf", FD "dejavu/DejaVuSans-BoldOblique.ttf", FD "lato/Lato-BoldItalic.ttf", NULL },
};
static const char *SERIF[4][4] = {
	{ FD "noto/NotoSerif-Regular.ttf", FD "dejavu/DejaVuSerif.ttf", FD "liberation/LiberationSerif-Regular.ttf", NULL },
	{ FD "noto/NotoSerif-Italic.ttf", FD "dejavu/DejaVuSerif-Italic.ttf", FD "liberation/LiberationSerif-Italic.ttf", NULL },
	{ FD "noto/NotoSerif-Bold.ttf", FD "dejavu/DejaVuSerif-Bold.ttf", FD "liberation/LiberationSerif-Bold.ttf", NULL },
	{ FD "noto/NotoSerif-BoldItalic.ttf", FD "dejavu/DejaVuSerif-BoldItalic.ttf", FD "liberation/LiberationSerif-BoldItalic.ttf",
	  NULL },
};
static const char *CJK_TTC[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
				 "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", NULL };
static const char *CJK_BOLD_TTC[] = { "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
				      "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc", NULL };
static const char *CJK_TTF[] = { FD "droid/DroidSansFallbackFull.ttf", FD "droid/DroidSansFallback.ttf", NULL };

static int load(DispFonts *f, const char *const paths[4][4]) {
	int n = 0;
	for (int s = 0; s < 4; s++) {
		const char *p = first(paths[s]);
		if (p && disp_font_add(f, p, s) == 0) n++;
	}
	return n;
}

/* ---- framebuffer: fills, clipping, refresh regions, pictures ---- */

typedef struct {
	int n, top, left, width, height, waveform, flash;
} RefLog;
static void on_refresh(void *ctx, int top, int left, int width, int height, const DispRefresh *how) {
	RefLog *l = ctx;
	l->n++;
	l->top = top, l->left = left, l->width = width, l->height = height, l->waveform = how->waveform, l->flash = how->flash;
}

static void test_fb(Display *d) {
	clear(d, 0xFF);
	disp_fill_rect(d, &(DispRect){ 10, 20, 30, 40 }, 0x55);
	CHECK(*disp_px(d, 10, 20) == 0x55 && *disp_px(d, 39, 59) == 0x55, "fill inside");
	CHECK(*disp_px(d, 9, 20) == 0xFF && *disp_px(d, 40, 20) == 0xFF && *disp_px(d, 10, 60) == 0xFF, "fill edges");
	disp_fill_rect(d, &(DispRect){ -50, -50, 60, 60 }, 0x11); /* clipped at the top-left corner */
	CHECK(*disp_px(d, 0, 0) == 0x11 && *disp_px(d, 9, 9) == 0x11 && *disp_px(d, 10, 10) == 0xFF, "fill clipped top-left");
	disp_fill_rect(d, &(DispRect){ W - 5, H - 5, 100, 100 }, 0x22); /* clipped at the bottom-right corner */
	CHECK(*disp_px(d, W - 1, H - 1) == 0x22 && *disp_px(d, W - 6, H - 1) == 0xFF, "fill clipped bottom-right");
	CHECK(d->fb[(size_t)(H - 1) * STRIDE + W] == 0xFF, "stride padding untouched");
	disp_fill_rect(d, &(DispRect){ W + 10, 0, 50, 50 }, 0); /* fully outside: nothing */
	disp_fill_rect(d, &(DispRect){ 0, 0, 0, 50 }, 0);

	RefLog log = { 0 };
	d->on_refresh = on_refresh;
	d->ctx = &log;
	DispRefresh gl16 = { .waveform = DISP_WFM_GL16 }, flash = { .waveform = DISP_WFM_GC16, .flash = true };
	disp_refresh(d, 100, 50, 200, 300, &gl16);
	CHECK(log.n == 1 && log.top == 100 && log.left == 50 && log.width == 200 && log.height == 300 && log.waveform == 3,
	      "refresh region (top %d left %d %dx%d)", log.top, log.left, log.width, log.height);
	disp_refresh(d, 0, 0, 0, 0, &flash);
	CHECK(log.n == 2 && log.top == 0 && log.left == 0 && log.width == W && log.height == H && log.flash, "0x0 = full screen");
	disp_refresh(d, (uint32_t)H - 10, (uint32_t)W - 20, 100, 100, &gl16);
	CHECK(log.n == 3 && log.width == 20 && log.height == 10, "refresh clipped to the screen (%dx%d)", log.width, log.height);
	disp_refresh(d, 10, (uint32_t)W + 5, 50, 50, &gl16);
	CHECK(log.n == 3, "a region off the screen refreshes nothing");
	disp_refresh(d, (uint32_t)-20, (uint32_t)-10, 50, 60, &gl16); /* negative top/left from caller arithmetic */
	CHECK(log.n == 4 && log.top == 0 && log.left == 0 && log.width == 40 && log.height == 40, "negative origin clipped (%dx%d)",
	      log.width, log.height);
	d->on_refresh = NULL;

	/* an ioctl is really sent when there is a device: /dev/null refuses it */
	int fd = open("/dev/null", O_RDWR);
	if (fd >= 0) {
		d->fd = fd;
		int rc = disp_refresh(d, 0, 0, 10, 10, &gl16);
		CHECK(rc == -ENOTTY && d->marker == 1, "refresh ioctl on /dev/null: %d (marker %u)", rc, d->marker);
		d->fd = -1;
		close(fd);
	}
	Display bad;
	CHECK(disp_open(&bad, "/dev/null") < 0, "disp_open refuses a non-framebuffer");

	/* pictures: a gray PNG is copied exactly, transparency blends, both clipped */
	enum { PW = 64, PH = 48 };
	unsigned char gray[PW * PH], ga[PW * PH * 2];
	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++) {
			gray[y * PW + x] = (unsigned char)(x * 4);
			ga[(y * PW + x) * 2] = 0x00;
			ga[(y * PW + x) * 2 + 1] = (unsigned char)(x < PW / 2 ? 128 : 255);
		}
	int len = 0;
	unsigned char *png = stbi_write_png_to_mem(gray, PW, PW, PH, 1, &len);
	clear(d, 0xFF);
	DispRect drawn;
	CHECK(disp_image_mem(d, png, (size_t)len, 300, 400, &drawn) == 0, "png decodes");
	CHECK(drawn.left == 300 && drawn.top == 400 && drawn.width == PW && drawn.height == PH, "png rect");
	int same = 1;
	for (int y = 0; y < PH; y++)
		for (int x = 0; x < PW; x++) same &= *disp_px(d, 300 + x, 400 + y) == gray[y * PW + x];
	CHECK(same, "png pixels copied exactly");
	disp_image_mem(d, png, (size_t)len, W - 10, -5, &drawn);
	CHECK(drawn.left == W - 10 && drawn.top == 0 && drawn.width == 10 && drawn.height == PH - 5, "png clipped (%d,%d %dx%d)",
	      drawn.left, drawn.top, drawn.width, drawn.height);
	STBIW_FREE(png);
	png = stbi_write_png_to_mem(ga, PW * 2, PW, PH, 2, &len);
	disp_fill_rect(d, &(DispRect){ 500, 500, PW, PH }, 0xC8);
	disp_image_mem(d, png, (size_t)len, 500, 500, NULL);
	CHECK(*disp_px(d, 500, 500) == (0xC8 * 127 + 127) / 255 && *disp_px(d, 500 + PW - 1, 500) == 0, "alpha blend (%d)",
	      *disp_px(d, 500, 500));
	char p[512];
	snprintf(p, sizeof p, "%s/blit-test.png", outdir);
	FILE *f = fopen(p, "wb");
	if (f) {
		fwrite(png, 1, (size_t)len, f);
		fclose(f);
		CHECK(disp_image_file(d, p, 600, 600, &drawn) == 0 && drawn.width == PW, "png from a file");
		char q[520];
		snprintf(q, sizeof q, "%s.link", p);
		unlink(q);
		if (symlink(p, q) == 0) {
			CHECK(disp_image_file(d, q, 600, 600, &drawn) < 0 && drawn.width == 0, "a symlinked picture is refused");
			unlink(q);
		}
		unlink(p);
	}
	CHECK(disp_image_file(d, "/nonexistent/x.png", 0, 0, NULL) < 0, "missing file");
	CHECK(disp_image_file(d, "/dev/zero", 0, 0, NULL) < 0, "not a regular file");
	STBIW_FREE(png);
	unsigned char junk[64] = { 0x89, 'P', 'N', 'G' };
	CHECK(disp_image_mem(d, junk, sizeof junk, 0, 0, NULL) < 0, "broken png refused");
	/* a picture wider than DISP_IMAGE_MAX_SIDE is refused from its header, before decoding */
	unsigned char *wide = calloc(DISP_IMAGE_MAX_SIDE + 1, 1);
	png = stbi_write_png_to_mem(wide, DISP_IMAGE_MAX_SIDE + 1, DISP_IMAGE_MAX_SIDE + 1, 1, 1, &len);
	CHECK(png && disp_image_mem(d, png, (size_t)len, 0, 0, &drawn) < 0 && drawn.width == 0, "oversized picture refused");
	STBIW_FREE(png);
	png = stbi_write_png_to_mem(wide, DISP_IMAGE_MAX_SIDE, DISP_IMAGE_MAX_SIDE, 1, 1, &len);
	CHECK(png && disp_image_mem(d, png, (size_t)len, 0, 0, &drawn) == 0 && drawn.width == W, "largest picture accepted");
	STBIW_FREE(png);
	free(wide);
	CHECK(disp_image_mem(d, junk, (size_t)DISP_IMAGE_MAX_BYTES + 1, 0, 0, NULL) < 0, "oversized buffer refused");
}

/* ---- text ---- */

static void test_text(Display *d, DispFonts *sans, DispFonts *serif) {
	const char *para = "The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs! How vexingly "
			   "quick daft zebras jump; sphinx of black quartz, judge my vow.";
	DispTextFit fit, m;
	clear(d, 0xFF);
	int widths[] = { 300, 517, 860, 1500 };
	for (int i = 0; i < 4; i++) {
		int x = 40, y = 40 + i * 330, w = widths[i];
		DispText t = { .left = x, .top = y, .right = x + w, .bottom = y + 320, .px = 34 };
		disp_text(d, sans, para, &t, &fit);
		DispText tm = t;
		tm.measure_only = true;
		disp_text(NULL, sans, para, &tm, &m);
		CHECK(m.lines == fit.lines && m.fitted == fit.fitted && m.width == fit.width, "measure = draw (%d/%d lines)", m.lines,
		      fit.lines);
		int x0, y0, x1, y1;
		ink_box(d, 0, y, W, 330, 0xFF, &x0, &y0, &x1, &y1);
		CHECK(x0 >= x && x1 < x + w, "wrapped ink inside the box at width %d (%d..%d)", w, x0, x1);
		CHECK(y1 < y + fit.fitted * disp_line_height(sans, TXT_REGULAR, 34), "ink above the last line's bottom");
		CHECK(fit.width <= w, "widest line %d fits %d", fit.width, w);
	}
	/* narrower boxes need at least as many lines */
	int prev = 0;
	for (int w = 1500; w >= 200; w -= 50) {
		DispText t = { .left = 0, .top = 0, .right = w, .bottom = H, .px = 34, .measure_only = true };
		disp_text(NULL, sans, para, &t, &m);
		CHECK(m.lines >= prev, "lines grow as the box narrows (w %d: %d < %d)", w, m.lines, prev);
		prev = m.lines;
	}
	/* centring: equal margins (within the rounding and the glyphs' side bearings) */
	DispText c = { .left = 40, .top = 1400, .right = 1820, .bottom = 1500, .px = 60, .style = TXT_BOLD, .centered = true };
	disp_text(d, sans, "Centred", &c, &fit);
	int x0, y0, x1, y1;
	ink_box(d, 0, 1400, W, 100, 0xFF, &x0, &y0, &x1, &y1);
	CHECK(abs((x0 - 40) - (1820 - 1 - x1)) <= 4, "centred (%d left, %d right)", x0 - 40, 1820 - 1 - x1);
	/* truncation: 2.5 lines of room give 2 lines, nothing drawn below the box */
	int lh = disp_line_height(sans, TXT_REGULAR, 30);
	clear(d, 0xFF);
	DispText tr = { .left = 40, .top = 100, .right = 400, .bottom = 100 + lh * 5 / 2, .px = 30 };
	disp_text(d, sans, para, &tr, &fit);
	CHECK(fit.fitted == 2 && fit.rendered == 2 && fit.truncated && fit.lines > 2, "truncated to 2 lines (%d of %d)", fit.rendered,
	      fit.lines);
	ink_box(d, 0, 0, W, H, 0xFF, &x0, &y0, &x1, &y1);
	CHECK(y1 < 100 + 2 * lh, "nothing below the second line (%d)", y1);
	tr.bottom = 100 + lh - 1;
	disp_text(d, sans, para, &tr, &fit);
	CHECK(fit.rendered == 0 && fit.truncated, "a box shorter than a line draws nothing");
	/* newlines, empty strings */
	DispText nl = { .left = 0, .top = 0, .right = 800, .bottom = H, .px = 30, .measure_only = true };
	disp_text(NULL, sans, "a\nb\n\nc", &nl, &m);
	CHECK(m.lines == 4, "newlines: %d lines", m.lines);
	disp_text(NULL, sans, "ends with a newline\n", &nl, &m);
	CHECK(m.lines == 1, "trailing newline adds no line (%d)", m.lines);
	CHECK(disp_text(NULL, sans, "", &nl, &m) < 0 && m.lines == 0, "empty string");
	CHECK(disp_text(NULL, NULL, "x", &nl, &m) < 0, "no fonts");
	/* an overlong word breaks inside, every piece within the box */
	clear(d, 0xFF);
	DispText lw = { .left = 100, .top = 100, .right = 400, .bottom = 1000, .px = 40 };
	disp_text(d, sans, "Supercalifragilisticexpialidocious", &lw, &fit);
	ink_box(d, 0, 0, W, H, 0xFF, &x0, &y0, &x1, &y1);
	CHECK(fit.lines >= 2 && x1 < 400 && x0 >= 100, "overlong word wrapped inside (%d lines, %d..%d)", fit.lines, x0, x1);

	/* markup: **bold** draws exactly like bold text, markers are not drawn; snake_case keeps its underscores */
	Display e;
	disp_open_mem(&e, W, H, STRIDE);
	DispText a1 = { .left = 50, .top = 50, .right = 1800, .bottom = 400, .px = 40, .formatted = true };
	DispText a2 = { .left = 50, .top = 50, .right = 1800, .bottom = 400, .px = 40, .style = TXT_BOLD };
	clear(d, 0xFF);
	disp_text(d, serif, "**Bold words**", &a1, NULL);
	disp_text(&e, serif, "Bold words", &a2, NULL);
	CHECK(!memcmp(d->fb, e.fb, d->fbsize), "**bold** = bold");
	clear(d, 0xFF);
	clear(&e, 0xFF);
	a2.style = TXT_ITALIC;
	disp_text(d, serif, "_slanted_", &a1, NULL);
	disp_text(&e, serif, "slanted", &a2, NULL);
	CHECK(!memcmp(d->fb, e.fb, d->fbsize), "_italic_ = italic");
	clear(d, 0xFF);
	clear(&e, 0xFF);
	a2.style = TXT_REGULAR;
	disp_text(d, serif, "file_name_here", &a1, NULL);
	disp_text(&e, serif, "file_name_here", &a2, NULL);
	CHECK(!memcmp(d->fb, e.fb, d->fbsize), "snake_case stays literal");
	clear(d, 0xFF);
	clear(&e, 0xFF);
	a2.style = TXT_BOLD_ITALIC;
	disp_text(d, serif, "***both***", &a1, NULL);
	disp_text(&e, serif, "both", &a2, NULL);
	CHECK(!memcmp(d->fb, e.fb, d->fbsize), "***both*** = bold italic");
	disp_close(&e);
	/* inverted: white ink on a dark button, the fill stays where there is no glyph */
	clear(d, 0xFF);
	disp_fill_rect(d, &(DispRect){ 100, 100, 400, 100 }, 0x00);
	DispText inv = { .left = 100, .top = 120, .right = 500, .bottom = 200, .px = 46, .style = TXT_BOLD, .centered = true,
			 .inverted = true };
	disp_text(d, sans, "OK", &inv, NULL);
	int white = 0;
	for (int y = 100; y < 200; y++)
		for (int x = 100; x < 500; x++) white += *disp_px(d, x, y) == 0xFF;
	CHECK(white > 200 && *disp_px(d, 101, 101) == 0x00, "inverted text is white (%d px)", white);
	/* fg gray: full-coverage pixels take exactly that gray */
	clear(d, 0xFF);
	DispText g = { .left = 10, .top = 10, .right = 800, .bottom = 200, .px = 80, .style = TXT_BOLD, .fg = 0x44 };
	disp_text(d, sans, "Gray", &g, NULL);
	int n44 = 0, darker = 0;
	for (int y = 0; y < 200; y++)
		for (int x = 0; x < 800; x++) n44 += *disp_px(d, x, y) == 0x44, darker += *disp_px(d, x, y) < 0x44;
	CHECK(n44 > 500 && darker == 0, "gray ink (%d px at 0x44, %d darker)", n44, darker);
}

/* ---- the screens to look at ---- */

static void screen_text(Display *d, DispFonts *sans, DispFonts *serif) {
	clear(d, 0xFF);
	int y = 30;
	static const char *names[4] = { "Regular", "Italic", "Bold", "Bold italic" };
	int sizes[] = { 24, 30, 36, 48, 72 };
	for (int i = 0; i < 5; i++) {
		int x = 30;
		for (int s = 0; s < 4; s++) {
			char b[64];
			snprintf(b, sizeof b, "%s %d", names[s], sizes[i]);
			DispText t = { .left = x, .top = y, .right = x + 450, .bottom = y + sizes[i] * 2, .px = sizes[i], .style = s };
			disp_text(d, sans, b, &t, NULL);
			x += 455;
		}
		y += sizes[i] + 24;
	}
	disp_fill_rect(d, &(DispRect){ 30, y, 1800, 2 }, 0x88);
	y += 20;
	DispText p = { .left = 30, .top = y, .right = 890, .bottom = y + 330, .px = 36 };
	disp_text(d, serif,
		  "Wrapped serif paragraph at 36 px in an 860 px box: the quick brown fox jumps over the lazy dog, and "
		  "well-known words, e.g. foo/bar, break where they should. https://example.com/a/long/path/without/spaces/ok",
		  &p, NULL);
	DispText f = { .left = 960, .top = y, .right = 1830, .bottom = y + 330, .px = 36, .formatted = true };
	disp_text(d, serif,
		  "Formatted: this is **bold**, this is *italic*, this is ***bold italic*** and a **bold phrase that runs "
		  "across the end of the line**, then *an italic one*. snake_case_names stay as they are.",
		  &f, NULL);
	y += 350;
	disp_fill_rect(d, &(DispRect){ 30, y, 1800, 120 }, 0xE6);
	DispText c = { .left = 30, .top = y + (120 - 64) / 2 - 8, .right = 1830, .bottom = y + 120, .px = 64, .style = TXT_BOLD,
		       .centered = true };
	disp_text(d, sans, "Centred title, 64 px", &c, NULL);
	y += 140;
	const char *btn[3] = { "Send", "Menu", "Close" };
	for (int i = 0; i < 3; i++) {
		int bx = 30 + i * 610;
		disp_fill_rect(d, &(DispRect){ bx, y, 590, 110 }, i == 1 ? 0x00 : 0x2A);
		DispText b = { .left = bx, .top = y + (110 - 46) / 2 - 46 / 8, .right = bx + 590, .bottom = y + 110, .px = 46,
			       .style = TXT_BOLD, .centered = true, .inverted = true };
		disp_text(d, sans, btn[i], &b, NULL);
	}
	y += 140;
	DispText st = { .left = 30, .top = y, .right = 1830, .bottom = y + 40, .px = 30, .style = TXT_ITALIC };
	disp_text(d, sans, "ready - Claude - page 3 of 7 - flick left/right to turn pages (status line, italic 30 px)", &st, NULL);
	y += 60;
	DispText gr = { .left = 30, .top = y, .right = 1830, .bottom = y + 60, .px = 40, .fg = 0x66 };
	disp_text(d, sans, "Gray ink 0x66: softer text, as for hints", &gr, NULL);
	y += 70;
	DispText tr = { .left = 30, .top = y, .right = 900, .bottom = y + 2 * disp_line_height(sans, 0, 34), .px = 34 };
	disp_fill_rect(d, &(DispRect){ 30, y, 870, tr.bottom - y }, 0xF0);
	disp_text(d, sans,
		  "Truncated to its box: this text needs four or five lines at this width but the gray box has room for "
		  "two, so the rest is dropped.",
		  &tr, NULL);
	DispText nl = { .left = 960, .top = y, .right = 1830, .bottom = y + 300, .px = 34 };
	disp_text(d, sans, "Line one\nLine two after a newline\n\nAfter an empty line", &nl, NULL);
	y += 320;
	DispText big = { .left = 30, .top = y, .right = 1830, .bottom = y + 220, .px = 100, .style = TXT_BOLD_ITALIC };
	disp_text(d, serif, "Big 100 px Hxgj", &big, NULL);
	y += 140;
	DispText acc = { .left = 30, .top = y, .right = 1830, .bottom = y + 200, .px = 40 };
	disp_text(d, serif, "Accents and symbols: Café naïve façade — “quotes” ‘single’ … 3×4 ≥ 10 € £ ¥ © Ωμπ ÄÖÜß ĞŞİ", &acc,
		  NULL);
}

static void screen_cjk(Display *d, DispFonts *ttc, DispFonts *ttf, DispFonts *latin) {
	clear(d, 0xFF);
	const char *ja = "日本語のテキスト、漢字とかなを含む文章です。「括弧」の前後でも正しく改行されるか、句読点が行頭に来ないかを確かめます。";
	const char *zh = "中文文本，简体字和標點符號。这一段文字用来检查换行是否正确，标点符号不应该出现在行首。";
	const char *ko = "한국어 텍스트입니다. 한글은 공백에서 줄을 바꿉니다. 이 문장은 줄 바꿈을 확인합니다.";
	DispFonts *sets[2] = { ttc, ttf };
	const char *label[2] = { "OpenType collection (.ttc, CFF outlines): Noto Sans CJK",
				 "TrueType (.ttf): Droid Sans Fallback (it has no Hangul: boxes)" };
	int y = 30;
	for (int s = 0; s < 2; s++) {
		if (!sets[s]->face[0]) continue;
		DispText h = { .left = 30, .top = y, .right = 1830, .bottom = y + 60, .px = 36, .style = TXT_BOLD };
		disp_text(d, latin, label[s], &h, NULL);
		y += 60;
		const char *txt[3] = { ja, zh, ko };
		for (int i = 0; i < 3; i++) {
			DispText t = { .left = 30, .top = y, .right = 30 + 700, .bottom = y + 400, .px = 40 };
			disp_fill_rect(d, &(DispRect){ 30, y, 700, 0 }, 0);
			DispTextFit fit;
			disp_text(d, sets[s], txt[i], &t, &fit);
			DispText t2 = { .left = 800, .top = y, .right = 1830, .bottom = y + 400, .px = 52, .style = TXT_BOLD };
			DispTextFit fit2;
			disp_text(d, sets[s], txt[i], &t2, &fit2);
			int lines = fit.rendered * disp_line_height(sets[s], 0, 40), l2 = fit2.rendered * disp_line_height(sets[s], 0, 52);
			y += (lines > l2 ? lines : l2) + 30;
		}
	}
	DispText m = { .left = 30, .top = y, .right = 1830, .bottom = y + 200, .px = 44 };
	if (ttc->face[0]) disp_text(d, ttc, "Mixed: 混合 Latin and 漢字, numbers 12345 and ｆｕｌｌｗｉｄｔｈ.", &m, NULL);
}

static void screen_fills(Display *d, DispFonts *sans) {
	clear(d, 0xFF);
	for (int i = 0; i < 16; i++) {
		uint8_t g = (uint8_t)(i * 17);
		disp_fill_rect(d, &(DispRect){ 30 + i * 112, 30, 108, 300 }, g);
		char b[8];
		snprintf(b, sizeof b, "%02X", g);
		DispText t = { .left = 30 + i * 112, .top = 340, .right = 30 + i * 112 + 108, .bottom = 400, .px = 30, .centered = true };
		disp_text(d, sans, b, &t, NULL);
	}
	for (int x = 0; x < 1800; x++) disp_fill_rect(d, &(DispRect){ 30 + x, 420, 1, 120 }, (uint8_t)(x * 255 / 1799));
	/* a page-like picture: lines and handwriting-ish strokes, blitted from a PNG in memory */
	enum { PW = 1200, PH = 900 };
	unsigned char *px = malloc(PW * PH);
	memset(px, 0xFF, PW * PH);
	for (int y = 60; y < PH; y += 80)
		for (int x = 0; x < PW; x++) px[y * PW + x] = 0xC0;
	for (int t = 0; t < 4000; t++) {
		double u = t / 4000.0;
		int x = 60 + (int)(u * 1080), y = 300 + (int)(120 * __builtin_sin(u * 40) * (1 - u));
		for (int dy = -3; dy <= 3; dy++)
			for (int dx = -3; dx <= 3; dx++) px[(y + dy) * PW + x + dx] = 0x00;
	}
	int len;
	unsigned char *png = stbi_write_png_to_mem(px, PW, PW, PH, 1, &len);
	disp_fill_rect(d, &(DispRect){ 300, 580, PW + 8, PH + 8 }, 0x00);
	DispRect drawn;
	disp_image_mem(d, png, (size_t)len, 304, 584, &drawn);
	DispText cap = { .left = 300, .top = 1500, .right = 1500, .bottom = 1560, .px = 30, .style = TXT_ITALIC };
	disp_text(d, sans, "PNG page picture blitted at (304, 584)", &cap, NULL);
	free(px);
	STBIW_FREE(png);
	/* a transparent picture over stripes */
	for (int i = 0; i < 10; i++) disp_fill_rect(d, &(DispRect){ 300, 1600 + i * 40, 1200, 20 }, 0x00);
	enum { AW = 600, AH = 400 };
	unsigned char *ga = malloc(AW * AH * 2);
	for (int y = 0; y < AH; y++)
		for (int x = 0; x < AW; x++) {
			ga[(y * AW + x) * 2] = 0xFF;
			ga[(y * AW + x) * 2 + 1] = (unsigned char)(x * 255 / (AW - 1));
		}
	png = stbi_write_png_to_mem(ga, AW * 2, AW, AH, 2, &len);
	disp_image_mem(d, png, (size_t)len, 600, 1600, NULL);
	DispText cap2 = { .left = 300, .top = 2020, .right = 1500, .bottom = 2080, .px = 30, .style = TXT_ITALIC };
	disp_text(d, sans, "white PNG with an alpha ramp (left clear, right opaque) over stripes", &cap2, NULL);
	free(ga);
	STBIW_FREE(png);
}

int main(int argc, char **argv) {
	outdir = argc > 1 ? argv[1] : ".";
	Display d;
	if (disp_open_mem(&d, W, H, STRIDE) < 0) return 1;
	test_fb(&d);
	DispFonts sans = { 0 }, serif = { 0 }, ttc = { 0 }, ttf = { 0 };
	int ns = load(&sans, SANS), nr = load(&serif, SERIF);
	const char *p;
	if ((p = first(CJK_TTC))) disp_font_add(&ttc, p, TXT_REGULAR);
	if ((p = first(CJK_BOLD_TTC))) disp_font_add(&ttc, p, TXT_BOLD);
	if ((p = first(CJK_TTF))) disp_font_add(&ttf, p, TXT_REGULAR);
	if (ns && nr) {
		test_text(&d, &sans, &serif);
		screen_text(&d, &sans, &serif);
		save(&d, "text");
		screen_fills(&d, &sans);
		save(&d, "fills");
		if (ttc.face[0] || ttf.face[0]) {
			screen_cjk(&d, &ttc, &ttf, &sans);
			save(&d, "cjk");
		} else printf("SKIP CJK screen: no Noto Sans CJK or Droid Sans Fallback font\n");
	} else printf("SKIP text checks and screens: no Noto/DejaVu/Lato fonts in %s\n", FD);
	disp_font_free(&sans);
	disp_font_free(&serif);
	disp_font_free(&ttc);
	disp_font_free(&ttf);
	disp_close(&d);
	printf("%d checks, %d failed\n", checks, failed);
	return failed != 0;
}

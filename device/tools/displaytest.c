/* displaytest: checks Inkling's display layer (device/app/display.c) on the Kindle, by eye and by log. Draws a set of
 * test patterns (gray steps, text in the Kindle's fonts, one box per waveform, an ink-like DU run, PNG pictures) and
 * logs the screen geometry and every refresh ioctl's return code and timing. Restores the previous screen at the end.
 *
 *   displaytest [PAUSE_SECONDS=3] [FONT_DIR=/usr/java/lib/fonts]
 *
 * Run it with device/tools/displaytest.sh, which pauses the Kindle UI first (as the app's launcher does). Built by
 * device/tools/build_displaytest.sh into build/displaytest. Each step's line in the log reads
 *   step <n> <what>: send rc=<0 ok, -errno> (<ms> to submit), wait rc=<..> (<ms> until the panel finished)
 * and the screen shows the step's name, so the panel can be checked against the log. */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "display.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

static Display D;
static DispFonts ui, serif, ja, zh, ko;
static int pause_s = 3, step_no, fails;
static const char *font_dir = "/usr/java/lib/fonts";

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static void logln(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logln(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	fflush(stdout);
}

static const char *wfm_name(int w) {
	switch (w) {
	case DISP_WFM_INIT: return "INIT";
	case DISP_WFM_DU: return "DU";
	case DISP_WFM_GC16: return "GC16";
	case DISP_WFM_GL16: return "GL16";
	case DISP_WFM_REAGL: return "GLR16 (REAGL)";
	case DISP_WFM_A2: return "A2";
	case DISP_WFM_DU4: return "DU4";
	case DISP_WFM_AUTO: return "AUTO";
	default: return "invalid";
	}
}

/* Refresh a region, wait for the panel, log both ioctls. expect_ok: 0 for the deliberate invalid one. */
static void refresh(const char *what, int x, int y, int w, int h, int wfm, bool flash, int expect_ok) {
	DispRefresh how = { .waveform = wfm, .flash = flash };
	double t0 = now_ms();
	int rc = disp_refresh(&D, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &how);
	double t1 = now_ms();
	int wrc = rc == 0 ? disp_wait(&D) : 0;
	double t2 = now_ms();
	char reg[48];
	if (w && h) snprintf(reg, sizeof reg, "%dx%d+%d+%d", w, h, x, y);
	else snprintf(reg, sizeof reg, "full screen");
	logln("step %d %s: %s%s %s marker %u: send rc=%d (%.1f ms to submit), wait rc=%d (%.0f ms until done)%s", step_no,
	      what, wfm_name(wfm), flash ? " full update (flash)" : "", reg, D.marker, rc, t1 - t0, wrc, t2 - t1,
	      (rc == 0) == (expect_ok != 0) ? "" : "   <-- UNEXPECTED");
	if ((rc == 0) != (expect_ok != 0)) fails++;
}

static void fill(int x, int y, int w, int h, uint8_t g) { disp_fill_rect(&D, &(DispRect){ x, y, w, h }, g); }

static int text(const DispFonts *f, const char *s, int x, int y, int w, int h, int px, int style, bool centered, bool inverted) {
	DispText t = { .left = x, .top = y, .right = x + w, .bottom = y + h, .px = px, .style = style, .centered = centered,
		       .inverted = inverted };
	DispTextFit fit;
	int r = disp_text(&D, f, s, &t, &fit);
	return r < 0 ? y : y + fit.rendered * disp_line_height(f, style, px);
}

static void title(const char *s) {
	step_no++;
	fill(0, 0, D.width, 130, 0xFF);
	char b[160];
	snprintf(b, sizeof b, "displaytest %d: %s", step_no, s);
	text(&ui, b, 40, 30, D.width - 80, 90, 56, TXT_BOLD, false, false);
	fill(40, 120, D.width - 80, 3, 0x00);
}

static void pause_step(void) { sleep((unsigned)pause_s); }

static int find_font(const char *stem, char *path, size_t n) {
	static const char *ext[] = { ".ttf", ".otf", ".ttc" };
	for (int i = 0; i < 3; i++) {
		snprintf(path, n, "%s/%s%s", font_dir, stem, ext[i]);
		if (access(path, R_OK) == 0) return 1;
	}
	return 0;
}

static void load(DispFonts *f, const char *const stems[4], const char *label) {
	char p[300];
	for (int s = 0; s < 4; s++) {
		if (!stems[s]) continue;
		int ok = find_font(stems[s], p, sizeof p) && disp_font_add(f, p, s) == 0;
		logln("font %s style %d: %s %s", label, s, stems[s], ok ? "loaded" : "MISSING");
	}
}

static void geometry(void) {
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo fx;
	if (ioctl(D.fd, FBIOGET_VSCREENINFO, &v) == 0 && ioctl(D.fd, FBIOGET_FSCREENINFO, &fx) == 0) {
		char id[17];
		memcpy(id, fx.id, 16);
		id[16] = 0;
		logln("fb: id \"%s\" %ux%u virtual %ux%u offset %u,%u rotate %u bpp %u grayscale %u line %u smem %u", id, v.xres,
		      v.yres, v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset, v.rotate, v.bits_per_pixel, v.grayscale,
		      fx.line_length, fx.smem_len);
	}
	logln("display: %dx%d stride %d yoffset %d buffer %zu bytes", D.width, D.height, D.stride, D.yoffset, D.fbsize);
	static const char *sys[] = { "rotate", "virtual_size", "pan", "stride", "bits_per_pixel" };
	for (size_t i = 0; i < sizeof sys / sizeof sys[0]; i++) {
		char p[96], b[64] = "";
		snprintf(p, sizeof p, "/sys/class/graphics/fb0/%s", sys[i]);
		FILE *f = fopen(p, "r");
		if (f) {
			if (!fgets(b, sizeof b, f)) b[0] = 0;
			fclose(f);
			b[strcspn(b, "\n")] = 0;
		}
		logln("sysfs %s: %s", sys[i], b);
	}
}

int main(int argc, char **argv) {
	if (argc > 1) pause_s = atoi(argv[1]);
	if (argc > 2) font_dir = argv[2];
	if (pause_s < 0 || pause_s > 60) pause_s = 3;
	if (disp_open(&D, "/dev/fb0") < 0) {
		logln("disp_open failed: %s", strerror(errno));
		return 1;
	}
	geometry();
	unsigned char *saved = malloc(D.fbsize);
	if (saved) memcpy(saved, D.fb, D.fbsize);
	static const char *UI[4] = { "Amazon-Ember-Regular", "Amazon-Ember-RegularItalic", "Amazon-Ember-Bold",
				     "Amazon-Ember-BoldItalic" };
	static const char *SERIF[4] = { "Bookerly-Regular", "Bookerly-Italic", "Bookerly-Bold", "Bookerly-BoldItalic" };
	static const char *JA[4] = { "TBGothicMed_213", NULL, "TBGothicBold_213", NULL };
	static const char *ZH[4] = { "STHeitiMedium", NULL, "STHeitiBold", NULL };
	static const char *KO[4] = { "NotoSansKR-Regular", NULL, "NotoSansKR-Black", NULL };
	load(&ui, UI, "ui");
	load(&serif, SERIF, "serif");
	load(&ja, JA, "ja");
	load(&zh, ZH, "zh");
	load(&ko, KO, "ko");
	const int W = D.width, H = D.height;

	/* 1: clear with a flashing full refresh */
	fill(0, 0, W, H, 0xFF);
	title("white, full GC16 refresh (flash)");
	refresh("clear", 0, 0, 0, 0, DISP_WFM_GC16, true, 1);
	pause_step();

	/* 2: 16 gray steps, GC16 then GL16 */
	title("16 gray steps: GC16, then GL16 on the same area");
	int sw = (W - 80) / 16;
	for (int i = 0; i < 16; i++) {
		char b[8];
		fill(40 + i * sw, 200, sw - 6, 500, (uint8_t)(i * 17));
		snprintf(b, sizeof b, "%02X", i * 17);
		text(&ui, b, 40 + i * sw, 710, sw - 6, 50, 32, TXT_REGULAR, true, false);
	}
	for (int x = 0; x < W - 80; x++) fill(40 + x, 800, 1, 150, (uint8_t)(x * 255 / (W - 81)));
	text(&ui, "smooth ramp 00..FF (expect 16 bands on the panel)", 40, 960, W - 80, 50, 32, TXT_ITALIC, false, false);
	refresh("grays", 0, 0, W, 1020, DISP_WFM_GC16, false, 1);
	pause_step();
	refresh("grays again", 0, 0, W, 1020, DISP_WFM_GL16, false, 1);
	pause_step();

	/* 3: text */
	fill(0, 130, W, H - 130, 0xFF);
	title("text: Kindle fonts, styles, sizes, markup, CJK");
	int y = 160;
	static const char *names[4] = { "Regular", "Italic", "Bold", "Bold italic" };
	int sizes[] = { 24, 36, 48, 72 };
	for (int i = 0; i < 4; i++) {
		for (int s = 0; s < 4; s++) {
			char b[48];
			snprintf(b, sizeof b, "%s %d", names[s], sizes[i]);
			text(&ui, b, 40 + s * 450, y, 445, sizes[i] * 2, sizes[i], s, false, false);
		}
		y += sizes[i] * 13 / 10 + 10;
	}
	y = text(&serif,
		 "Bookerly 40 px, wrapped in a 1000 px box: the quick brown fox jumps over the lazy dog, well-known words, e.g. "
		 "foo/bar, and https://example.com/a/long/path/without/spaces break where they should.",
		 40, y + 10, 1000, 400, 40, TXT_REGULAR, false, false);
	DispText f = { .left = 40, .top = y + 20, .right = W - 40, .bottom = y + 220, .px = 40, .formatted = true };
	disp_text(&D, &serif, "Formatted: **bold**, *italic*, ***bold italic***; snake_case_names keep their underscores.", &f, NULL);
	y += 140;
	fill(40, y, 600, 110, 0x2A);
	text(&ui, "Inverted", 40, y + (110 - 46) / 2 - 6, 600, 110, 46, TXT_BOLD, true, true);
	fill(680, y, 600, 110, 0xE6);
	text(&ui, "Centred on gray", 680, y + (110 - 46) / 2 - 6, 600, 110, 46, TXT_BOLD, true, false);
	y += 140;
	y = text(&ja, "日本語：漢字とかなの文章です。「括弧」と句読点、。を確かめます。", 40, y, W - 80, 200, 44, TXT_REGULAR, false, false);
	y = text(&zh, "中文：简体字和標點符號，这一段文字用来检查换行。", 40, y + 10, W - 80, 200, 44, TXT_REGULAR, false, false);
	y = text(&ko, "한국어: 한글 텍스트입니다. 줄 바꿈을 확인합니다.", 40, y + 10, W - 80, 200, 44, TXT_REGULAR, false, false);
	text(&ui, "status line, italic 30 px - page 3 of 7 - flick left/right to turn pages", 30, H - 60, W - 60, 40, 30, TXT_ITALIC,
	     false, false);
	refresh("text", 0, 0, 0, 0, DISP_WFM_GC16, false, 1);
	pause_step();

	/* 4: one box per waveform, each refreshed on its own */
	fill(0, 130, W, H - 130, 0xFF);
	title("waveforms: each box refreshed with its own mode");
	refresh("clear", 0, 0, 0, 0, DISP_WFM_GC16, true, 1);
	static const int modes[] = { DISP_WFM_DU, DISP_WFM_GC16, DISP_WFM_GL16, DISP_WFM_REAGL, DISP_WFM_A2, DISP_WFM_DU4,
				     DISP_WFM_AUTO };
	int bw = (W - 120) / 2, bh = 250;
	for (size_t i = 0; i < sizeof modes / sizeof modes[0] + 1; i++) {
		int bx = 40 + (int)(i % 2) * (bw + 40), by = 170 + (int)(i / 2) * (bh + 30);
		int m = i < sizeof modes / sizeof modes[0] ? modes[i] : DISP_WFM_GC16;
		bool fl = i == sizeof modes / sizeof modes[0];
		fill(bx, by, bw, bh, 0x00);
		fill(bx + 8, by + 8, bw - 16, bh - 16, 0xFF);
		for (int g = 0; g < 8; g++) fill(bx + 30 + g * ((bw - 60) / 8), by + 150, (bw - 60) / 8, 70, (uint8_t)(g * 36));
		char b[64];
		snprintf(b, sizeof b, "%s%s", wfm_name(m), fl ? " full (flash)" : "");
		text(&ui, b, bx, by + 40, bw, 80, 60, TXT_BOLD, true, false);
		refresh("box", bx, by, bw, bh, m, fl, 1);
		pause_step();
	}
	/* the driver must refuse an unknown waveform: proves it reads our struct (expected: an error) */
	refresh("invalid waveform 99 (expect an error)", 40, 170, 100, 100, 99, false, 0);

	/* 5: ink-like DU run: many small refreshes without waiting, as the pen does */
	fill(0, 130, W, H - 130, 0xFF);
	title("ink: 400 small DU refreshes along a curve, no waiting");
	refresh("clear", 0, 0, 0, 0, DISP_WFM_GC16, false, 1);
	double t0 = now_ms(), worst = 0;
	int bad = 0;
	for (int i = 0; i < 400; i++) {
		int x = 100 + i * (W - 200) / 400, yy = 1200 + (int)(400 * sin(i / 25.0));
		fill(x, yy, 8, 8, 0x00);
		double a = now_ms();
		DispRefresh du = { .waveform = DISP_WFM_DU };
		if (disp_refresh(&D, (uint32_t)yy, (uint32_t)x, 8, 8, &du) != 0) bad++;
		double b = now_ms() - a;
		if (b > worst) worst = b;
	}
	double t1 = now_ms();
	int wrc = disp_wait(&D);
	logln("step %d ink: 400 DU refreshes, %d failed, %.3f ms mean / %.2f ms worst to submit, last finished %.0f ms later (wait rc=%d)",
	      step_no, bad, (t1 - t0) / 400, worst, now_ms() - t1, wrc);
	if (bad) fails++;
	pause_step();

	/* 6: PNG pictures, from a file and from memory */
	fill(0, 130, W, H - 130, 0xFF);
	title("PNG: from a file (left) and from memory (right)");
	enum { PW = 800, PH = 900 };
	unsigned char *px = malloc(PW * PH);
	for (int yy = 0; yy < PH; yy++)
		for (int x = 0; x < PW; x++) px[yy * PW + x] = (unsigned char)((x / 50 + yy / 50) % 2 ? x * 255 / PW : 255 - yy * 255 / PH);
	int len = 0;
	unsigned char *png = px ? stbi_write_png_to_mem(px, PW, PW, PH, 1, &len) : NULL;
	const char *tmp = "/tmp/displaytest.png";
	FILE *pf = png ? fopen(tmp, "wb") : NULL;
	if (pf) {
		fwrite(png, 1, (size_t)len, pf);
		fclose(pf);
	}
	DispRect r1, r2;
	int a = disp_image_file(&D, tmp, 60, 200, &r1), b = png ? disp_image_mem(&D, png, (size_t)len, 960, 200, &r2) : -1;
	logln("step %d png: file rc=%d (%dx%d), memory rc=%d", step_no, a, r1.width, r1.height, b);
	if (a || b) fails++;
	unlink(tmp);
	free(px);
	if (png) STBIW_FREE(png);
	refresh("png", 0, 0, 0, 0, DISP_WFM_GC16, false, 1);
	pause_step();

	/* 7: done: summary, then the previous screen back */
	fill(0, 130, W, H - 130, 0xFF);
	title(fails ? "done - something failed, see the log" : "done - every ioctl answered as expected");
	char sum[120];
	snprintf(sum, sizeof sum, "%d steps, %d unexpected results. The previous screen comes back next.", step_no, fails);
	text(&ui, sum, 40, 200, W - 80, 200, 44, TXT_REGULAR, false, false);
	refresh("summary", 0, 0, 0, 0, DISP_WFM_GC16, true, 1);
	pause_step();
	if (saved) {
		memcpy(D.fb, saved, D.fbsize);
		refresh("restore", 0, 0, 0, 0, DISP_WFM_GC16, true, 1);
	}
	logln("displaytest: %d unexpected results", fails);
	disp_font_free(&ui);
	disp_font_free(&serif);
	disp_font_free(&ja);
	disp_font_free(&zh);
	disp_font_free(&ko);
	disp_close(&D);
	free(saved);
	return fails ? 1 : 0;
}

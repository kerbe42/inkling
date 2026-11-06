/* fbcap: record the Kindle screen for demo videos. Developer tool, not part of the release.
 *   fbcap SECONDS FPS OUTDIR          -> OUTDIR/f_<ms>.png (930x1240 gray: every other row, pixel pairs averaged)
 *   fbcap SECONDS FPS OUTDIR delta    -> OUTDIR/frames.bin: only the rows that changed since the previous frame, raw
 *                                        (much cheaper than PNG, so higher frame rates; make_demo2.py decodes it)
 * frames.bin: "FBC1", u16 width, u16 height; then per frame u32 ms since the start, u16 rows, and per row u16 y plus
 * width bytes (little-endian). SIGTERM/SIGINT end the recording cleanly.
 * Reading the framebuffer is slow (uncached memory: about 150 ms for the whole screen on the Scribe), so only every
 * other row is read, a row at a time with memcpy (about 75 ms a frame); delta mode then manages about 12 fps.
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -O2 -static -I device/app/vendor -o build/fbcap
 *        device/tools/fbcap.c -lm */
#include <fcntl.h>
#include <linux/fb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static volatile sig_atomic_t stop;
static void on_signal(int s) { (void)s; stop = 1; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static void put16(FILE *o, unsigned v) { unsigned char b[2] = { v & 255, v >> 8 }; fwrite(b, 1, 2, o); }
static void put32(FILE *o, uint32_t v) { unsigned char b[4] = { v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24 }; fwrite(b, 1, 4, o); }

int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: fbcap SECONDS FPS OUTDIR [delta]\n"); return 2; }
	double secs = atof(argv[1]), fps = atof(argv[2]);
	int delta = argc > 4 && !strcmp(argv[4], "delta");
	int fd = open("/dev/fb0", O_RDONLY);
	struct fb_var_screeninfo v; struct fb_fix_screeninfo f;
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &v) || ioctl(fd, FBIOGET_FSCREENINFO, &f)) { perror("fb"); return 1; }
	unsigned char *fb = mmap(NULL, f.smem_len, PROT_READ, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) { perror("mmap"); return 1; }
	int W = (int)v.xres / 2, H = (int)v.yres / 2, stride = (int)f.line_length;
	unsigned char *row = malloc((size_t)stride), *img = malloc((size_t)W * H), *prev = calloc((size_t)W * H, 1);
	if (!row || !img || !prev) return 1;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	FILE *out = NULL;
	if (delta) {
		char path[256];
		snprintf(path, sizeof path, "%s/frames.bin", argv[3]);
		if (!(out = fopen(path, "wb"))) { perror(path); return 1; }
		setvbuf(out, NULL, _IOFBF, 1 << 20);
		fwrite("FBC1", 1, 4, out); put16(out, (unsigned)W); put16(out, (unsigned)H);
	}
	stbi_write_png_compression_level = 1;
	double t0 = now_ms(), next = t0;
	int n = 0, first = 1;
	while (!stop && now_ms() - t0 < secs * 1000) {
		double t = now_ms();
		if (t < next) { usleep((useconds_t)((next - t) * 1000)); continue; }
		next += 1000 / fps;
		if (next < t) next = t; /* fell behind: don't try to catch up */
		ioctl(fd, FBIOGET_VSCREENINFO, &v); /* the app may pan between buffers */
		const unsigned char *base = fb + (size_t)v.yoffset * stride;
		for (int y = 0; y < H; y++) {
			memcpy(row, base + (size_t)(2 * y) * stride, (size_t)stride);
			unsigned char *o = img + (size_t)y * W;
			for (int x = 0; x < W; x++) o[x] = (unsigned char)((row[2 * x] + row[2 * x + 1] + 1) / 2);
		}
		if (delta) {
			int rows = 0;
			for (int y = 0; y < H; y++) rows += first || memcmp(img + (size_t)y * W, prev + (size_t)y * W, (size_t)W) != 0;
			put32(out, (uint32_t)(t - t0)); put16(out, (unsigned)rows);
			for (int y = 0; y < H; y++)
				if (first || memcmp(img + (size_t)y * W, prev + (size_t)y * W, (size_t)W)) { put16(out, (unsigned)y); fwrite(img + (size_t)y * W, 1, (size_t)W, out); }
			fflush(out);
			unsigned char *sw = prev; prev = img; img = sw;
			first = 0;
		} else {
			char path[256];
			snprintf(path, sizeof path, "%s/f_%08d.png", argv[3], (int)(t - t0));
			stbi_write_png(path, W, H, 1, img, W);
		}
		n++;
	}
	if (out) fclose(out);
	fprintf(stderr, "fbcap: %d frames in %.1f s\n", n, (now_ms() - t0) / 1000);
	return 0;
}

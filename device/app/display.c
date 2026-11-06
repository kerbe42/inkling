/* Inkling's display layer (see display.h). Written from scratch for this app; no code from other display libraries.
 *
 * Kernel interface provenance (e-ink refresh). The Kindle Scribe (2022) runs Linux 4.9.77-lab126 with MediaTek's
 * hwtcon_v2 e-ink driver. Its interface is defined in the kernel sources Amazon publishes for this device on its
 * "Source Code Notice for Kindle E-Readers" page: Kindle_src_5.19.6_4832190042.tar.gz (Kindle Scribe - 2022 Release,
 * firmware 5.19.6; sha256 5fd2df0ee6cd445b66de0392fdc59fa4b370c204745598fd2ebdab0ee6e8d744), inside it
 * gplrelease/linux-4.9.tar.gz:
 *   - include/linux/hwtcon_ioctl_cmd.h: struct mxcfb_rect, struct mxcfb_update_data (96 bytes on ARM),
 *     struct mxcfb_update_marker_data, MXCFB_SEND_UPDATE = _IOW('F', 0x2E, struct mxcfb_update_data),
 *     MXCFB_WAIT_FOR_UPDATE_COMPLETE = _IOWR('F', 0x2F, struct mxcfb_update_marker_data), enum WAVEFORM_MODE_ENUM
 *     (DU 1, GC16 2, GL16 3, GLR16 4, A2 6, DU4 7, AUTO 257), UPDATE_MODE_PARTIAL 0 / UPDATE_MODE_FULL 1,
 *     TEMP_USE_AMBIENT 0x1000, EPDC_FLAG_FORCE_MONOCHROME 0x02;
 *   - drivers/misc/mediatek/hwtcon_v2/hwtcon_fb.c: what SEND_UPDATE accepts (update_mode 0 or 1, a region inside
 *     the panel in framebuffer coordinates - the driver applies the panel rotation itself -, a waveform from the
 *     list above; GC16 without a full update becomes GC16_PARTIAL), that WAIT_FOR_UPDATE_COMPLETE reads only the
 *     marker (first u32) and times out after 5 s, and that the driver never pans (pan_display is a no-op).
 * The structs below are our own declarations of that binary layout (field order and sizes), checked by the static
 * asserts; ioctl numbers and struct layouts are interface facts. Framebuffer geometry comes from the standard Linux
 * UAPI <linux/fb.h> (FBIOGET_VSCREENINFO / FBIOGET_FSCREENINFO).
 *
 * Text: stb_truetype (public domain / MIT, vendor/stb_truetype.h). Pictures: stb_image (same, vendor/stb_image.h).
 */
#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* the libraries: only what this file uses is compiled in (static), unused helpers are not warned about */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS DISP_IMAGE_MAX_SIDE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#pragma GCC diagnostic pop

/* ---- hwtcon_v2 refresh interface (layout of include/linux/hwtcon_ioctl_cmd.h, see the top of this file) ---- */

typedef struct {
	uint32_t top, left, width, height;
} HwRect;

typedef struct {
	HwRect region;          /* in framebuffer coordinates */
	uint32_t waveform_mode; /* DISP_WFM_* */
	uint32_t update_mode;   /* HW_UPDATE_PARTIAL / HW_UPDATE_FULL */
	uint32_t update_marker; /* our id for the update, for the wait ioctl */
	int32_t temp;           /* HW_TEMP_AMBIENT */
	uint32_t flags;
	int32_t dither_mode;    /* 0: none */
	int32_t quant_bit;
	struct {
		uint32_t phys_addr, width, height;
		HwRect region;
	} alt_buffer;           /* unused (no alternate-buffer flag) */
	struct {
		uint32_t direction, steps;
	} swipe;                /* unused (no swipe flag) */
	uint32_t hist_bw_waveform_mode, hist_gray_waveform_mode, ts_pxp, ts_epdc;
} HwUpdate;

typedef struct {
	uint32_t update_marker, collision_test;
} HwMarker;

#define HW_UPDATE_PARTIAL 0u
#define HW_UPDATE_FULL 1u
#define HW_TEMP_AMBIENT 0x1000
#define HW_FLAG_FORCE_MONOCHROME 0x02u
#define HW_SEND_UPDATE _IOW('F', 0x2E, HwUpdate)
#define HW_WAIT_FOR_UPDATE_COMPLETE _IOWR('F', 0x2F, HwMarker)

_Static_assert(sizeof(HwRect) == 16, "mxcfb_rect layout");
_Static_assert(sizeof(HwUpdate) == 96, "mxcfb_update_data layout");
_Static_assert(offsetof(HwUpdate, waveform_mode) == 16 && offsetof(HwUpdate, update_marker) == 24 &&
		       offsetof(HwUpdate, flags) == 32 && offsetof(HwUpdate, alt_buffer) == 44 &&
		       offsetof(HwUpdate, swipe) == 72 && offsetof(HwUpdate, hist_bw_waveform_mode) == 80,
	       "mxcfb_update_data field offsets");
_Static_assert(HW_SEND_UPDATE == 0x4060462Eu, "MXCFB_SEND_UPDATE number");
_Static_assert(HW_WAIT_FOR_UPDATE_COMPLETE == 0xC008462Fu, "MXCFB_WAIT_FOR_UPDATE_COMPLETE number");

/* ---- framebuffer ---- */

int disp_open(Display *d, const char *path) {
	memset(d, 0, sizeof *d);
	d->fd = open(path ? path : "/dev/fb0", O_RDWR | O_CLOEXEC);
	if (d->fd < 0) {
		fprintf(stderr, "display: open %s: %s\n", path ? path : "/dev/fb0", strerror(errno));
		return -1;
	}
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	if (ioctl(d->fd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(d->fd, FBIOGET_FSCREENINFO, &f) < 0) {
		fprintf(stderr, "display: screen info: %s\n", strerror(errno));
		goto fail;
	}
	if (v.bits_per_pixel != 8 || f.line_length < v.xres || !v.xres || !v.yres) {
		fprintf(stderr, "display: unsupported %ux%u %u bpp, line %u\n", v.xres, v.yres, v.bits_per_pixel, f.line_length);
		goto fail;
	}
	d->width = (int)v.xres;
	d->height = (int)v.yres;
	d->stride = (int)f.line_length;
	d->yoffset = (int)v.yoffset;
	d->fbsize = f.smem_len;
	if ((size_t)(d->yoffset + d->height) * (size_t)d->stride > d->fbsize) d->yoffset = 0;
	if ((size_t)d->height * (size_t)d->stride > d->fbsize) {
		fprintf(stderr, "display: buffer %zu too small\n", d->fbsize);
		goto fail;
	}
	void *m = mmap(NULL, d->fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, 0);
	if (m == MAP_FAILED) {
		fprintf(stderr, "display: mmap: %s\n", strerror(errno));
		goto fail;
	}
	d->fb = m;
	return 0;
fail:
	close(d->fd);
	d->fd = -1;
	return -1;
}

int disp_open_mem(Display *d, int width, int height, int stride) {
	memset(d, 0, sizeof *d);
	d->fd = -1;
	d->width = width;
	d->height = height;
	d->stride = stride;
	d->fbsize = (size_t)stride * (size_t)height;
	d->fb = malloc(d->fbsize);
	if (!d->fb) return -1;
	memset(d->fb, 0xFF, d->fbsize);
	return 0;
}

void disp_close(Display *d) {
	if (!d->fb) return;
	if (d->fd >= 0) {
		munmap(d->fb, d->fbsize);
		close(d->fd);
	} else free(d->fb);
	d->fb = NULL;
	d->fd = -1;
}

/* Clip x, y, w, h to the screen; 0 if nothing is left. */
static int clip(const Display *d, int *x, int *y, int *w, int *h) {
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > d->width) *w = d->width - *x;
	if (*y + *h > d->height) *h = d->height - *y;
	return *w > 0 && *h > 0;
}

void disp_fill_rect(Display *d, const DispRect *r, uint8_t gray) {
	int x = r->left, y = r->top, w = r->width, h = r->height;
	if (!d->fb || !clip(d, &x, &y, &w, &h)) return;
	for (int yy = y; yy < y + h; yy++) memset(disp_px(d, x, yy), gray, (size_t)w);
}

int disp_refresh(Display *d, uint32_t top, uint32_t left, uint32_t width, uint32_t height, const DispRefresh *how) {
	/* clipped as ints: a negative top or left from a caller's arithmetic is clipped, not taken as a huge number */
	int x = (int)left, y = (int)top, w = (int)width, h = (int)height;
	if (!width || !height) x = 0, y = 0, w = d->width, h = d->height;
	if (!clip(d, &x, &y, &w, &h)) return 0;
	if (d->on_refresh) d->on_refresh(d->ctx, y, x, w, h, how);
	if (d->fd < 0) return d->last_rc = 0;
	HwUpdate u;
	memset(&u, 0, sizeof u);
	u.region.top = (uint32_t)y;
	u.region.left = (uint32_t)x;
	u.region.width = (uint32_t)w;
	u.region.height = (uint32_t)h;
	u.waveform_mode = (uint32_t)how->waveform;
	u.update_mode = how->flash ? HW_UPDATE_FULL : HW_UPDATE_PARTIAL;
	if (++d->marker == 0) d->marker = 1;
	u.update_marker = d->marker;
	u.temp = HW_TEMP_AMBIENT;
	u.flags = how->waveform == DISP_WFM_A2 ? HW_FLAG_FORCE_MONOCHROME : 0;
	u.hist_bw_waveform_mode = DISP_WFM_DU;
	u.hist_gray_waveform_mode = DISP_WFM_GC16;
	int rc = ioctl(d->fd, HW_SEND_UPDATE, &u);
	if (rc < 0) rc = -errno;
	else if (how->wait) rc = disp_wait(d);
	return d->last_rc = rc;
}

int disp_wait(Display *d) {
	if (d->fd < 0 || !d->marker) return 0;
	HwMarker m = { d->marker, 0 };
	int rc = ioctl(d->fd, HW_WAIT_FOR_UPDATE_COMPLETE, &m);
	return rc < 0 ? -errno : rc;
}

/* ---- pictures ---- */

static int blit_gray_alpha(Display *d, const unsigned char *px, int iw, int ih, int x, int y, DispRect *drawn) {
	int cx = x, cy = y, cw = iw, ch = ih;
	if (drawn) *drawn = (DispRect){ 0, 0, 0, 0 };
	if (!clip(d, &cx, &cy, &cw, &ch)) return 0;
	for (int yy = cy; yy < cy + ch; yy++) {
		const unsigned char *s = px + ((size_t)(yy - y) * (size_t)iw + (size_t)(cx - x)) * 2;
		unsigned char *o = disp_px(d, cx, yy);
		for (int xx = 0; xx < cw; xx++, s += 2, o++) {
			unsigned a = s[1];
			if (a == 255) *o = s[0];
			else if (a) *o = (unsigned char)((s[0] * a + *o * (255 - a) + 127) / 255);
		}
	}
	if (drawn) *drawn = (DispRect){ cx, cy, cw, ch };
	return 0;
}

int disp_image_mem(Display *d, const unsigned char *data, size_t len, int x, int y, DispRect *drawn) {
	int w, h, n;
	if (drawn) *drawn = (DispRect){ 0, 0, 0, 0 };
	if (!d->fb || !data || len < 8 || len > DISP_IMAGE_MAX_BYTES) {
		fprintf(stderr, "display: image: %zu bytes refused\n", len);
		return -1;
	}
	/* check the header's size before decoding anything (the decoder allocates width * height * 2 bytes) */
	if (!stbi_info_from_memory(data, (int)len, &w, &h, &n) || w < 1 || h < 1 || w > DISP_IMAGE_MAX_SIDE ||
	    h > DISP_IMAGE_MAX_SIDE) {
		fprintf(stderr, "display: image: not a PNG or too large\n");
		return -1;
	}
	unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 2);
	if (!px) {
		fprintf(stderr, "display: image: %s\n", stbi_failure_reason());
		return -1;
	}
	blit_gray_alpha(d, px, w, h, x, y, drawn);
	stbi_image_free(px);
	return 0;
}

int disp_image_file(Display *d, const char *path, int x, int y, DispRect *drawn) {
	if (drawn) *drawn = (DispRect){ 0, 0, 0, 0 };
	if (!d->fb) return -1;
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 8 || st.st_size > DISP_IMAGE_MAX_BYTES) {
		fprintf(stderr, "display: image %s: missing, not a file or too large\n", path);
		if (fd >= 0) close(fd);
		return -1;
	}
	size_t len = (size_t)st.st_size, got = 0;
	unsigned char *buf = malloc(len);
	while (buf && got < len) {
		ssize_t r = read(fd, buf + got, len - got);
		if (r <= 0) break;
		got += (size_t)r;
	}
	close(fd);
	int rc = buf && got == len ? disp_image_mem(d, buf, len, x, y, drawn) : -1;
	free(buf);
	return rc;
}

/* ---- fonts ---- */

struct DispFace {
	stbtt_fontinfo info;
	unsigned char *data;
	size_t size;
	int ascent, descent, linegap; /* font units */
};

int disp_font_add(DispFonts *f, const char *path, int style) {
	if (style < 0 || style > 3) return -1;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	struct stat st;
	if (fstat(fd, &st) < 0 || st.st_size < 12) {
		close(fd);
		return -1;
	}
	void *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return -1;
	DispFace *face = calloc(1, sizeof *face);
	int off = stbtt_GetFontOffsetForIndex(m, 0);
	if (!face || off < 0 || !stbtt_InitFont(&face->info, m, off)) {
		free(face);
		munmap(m, (size_t)st.st_size);
		return -1;
	}
	face->data = m;
	face->size = (size_t)st.st_size;
	stbtt_GetFontVMetrics(&face->info, &face->ascent, &face->descent, &face->linegap);
	if (face->ascent - face->descent <= 0) face->ascent = 800, face->descent = -200; /* broken metrics */
	if (f->face[style]) {
		munmap(f->face[style]->data, f->face[style]->size);
		free(f->face[style]);
	}
	f->face[style] = face;
	return 0;
}

void disp_font_free(DispFonts *f) {
	for (int i = 0; i < 4; i++) {
		if (!f->face[i]) continue;
		munmap(f->face[i]->data, f->face[i]->size);
		free(f->face[i]);
		f->face[i] = NULL;
	}
}

static const DispFace *face_for(const DispFonts *f, int style) {
	style &= 3;
	if (f->face[style]) return f->face[style];
	if (style == TXT_BOLD_ITALIC && f->face[TXT_BOLD]) return f->face[TXT_BOLD];
	for (int i = 0; i < 4; i++)
		if (f->face[i]) return f->face[i];
	return NULL;
}

static float face_scale(const DispFace *fc, int px) {
	return (float)px / (float)(fc->ascent - fc->descent);
}

/* Line pitch: ascent + descent + line gap, rounded up (with a little slack for float error). */
static int line_height(const DispFace *fc, int px) {
	return (int)ceilf((float)(fc->ascent - fc->descent + fc->linegap) * face_scale(fc, px) - 0.001f);
}

int disp_line_height(const DispFonts *f, int style, int px) {
	const DispFace *fc = face_for(f, style);
	return fc ? line_height(fc, px) : px;
}

/* ---- text layout ---- */

typedef struct {
	uint32_t cp;
	uint8_t style;
	int glyph;
	int adv;    /* whole-pixel advance, kerning with the next character included */
	int x0, x1; /* the glyph's ink from its pen position */
} Ch;

static uint32_t utf8_next(const unsigned char **pp) {
	const unsigned char *p = *pp;
	uint32_t c = *p++;
	int n = c >= 0xF0 && c < 0xF5 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : c < 0x80 ? 0 : -1;
	if (c >= 0xF5) n = -1;
	if (n < 0) {
		*pp = p;
		return 0xFFFD;
	}
	c &= n == 3 ? 0x07 : n == 2 ? 0x0F : n == 1 ? 0x1F : 0x7F;
	for (int i = 0; i < n; i++, p++) {
		if ((*p & 0xC0) != 0x80) {
			*pp = p;
			return 0xFFFD;
		}
		c = c << 6 | (*p & 0x3F);
	}
	*pp = p;
	if ((n == 2 && c < 0x800) || (n == 3 && (c < 0x10000 || c > 0x10FFFF)) || (c >= 0xD800 && c <= 0xDFFF)) return 0xFFFD;
	return c;
}

static int is_alnum_cp(uint32_t c) {
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0;
}

/* Decode s; with formatted, * and _ runs toggle italic (1), bold (2) or both (3+) and are not kept. An _ inside a
 * word (snake_case) stays text. Returns the number of characters. */
static int decode(const char *s, int style, bool formatted, Ch *out) {
	const unsigned char *p = (const unsigned char *)s;
	int n = 0, st = style & 3;
	uint32_t prev = 0;
	while (*p) {
		if (formatted && (*p == '*' || *p == '_')) {
			unsigned char m = *p;
			int run = 0;
			while (p[run] == m) run++;
			const unsigned char *q = p + run;
			uint32_t next = *q ? utf8_next(&q) : 0;
			if (m == '*' || !is_alnum_cp(prev) || !is_alnum_cp(next)) {
				st ^= run == 1 ? TXT_ITALIC : run == 2 ? TXT_BOLD : TXT_BOLD_ITALIC;
				p += run;
				continue;
			}
		}
		uint32_t c = utf8_next(&p);
		if (c == '\r') continue;
		if (c == '\t') c = ' ';
		out[n].cp = c;
		out[n].style = (uint8_t)st;
		n++;
		prev = c;
	}
	return n;
}

static int is_space(uint32_t c) { return c == ' ' || c == 0x3000 || c == 0xA0; }

static int is_cjk(uint32_t c) {
	return (c >= 0x2E80 && c <= 0x2FDF) || (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3100 && c <= 0x31BF) ||
	       (c >= 0x31F0 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
	       (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x3FFFF);
}

/* No line may start with these: closing punctuation, CJK closers, small kana, prolonged sound and iteration marks. */
static int no_break_before(uint32_t c) {
	static const uint32_t L[] = { ')', ']', '}', '>', ',', '.', ';', ':', '!', '?', '%', '\'', '"', 0x2019, 0x201D,
				      0x2026, 0x00BB, 0x203A, 0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F,
				      0xFF01, 0xFF09, 0xFF3D, 0xFF5D, 0x300D, 0x300F, 0x3011, 0x3015, 0x3009, 0x300B,
				      0x3017, 0x3019, 0x301F, 0x30FB, 0x30FC, 0x3005, 0x309D, 0x309E, 0x30FD, 0x30FE,
				      0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E,
				      0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,
				      0x30F5, 0x30F6, 0xFF61, 0xFF64, 0xFF63 };
	for (size_t i = 0; i < sizeof L / sizeof L[0]; i++)
		if (L[i] == c) return 1;
	return 0;
}

/* No line may end with these: opening punctuation. */
static int no_break_after(uint32_t c) {
	static const uint32_t L[] = { '(', '[', '{', '<', 0x2018, 0x201C, 0x00AB, 0x2039, 0xFF08, 0xFF3B, 0xFF5B,
				      0x300C, 0x300E, 0x3010, 0x3014, 0x3008, 0x300A, 0x3016, 0x3018, 0x301D, 0xFF62 };
	for (size_t i = 0; i < sizeof L / sizeof L[0]; i++)
		if (L[i] == c) return 1;
	return 0;
}

static int is_digit(uint32_t c) { return c >= '0' && c <= '9'; }

/* May a line start at c[i] (i > 0)? */
static int can_break(const Ch *c, int i) {
	uint32_t a = c[i - 1].cp, b = c[i].cp;
	if (is_space(b)) return 0;
	if (no_break_before(b) || no_break_after(a)) return 0;
	if (is_space(a)) return 1;
	if (a == 0x2014 || b == 0x2014) return 1; /* em dash */
	if ((a == '-' || a == 0x2010 || a == 0x2013) && i > 1 && !is_space(c[i - 2].cp) && !is_digit(b) && b != '-') return 1;
	if (a == '/' && !is_digit(b) && b != '/') return 1;
	if (is_cjk(a) || is_cjk(b)) return 1;
	return 0;
}

typedef struct {
	int start, end; /* characters [start, end) */
	int width;      /* to the right edge of the last glyph's ink (trailing spaces excluded) */
} Line;

static int span_width(const Ch *c, int start, int end) {
	while (end > start && (is_space(c[end - 1].cp) || c[end - 1].cp == '\n')) end--;
	int x = start < end && c[start].x0 < 0 ? -c[start].x0 : 0; /* ink that starts left of the pen moves the line right */
	for (int i = start; i < end - 1; i++) x += c[i].adv;
	return end > start ? x + c[end - 1].x1 : 0;
}

/* Greedy line breaking: a line ends at the last break opportunity before the first glyph whose ink would cross the
 * box's right edge (or at that glyph, inside a word longer than the box). Returns the number of lines; lines may be
 * NULL to count only. */
static int break_lines(const Ch *c, int n, int maxw, Line *lines) {
	int nl = 0, start = 0;
	for (;;) {
		int i = start, brk = -1, end = -1, next = -1, x = start < n && c[start].x0 < 0 ? -c[start].x0 : 0;
		for (; i < n; i++) {
			if (c[i].cp == '\n') {
				end = i;
				next = i + 1;
				break;
			}
			if (i > start && can_break(c, i)) brk = i;
			if (i > start && !is_space(c[i].cp) && x + c[i].x1 > maxw) {
				end = next = brk > start ? brk : i;
				break;
			}
			x += c[i].adv;
		}
		if (end < 0) end = next = n;
		if (lines) {
			lines[nl].start = start;
			lines[nl].end = end;
			lines[nl].width = span_width(c, start, end);
		}
		nl++;
		if (next >= n) break; /* (a text ending in a newline has no empty last line) */
		start = next;
	}
	return nl;
}

static void draw_glyph(Display *d, const stbtt_fontinfo *info, int glyph, float scale, int x, int baseline, uint8_t ink,
		       unsigned char **buf, size_t *bufsize) {
	int x0, y0, x1, y1;
	stbtt_GetGlyphBitmapBox(info, glyph, scale, scale, &x0, &y0, &x1, &y1);
	int w = x1 - x0, h = y1 - y0;
	if (w <= 0 || h <= 0) return;
	size_t need = (size_t)w * (size_t)h;
	if (need > *bufsize) {
		unsigned char *nb = realloc(*buf, need);
		if (!nb) return;
		*buf = nb;
		*bufsize = need;
	}
	stbtt_MakeGlyphBitmap(info, *buf, w, h, w, scale, scale, glyph);
	for (int yy = 0; yy < h; yy++) {
		int sy = baseline + y0 + yy;
		if (sy < 0 || sy >= d->height) continue;
		const unsigned char *row = *buf + (size_t)yy * (size_t)w;
		for (int xx = 0; xx < w; xx++) {
			int sx = x + x0 + xx;
			unsigned a = row[xx];
			if (!a || sx < 0 || sx >= d->width) continue;
			unsigned char *o = disp_px(d, sx, sy);
			*o = a == 255 ? ink : (unsigned char)((ink * a + *o * (255 - a) + 127) / 255);
		}
	}
}

int disp_text(Display *d, const DispFonts *f, const char *s, const DispText *t, DispTextFit *fit) {
	if (fit) memset(fit, 0, sizeof *fit);
	const DispFace *base = f ? face_for(f, t->style) : NULL;
	if (!base || !s || !*s || t->px <= 0) return -1;
	size_t len = strlen(s);
	Ch *c = malloc((len + 1) * sizeof *c);
	Line *lines = NULL;
	if (!c) return -1;
	int n = decode(s, t->style, t->formatted, c);
	/* glyphs, whole-pixel advances (kerning included) and ink extents */
	for (int i = 0; i < n; i++) {
		const DispFace *fc = face_for(f, c[i].style);
		float sc = face_scale(fc, t->px);
		c[i].glyph = c[i].adv = c[i].x0 = c[i].x1 = 0;
		if (c[i].cp == '\n') continue;
		c[i].glyph = stbtt_FindGlyphIndex(&fc->info, (int)c[i].cp);
		int aw = 0, lsb = 0, y0, y1;
		stbtt_GetGlyphHMetrics(&fc->info, c[i].glyph, &aw, &lsb);
		c[i].adv = (int)lroundf((float)aw * sc);
		stbtt_GetGlyphBitmapBox(&fc->info, c[i].glyph, sc, sc, &c[i].x0, &y0, &c[i].x1, &y1);
		if (i > 0 && c[i - 1].style == c[i].style && c[i - 1].cp != '\n')
			c[i - 1].adv += (int)lroundf((float)stbtt_GetGlyphKernAdvance(&fc->info, c[i - 1].glyph, c[i].glyph) * sc);
	}
	int maxw = t->right - t->left;
	if (maxw < 1) maxw = 1;
	int nl = break_lines(c, n, maxw, NULL);
	lines = malloc((size_t)nl * sizeof *lines);
	if (!lines) {
		free(c);
		return -1;
	}
	break_lines(c, n, maxw, lines);
	float bsc = face_scale(base, t->px);
	int lh = line_height(base, t->px);
	int asc = (int)ceilf((float)base->ascent * bsc - 0.001f);
	int widest = 0;
	for (int i = 0; i < nl; i++)
		if (lines[i].width > widest) widest = lines[i].width;
	int fits = nl;
	while (fits > 0 && t->top + fits * lh > t->bottom) fits--;
	if (fit) {
		fit->lines = nl;
		fit->fitted = fits;
		fit->width = widest;
		fit->height = fits ? fits * lh - (int)ceilf((float)base->linegap * bsc / 2 - 0.001f) : 0; /* (half a gap below) */
		fit->truncated = fits < nl;
	}
	if (!t->measure_only && d && d->fb) {
		uint8_t ink = t->inverted ? (uint8_t)(255 - t->fg) : t->fg;
		unsigned char *buf = NULL;
		size_t bufsize = 0;
		for (int li = 0; li < fits; li++) {
			const Line *L = &lines[li];
			int x = t->left + (t->centered && L->width < maxw ? (maxw - L->width) / 2 : 0);
			if (L->start < L->end && c[L->start].x0 < 0) x -= c[L->start].x0;
			int baseline = t->top + li * lh + asc;
			for (int i = L->start; i < L->end; i++) {
				const DispFace *fc = face_for(f, c[i].style);
				if (!is_space(c[i].cp) && c[i].cp != '\n')
					draw_glyph(d, &fc->info, c[i].glyph, face_scale(fc, t->px), x, baseline, ink, &buf, &bufsize);
				x += c[i].adv;
			}
		}
		free(buf);
		if (fit) fit->rendered = fits;
	}
	free(lines);
	free(c);
	return t->top + fits * lh;
}

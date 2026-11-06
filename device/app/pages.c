/* Pages: each page's canvas is kept as a PNG in the notebook folder on the USB storage
 * (notebooks/<notebook>/page-0001.png ...), saved when you leave it and restored when you come back. The conversation
 * history belongs to the notebook, so it carries across pages. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <pthread.h>

#include "scribe.h"
#include "stb_image_write.h" /* implementation lives in brain.c */

static void page_path(App *a, int n, char *p, size_t len) {
	snprintf(p, len, USB_DIR "/notebooks/%s/page-%04d.png", a->notebook, n);
}

static void file_cb(void *ctx, void *data, int size) { fwrite(data, 1, (size_t)size, (FILE *)ctx); }

/* Lowest row of the canvas with ink on it (or the canvas top when blank). */
static int scan_ink_bottom(App *a) {
	for (int y = a->cy + a->ch - 1; y >= a->cy; y--) {
		const unsigned char *row = a->fb + (size_t)(a->pan_y + y) * (size_t)a->stride + (size_t)a->cx;
		for (int x = 0; x < a->cw; x += 2)
			if (row[x] < 160) return y;
	}
	return a->cy;
}

int page_is_blank(App *a) { return scan_ink_bottom(a) <= a->cy; }
int page_ink_bottom(App *a) { return scan_ink_bottom(a); }

/* Saving a page costs ~140 ms to copy it out of framebuffer memory and ~270 ms to encode the PNG (measured on the
 * Kindle), so the encoding (and for the idle autosave, the copy too) runs on a second thread: the pen never waits.
 * Anything that deletes or reloads a page file waits for a save in progress first (page_save_wait). */
typedef struct {
	unsigned char *px;     /* the canvas, or NULL: copy it from the framebuffer first */
	const unsigned char *fb;
	int w, h, stride, y0, x0;
	char path[256];
} SaveJob;

static pthread_t sv_th;
static int sv_running;

static int px_blank(const unsigned char *px, int w, int h) {
	for (int y = 0; y < h; y += 2)
		for (int x = 0; x < w; x += 2)
			if (px[(size_t)y * (size_t)w + (size_t)x] < 160) return 0;
	return 1;
}

static unsigned char *copy_canvas(const unsigned char *fb, int stride, int x0, int y0, int w, int h) {
	unsigned char *px = malloc((size_t)w * (size_t)h);
	if (!px) return NULL;
	for (int y = 0; y < h; y++) memcpy(px + (size_t)y * (size_t)w, fb + (size_t)(y0 + y) * (size_t)stride + (size_t)x0, (size_t)w);
	return px;
}

static void *save_thread(void *arg) {
	SaveJob *j = arg;
	if (!j->px) j->px = copy_canvas(j->fb, j->stride, j->x0, j->y0, j->w, j->h);
	if (j->px) {
		if (px_blank(j->px, j->w, j->h)) unlink(j->path);
		else {
			char tmp[280];
			snprintf(tmp, sizeof tmp, "%s.tmp", j->path);
			FILE *f = fopen(tmp, "wb");
			if (f) {
				int ok = stbi_write_png_to_func(file_cb, f, j->w, j->h, 1, j->px, j->w);
				if (fclose(f) != 0) ok = 0;
				if (ok) rename(tmp, j->path);
				else unlink(tmp);
			}
		}
	}
	free(j->px);
	free(j);
	return NULL;
}

void page_save_wait(void) {
	if (sv_running) { pthread_join(sv_th, NULL); sv_running = 0; }
}

static void save(App *a, int copy_now) {
	if (a->in_menu || a->in_settings || a->in_games || a->in_pagemenu || a->in_modes) return; /* the canvas isn't the page */
	page_save_wait();
	char dir[200];
	snprintf(dir, sizeof dir, USB_DIR "/notebooks/%s", a->notebook);
	mkdir(dir, 0755);
	SaveJob *j = calloc(1, sizeof *j);
	if (!j) return;
	page_path(a, a->page, j->path, sizeof j->path);
	j->fb = a->fb; j->stride = a->stride; j->x0 = a->cx; j->y0 = a->pan_y + a->cy; j->w = a->cw; j->h = a->ch;
	if (copy_now) j->px = copy_canvas(j->fb, j->stride, j->x0, j->y0, j->w, j->h); /* before anything draws over it */
	a->page_dirty = 0;
	stbi_write_png_compression_level = 3; /* fast enough on the Kindle, still small for mostly-white pages */
	if (pthread_create(&sv_th, NULL, save_thread, j) == 0) sv_running = 1;
	else save_thread(j); /* no thread: do it here */
}

/* Saves the page now (the canvas is copied before returning, so it may be drawn over straight away). */
void page_save(App *a) { save(a, 1); }

/* Idle autosave: copy and encode both happen off the main thread. */
void page_autosave(App *a) { save(a, 0); }

/* Clears the canvas and draws page n's saved image, if any. */
void page_load(App *a, int n) {
	page_save_wait();
	char path[256];
	page_path(a, n, path, sizeof path);
	DispRect rc = { a->cx, a->cy, a->cw, a->ch };
	disp_fill_rect(&a->disp, &rc, 0xFF);
	if (access(path, R_OK) == 0 && disp_image_file(&a->disp, path, a->cx, a->cy, NULL) < 0)
		logf_("could not draw %s", path);
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, &a->flash);
	a->ink_bottom = scan_ink_bottom(a);
}

/* Wipes the page (Clear button, Menu > Page > Clear this page); the conversation is kept. */
void page_clear(App *a) {
	char path[256];
	page_save_wait();
	page_path(a, a->page, path, sizeof path);
	pending_clear(a);
	unlink(path);
	clear_canvas(a);
	a->page_dirty = 0;
	logf_("page %d cleared", a->page);
}

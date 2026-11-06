/* Ink: stamping onto the framebuffer, dirty-box refreshes, stroke recording. */
#include <stdlib.h>
#include <string.h>

#include "scribe.h"

void dirty_add(Dirty *d, int l, int t, int r, int b) {
	if (d->x1 < d->x0) {
		d->x0 = l; d->y0 = t; d->x1 = r; d->y1 = b;
		return;
	}
	if (l < d->x0) d->x0 = l;
	if (t < d->y0) d->y0 = t;
	if (r > d->x1) d->x1 = r;
	if (b > d->y1) d->y1 = b;
}

/* Square stamp of radius r, clipped to the canvas (never draws on the bar). */
void stamp(App *a, int x, int y, int r, uint8_t gray, Dirty *d) {
	int l = x - r, t = y - r, rr = x + r, b = y + r;
	if (l < a->cx) l = a->cx;
	if (t < a->cy) t = a->cy;
	if (rr > a->cx + a->cw - 1) rr = a->cx + a->cw - 1;
	if (b > a->cy + a->ch - 1) b = a->cy + a->ch - 1;
	if (rr < l || b < t) return;
	DispRect rc = { l, t, (rr - l + 1), (b - t + 1) };
	disp_fill_rect(&a->disp, &rc, gray);
	dirty_add(d, l, t, rr, b);
	if (gray == 0x00 && b > a->ink_bottom) a->ink_bottom = b;
}

void stamp_line(App *a, int x0, int y0, int x1, int y1, int r, uint8_t gray, Dirty *d) {
	int dx = x1 - x0, dy = y1 - y0;
	int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
	/* step by about half the brush so thick lines stay solid without stamping every pixel */
	int step = r > 1 ? r : 1;
	for (int s = 0; s < n; s += step)
		stamp(a, x0 + dx * s / n, y0 + dy * s / n, r, gray, d);
	stamp(a, x1, y1, r, gray, d);
}

void flush_dirty(App *a, Dirty *d, const DispRefresh *how) {
	if (d->x1 < d->x0) return;
	disp_refresh(&a->disp, (uint32_t)d->y0, (uint32_t)d->x0, (uint32_t)(d->x1 - d->x0 + 1), (uint32_t)(d->y1 - d->y0 + 1),
		      how);
	d->x0 = 0; d->y0 = 0; d->x1 = -1; d->y1 = -1;
}

void stroke_add(Stroke *s, int x, int y, int p) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 256;
		s->pts = realloc(s->pts, (size_t)s->cap * sizeof *s->pts);
	}
	s->pts[s->n].x = (int16_t)x;
	s->pts[s->n].y = (int16_t)y;
	s->pts[s->n].p = (uint16_t)p;
	s->n++;
}

/* Takes ownership of the stroke's points. */
void pending_push(App *a, Stroke *s) {
	if (a->npending == a->cappending) {
		a->cappending = a->cappending ? a->cappending * 2 : 32;
		a->pending = realloc(a->pending, (size_t)a->cappending * sizeof *a->pending);
	}
	a->pending[a->npending++] = *s;
	memset(s, 0, sizeof *s);
}

void pending_clear(App *a) {
	for (int i = 0; i < a->npending; i++) free(a->pending[i].pts);
	a->npending = 0;
}

void clear_canvas(App *a) {
	DispRect rc = { a->cx, a->cy, a->cw, a->ch };
	disp_fill_rect(&a->disp, &rc, 0xFF);
	disp_refresh(&a->disp, (uint32_t)a->cy, (uint32_t)a->cx, (uint32_t)a->cw, (uint32_t)a->ch, &a->flash);
	a->ink_bottom = a->cy;
}

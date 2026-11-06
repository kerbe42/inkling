/* Reply playback: the drawing ops of an answer (made by brain.c), played back with pen strokes animated at writing
 * pace. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "scribe.h"

Op *op_new(App *a, OpType t) {
	a->ops = realloc(a->ops, (size_t)(a->nops + 1) * sizeof *a->ops);
	Op *o = &a->ops[a->nops++];
	memset(o, 0, sizeof *o);
	o->t = t;
	return o;
}

void reply_free(App *a) {
	for (int i = 0; i < a->nops; i++) {
		free(a->ops[i].s);
		free(a->ops[i].xy);
		free(a->ops[i].dt);
	}
	free(a->ops);
	a->ops = NULL;
	a->nops = a->opi = a->seg = 0;
	a->seg_t = 0;
}

static void run_simple(App *a, Op *o) {
	switch (o->t) {
	case OP_STATUS:
		ui_set_status(a, "%s", o->s);
		break;
	case OP_MODE:
		for (int i = 0; i < a->nmodes; i++)
			if (!strcmp(a->mode_id[i], o->s)) { a->mode = i; ui_draw_bar(a); state_save(a); }
		break;
	case OP_NEWPAGE:
		if (a->npending) logf_("NEWPAGE dropped %d pending strokes", a->npending);
		pending_clear(a);
		page_save(a);
		a->page = a->last_page + 1;
		a->last_page = a->page;
		clear_canvas(a);
		state_save(a);
		brain_new_page(a);
		break;
	case OP_CLEAR: {
		int l = o->a < a->cx ? a->cx : o->a, t = o->b < a->cy ? a->cy : o->b;
		int r = o->a + o->c, b = o->b + o->d;
		if (r > a->cx + a->cw) r = a->cx + a->cw;
		if (b > a->cy + a->ch) b = a->cy + a->ch;
		if (r <= l || b <= t) break;
		DispRect rc = { l, t, (r - l), (b - t) };
		disp_fill_rect(&a->disp, &rc, 0xFF);
		disp_refresh(&a->disp, (uint32_t)t, (uint32_t)l, (uint32_t)(r - l), (uint32_t)(b - t), &a->gc16);
		break;
	}
	case OP_TEXT:
		if (o->b < a->cy) break;
		text_draw(a, o->s, o->a, o->b, o->c, o->d, o->size, o->style);
		if (!(o->style & 0x1000)) disp_refresh(&a->disp, (uint32_t)o->b, (uint32_t)o->a, (uint32_t)o->c, (uint32_t)o->d, &a->gc16);
		if (o->b + o->d > a->ink_bottom) a->ink_bottom = o->b + o->d;
		break;
	case OP_RULE: {
		if (o->b < a->cy) break;
		DispRect rc = { o->a, o->b, o->c, o->d };
		disp_fill_rect(&a->disp, &rc, 0x00);
		disp_refresh(&a->disp, (uint32_t)o->b, (uint32_t)o->a, (uint32_t)o->c, (uint32_t)o->d, &a->fast);
		if (o->b + o->d > a->ink_bottom) a->ink_bottom = o->b + o->d;
		break;
	}
	case OP_FILL: {
		if (o->b < a->cy) break;
		DispRect rc = { o->a, o->b, o->c, o->d };
		disp_fill_rect(&a->disp, &rc, (uint8_t)o->style);
		disp_refresh(&a->disp, (uint32_t)o->b, (uint32_t)o->a, (uint32_t)o->c, (uint32_t)o->d,
			      o->style == 0 || o->style == 255 ? &a->fast : &a->gl16);
		if (o->style < 128 && o->b + o->d > a->ink_bottom) a->ink_bottom = o->b + o->d;
		break;
	}
	case OP_DISC: {
		int r = o->c;
		for (int dy = -r; dy <= r; dy++) {
			int hw = (int)sqrt((double)(r * r - dy * dy));
			DispRect rc = { (o->a - hw), (o->b + dy), (2 * hw + 1), 1 };
			disp_fill_rect(&a->disp, &rc, 0x00);
		}
		disp_refresh(&a->disp, (uint32_t)(o->b - r), (uint32_t)(o->a - r), (uint32_t)(2 * r + 1), (uint32_t)(2 * r + 1), &a->fast);
		break;
	}
	default:
		break;
	}
}

/* Plays due ops. Returns 1 while a reply is still being played. */
int reply_step(App *a, double now) {
	while (a->opi < a->nops) {
		Op *o = &a->ops[a->opi];
		if (o->t == OP_PAUSE) {
			if (a->seg_t == 0) a->seg_t = now + o->a / (a->anim_speed > 0 ? a->anim_speed : 1.0);
			if (now < a->seg_t) return 1;
			a->seg_t = 0;
			a->opi++;
			continue;
		}
		if (o->t == OP_REVEAL) { /* show already-drawn text a band at a time, as if written */
			int lh = o->size > 0 ? o->size : 60, step = 40;
			int lines = (o->d + lh - 1) / lh, nsteps = (o->c + step - 1) / step;
			while (a->seg < lines * nsteps) {
				if (a->seg_t > now) return 1;
				int l = a->seg / nsteps, k = a->seg % nsteps;
				int x = o->a + k * step, y = o->b + l * lh;
				int w = x + step > o->a + o->c ? o->a + o->c - x : step, h = y + lh > o->b + o->d ? o->b + o->d - y : lh;
				a->seg++;
				int ink = 0;
				for (int yy = y; yy < y + h && !ink; yy += 2) {
					const unsigned char *row = a->fb + (size_t)(a->pan_y + yy) * (size_t)a->stride;
					for (int xx = x; xx < x + w; xx += 2) if (row[xx] < 200) { ink = 1; break; }
				}
				if (!ink) continue;
				disp_refresh(&a->disp, (uint32_t)y, (uint32_t)x, (uint32_t)w, (uint32_t)h, &a->fast);
				a->seg_t = now + 45 / (a->anim_speed > 0 ? a->anim_speed : 1.0);
				return 1;
			}
			if (o->b + o->d > a->ink_bottom) a->ink_bottom = o->b + o->d;
			a->seg = 0;
			a->seg_t = 0;
			a->opi++;
			continue;
		}
		if (o->t != OP_STROKE) {
			run_simple(a, o);
			a->opi++;
			continue;
		}
		if (o->n <= 0) { a->opi++; continue; }
		double speed = a->anim_speed > 0 ? a->anim_speed : 1.0;
		if (a->seg == 0 && a->seg_t == 0) a->seg_t = now + o->dt[0] / speed;
		Dirty d = { 0, 0, -1, -1 };
		int r = o->a / 2 > 1 ? o->a / 2 : 1;
		while (a->seg < o->n && now >= a->seg_t) {
			int i = a->seg;
			if (i == 0) stamp(a, o->xy[0], o->xy[1], r, 0x00, &d);
			else stamp_line(a, o->xy[2 * i - 2], o->xy[2 * i - 1], o->xy[2 * i], o->xy[2 * i + 1], r, 0x00, &d);
			a->seg++;
			if (a->seg < o->n) a->seg_t += o->dt[a->seg] / speed;
		}
		flush_dirty(a, &d, &a->fast);
		if (a->seg < o->n) return 1;
		a->seg = 0;
		a->seg_t = 0;
		a->opi++;
	}
	if (a->nops) reply_free(a);
	return 0;
}

/* Draws the rest of a reply at once (Menu or Games tapped while an answer is still being written). */
void reply_finish(App *a) {
	double t = mono_ms();
	for (int guard = 0; a->nops && guard < 200000; guard++) { t += 1e6; reply_step(a, t); }
	if (!a->in_games) page_save(a);
}


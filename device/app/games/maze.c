/* Maze: draw your way from Start (top left) to Finish (bottom right) with the pen. The rows are fixed per level
 * (Easy 16, Medium 26, Hard 36) and the columns fill the play area (about 14, 24 and 33 on the Scribe). Every maze
 * is perfect (exactly one way between any two cells): Easy is a long winding depth-first maze, Medium and Hard
 * branch more often, so there are more dead ends. Each stroke is judged as it ends: one that goes through a wall
 * is wiped and the wall it hit is circled (until the next stroke); the others stay on the page. The game is won
 * when the kept strokes link Start to Finish through open passages. */
#include "../games.h"
#include <stdio.h>
#include <string.h>

#define M 20
#define LAB 56 /* height of the Start / Finish label strips */
#define MAXR 44
#define MAXC 44
#define MAXP 12000 /* points of kept strokes, every <= 8 px */

enum { WR = 1, WB = 2 }; /* a wall on the right of / below a cell */

typedef struct {
	int R, C, cs, gx, gy, ww, tol, lvl;
	unsigned char w[MAXR][MAXC];
	unsigned char seen[MAXR][MAXC]; /* cells the kept strokes went through */
	unsigned char cur[MAXR][MAXC];  /* cells the current stroke went through (also the generator's visited set) */
	unsigned char dirty[MAXR][MAXC];
	int dirty_out, dirty_start, dirty_finish;
	short px[MAXP], py[MAXP]; /* kept strokes; px = -1 ends a stroke */
	int np;
	short list[MAXR * MAXC];
	int sx, sy, sw, sh, fx, fy, fw, fh; /* label boxes */
	int mark, mx, my, mr;               /* circle around the wall last hit */
	int hx, hy;                         /* where the current stroke hit a wall */
	int strokes, wiped;
	char msg[96];
} S;

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* --- generating --- */
static void generate(GameCtx *g, S *s, int newest_pct) {
	memset(s->w, WR | WB, sizeof s->w);
	memset(s->cur, 0, sizeof s->cur);
	int n = 0, r = g_rand(g, s->R), c = g_rand(g, s->C);
	s->cur[r][c] = 1;
	s->list[n++] = (short)(r * MAXC + c);
	while (n) { /* growing tree: newest cell (depth-first) or, sometimes, a random one (branches) */
		int i = g_rand(g, 100) < newest_pct ? n - 1 : g_rand(g, n), nb[4], k = 0;
		r = s->list[i] / MAXC; c = s->list[i] % MAXC;
		if (r > 0 && !s->cur[r - 1][c]) nb[k++] = 0;
		if (r < s->R - 1 && !s->cur[r + 1][c]) nb[k++] = 1;
		if (c > 0 && !s->cur[r][c - 1]) nb[k++] = 2;
		if (c < s->C - 1 && !s->cur[r][c + 1]) nb[k++] = 3;
		if (!k) {
			memmove(&s->list[i], &s->list[i + 1], (size_t)(n - i - 1) * sizeof s->list[0]);
			n--;
			continue;
		}
		int d = nb[g_rand(g, k)], nr = r, nc = c;
		if (d == 0) { nr--; s->w[nr][c] &= (unsigned char)~WB; }
		else if (d == 1) { nr++; s->w[r][c] &= (unsigned char)~WB; }
		else if (d == 2) { nc--; s->w[r][nc] &= (unsigned char)~WR; }
		else { nc++; s->w[r][c] &= (unsigned char)~WR; }
		s->cur[nr][nc] = 1;
		s->list[n++] = (short)(nr * MAXC + nc);
	}
	memset(s->cur, 0, sizeof s->cur);
}

/* walls: horizontal one at the top of row y over column c; vertical one left of column x in row r */
static int has_h(S *s, int y, int c) { return y == 0 ? c != 0 : y == s->R ? c != s->C - 1 : (s->w[y - 1][c] & WB) != 0; }
static int has_v(S *s, int x, int r) { return x == 0 || x == s->C ? 1 : (s->w[r][x - 1] & WR) != 0; }

static void hwall(GameCtx *g, S *s, int y, int c0, int c1) { /* cells c0..c1-1 */
	int e = s->ww / 2;
	g_line(g, s->gx + c0 * s->cs - e, s->gy + y * s->cs, s->gx + c1 * s->cs + e, s->gy + y * s->cs, s->ww);
}
static void vwall(GameCtx *g, S *s, int x, int r0, int r1) {
	int e = s->ww / 2;
	g_line(g, s->gx + x * s->cs, s->gy + r0 * s->cs - e, s->gx + x * s->cs, s->gy + r1 * s->cs + e, s->ww);
}

static void draw_walls(GameCtx *g, S *s) {
	for (int y = 0; y <= s->R; y++)
		for (int c = 0; c < s->C; c++) {
			if (!has_h(s, y, c)) continue;
			int e = c;
			while (e < s->C && has_h(s, y, e)) e++;
			hwall(g, s, y, c, e);
			c = e;
		}
	for (int x = 0; x <= s->C; x++)
		for (int r = 0; r < s->R; r++) {
			if (!has_v(s, x, r)) continue;
			int e = r;
			while (e < s->R && has_v(s, x, e)) e++;
			vwall(g, s, x, r, e);
			r = e;
		}
}

static void draw_labels(GameCtx *g, S *s, int start, int finish) {
	if (start) g_print(g, s->sx, s->sy, s->sw, s->sh, 34, 1, "Start");
	if (finish) g_print(g, s->fx, s->fy, s->fw, s->fh, 34, 1, "Finish");
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	static const int ROWS[3] = { 16, 26, 36 }, NEWEST[3] = { 100, 80, 70 };
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	s->lvl = g_level(g);
	int availh = ah - 2 * M - 2 * LAB, availw = aw - 2 * M;
	s->R = ROWS[s->lvl];
	s->cs = availh / s->R;
	if (s->cs < 34) { s->cs = 34; s->R = availh / s->cs; }
	if (s->R > MAXR) s->R = MAXR;
	s->C = availw / s->cs;
	if (s->C > MAXC) s->C = MAXC;
	if (s->C < 4) s->C = 4;
	s->gx = ax + (aw - s->C * s->cs) / 2;
	s->gy = ay + M + LAB + (availh - s->R * s->cs) / 2;
	s->ww = s->cs >= 100 ? 6 : s->cs >= 64 ? 5 : 4;
	s->tol = s->cs * 3 / 10;
	generate(g, s, NEWEST[s->lvl]);

	s->sw = s->fw = 200;
	s->sh = s->fh = LAB - 8;
	s->sx = s->gx + s->cs / 2 - s->sw / 2;
	s->fx = s->gx + s->C * s->cs - s->cs / 2 - s->fw / 2;
	if (s->sx < ax) s->sx = ax;
	if (s->fx + s->fw > ax + aw) s->fx = ax + aw - s->fw;
	s->sy = s->gy - LAB;
	s->fy = s->gy + s->R * s->cs + 8;

	g_speed(g, 40);
	draw_walls(g, s);
	g_speed(g, 12);
	draw_labels(g, s, 1, 1);
	snprintf(s->msg, sizeof s->msg, "A %dx%d maze - draw a path from Start to Finish", s->C, s->R);
	g_remark(g, s->msg);
	g_say(g, "draw your way through without crossing a wall");
}

/* --- judging a stroke --- */
static int can_go(S *s, int r, int c, int dr, int dc) {
	if (dr == -1 && r == 0 && c == 0) return 1;                  /* out through the entrance */
	if (dr == 1 && r == s->R - 1 && c == s->C - 1) return 1;     /* out through the exit */
	int nr = r + dr, nc = c + dc;
	if (nr < 0 || nr >= s->R || nc < 0 || nc >= s->C) return 0;
	if (dc == 1) return !(s->w[r][c] & WR);
	if (dc == -1) return !(s->w[r][nc] & WR);
	if (dr == 1) return !(s->w[r][c] & WB);
	return !(s->w[nr][c] & WB);
}

static void hit_wall(S *s, int r, int c, int dr, int dc) { /* the middle of the wall between r,c and its neighbour */
	s->hx = s->gx + c * s->cs + s->cs / 2 + dc * s->cs / 2;
	s->hy = s->gy + r * s->cs + s->cs / 2 + dr * s->cs / 2;
}

/* Follow the stroke cell by cell (a cell is left only once the pen is tol px past its edge, so grazing a wall is
 * fine). Returns 1 if it never went through a wall; marks the cells it went through in s->cur. */
static int judge(S *s, const GStroke *st) {
	int W = s->C * s->cs, H = s->R * s->cs, t = s->tol, r = -1, c = -1;
	int x = st->x[0], y = st->y[0], lx = x, ly = y;
	memset(s->cur, 0, sizeof s->cur);
	if (x >= s->gx && x < s->gx + W && y >= s->gy && y < s->gy + H) {
		r = (y - s->gy) / s->cs; c = (x - s->gx) / s->cs;
		s->cur[r][c] = 1;
	}
	for (int i = 0; i < st->n; i++) {
		int tx = st->x[i], ty = st->y[i], dx = tx - lx, dy = ty - ly;
		int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, m = (adx > ady ? adx : ady) / 3 + 1;
		for (int j = 1; j <= m; j++) {
			int px = x, py = y;
			x = lx + dx * j / m; y = ly + dy * j / m;
			if (r < 0) { /* outside: may only come in through the entrance or the exit */
				if (!(x > s->gx + t && x < s->gx + W - t && y > s->gy + t && y < s->gy + H - t)) continue;
				int nr = (y - s->gy) / s->cs, nc = (x - s->gx) / s->cs;
				int top = py <= s->gy + t, bottom = !top && py >= s->gy + H - t;
				if ((top && nr == 0 && nc == 0) || (bottom && nr == s->R - 1 && nc == s->C - 1)) {
					r = nr; c = nc;
					s->cur[r][c] = 1;
					continue;
				}
				s->hx = px < s->gx ? s->gx : px > s->gx + W ? s->gx + W : px;
				s->hy = py < s->gy ? s->gy : py > s->gy + H ? s->gy + H : py;
				return 0;
			}
			for (int guard = 0; guard < 4 && r >= 0; guard++) {
				int x0 = s->gx + c * s->cs, y0 = s->gy + r * s->cs;
				int mx = x > x0 + s->cs + t ? 1 : x < x0 - t ? -1 : 0, my = y > y0 + s->cs + t ? 1 : y < y0 - t ? -1 : 0;
				if (!mx && !my) break;
				if (mx && my) { /* cut across a corner: fine if either way round is open */
					if (can_go(s, r, c, 0, mx) && can_go(s, r, c + mx, my, 0)) s->cur[r][c + mx] = 1;
					else if (can_go(s, r, c, my, 0) && can_go(s, r + my, c, 0, mx)) s->cur[r + my][c] = 1;
					else { if (!can_go(s, r, c, 0, mx)) hit_wall(s, r, c, 0, mx); else hit_wall(s, r, c, my, 0); return 0; }
				} else if (!can_go(s, r, c, my, mx)) { hit_wall(s, r, c, my, mx); return 0; }
				r += my; c += mx;
				if (r < 0 || r >= s->R || c < 0 || c >= s->C) r = c = -1; /* left through the entrance or exit */
				else s->cur[r][c] = 1;
			}
		}
		lx = tx; ly = ty;
	}
	return 1;
}

/* --- keeping and wiping ink --- */
static void keep_points(S *s, const GStroke *st) {
	if (s->np + 2 >= MAXP) return;
	int lx = st->x[0], ly = st->y[0];
	s->px[s->np] = (short)lx; s->py[s->np] = (short)ly; s->np++;
	for (int i = 1; i < st->n && s->np + 2 < MAXP; i++) {
		int dx = st->x[i] - lx, dy = st->y[i] - ly, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, d = adx > ady ? adx : ady;
		if (d < 8 && i < st->n - 1) continue;
		int m = (d + 7) / 8;
		for (int j = 1; j <= m && s->np + 2 < MAXP; j++) {
			s->px[s->np] = (short)(lx + dx * j / m); s->py[s->np] = (short)(ly + dy * j / m); s->np++;
		}
		lx = st->x[i]; ly = st->y[i];
	}
	s->px[s->np] = -1; s->py[s->np] = -1; s->np++;
}

static int overlaps(int x0, int y0, int x1, int y1, int bx, int by, int bw, int bh) { return x0 < bx + bw && x1 > bx && y0 < by + bh && y1 > by; }

/* Clear a rectangle (clipped to the play area) and note what must be redrawn there. */
static void clear_rect(GameCtx *g, S *s, int x0, int y0, int x1, int y1) {
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	if (x0 < ax) x0 = ax;
	if (y0 < ay) y0 = ay;
	if (x1 > ax + aw) x1 = ax + aw;
	if (y1 > ay + ah) y1 = ay + ah;
	if (x1 <= x0 || y1 <= y0) return;
	g_clear(g, x0, y0, x1 - x0, y1 - y0);
	int e = s->ww + 2, W = s->C * s->cs, H = s->R * s->cs;
	int c0 = floordiv(x0 - e - s->gx, s->cs), c1 = floordiv(x1 + e - s->gx, s->cs);
	int r0 = floordiv(y0 - e - s->gy, s->cs), r1 = floordiv(y1 + e - s->gy, s->cs);
	if (c0 < 0) c0 = 0;
	if (r0 < 0) r0 = 0;
	if (c1 >= s->C) c1 = s->C - 1;
	if (r1 >= s->R) r1 = s->R - 1;
	for (int r = r0; r <= r1; r++)
		for (int c = c0; c <= c1; c++) s->dirty[r][c] = 1;
	if (x0 < s->gx || x1 > s->gx + W || y0 < s->gy || y1 > s->gy + H) s->dirty_out = 1;
	if (overlaps(x0, y0, x1, y1, s->sx, s->sy, s->sw, s->sh)) s->dirty_start = 1;
	if (overlaps(x0, y0, x1, y1, s->fx, s->fy, s->fw, s->fh)) s->dirty_finish = 1;
}

static int point_dirty(S *s, int x, int y) {
	int c = floordiv(x - s->gx, s->cs), r = floordiv(y - s->gy, s->cs);
	if (r < 0 || r >= s->R || c < 0 || c >= s->C) return s->dirty_out;
	return s->dirty[r][c];
}

/* Redraw the walls, kept ink and labels in what was cleared. */
static void redraw(GameCtx *g, S *s) {
	g_speed(g, 50);
	for (int r = 0; r < s->R; r++)
		for (int c = 0; c < s->C; c++) {
			if (!s->dirty[r][c]) continue;
			if (has_h(s, r, c)) hwall(g, s, r, c, c + 1);
			if (has_h(s, r + 1, c)) hwall(g, s, r + 1, c, c + 1);
			if (has_v(s, c, r)) vwall(g, s, c, r, r + 1);
			if (has_v(s, c + 1, r)) vwall(g, s, c + 1, r, r + 1);
		}
	for (int i = 0; i + 1 < s->np; i++) {
		if (s->px[i] < 0 || s->px[i + 1] < 0) continue;
		if (point_dirty(s, s->px[i], s->py[i]) || point_dirty(s, s->px[i + 1], s->py[i + 1]))
			g_line(g, s->px[i], s->py[i], s->px[i + 1], s->py[i + 1], 6);
	}
	if (s->dirty_start || s->dirty_finish) {
		if (s->dirty_start) g_clear(g, s->sx, s->sy, s->sw, s->sh);
		if (s->dirty_finish) g_clear(g, s->fx, s->fy, s->fw, s->fh);
		draw_labels(g, s, s->dirty_start, s->dirty_finish);
	}
	memset(s->dirty, 0, sizeof s->dirty);
	s->dirty_out = s->dirty_start = s->dirty_finish = 0;
}

static void clear_mark(GameCtx *g, S *s) {
	if (!s->mark) return;
	s->mark = 0;
	clear_rect(g, s, s->mx - s->mr - 4, s->my - s->mr - 4, s->mx + s->mr + 5, s->my + s->mr + 5);
}

static void wipe_stroke(GameCtx *g, S *s, const GStroke *st) {
	int lx = st->x[0], ly = st->y[0];
	clear_rect(g, s, lx - 9, ly - 9, lx + 10, ly + 10);
	for (int i = 1; i < st->n; i++) {
		int dx = st->x[i] - lx, dy = st->y[i] - ly, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, d = adx > ady ? adx : ady;
		if (d < 8 && i < st->n - 1) continue;
		int m = (d + 7) / 8;
		for (int j = 1; j <= m; j++) {
			int x = lx + dx * j / m, y = ly + dy * j / m;
			clear_rect(g, s, x - 9, y - 9, x + 10, y + 10);
		}
		lx = st->x[i]; ly = st->y[i];
	}
}

static int solved(S *s) {
	if (!s->seen[0][0]) return 0;
	memset(s->cur, 0, sizeof s->cur);
	int n = 0;
	s->list[n++] = 0;
	s->cur[0][0] = 1;
	while (n) {
		int v = s->list[--n], r = v / MAXC, c = v % MAXC;
		if (r == s->R - 1 && c == s->C - 1) return 1;
		static const int D[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		for (int d = 0; d < 4; d++) {
			int nr = r + D[d][0], nc = c + D[d][1];
			if (nr < 0 || nr >= s->R || nc < 0 || nc >= s->C || s->cur[nr][nc] || !s->seen[nr][nc]) continue;
			if (!can_go(s, r, c, D[d][0], D[d][1])) continue;
			s->cur[nr][nc] = 1;
			s->list[n++] = (short)(nr * MAXC + nc);
		}
	}
	return 0;
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	/* the board is complete in the state from the start, so strokes drawn while it animates are judged fine */
	if (st->finger) { g_say(g, "draw your path with the pen"); return; }
	if (st->tap) {
		wipe_stroke(g, s, st);
		clear_mark(g, s);
		redraw(g, s);
		g_say(g, "draw your path with the pen - a line from Start to Finish");
		return;
	}
	int ok = judge(s, st), any = 0;
	for (int r = 0; r < s->R && !any; r++)
		for (int c = 0; c < s->C && !any; c++) any = s->cur[r][c];
	if (ok && any) {
		s->strokes++;
		for (int r = 0; r < s->R; r++)
			for (int c = 0; c < s->C; c++) s->seen[r][c] |= s->cur[r][c];
		keep_points(s, st);
		clear_mark(g, s);
		redraw(g, s);
		if (solved(s)) {
			snprintf(s->msg, sizeof s->msg, s->wiped ? "You made it through! (%d strokes, %d wiped)" : "You made it through - no walls hit!", s->strokes + s->wiped, s->wiped);
			g_over(g, s->msg);
			return;
		}
		g_say(g, "no walls hit - keep going");
		return;
	}
	s->wiped++;
	wipe_stroke(g, s, st);
	clear_mark(g, s);
	redraw(g, s);
	if (!ok) {
		int ax, ay, aw, ah;
		g_area(g, &ax, &ay, &aw, &ah);
		s->mr = s->cs * 45 / 100;
		s->mx = s->hx; s->my = s->hy;
		if (s->mx - s->mr - 4 < ax) s->mx = ax + s->mr + 4;
		if (s->mx + s->mr + 5 > ax + aw) s->mx = ax + aw - s->mr - 5;
		if (s->my - s->mr - 4 < ay) s->my = ay + s->mr + 4;
		if (s->my + s->mr + 5 > ay + ah) s->my = ay + ah - s->mr - 5;
		g_speed(g, 6);
		g_circle(g, s->mx, s->my, s->mr, 3);
		s->mark = 1;
		g_say(g, "that line went through a wall (circled) - wiped it, try again");
	} else g_say(g, "draw inside the maze, from Start to Finish");
}

const GameDef GAME_maze = { "maze", "Maze", "Draw your way from Start to Finish", sizeof(S), 1, 1, start, stroke, 0 };

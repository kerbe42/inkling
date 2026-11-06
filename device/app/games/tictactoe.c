/* Tic-tac-toe: draw an X (or tap a square); the Kindle answers with an O. Easy plays loosely, Hard never loses. */
#include "../games.h"

typedef struct {
	int b[9]; /* 0 empty, 1 you (X), 2 me (O) */
	int x, y, cell, my_turn;
} S;

static const int LINES[8][3] = { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 }, { 0, 3, 6 }, { 1, 4, 7 }, { 2, 5, 8 }, { 0, 4, 8 }, { 2, 4, 6 } };

static int winner(const int *b, int *line) {
	for (int i = 0; i < 8; i++) {
		int p = b[LINES[i][0]];
		if (p && p == b[LINES[i][1]] && p == b[LINES[i][2]]) { if (line) *line = i; return p; }
	}
	for (int i = 0; i < 9; i++) if (!b[i]) return 0;
	return 3; /* draw */
}

static int minimax(int *b, int who, int depth) {
	int w = winner(b, 0);
	if (w == 2) return 10 - depth;
	if (w == 1) return depth - 10;
	if (w == 3) return 0;
	int best = who == 2 ? -100 : 100;
	for (int i = 0; i < 9; i++) {
		if (b[i]) continue;
		b[i] = who;
		int v = minimax(b, 3 - who, depth + 1);
		b[i] = 0;
		if (who == 2 ? v > best : v < best) best = v;
	}
	return best;
}

static void cell_center(S *s, int i, int *cx, int *cy) { *cx = s->x + (i % 3) * s->cell + s->cell / 2; *cy = s->y + (i / 3) * s->cell + s->cell / 2; }

static int finish(GameCtx *g, S *s) {
	int line, w = winner(s->b, &line);
	if (!w) return 0;
	if (w != 3) {
		int x0, y0, x1, y1;
		cell_center(s, LINES[line][0], &x0, &y0);
		cell_center(s, LINES[line][2], &x1, &y1);
		g_speed(g, 3);
		g_line(g, x0 + (x0 - x1) / 6, y0 + (y0 - y1) / 6, x1 + (x1 - x0) / 6, y1 + (y1 - y0) / 6, 8);
	}
	g_over(g, w == 1 ? "You win! Well played." : w == 2 ? "I win this one!" : "A draw - nobody wins.");
	return 1;
}

static void start(GameCtx *g) {
	S *s = g_state(g);
	int ax, ay, aw, ah;
	g_area(g, &ax, &ay, &aw, &ah);
	int size = (aw < ah ? aw : ah) * 8 / 10;
	s->cell = size / 3;
	s->x = ax + (aw - 3 * s->cell) / 2;
	s->y = ay + (ah - 3 * s->cell) / 2;
	g_speed(g, 5);
	for (int i = 1; i < 3; i++) {
		g_line(g, s->x + i * s->cell, s->y, s->x + i * s->cell, s->y + 3 * s->cell, 6);
		g_line(g, s->x, s->y + i * s->cell, s->x + 3 * s->cell, s->y + i * s->cell, 6);
	}
	g_remark(g, "You're X - your move!");
	g_say(g, "draw an X in a square (or tap it)");
}

static void my_move(GameCtx *g) {
	S *s = g_state(g);
	int free_cells[9], n = 0, pick = -1;
	for (int i = 0; i < 9; i++) if (!s->b[i]) free_cells[n++] = i;
	int lvl = g_level(g);
	int smart = lvl == 2 || (lvl == 1 && g_rand(g, 100) < 70) || (lvl == 0 && g_rand(g, 100) < 30);
	if (smart) {
		int best = -1000;
		for (int k = 0; k < n; k++) {
			s->b[free_cells[k]] = 2;
			int v = minimax(s->b, 1, 0);
			s->b[free_cells[k]] = 0;
			if (v > best) { best = v; pick = free_cells[k]; }
		}
	} else pick = free_cells[g_rand(g, n)];
	s->b[pick] = 2;
	int cx, cy;
	cell_center(s, pick, &cx, &cy);
	g_speed(g, 2.5);
	g_circle(g, cx, cy, s->cell * 30 / 100, 6);
	if (!finish(g, s)) {
		s->my_turn = 0;
		g_say(g, "your move");
	}
}

static void stroke(GameCtx *g, const GStroke *st) {
	S *s = g_state(g);
	if (s->my_turn || g_busy(g)) { g_say(g, "wait for my move"); return; }
	int c = (st->cx - s->x) / s->cell, r = (st->cy - s->y) / s->cell;
	if (st->cx < s->x || st->cy < s->y || c > 2 || r > 2) { g_say(g, "mark a square on the board"); return; }
	int i = r * 3 + c;
	if (s->b[i]) { g_say(g, "that square is taken"); return; }
	s->b[i] = 1;
	if (st->tap || st->finger) { /* tapped: draw the X for them */
		int cx, cy;
		cell_center(s, i, &cx, &cy);
		g_speed(g, 4);
		g_cross(g, cx, cy, s->cell * 28 / 100, 6);
	}
	if (finish(g, s)) return;
	s->my_turn = 1;
	g_say(g, "my turn...");
	g_after(g, 500);
}

const GameDef GAME_tictactoe = { "tictactoe", "Tic-tac-toe", "Three in a row - you're X", sizeof(S), 1, 1, start, stroke, my_move };

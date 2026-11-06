/* No-repeat picking kept on the Kindle (games.h g_load/g_save), shared by the games with fixed content lists.
 * Items are remembered by a hash of their text (FNV-1a), never by index, so lists can grow, shrink or be reordered
 * between versions (a renamed item just comes round again sooner).
 *  - bag_next(): every item once, in random order, before any comes back (a round), and none comes back within
 *    `gap` picks across the change of round either.
 *  - recent_seen() / recent_add(): just the last RECENT_MAX items, for lists too big for rounds. */
#ifndef GAMES_BAG_H
#define GAMES_BAG_H

#include "../games.h"
#include <stdint.h>
#include <string.h>

static inline uint32_t bag_hash(const char *t) {
	uint32_t h = 2166136261u;
	while (*t) { h ^= (unsigned char)*t++; h *= 16777619u; }
	return h;
}

#define BAG_MAX 1024 /* items in a list */
#define BAG_GAP 128  /* longest gap */
typedef struct { uint32_t magic, nused, nlast, lpos, last[BAG_GAP], used[BAG_MAX]; } Bag;
#define BAG_MAGIC 0x53424731u /* "SBG1" */

static inline int bag_in(const uint32_t *a, uint32_t n, uint32_t h) {
	for (uint32_t i = 0; i < n; i++) if (a[i] == h) return 1;
	return 0;
}

static inline int bag_lately(const Bag *b, uint32_t h, int gap) { /* among the last `gap` played? */
	for (int k = 1; k <= gap && k <= (int)b->nlast; k++)
		if (b->last[(b->lpos + BAG_GAP - (uint32_t)k) % BAG_GAP] == h) return 1;
	return 0;
}

static inline void bag_load(GameCtx *g, const char *key, Bag *b) {
	int got = g_load(g, key, b, sizeof *b);
	if (got != (int)sizeof *b || b->magic != BAG_MAGIC || b->nused > BAG_MAX || b->nlast > BAG_GAP || b->lpos >= BAG_GAP) memset(b, 0, sizeof *b);
}

/* Record that the item with hash h was played (after bag_peek() chose it) and save the bag. */
static inline void bag_take(GameCtx *g, const char *key, uint32_t h) {
	static Bag b;
	bag_load(g, key, &b);
	if (bag_in(b.used, b.nused, h) || b.nused >= BAG_MAX) b.nused = 0; /* it was a new round (or stale hashes piled up) */
	b.magic = BAG_MAGIC;
	b.used[b.nused++] = h;
	b.last[b.lpos] = h;
	b.lpos = (b.lpos + 1) % BAG_GAP;
	if (b.nlast < BAG_GAP) b.nlast++;
	g_save(g, key, &b, sizeof b);
}

/* The index (0..n-1, n <= BAG_MAX) of the item to play next, given the hashes of all n items (nothing is saved). */
static inline int bag_peek(GameCtx *g, const char *key, const uint32_t *hash, int n, int gap) {
	static Bag b;
	static int fresh[BAG_MAX];
	int nf = 0;
	bag_load(g, key, &b);
	if (n > BAG_MAX) n = BAG_MAX;
	if (gap > BAG_GAP) gap = BAG_GAP;
	/* not used this round, and (early in a round) not among the last `gap` of the round before */
	for (int i = 0; i < n; i++) if (!bag_in(b.used, b.nused, hash[i]) && !bag_lately(&b, hash[i], gap)) fresh[nf++] = i;
	if (!nf) for (int i = 0; i < n; i++) if (!bag_in(b.used, b.nused, hash[i])) fresh[nf++] = i;
	if (!nf) { /* round over */
		b.nused = 0;
		for (int i = 0; i < n; i++) if (!bag_lately(&b, hash[i], gap)) fresh[nf++] = i;
		if (!nf) for (int i = 0; i < n; i++) fresh[nf++] = i;
	}
	return fresh[g_rand(g, nf)];
}

/* bag_peek() + bag_take(): the index of the next item, recorded as played */
static inline int bag_next(GameCtx *g, const char *key, const uint32_t *hash, int n, int gap) {
	int i = bag_peek(g, key, hash, n, gap);
	bag_take(g, key, hash[i]);
	return i;
}

#define RECENT_MAX 256
typedef struct { uint32_t magic, n, pos, h[RECENT_MAX]; } Recent;
#define RECENT_MAGIC 0x53524331u /* "SRC1" */

static inline void recent_load(GameCtx *g, const char *key, Recent *r) {
	if (g_load(g, key, r, sizeof *r) != (int)sizeof *r || r->magic != RECENT_MAGIC || r->n > RECENT_MAX || r->pos >= RECENT_MAX)
		memset(r, 0, sizeof *r);
}

static inline int recent_seen(const Recent *r, uint32_t h, int last) { /* among the last `last` (<= RECENT_MAX)? */
	for (int k = 1; k <= last && k <= (int)r->n; k++)
		if (r->h[(r->pos + RECENT_MAX - (uint32_t)k) % RECENT_MAX] == h) return 1;
	return 0;
}

static inline void recent_add(GameCtx *g, const char *key, Recent *r, uint32_t h) {
	r->magic = RECENT_MAGIC;
	r->h[r->pos] = h;
	r->pos = (r->pos + 1) % RECENT_MAX;
	if (r->n < RECENT_MAX) r->n++;
	g_save(g, key, r, sizeof *r);
}

#endif

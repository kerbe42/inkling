/* Helpers on top of dict_words.h for games that use dictionary words as puzzle answers (Hangman, Criss-Cross):
 * base forms only (no plurals or -ed/-ing forms of another word in the list, which make dull puzzles: "cats",
 * "walked") and a skip list of words that are not offensive (the word list has none) but no fun in a family game,
 * or are also brand names. */
#ifndef DICT_PICK_H
#define DICT_PICK_H

#include "dict_words.h"
#include <stdio.h>
#include <string.h>

/* space-separated, with a space at both ends */
static const char DICT_SKIP[] =
	" abort abortion abortive autopsy bible boozer booze bra brandy bullet bum butt buttock carcass christen coffin "
	"corpse cripple deathbed demon demonic devil devilish drug drunk drunkard dumb execute executor fart fat fatty "
	"freak gallows genocide gin godless gunfire gunman gunpoint gunshot hangover idiot idiotic kill killer lame liquor "
	"loser lust lusty massacre moronic murder murderer naked noose nude nudist nudity occult pee pimple pimply pistol "
	"poop puberty puke sex sexual sexually sexy "
	"rifle roadkill rum satanic satanism scotch sexism sexist sinful sinner slave slavery snot snotty stupid stupidly "
	"suicidal suicide tarmac tipsy torture torturer twitter ugly virgin vodka vomit warhead warlock weirdo whiskey "
	"witch ";

static inline int dict_in(const char *w, int n) { return dict_find(w, n) >= 0; }

/* 1 if w (lower case, n letters) is a plural or -ed/-ing form of another word in the list */
static inline int dict_inflected(const char *w, int n) {
	char b[DICT_MAXLEN + 2];
	if (n < 4 || n > DICT_MAXLEN) return 0;
	if (n >= 5 && (!strcmp(w + n - 3, "ies") || !strcmp(w + n - 3, "ied"))) {
		memcpy(b, w, (size_t)n - 3);
		b[n - 3] = 'y';
		b[n - 2] = 0;
		if (dict_in(b, n - 2)) return 1;
	}
	if (n >= 5 && !strcmp(w + n - 3, "ves")) { /* thieves, knives */
		memcpy(b, w, (size_t)n - 3);
		b[n - 3] = 'f';
		b[n - 2] = 0;
		if (dict_in(b, n - 2)) return 1;
		b[n - 2] = 'e';
		b[n - 1] = 0;
		if (dict_in(b, n - 1)) return 1;
	}
	if (!strcmp(w + n - 2, "es") && dict_in(w, n - 2)) return 1;
	if (w[n - 1] == 's' && w[n - 2] != 's' && dict_in(w, n - 1)) return 1;
	if (!strcmp(w + n - 2, "ed") && (dict_in(w, n - 2) || dict_in(w, n - 1) || (n > 4 && w[n - 3] == w[n - 4] && dict_in(w, n - 3))))
		return 1;
	if (n >= 6 && !strcmp(w + n - 3, "ing")) {
		if (dict_in(w, n - 3) || (w[n - 4] == w[n - 5] && dict_in(w, n - 4))) return 1;
		memcpy(b, w, (size_t)n - 3);
		b[n - 3] = 'e';
		b[n - 2] = 0;
		if (dict_in(b, n - 2)) return 1;
	}
	return 0;
}

/* 1 if w (lower case) is on the skip list */
static inline int dict_skipped(const char *w) {
	char t[24];
	if (strlen(w) > sizeof t - 3) return 0;
	snprintf(t, sizeof t, " %s ", w);
	return strstr(DICT_SKIP, t) != 0;
}

/* 1 if the i-th word of that length is a common base-form word not on the skip list; w gets the word (lower case) */
static inline int dict_pickable(int len, int i, char *w) {
	if (!dict_common(len, i)) return 0;
	dict_word(len, i, w);
	return !dict_inflected(w, len) && !dict_skipped(w);
}

#endif

/* The one definition of the word list arrays (dict_words.h); every other file gets extern declarations. Link this
 * with any game that includes dict_words.h (the device build takes every .c file in games/). */
#define DICT_WORDS_DEFINE
#include "dict_words.h"

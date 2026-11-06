#!/usr/bin/env python3
"""Generate device/app/games/dict_words.h: the word list for Word Grid, Word Ladder, Hangman and Criss-Cross.

Source: 12dicts 6.0.2 by Alan Beale, which he released to the public domain (see the end of its ReadMe.html: "I
explicitly release them to the public domain, but request acknowledgment of their use"; the exception, 2of12inf and
the 2+2+3 lists, which derive from AGID, is NOT used here).
  Download: http://downloads.sourceforge.net/wordlist/12dicts-6.0.2.zip
  sha256:   64ac1d35acb66b550c7ebc56e080b62e0bad8f5984d72059dc2e05ac48780e52  (1992138 bytes)
Lists used (read straight from the zip, nothing extracted):
  International/3of6game.txt - the word-game list (inflections included): the accepted words.
  American/3esl.txt          - a core-vocabulary list: these words, and their plain inflections (-s, -es, -ed,
                               -d, -ing, y->ies/ied, e->ing) found in 3of6game, are marked "common". The games
                               build puzzles and show hints from common words only.
Filter: lowercase a-z only, 3 to 8 letters, annotation marks ($ ^ & + !) stripped, minus a blocklist of offensive
words (kept rot13-encoded below so the file does not read as a list of slurs).

Output format: one string per length, the words of that length sorted and concatenated without separators; a common
word has its first letter in upper case (compare case-insensitively). The arrays are defined in one place only,
dict_words.c (which defines DICT_WORDS_DEFINE before including the header); everyone else sees extern declarations.
(They used to be weak definitions in every file including the header, but without section GC the linker kept every
copy: 240 KB each.)

Usage: python3 gen_dict.py 12dicts-6.0.2.zip ../dict_words.h
"""
import codecs
import hashlib
import re
import sys
import zipfile

SHA256 = "64ac1d35acb66b550c7ebc56e080b62e0bad8f5984d72059dc2e05ac48780e52"
URL = "http://downloads.sourceforge.net/wordlist/12dicts-6.0.2.zip"
MINLEN, MAXLEN = 3, 8

# Offensive words removed (profanity, slurs, sexual terms), rot13-encoded.
BLOCK_ROT13 = """
nany nahf nefr nefrubyr nefrf nff nffrf nffubyr nffubyrf onfgneq onfgneqf ovzob ovzobf ovgpu ovgpurq
ovgpurf ovgpuvre ovgpuvat ovgpul obyybpxf obaqntr obare obaref obax obaxrq obaxvat obbo obborq
obbovrf obbovat obbof obbol oebgury oebguryf ohttre ohttrerq ohttref ohttrel ohyyfuvg puvax pyvgbeny
pyvgbevf pbpnvar pbpx pbpxf pbaqbz pbaqbzf pbbyvr pbbyvrf pbba pbbaf penc penccrq penccvat penccl
pergva phz phag phagf qntb qntbrf qntbf qnzzvg qnza qnzarq qnzavat qnzaf qnexvr qnexvrf qvpx qvpxf
qvyqb qvyqbrf qvyqbf qlxr qlxrf rebgvp rebgvpn snt snttrq snttvat snttbg snttbgf sntf sryyngvb
srgvfu srgvfurf sybbml shpx shpxrq shpxre shpxref shpxvat shpxf tvtbyb tvtbybf tbqqnz tbqqnza tbbx
tevatb tevatbf tlcfvrf tlcfl uneybg urebva ubaxvrf ubaxl ubbxre ubbxref ubeal uhzcvat uhffl vzorpvyr
vaprfg xvxr xvaxl zvqtrg zvqtrgf zbyrfg zbyrfgrq zbyrfgre zbyrfgf zbeba zbebaf arterff artebvq avttn
avttnf avttnm avttre avttref avccyr avccyrf ahqr ahqrf ahqvgl alzcub alzcubf bcvhz betnfz betnfzrq
betnfzvp betnfzf betvrf betl cravf cravfrf crei creireg creiregf cvzc cvzcrq cvzcvat cvzcf cvff
cvffrq cvffrf cvffvat cbbs cbbsgre cbbsgref cbea cbeab cevpx cevpxf chovp chffvrf chffl dhrre dhrref
encr encrq encrf encvat encvfg encvfgf erqarpx erqarpxf ergneq ergneqrq ergneqf fpebgn fpebghz
fpebghzf frzra frk frkrq frkrf frkvre frkvrfg frkhny frkhnyyl frkl funt funttrq funttvat funtf fuvg
fuvgr fuvgrf fuvgurnq fuvgyrff fuvgybnq fuvgf fuvggrq fuvggvre fuvggvat fuvggl fxnax fxnaxf fxnaxl
fynt fyntf fyhg fyhgf fyhggvre fyhggvfu fyhggl fbqbzvfr fbqbzvgr fbqbzvmr fbqbzl fcnfgvp fcnfgvpf
fcrez fcrezf fcvp fchax fgevccre grfgvpyr gvg gvgf gvggvrf gvggl gbffre gbffref genaal gheq gheqf
gjng gjngf intvan intvanr intvany intvanf ivoengbe jnax jnaxrq jnaxre jnaxref jnaxvat jnaxf jrgonpx
jrgonpxf juber juberf jbc lvq lvqf
"""


def read_list(z, name):
    out = []
    for line in z.read(name).decode("latin-1").splitlines():
        w = line.strip().rstrip("$^&+!")
        if re.fullmatch("[a-z]{%d,%d}" % (MINLEN, MAXLEN), w):
            out.append(w)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        sys.exit("sha256 mismatch: got %s, want %s (download %s)" % (digest, SHA256, URL))
    import io
    z = zipfile.ZipFile(io.BytesIO(data))
    block = set(codecs.decode(BLOCK_ROT13, "rot13").split())
    words = sorted(set(read_list(z, "International/3of6game.txt")) - block)
    have = set(words)
    core = set(read_list(z, "American/3esl.txt")) & have
    common = set(core)
    for w in core:
        for suf in ("s", "es", "ed", "d", "ing"):
            if w + suf in have:
                common.add(w + suf)
        if w.endswith("e") and w[:-1] + "ing" in have:
            common.add(w[:-1] + "ing")
        if w.endswith("y"):
            for suf in ("ies", "ied"):
                if w[:-1] + suf in have:
                    common.add(w[:-1] + suf)
    common -= block

    out = []
    out.append("/* GENERATED by tools/gen_dict.py - do not edit.\n")
    out.append(" * Word list for the word games: 12dicts 6.0.2 (Alan Beale), lists 3of6game (accepted words) and\n")
    out.append(" * 3esl (marks common words), released to the PUBLIC DOMAIN by their author (12dicts ReadMe; acknowledgment\n")
    out.append(" * requested: thank you, Alan Beale). Source: %s\n" % URL)
    out.append(" * sha256 %s. Filtered: a-z only, %d-%d letters, offensive words removed.\n" % (SHA256, MINLEN, MAXLEN))
    out.append(" * Format: DICT_Wn holds the n-letter words, sorted, concatenated; a common word starts with an upper-case letter.\n")
    out.append(" * The arrays are defined once, in dict_words.c (DICT_WORDS_DEFINE); other files get extern declarations. */\n")
    out.append("#ifndef DICT_WORDS_H\n#define DICT_WORDS_H\n\n")
    out.append("#define DICT_MINLEN %d\n#define DICT_MAXLEN %d\n" % (MINLEN, MAXLEN))
    counts = {}
    for n in range(MINLEN, MAXLEN + 1):
        ws = [w for w in words if len(w) == n]
        counts[n] = len(ws)
        out.append("#define DICT_N%d %d\n" % (n, len(ws)))
    total = sum(counts.values())
    out.append("#define DICT_TOTAL %d\n#define DICT_COMMON %d\n\n" % (total, len(common)))
    out.append("#ifdef DICT_WORDS_DEFINE\n")
    for n in range(MINLEN, MAXLEN + 1):
        ws = [(w[0].upper() + w[1:]) if w in common else w for w in words if len(w) == n]
        out.append("const char DICT_W%d[] =\n" % n)
        per = max(1, 112 // n)
        for i in range(0, len(ws), per):
            out.append('\t"%s"\n' % "".join(ws[i:i + per]))
        out.append(";\n\n")
    out.append("#else\n")
    out.append("extern const char %s;\n\n" % ", ".join("DICT_W%d[]" % n for n in range(MINLEN, MAXLEN + 1)))
    out.append(HELPERS)
    out.append("#endif\n")
    out.append("\n#endif\n")
    open(sys.argv[2], "w").write("".join(out))
    print("%d words (%d common), %s" % (total, len(common), ", ".join("%d:%d" % kv for kv in sorted(counts.items()))))


HELPERS = r"""/* --- lookups --- */
static const char *const DICT_W[DICT_MAXLEN + 1] = { 0, 0, 0, DICT_W3, DICT_W4, DICT_W5, DICT_W6, DICT_W7, DICT_W8 };
static const int DICT_N[DICT_MAXLEN + 1] = { 0, 0, 0, DICT_N3, DICT_N4, DICT_N5, DICT_N6, DICT_N7, DICT_N8 };

static inline int dict_count(int len) { return len >= DICT_MINLEN && len <= DICT_MAXLEN ? DICT_N[len] : 0; }

/* 1 if the i-th word of that length is a common one */
static inline int dict_common(int len, int i) { char c = DICT_W[len][i * len]; return c >= 'A' && c <= 'Z'; }

/* the i-th word of that length, lower case, NUL-terminated (out needs len + 1 bytes) */
static inline void dict_word(int len, int i, char *out) {
	const char *p = DICT_W[len] + i * len;
	for (int k = 0; k < len; k++) out[k] = (char)(p[k] | 0x20);
	out[len] = 0;
}

/* index of the lower-case word w (len letters) in its length list, or -1 */
static inline int dict_find(const char *w, int len) {
	if (len < DICT_MINLEN || len > DICT_MAXLEN) return -1;
	const char *base = DICT_W[len];
	int lo = 0, hi = DICT_N[len] - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2, d = 0;
		const char *p = base + mid * len;
		for (int k = 0; k < len && !d; k++) d = (unsigned char)(p[k] | 0x20) - (unsigned char)w[k];
		if (!d) return mid;
		if (d < 0) lo = mid + 1; else hi = mid - 1;
	}
	return -1;
}
"""

if __name__ == "__main__":
    main()

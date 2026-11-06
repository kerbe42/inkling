#!/usr/bin/env python3
"""Check the Word Search themes in device/app/games/ws_themes.h.

Rules (exit status 1 if any is broken):
  - theme names: lower-case letters and spaces, at most 24 characters, unique (and unique 32-bit FNV-1a hashes, which
    the "no repeats" bag stores); words: A-Z only, separated by single spaces, 4 to 14 letters, no duplicates, no
    palindromes, no word that is another word of the theme backwards; 40 to 120 words per theme;
  - every level works with margin. The levels (see N/NW/MINLEN/MAXLEN/NLONG in wordsearch.c) need:
      Easy   12 words of 4-8 letters               (a theme must have at least 18)
      Medium 20 words of 5-10 letters, 6 of 8+     (at least 28, 10 of them 8+)
      Hard   28 words of 6-14 letters, 14 of 8+    (at least 36, 20 of them 8+)
    and wordsearch.c's word picker (no word inside another) is run 300 times per level and must never come up short;
  - no brand or trademark names (BRANDS below) and nothing offensive (gen_dict.py's block list plus EXTRA_BLOCK);
  - spelling: each word must be in a word list - /usr/share/dict/{words,american-english,british-english} and, if
    given, the 12dicts lists - either whole, as a phrase from 12dicts' 6phrase list with the spaces taken out (SEA
    CUCUMBER), or joined from two or three listed words of 3+ letters (HORSESHOE+CRAB). Words no list knows are
    printed; each must be checked by hand and then added to REVIEWED with the reason it is right.

Usage: python3 check_themes.py [path/to/ws_themes.h] [--12dicts 12dicts-6.0.2.zip] [-v]
  (12dicts: see gen_dict.py for the download; also found via $TWELVEDICTS or next to this script)
"""
import codecs
import os
import random
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_dict import BLOCK_ROT13  # noqa: E402

N, NW, MINLEN, MAXLEN, NLONG = [15, 20, 25], [12, 20, 28], [4, 5, 6], [8, 10, 14], [0, 6, 14]
NEED = [(18, 0), (28, 10), (36, 20)]  # per level: qualifying words, of them 8+ letters
LEVELS = ["easy", "medium", "hard"]
MINWORDS, MAXWORDS = 40, 120

# Brand / trademark names (and generic-sounding ones that are still trademarks somewhere). Never use these.
BRANDS = set("""
FRISBEE JACUZZI VELCRO LEGO ZAMBONI POPSICLE KLEENEX STYROFOAM PLEXIGLAS PLEXIGLASS TEFLON LYCRA KEVLAR VASELINE
BANDAID QTIP TUPPERWARE CROCKPOT PINGPONG HACKYSACK DUMPSTER JEEP TASER WALKMAN SEGWAY XEROX POSTIT ROLODEX THERMOS
BUBBLEWRAP JETSKI ROLLERBLADE ROLLERBLADES CHAPSTICK BOTOX TABASCO NUTELLA OREO JELLO SPAM TWINKIE DORITOS CHEERIOS
COCACOLA COKE PEPSI ZIPLOC SARAN WINDEX LYSOL CLOROX BRILLO FORMICA FIBERGLAS ONESIE BOOGIEBOARD SNOWCAT SNOCAT
MONOPOLY SCRABBLE JENGA TWISTER YAHTZEE PARCHEESI LAUNDROMAT HOOVER GOOGLE IPAD IPHONE KINDLE SHARPIE CRAYOLA
PLAYDOH SLINKY ETCHASKETCH SKITTLES HUMVEE VESPA AQUALUNG DRAMAMINE NOVOCAIN NOVOCAINE ADRENALIN TYLENOL ASPIRIN
POLAROID TECHNICOLOR CINEMASCOPE MUZAK WIFFLEBALL STETSON BIRO TANNOY HOVERBOARD TARMAC AUTOHARP LAVALAMP HULAHOOP
JETWAY KITTYLITTER PLASTICINE CRISCO SPORK TIVO TOFURKY JAZZERCISE SPEEDO
""".split())

EXTRA_BLOCK = set("""
GYPSY GYPSIES ESKIMO ESKIMOS REDSKIN SQUAW MIDGET RETARD SPASTIC CRIPPLE LUNATIC SAVAGE HOOKER STRIPPER BIKINI
GUNFIGHT GUNSHOT MURDER KILLER SUICIDE COCAINE MARIJUANA WHISKEY WHISKY VODKA TEQUILA BEER WINE CHAMPAGNE COCKTAIL
CIGAR CIGARETTE TOBACCO CASINO GAMBLING HEROIN OPIUM
""".split())

# Words the lists don't know, checked by hand (spelling as in English-language references). word -> why it is right.
REVIEWED = {}
for _why, _words in [
    ("dinosaur or other prehistoric reptile", "ANKYLOSAURUS BARYONYX CARNOTAURUS COMPSOGNATHUS DEINONYCHUS GALLIMIMUS "
     "GIGANOTOSAURUS MAIASAURA MOSASAUR OVIRAPTOR PROTOCERATOPS PTERANODON SPINOSAURUS TROODON"),
    ("dog breed", "BRIARD HAVANESE KEESHOND LABRADOODLE LHASAAPSO PAPILLON SALUKI SHARPEI SHIBAINU SHIHTZU VIZSLA "
     "WEIMARANER WESTIE"),
    ("animal", "BILBY CURRAWONG CUSCUS GOANNA PADEMELON QUOKKA QUOLL FENNECFOX TAMARIN CAECILIAN TUATARA DIKDIK SERVAL "
     "NUDIBRANCH"),
    ("food or drink", "BABKA BEIGNET PANETTONE CHURRO GELATO AMERICANO HORCHATA KEFIR MACCHIATO LINGONBERRY LOVAGE HALVA "
     "CAPRESE FARFALLE FUSILLI ORECCHIETTE OSSOBUCO PANNACOTTA PAPPARDELLE ARANCINI INJERA PIEROGI SPANAKOPITA "
     "YAKITORI"),
    ("place", "BURKINAFASO ESWATINI TAJMAHAL BANFF KAMLOOPS KELOWNA MIRAMICHI MONCTON KYIV NAMIB SANTORINI TENERIFE "
     "COSTARICA CUSCO ELSALVADOR MACHUPICCHU TIERRADELFUEGO KOLKATA BUENOSAIRES SAOPAULO KARNAK LUXOR"),
    ("ancient Egypt", "CANOPICJAR MASTABA TUTANKHAMUN"),
    ("myth, legend or fairy tale", "THUMBELINA ARGONAUTS KRONOS CHUPACABRA HIPPOGRIFF WYVERN BIFROST FENRIR FRIGG "
     "HEIMDALL MIDGARD MJOLNIR RAGNAROK SLEIPNIR"),
    ("astronomy", "MAGNETAR DIONE ENCELADUS HAUMEA KUIPERBELT MIMAS CANISMAJOR CANISMINOR"),
    ("anatomy, medicine or geometry", "OTOSCOPE CALCANEUS HYOID OSSICLES ICOSAHEDRON"),
    ("mineral or gem", "AZURITE TANZANITE"),
    ("dance, game or language", "BACHATA PASODOBLE GOFISH MANCALA SHOGI OJIBWE"),
    ("phrase (hall of mirrors, tucked in)", "HALLOFMIRRORS TUCKEDIN"),
]:
    REVIEWED.update(dict.fromkeys(_words.split(), _why))


def fnv1a(s):
    h = 2166136261
    for ch in s.encode():
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def parse_header(path):
    src = open(path, encoding="ascii").read()
    m = re.search(r"THEMES\[\]\s*=\s*\{(.*)\n\};", src, re.S)
    if not m:
        sys.exit("no THEMES[] = { ... }; in " + path)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    themes = []
    for e in re.finditer(r"\{\s*((?:\"[^\"\\]*\"\s*)+),\s*((?:\"[^\"\\]*\"\s*)+)\}", body):
        name = "".join(re.findall(r"\"([^\"\\]*)\"", e.group(1)))
        words = "".join(re.findall(r"\"([^\"\\]*)\"", e.group(2)))
        themes.append((name, words))
    if len(themes) != body.count("{"):
        sys.exit("could not parse every theme entry (%d parsed, %d braces)" % (len(themes), body.count("{")))
    return themes


def load_words(zip_path):
    words, phrases = set(), set()
    for p in ("/usr/share/dict/words", "/usr/share/dict/american-english", "/usr/share/dict/british-english"):
        if os.path.exists(p):
            for line in open(p, encoding="utf-8", errors="replace"):
                w = line.strip()
                if w.endswith("'s"):
                    continue
                if re.fullmatch(r"[A-Za-z]+", w):
                    words.add(w.upper())
    have12 = False
    if zip_path and os.path.exists(zip_path):
        have12 = True
        with zipfile.ZipFile(zip_path) as z:
            for name in z.namelist():
                if not name.endswith(".txt") or name.endswith("agid.txt"):
                    continue
                for line in z.read(name).decode("latin-1").splitlines():
                    for t in re.findall(r"[A-Za-z]+", line):
                        words.add(t.upper())
                    for t in re.findall(r"[A-Za-z]+(?:-[A-Za-z]+)+", line):  # forget-me-not
                        words.add(t.replace("-", "").upper())
                    if name.endswith("6phrase.txt"):
                        joined = re.sub(r"[^A-Za-z]", "", line.split(";")[0])
                        if joined:
                            phrases.add(joined.upper())
    return words, phrases, have12


def splits(w, words, parts):
    """Can w be joined from up to `parts` listed words of 3+ letters? Returns the pieces or None."""
    if w in words and len(w) >= 3:
        return [w]
    if parts == 1:
        return None
    for i in range(3, len(w) - 2):
        if w[:i] in words:
            rest = splits(w[i:], words, parts - 1)
            if rest:
                return [w[:i]] + rest
    return None


def picker_ok(words, lvl, rng, tries=300):
    pool = [w for w in words if MINLEN[lvl] <= len(w) <= MAXLEN[lvl] and len(w) <= N[lvl] and w != w[::-1]]
    for _ in range(tries):
        rng.shuffle(pool)
        pick, taken, nlong = [], set(), 0
        for pas in (0, 1):
            for k, w in enumerate(pool):
                if len(pick) >= NW[lvl]:
                    break
                if k in taken or (pas == 0 and (len(w) < 8 or nlong >= NLONG[lvl])):
                    continue
                if any(w in p or p in w for p in pick):
                    continue
                taken.add(k)
                pick.append(w)
                nlong += len(w) >= 8
        if len(pick) < NW[lvl] or nlong < NLONG[lvl]:
            return False
    return True


def main():
    args = sys.argv[1:]
    verbose = "-v" in args
    args = [a for a in args if a != "-v"]
    zip_path = os.environ.get("TWELVEDICTS") or os.path.join(HERE, "12dicts-6.0.2.zip")
    if "--12dicts" in args:
        i = args.index("--12dicts")
        zip_path = args[i + 1]
        del args[i:i + 2]
    header = args[0] if args else os.path.join(HERE, "..", "ws_themes.h")
    themes = parse_header(header)
    block = set(codecs.decode(BLOCK_ROT13, "rot13").upper().split()) | EXTRA_BLOCK
    words, phrases, have12 = load_words(zip_path)
    rng = random.Random(1)
    errors, unknown, joined, used_reviewed = [], {}, [], set()
    names, hashes = set(), {}

    for name, text in themes:
        tag = "'%s'" % name
        if not re.fullmatch(r"[A-Za-z]+( [A-Za-z]+)*", name) or len(name) > 24:
            errors.append("%s: theme names are letters and single spaces, at most 24 characters" % tag)
        if name in names:
            errors.append("%s: two themes with this name" % tag)
        names.add(name)
        h = fnv1a(name)
        if h in hashes:
            errors.append("%s: same FNV-1a hash as '%s'" % (tag, hashes[h]))
        hashes[h] = name
        if text != text.strip() or "  " in text:
            errors.append("%s: words must be separated by single spaces (no leading/trailing space)" % tag)
        ws = text.split()
        if not MINWORDS <= len(ws) <= MAXWORDS:
            errors.append("%s: %d words (want %d-%d)" % (tag, len(ws), MINWORDS, MAXWORDS))
        seen = set()
        for w in ws:
            if not re.fullmatch(r"[A-Z]+", w):
                errors.append("%s: '%s' is not A-Z" % (tag, w))
                continue
            if not 4 <= len(w) <= 14:
                errors.append("%s: '%s' has %d letters (4-14, or it can never be used)" % (tag, w, len(w)))
            if w in seen:
                errors.append("%s: '%s' twice" % (tag, w))
            seen.add(w)
            if w == w[::-1]:
                errors.append("%s: '%s' is a palindrome" % (tag, w))
            elif w[::-1] in ws:
                errors.append("%s: '%s' is '%s' backwards" % (tag, w, w[::-1]))
            if w in BRANDS:
                errors.append("%s: '%s' is a brand/trademark name" % (tag, w))
            if w in block:
                errors.append("%s: '%s' is on the block list" % (tag, w))
            if w in REVIEWED:
                used_reviewed.add(w)
                continue
            if w in words:
                continue
            if w in phrases:
                joined.append((name, w, "phrase"))
                continue
            sp = splits(w, words, 3)
            if sp:
                joined.append((name, w, "+".join(sp)))
                continue
            unknown.setdefault(w, []).append(name)
        for lvl in range(3):
            q = [w for w in set(ws) if MINLEN[lvl] <= len(w) <= MAXLEN[lvl] and w != w[::-1]]
            nl = sum(len(w) >= 8 for w in q)
            need, needl = NEED[lvl]
            if len(q) < need or nl < needl:
                errors.append("%s: %s has %d words of %d-%d letters, %d of 8+ (need %d, %d of 8+)" %
                              (tag, LEVELS[lvl], len(q), MINLEN[lvl], MAXLEN[lvl], nl, need, needl))
            elif not picker_ok(sorted(set(ws)), lvl, rng):
                errors.append("%s: %s: the word picker came up short (too many words inside others)" % (tag, LEVELS[lvl]))

    nwords = sum(len(t.split()) for _, t in themes)
    print("%d themes, %d words (%d-%d per theme)" % (len(themes), nwords, min(len(t.split()) for _, t in themes),
                                                     max(len(t.split()) for _, t in themes)))
    if not have12:
        print("note: 12dicts not found (%s): spelling is checked against /usr/share/dict only, which lacks most\n"
              "      compounds and plurals, so the words it can't verify are listed as warnings, not errors" % zip_path)
    if verbose:
        for name, w, how in joined:
            print("  joined  %-14s %-22s (%s)" % (w, how, name))
    stale = sorted(set(REVIEWED) - used_reviewed)
    for w in stale:
        print("warning: REVIEWED word '%s' is no longer used" % w)
    for w in sorted(unknown):
        msg = "spelling: '%s' (%s) is in no word list - check it, then add it to REVIEWED" % (w, ", ".join(unknown[w]))
        if have12:
            errors.append(msg)
        else:
            print("warning " + msg)
    for e in errors:
        print("ERROR " + e)
    print("%d problems" % len(errors) if errors else "all themes OK")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

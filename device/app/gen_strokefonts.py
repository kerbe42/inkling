#!/usr/bin/env python3
"""Turns the single-stroke fonts in fonts/ into hershey_font.h (types), stroke_fonts.c (the Hershey fonts and the font
table) and ofl_fonts.h (the SIL Open Font License fonts, included by stroke_fonts.c) for the Kindle app. Run it after changing FONTS below or a font file; it prints one line per font.

Sources (credits and licences: fonts/FONT-CREDITS.txt):
  fonts/*.jhf      Hershey fonts, James Hurt format (fonts/HERSHEY-NOTICE.txt). Copied unchanged: Hershey units,
                   cap height about 21.
  fonts/svg/*.svg  SVG 1.1 single-line fonts (fonts/OFL.txt). Paths (move/line/quadratic/cubic/arc, absolute or
                   relative) are flattened into polylines and scaled to a cap height of about 64 units so curves stay
                   smooth in signed char coordinates.

Glyph format (both): ASCII 32..126; points are signed char (x, y) pairs relative to the glyph centre, y down, baseline
at y = 9; (-128, -128) is a pen-up; left/right are the advance edges; HFont.cap is the cap height in font units (the
renderers scale by the requested cap in pixels / cap)."""
import math, pathlib, re, sys

here = pathlib.Path(__file__).parent
SRC = here / 'fonts'
OUT = here
BASE = 9          # baseline y in font units (the Hershey convention; games.c and layout.c assume it)
TARGET_CAP = 64   # SVG fonts: cap height in font units (shrunk per font if a glyph would not fit in -127..127)

# id (config value, never change one), label (settings chip), group (settings sub-heading), source, slant (accent
# shift per unit of height, percent), size (1 = cap height matches the other fonts; >1 writes it bigger).
# Groups: cursive (joined-up), print, italic (italic and calligraphic hands), fancy (blackletter and decorative).
FONTS = [
    # joined-up handwriting
    ('script',       'Classic looped cursive',   'cursive', 'scripts.jhf',              22, 1.0),
    ('elegant',      'Bold looped cursive',      'cursive', 'scriptc.jhf',              22, 1.0),
    ('cursive',      'Simple cursive',           'cursive', 'cursive.jhf',              22, 1.0),
    ('copperplate',  'Copperplate-style script', 'cursive', 'EMSDecorousScript.svg',    25, 1.0),
    ('flowing',      'Formal flowing script',    'cursive', 'EMSAllure.svg',            25, 1.05),
    ('signature',    'Old-fashioned signature',  'cursive', 'EMSSociety.svg',           30, 1.3),
    ('retro',        'Thin retro script',        'cursive', 'EMSCapitol.svg',           15, 1.2),
    ('swirly',       'Swirly looped script',     'cursive', 'EMSLeague.svg',            25, 1.0),
    ('brush',        'Brush script',             'cursive', 'EMSBrush.svg',             25, 1.1),
    ('modern',       'Modern semi-joined hand',  'cursive', 'EMSNeato.svg',             15, 1.1),
    # print
    ('print',        'Neat print',               'print',   'futural.jhf',               0, 1.0),
    ('boldprint',    'Bold print',               'print',   'futuram.jhf',               0, 1.0),
    ('serif',        'Classic book serif',       'print',   'timesr.jhf',                0, 1.0),
    ('clean',        'Clean modern print',       'print',   'EMSReadability.svg',        0, 1.0),
    ('rounded',      'Rounded friendly print',   'print',   'EMSDelight.svg',            0, 1.0),
    ('marker',       'Casual marker print',      'print',   'EMSCasualHand.svg',         5, 1.0),
    ('architect',    'Architect-style lettering', 'print',  'EMSTech.svg',               0, 1.0),
    ('playful',      'Playful print',            'print',   'EMSPancakes.svg',           0, 1.0),
    ('typewriter',   'Typewriter-style',         'print',   'EMSNixish.svg',             0, 1.0),
    # italic and calligraphic hands
    ('italic',       'Book italic',              'italic',  'timesi.jhf',               22, 1.0),
    ('modernitalic', 'Modern italic',            'italic',  'EMSReadabilityItalic.svg', 18, 1.0),
    ('calligraphic', 'Calligraphic italic',      'italic',  'EMSFelix.svg',             15, 1.0),
    ('chancery',     'Chancery-style hand',      'italic',  'EMSInvite.svg',            20, 1.15),
    ('finescript',   'Fine calligraphy',         'italic',  'EMSSwiss.svg',             25, 1.05),
    # blackletter and decorative
    ('gothic',       'English blackletter',      'fancy',   'gothiceng.jhf',             0, 1.0),
    ('rotunda',      'Italian blackletter',      'fancy',   'gothicita.jhf',             0, 1.0),
    ('storybook',    'Storybook',                'fancy',   'EMSElfin.svg',              0, 1.0),
    ('fairytale',    'Fairy-tale script',        'fancy',   'EMSLittlePrincess.svg',    10, 1.1),
    ('deco',         'Art Deco-style',           'fancy',   'EMSHerculean.svg',          0, 1.0),
    ('scifi',        'Sci-fi lettering',         'fancy',   'EMSOsmotron.svg',           0, 1.0),
]


# ---------- Hershey .jhf ----------

def load_jhf(path):
    """Returns (glyphs, cap): glyphs = list of 95 (left, right, strokes); strokes = lists of (x, y) ints."""
    lines = [l.rstrip('\n') for l in path.read_text(encoding='latin-1').splitlines() if l.strip()]
    recs, buf = [], ''
    for l in lines:
        buf = l if not buf else buf + l
        if len(buf) - 8 >= 2 * int(buf[5:8]):
            recs.append(buf); buf = ''
    assert len(recs) >= 95, (path, len(recs))
    glyphs = []
    for r in recs[:95]:
        n = int(r[5:8]); data = r[8:8 + 2 * n]
        left, right = ord(data[0]) - 82, ord(data[1]) - 82
        strokes, cur = [], []
        for i in range(2, len(data), 2):
            pair = data[i:i + 2]
            if pair == ' R':
                strokes.append(cur); cur = []
            else:
                cur.append((ord(pair[0]) - 82, ord(pair[1]) - 82))
        strokes.append(cur)
        glyphs.append((left, right, strokes))
    # cap height in font units: baseline (9) minus the top of 'H'
    return glyphs, BASE - min(y for s in glyphs[ord('H') - 32][2] for x, y in s)


# ---------- SVG fonts ----------

NUM = r'[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?'


def unescape(t):
    t = t.replace('&quot;', '"').replace('&lt;', '<').replace('&gt;', '>').replace('&apos;', "'")
    t = re.sub(r'&#x([0-9a-fA-F]+);', lambda m: chr(int(m.group(1), 16)), t)
    t = re.sub(r'&#([0-9]+);', lambda m: chr(int(m.group(1))), t)
    return t.replace('&amp;', '&')


def attr(tag, name, default=None):
    m = re.search(r'(?<![-\w])' + name + r'\s*=\s*"([^"]*)"', tag)
    return m.group(1) if m else default


def path_subpaths(d, tol):
    """Flattens an SVG path into subpaths (lists of (x, y) floats). tol = max chord deviation in source units."""
    toks = re.findall(r'[MmLlHhVvCcSsQqTtAaZz]|' + NUM, d)
    subs, cur = [], []
    x = y = sx = sy = 0.0
    lc = None  # last control point (for S/T)
    cmd, prev, i = None, '', 0

    def num():
        nonlocal i
        v = float(toks[i]); i += 1
        return v

    def bez(pts):
        # adaptive enough: segment count from the control polygon length
        L = sum(math.dist(pts[k], pts[k + 1]) for k in range(len(pts) - 1))
        n = max(2, int(math.ceil(math.sqrt(L / max(tol, 1e-6)) * 0.9)))
        out = []
        for k in range(1, n + 1):
            t = k / n
            q = list(pts)
            while len(q) > 1:
                q = [((1 - t) * q[j][0] + t * q[j + 1][0], (1 - t) * q[j][1] + t * q[j + 1][1]) for j in range(len(q) - 1)]
            out.append(q[0])
        return out

    def arc(x1, y1, rx, ry, phi, fa, fs, x2, y2):
        if rx == 0 or ry == 0:
            return [(x2, y2)]
        rx, ry = abs(rx), abs(ry)
        c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
        dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
        x1p, y1p = c * dx + s * dy, -s * dx + c * dy
        lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
        if lam > 1:
            rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
        num_ = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
        den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
        co = math.sqrt(max(0, num_ / den)) if den else 0
        if fa == fs:
            co = -co
        cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
        cx, cy = c * cxp - s * cyp + (x1 + x2) / 2, s * cxp + c * cyp + (y1 + y2) / 2

        def ang(ux, uy, vx, vy):
            a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
            return a
        t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
        dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
        if not fs and dt > 0:
            dt -= 2 * math.pi
        elif fs and dt < 0:
            dt += 2 * math.pi
        n = max(2, int(math.ceil(abs(dt) * math.sqrt(max(rx, ry) / max(tol, 1e-6)) / 1.5)))
        out = []
        for k in range(1, n + 1):
            t = t1 + dt * k / n
            px, py = rx * math.cos(t), ry * math.sin(t)
            out.append((c * px - s * py + cx, s * px + c * py + cy))
        return out

    while i < len(toks):
        if re.fullmatch(r'[A-Za-z]', toks[i]):
            cmd = toks[i]; i += 1
            if cmd in 'Zz':
                if cur:
                    if math.dist(cur[-1], cur[0]) > 1e-9:
                        cur.append(cur[0])
                    subs.append(cur)
                cur = []
                x, y = sx, sy
                lc = None
                continue
        rel = cmd.islower()
        C = cmd.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if C == 'M':
            if cur:
                subs.append(cur)
            x, y = ox + num(), oy + num()
            sx, sy = x, y
            cur = [(x, y)]
            cmd = 'l' if rel else 'L'  # implicit lineto after a moveto
            lc = None
        elif C == 'L':
            x, y = ox + num(), oy + num(); cur.append((x, y)); lc = None
        elif C == 'H':
            x = ox + num(); cur.append((x, y)); lc = None
        elif C == 'V':
            y = oy + num(); cur.append((x, y)); lc = None
        elif C == 'C':
            c1 = (ox + num(), oy + num()); c2 = (ox + num(), oy + num()); e = (ox + num(), oy + num())
            cur += bez([(x, y), c1, c2, e]); lc = c2; x, y = e
        elif C == 'S':
            c1 = (2 * x - lc[0], 2 * y - lc[1]) if lc and prev in 'CcSs' else (x, y)
            c2 = (ox + num(), oy + num()); e = (ox + num(), oy + num())
            cur += bez([(x, y), c1, c2, e]); lc = c2; x, y = e
        elif C == 'Q':
            c1 = (ox + num(), oy + num()); e = (ox + num(), oy + num())
            cur += bez([(x, y), c1, e]); lc = c1; x, y = e
        elif C == 'T':
            c1 = (2 * x - lc[0], 2 * y - lc[1]) if lc and prev in 'QqTt' else (x, y)
            e = (ox + num(), oy + num())
            cur += bez([(x, y), c1, e]); lc = c1; x, y = e
        elif C == 'A':
            rx, ry, phi, fa, fs = num(), num(), num(), num(), num()
            e = (ox + num(), oy + num())
            cur += arc(x, y, rx, ry, phi, int(fa), int(fs), e[0], e[1]); lc = None; x, y = e
        else:
            raise ValueError('path command ' + cmd)
        if not cur:
            cur = [(x, y)]
        prev = cmd
    if cur:
        subs.append(cur)
    return [s for s in subs if len(s) >= 1]


def simplify(pts, eps):
    """Douglas-Peucker."""
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts); keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        a, b = stack.pop()
        ax, ay = pts[a]; bx, by = pts[b]
        L = math.hypot(bx - ax, by - ay)
        best, bi = -1, -1
        for k in range(a + 1, b):
            px, py = pts[k]
            dd = abs((bx - ax) * (ay - py) - (ax - px) * (by - ay)) / L if L > 1e-9 else math.hypot(px - ax, py - ay)
            if dd > best:
                best, bi = dd, k
        if best > eps:
            keep[bi] = True
            stack += [(a, bi), (bi, b)]
    return [p for p, k in zip(pts, keep) if k]


def load_svg(path, size):
    s = path.read_text(encoding='utf-8')
    font = re.search(r'<font\b[^>]*>', s).group(0)
    dflt_adv = float(attr(font, 'horiz-adv-x', '500'))
    raw = {}
    for tag in re.findall(r'<glyph\b[^>]*?/?>', s, re.S):
        u = attr(tag, 'unicode')
        if u is None:
            continue
        u = unescape(u)
        if len(u) != 1 or not 32 <= ord(u) <= 126:
            continue
        raw[ord(u)] = (float(attr(tag, 'horiz-adv-x', dflt_adv)), attr(tag, 'd', '') or '')
    missing = [chr(c) for c in range(32, 127) if c not in raw]
    flat = {}
    for c, (adv, d) in raw.items():
        sps = path_subpaths(d, 1.0)
        # a few source glyphs carry a stray copy of a stroke far off to the side (e.g. ';' at x = 3600): drop it
        keep = [sp for sp in sps if min(x for x, y in sp) < adv + 1500 and max(x for x, y in sp) > -1500]
        if len(keep) < len(sps):
            print(f'  {path.name}: dropped {len(sps) - len(keep)} stray stroke(s) from {chr(c)!r}', file=sys.stderr)
        flat[c] = (adv, keep)

    def tops(chars):
        v = sorted(max(y for sp in flat[ord(ch)][1] for x, y in sp) for ch in chars if ord(ch) in flat and flat[ord(ch)][1])
        return v[len(v) // 2]
    capH = tops('EFHIKLNTZ')
    xh = tops('xzvwnmu')
    # scale: cap -> TARGET_CAP, shrunk if any coordinate would leave -127..127
    f = TARGET_CAP / capH
    ext = 0
    for c, (adv, sps) in flat.items():
        for sp in sps:
            for x, y in sp:
                ext = max(ext, abs(x - adv / 2), abs(y) + BASE)
    f = min(f, 126.0 / ext)
    glyphs = []
    for c in range(32, 127):
        if c not in flat:
            glyphs.append(None)
            continue
        adv, sps = flat[c]
        half = adv * f / 2
        strokes = []
        for sp in sps:
            pts = [((x * f) - half, BASE - y * f) for x, y in sp]
            pts = simplify(pts, 0.3)
            ip = []
            for x, y in pts:
                q = (int(round(x)), int(round(y)))
                if not ip or ip[-1] != q:
                    ip.append(q)
            if strokes and strokes[-1] and math.dist(strokes[-1][-1], ip[0]) <= 1.0:
                strokes[-1] += ip[1:] if strokes[-1][-1] == ip[0] else ip
            else:
                strokes.append(ip)
        left = -int(round(half)); right = int(round(adv * f)) + left
        glyphs.append((left, right, [st for st in strokes if st]))
    cap = int(round(capH * f / size))
    return glyphs, cap, missing, dict(f=f, capH=capH, xh=xh, ext=ext)


def dotless(glyphs, name):
    """Appends dotless i and j (glyphs 95 and 96: the renderer writes them for an accented i/j and for the dotless i)
    and moves the dots of i and j after the stems, so the hand writes the stem first. A dot is a stroke lying
    entirely above the top of the tallest stroke."""
    for ch in 'ij':
        l, r, st = glyphs[ord(ch) - 32]
        main = max(st, key=lambda k: max(p[1] for p in k) - min(p[1] for p in k))
        top = min(p[1] for p in main)
        dots = [k for k in st if max(p[1] for p in k) < top]
        if not dots:
            print(f'  {name}: no dot found on {ch!r}', file=sys.stderr)
        body = [k for k in st if k not in dots]
        glyphs[ord(ch) - 32] = (l, r, body + dots)
        glyphs.append((l, r, body))


def encode(glyphs):
    pts, out = [], []
    q = next(g for g in glyphs if g)  # stand-in for a missing glyph: '?' if present
    qm = glyphs[ord('?') - 32] or q
    for gi, g in enumerate(glyphs):
        left, right, strokes = g or qm
        if gi >= 95:  # dotless i / j: the first strokes of 'i' / 'j' (their dots come last)
            fl, fr, foff, _ = out[ord('ij'[gi - 95]) - 32]
            out.append((left, right, foff, sum(len(st) for st in strokes) + len(strokes) - 1))
            continue
        off = len(pts) // 2; cnt = 0
        for k, st in enumerate(strokes):
            if k:
                pts += [-128, -128]; cnt += 1
            for x, y in st:
                assert -127 <= x <= 127 and -127 <= y <= 127, (x, y)
                pts += [x, y]; cnt += 1
        out.append((left, right, off, cnt))
    return pts, out


def emit(fid, pts, glyphs, out):
    out.append(f'static const signed char HF_{fid}_PTS[] = {{')
    for i in range(0, len(pts), 24):
        out.append('\t' + ','.join(str(v) for v in pts[i:i + 24]) + ',')
    out.append('};')
    out.append(f'static const HGlyph HF_{fid}_G[97] = {{')
    for i in range(0, 97, 4):
        out.append('\t' + ' '.join('{ %d, %d, %d, %d },' % g for g in glyphs[i:i + 4]))
    out.append('};')


def main():
    hdr = ['/* Generated by gen_strokefonts.py. Do not edit. The handwriting (single-stroke) fonts: the data is in',
           ' * stroke_fonts.c (Hershey fonts) and ofl_fonts.h (SIL Open Font License fonts); see fonts/FONT-CREDITS.txt. */',
           '#ifndef HERSHEY_FONT_H', '#define HERSHEY_FONT_H',
           '/* glyphs: ASCII 32..126 (index c - 32), then dotless i and dotless j */',
           'typedef struct { signed char left, right; unsigned short off, n; } HGlyph;',
           '#define HG_DOTLESS_I 95', '#define HG_DOTLESS_J 96',
           '/* cap: cap height in font units; group: settings sub-heading; slant: accent shift per unit of height, % */',
           'typedef struct { const char *id, *label; const HGlyph *g; const signed char *pts; int cap; const char *group; int slant; } HFont;',
           'extern const HFont HFONTS[];', 'extern const int hfont_count;', '#define NHFONTS hfont_count', '#endif', '']
    data = ['/* Generated by gen_strokefonts.py from the fonts/ folder. Do not edit.',
            ' * The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the U. S. National Bureau',
            ' * of Standards. The format of the font data was originally created by James Hurt, Cognition, Inc.',
            ' * See fonts/HERSHEY-NOTICE.txt. The other fonts (ofl_fonts.h) are under the SIL Open Font License 1.1:',
            ' * fonts/OFL.txt and fonts/FONT-CREDITS.txt. */',
            '#include "hershey_font.h"']
    ofl = ['/* Generated by gen_strokefonts.py from fonts/svg/. Do not edit. Included by stroke_fonts.c.',
           ' * Font data converted from single-line SVG fonts licensed under the SIL Open Font License, Version 1.1',
           ' * (fonts/OFL.txt); copyright notices, designers and sources: fonts/FONT-CREDITS.txt. This font data is',
           ' * distributed under that licence. */']
    table, total = [], 0
    for fid, label, group, src, slant, size in FONTS:
        p = SRC / src if src.endswith('.jhf') else SRC / 'svg' / src
        if src.endswith('.jhf'):
            glyphs, cap = load_jhf(p)
            xt = min(y for st in glyphs[ord('x') - 32][2] for x, y in st)
            info = f'xh/cap {(BASE - xt) / cap:.2f}'
            target = data
        else:
            glyphs, cap, missing, m = load_svg(p, size)
            info = f'scale {m["f"]:.3f} capH {m["capH"]:.0f} xh/cap {m["xh"] / m["capH"]:.2f}' + (f' missing {"".join(missing)!r}' if missing else '')
            target = ofl
        dotless(glyphs, fid)
        pts, enc = encode(glyphs)
        emit(fid, pts, enc, target)
        total += len(pts) + 6 * 97
        table.append(f'\t{{ "{fid}", "{label}", HF_{fid}_G, HF_{fid}_PTS, {cap}, "{group}", {slant} }},')
        print(f'{fid:12s} {src:28s} {len(pts) // 2:6d} points, cap {cap:3d}  {info}', file=sys.stderr)
    data += ['#include "ofl_fonts.h"', 'const HFont HFONTS[] = {'] + table + \
        ['};', 'const int hfont_count = (int)(sizeof HFONTS / sizeof HFONTS[0]);', '']
    (OUT / 'hershey_font.h').write_text('\n'.join(hdr))
    (OUT / 'stroke_fonts.c').write_text('\n'.join(data))
    (OUT / 'ofl_fonts.h').write_text('\n'.join(ofl + ['']))
    print(f'{len(FONTS)} fonts, about {total // 1024} KB of font data', file=sys.stderr)


if __name__ == '__main__':
    main()

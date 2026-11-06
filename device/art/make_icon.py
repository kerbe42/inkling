#!/usr/bin/env python3
"""Draws the library icon for the Kindle app (black and white for e-ink): a quill writing "Inkling" in a script hand,
with a few sparkles. Output: device/art/icon.png (600x800). Uses the Hershey script font from device/app/fonts."""
import math, pathlib
from PIL import Image, ImageDraw, ImageFilter

HERE = pathlib.Path(__file__).parent
S = 4  # supersampling
W, H = 600 * S, 800 * S

def load_hershey(name):
    lines = [l.rstrip('\n') for l in (HERE.parent / 'app' / 'fonts' / f'{name}.jhf').read_text(encoding='latin-1').splitlines() if l.strip()]
    recs, buf = [], ''
    for l in lines:
        buf = l if not buf else buf + l
        if len(buf) - 8 >= 2 * int(buf[5:8]): recs.append(buf); buf = ''
    glyphs = []
    for r in recs[:95]:
        n = int(r[5:8]); d = r[8:8 + 2 * n]; L, R = ord(d[0]) - 82, ord(d[1]) - 82; strokes = [[]]
        for i in range(2, len(d), 2):
            p = d[i:i + 2]
            if p == ' R': strokes.append([])
            else: strokes[-1].append((ord(p[0]) - 82, ord(p[1]) - 82))
        glyphs.append((L, R, [s for s in strokes if s]))
    return glyphs

def text_width(g, text, s):
    return sum((g[ord(c) - 32][1] - g[ord(c) - 32][0]) * s for c in text)

def write(dr, g, text, x, base, s, width, fill):
    for c in text:
        L, R, strokes = g[ord(c) - 32]
        ox = x - L * s
        for st in strokes:
            pts = [(ox + px * s, base + (py - 9) * s) for px, py in st]
            if len(pts) > 1: dr.line(pts, fill=fill, width=width, joint='curve')
            for p in (pts[0], pts[-1]): dr.ellipse([p[0] - width / 2, p[1] - width / 2, p[0] + width / 2, p[1] + width / 2], fill=fill)
        x += (R - L) * s

def bezier(p0, p1, p2, p3, n=60):
    return [((1-t)**3*p0[0] + 3*(1-t)**2*t*p1[0] + 3*(1-t)*t*t*p2[0] + t**3*p3[0],
             (1-t)**3*p0[1] + 3*(1-t)**2*t*p1[1] + 3*(1-t)*t*t*p2[1] + t**3*p3[1]) for t in (i / n for i in range(n + 1))]

def star(dr, cx, cy, r, fill):
    pts = []
    for i in range(8):
        a = math.pi / 4 * i - math.pi / 2
        rr = r if i % 2 == 0 else r * 0.28
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    dr.polygon(pts, fill=fill)

img = Image.new('L', (W, H), 0)
dr = ImageDraw.Draw(img)
m = 30 * S
dr.rounded_rectangle([m, m, W - m, H - m], radius=44 * S, outline=255, width=6 * S)

def fit(g, text, width_px):
    return width_px * S / text_width(g, text, 1)

# title in a single-stroke script
g = load_hershey('scripts')
s = fit(g, 'Inkling', 400)
tw = text_width(g, 'Inkling', s)
x0 = (W - tw) / 2 - 30 * S
write(dr, g, 'Inkling', x0, 232 * S, s, 12 * S, 255)  # high enough that the g clears the underline
# flowing underline ending at the quill's nib
nib = (468 * S, 300 * S)
ul = bezier((105 * S, 312 * S), (230 * S, 286 * S), (340 * S, 330 * S), nib)
dr.line(ul, fill=255, width=6 * S, joint='curve')

# quill: nib at the end of the underline, feather sweeping up to the top right
tip = (585 * S, 40 * S)
shaft = bezier(nib, (500 * S, 230 * S), (540 * S, 120 * S), tip)
n = len(shaft)
left, right = [], []
for i in range(int(n * 0.28), n):
    t = (i - n * 0.28) / (n * 0.72)
    wdt = max(0.0, math.sin(math.pi * min(1, t * 1.15))) ** 0.8 * 46 * S * (1 - 0.35 * t)
    x, y = shaft[i]
    x2, y2 = shaft[min(i + 1, n - 1)] if i < n - 1 else shaft[i]
    dx, dy = x2 - x, y2 - y
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L, dx / L
    left.append((x + nx * wdt * 1.15, y + ny * wdt * 1.15))
    right.append((x - nx * wdt * 0.8, y - ny * wdt * 0.8))
dr.polygon(left + list(reversed(right)) + [tip], fill=255)
for i in range(int(n * 0.32), n - 3, 4):  # barbs: thin black cuts
    x, y = shaft[i]
    dr.line([(x, y), left[i - int(n * 0.28)]], fill=0, width=2 * S)
    dr.line([(x, y), right[i - int(n * 0.28)]], fill=0, width=2 * S)
dr.line(shaft[int(n * 0.28):], fill=0, width=5 * S, joint='curve')
dr.line(shaft[:int(n * 0.30)], fill=255, width=7 * S, joint='curve')
dr.polygon([(nib[0] - 6 * S, nib[1] - 30 * S), (nib[0] + 8 * S, nib[1] - 32 * S), nib], fill=255)

# a little conversation on ruled lines
for y in (470, 590, 710):
    dr.line([(80 * S, y * S), (520 * S, y * S)], fill=110, width=2 * S)
gp = load_hershey('futural')
sp = fit(gp, 'Hello there!', 230)
write(dr, gp, 'Hello there!', 92 * S, 455 * S, sp, 4 * S, 255)
s2 = fit(g, 'Your notebook.', 330)
write(dr, g, 'Your notebook.', 150 * S, 575 * S, s2, 5 * S, 255)
s3 = fit(g, 'Write to me!', 290)
write(dr, g, 'Write to me!', 150 * S, 695 * S, s3, 5 * S, 255)

for cx, cy, r in [(120, 360, 24), (200, 395, 12), (560, 300, 14), (535, 420, 20), (90, 540, 12), (535, 650, 12)]:
    star(dr, cx * S, cy * S, r * S, 255)

out = img.resize((600, 800), Image.LANCZOS)
out.save(HERE / 'icon.png', optimize=True)
print('icon.png', out.size, (HERE / 'icon.png').stat().st_size, 'bytes')

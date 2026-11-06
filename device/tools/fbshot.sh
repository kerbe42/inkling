#!/bin/sh
# Screenshot the Kindle over SSH: fbshot.sh OUT.png  (reads the visible half of the 8-bit framebuffer)
set -e
OUT="${1:-fbshot.png}"
PAN=$(ssh scribe-kindle 'cat /sys/class/graphics/fb0/pan' | cut -d, -f2)
ssh scribe-kindle "dd if=/dev/fb0 bs=1872 skip=${PAN:-0} count=2480 2>/dev/null" > "$OUT.raw"
python3 -I - "$OUT.raw" "$OUT" <<'PY'
import sys, zlib, struct
raw = open(sys.argv[1], 'rb').read(); W, S, H = 1860, 1872, 2480
rows = b''.join(b'\x00' + raw[y*S:y*S+W] for y in range(H))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 0, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
PY
rm -f "$OUT.raw"
echo "$OUT"

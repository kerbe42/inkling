#!/bin/sh
# Checks every handwriting font glyph and the accent placement (tests/test_fonts.c, through layout.c), then writes a
# preview sheet of every font to dist/fonts-preview.png (needs gcc and python3 + Pillow).
# Usage: device/app/tests/run_font_tests.sh
set -e
cd "$(dirname "$0")/../../.."
out=${TMPDIR:-/tmp}/scribe-font-tests
mkdir -p "$out" dist
gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined \
	-fno-sanitize-recover=undefined -I device/app -o "$out/test_fonts" device/app/tests/test_fonts.c device/app/stroke_fonts.c -lm
"$out/test_fonts" "$out/fonts-preview.pgm"
python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' \
	"$out/fonts-preview.pgm" dist/fonts-preview.png
echo "preview: dist/fonts-preview.png"

#!/bin/sh
# Host tests for the display layer (tests/test_display.c, display.c on a memory framebuffer): fills and clipping,
# refresh regions and the ioctl path, PNG blits (gray, alpha, broken files), text layout (wrapping, centring,
# truncation, newlines, markup, inverted and gray ink), with ASan/UBSan. Writes test screens to dist/display-tests/
# (text.png, cjk.png, fills.png) to look at. Uses the computer's free fonts (Noto/DejaVu/Lato, Noto Sans CJK, Droid);
# checks needing a missing font are skipped. Needs gcc. Usage: device/app/tests/run_display_tests.sh
set -e
cd "$(dirname "$0")/../../.."
out=${TMPDIR:-/tmp}/scribe-display-tests
mkdir -p "$out" dist/display-tests
A=device/app
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I $A -I $A/vendor \
	-o "$out/test_display" $A/tests/test_display.c $A/display.c -lm
"$out/test_display" dist/display-tests

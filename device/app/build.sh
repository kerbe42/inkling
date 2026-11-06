#!/bin/sh
# Cross-build the Kindle app: static armhf (musl) with Ubuntu's zig. Drawing is our own display layer (display.c: the
# framebuffer, the hwtcon e-ink refresh ioctls, stb_truetype text, stb_image PNGs); no other libraries are linked.
# (-Wno-missing-field-initializers: games leave out the optional GameDef callbacks they don't use.)
# SCRIBE_VERSION (device/release/VERSION) is what Menu > Check for updates compares with the latest release.
# --dev: a test build with the developer hooks (INKLING_DEV: SIGUSR1/SIGUSR2 + the root-only devtap, devink, devinput,
# devdump, fake_response.json and fake_email files in SECURE_DIR) that the e2e tests and demos drive. Releases
# (make_release.sh) never pass it.
set -e
cd "$(dirname "$0")/../.."
DEV=""
case "$1" in
--dev) DEV="-DINKLING_DEV" ;;
"") ;;
*) echo "usage: $0 [--dev]" >&2; exit 2 ;;
esac
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$HOME/.cache/zig}"
VER=$(cat device/release/VERSION)
mkdir -p build
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -O2 -static -s -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-DSCRIBE_VERSION="\"$VER\"" $DEV -I device/app/vendor -o build/scribe device/app/*.c device/app/games/*.c \
	device/app/vendor/cJSON.c device/app/vendor/monocypher.c device/app/vendor/monocypher-ed25519.c -lm -lpthread
echo "built build/scribe${DEV:+ (dev build: developer hooks on)}"

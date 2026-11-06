#!/bin/sh
# Builds build/displaytest (device/tools/displaytest.c + the app's display layer), static armhf like the app.
# On the Kindle (as root; /mnt/us is noexec, so keep both files on /var/local):
#   scp build/displaytest device/tools/displaytest.sh scribe-kindle:/var/local/scribe/bin/
#   ssh scribe-kindle sh /var/local/scribe/bin/displaytest.sh [PAUSE_SECONDS]
# The log is printed and kept in /mnt/us/inkling/displaytest.log.
set -e
cd "$(dirname "$0")/../.."
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$HOME/.cache/zig}"
mkdir -p build
zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -O2 -static -s -Wall -Wextra -Wno-unused-parameter \
	-I device/app -I device/app/vendor -o build/displaytest device/tools/displaytest.c device/app/display.c -lm
echo "built build/displaytest"

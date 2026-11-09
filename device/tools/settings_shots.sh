#!/bin/sh
# Draws the Advanced AI settings pages on the computer (device/tools/settings_shots.c) and saves them as PNGs in
# dist/settings-shots/ for a look by eye. Needs gcc and the DejaVu fonts. Usage: device/tools/settings_shots.sh
set -e
cd "$(dirname "$0")/../.."
out=$(pwd)/dist/settings-shots
rm -rf "$out"
mkdir -p "$out"
gcc -O1 -g -w -I device/app -I device/app/vendor -DINKLING_DEV -DSECURE_DIR="\"$out/secure\"" -DUSB_DIR="\"$out/usb\"" \
	-o "$out/settings_shots" device/tools/settings_shots.c device/app/settings.c device/app/ui.c device/app/display.c \
	device/app/fonts.c device/app/keyboard.c device/app/keys.c device/app/brain.c device/app/providers.c \
	device/app/prompts.c device/app/stroke_fonts.c device/app/ink.c device/app/email.c device/app/vendor/cJSON.c -lm
"$out/settings_shots" "$out" 2>"$out/log.txt"
rm -rf "$out/secure" "$out/usb" "$out/hits.txt"

#!/bin/sh
# Runs displaytest on the Kindle with the Kindle UI paused (the same processes the app's launch.sh pauses), so
# nothing paints over the test patterns; the UI is resumed afterwards however displaytest ends.
# Usage (as root, from /var/local/scribe/bin): sh displaytest.sh [PAUSE_SECONDS=3]
# Not while Inkling is open (it would fight over the screen).
DIR=$(dirname "$0")
LOG=/mnt/us/inkling/displaytest.log
UI_PROCS="KPPMainApp JunoStatusBarDriver pillowd kfxreader kfxview webreader"
[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
if pidof scribe >/dev/null; then echo "Inkling is open: close it first" >&2; exit 1; fi
signal_ui() {
	for p in $UI_PROCS; do
		for pid in $(pidof "$p"); do kill -"$1" "$pid" 2>/dev/null; done
	done
}
trap 'signal_ui CONT' EXIT
trap 'signal_ui CONT; exit 1' TERM INT HUP
signal_ui STOP
sleep 1 # let screen updates already queued by the paused UI finish
mkdir -p "$(dirname "$LOG")" 2>/dev/null
"$DIR/displaytest" "${1:-3}" 2>&1 | tee "$LOG"

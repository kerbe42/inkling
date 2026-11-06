#!/bin/sh
# Installed as /var/local/scribe/app/launch.sh. Started from the library entry (inkling.sh, run by the
# jailbreak's shell integration): becomes root, then runs the app detached and returns at once.
#
# While the app is open, the Kindle UI processes that draw on screen are paused (SIGSTOP) so the home screen,
# the status-bar clock and pop-ups can't paint over the page. They are resumed (SIGCONT) when the app exits,
# however it exits (Exit button, power key, crash), and also if this script itself is killed. The framework core,
# the X server, power management and Wi-Fi keep running.
#
# When the Kindle goes to sleep (cover, power key, screensaver timeout) the app leaves with exit code 3 so the
# Kindle's own sleep screen and passcode lock work normally; this script then waits until the Kindle has woken up
# and been unlocked (the power daemon's outOfScreenSaver comes after the passcode) and reopens the app.
[ "$(id -u)" = 0 ] || exec /var/local/mkk/su -c "/bin/sh /var/local/scribe/app/launch.sh"
LOG=/var/local/scribe/app/scribe.log
WAKE=/var/local/scribe/app/reopen-on-wake # holds the PID of the launcher waiting to reopen the app
APP=/var/local/scribe/bin/scribe
UI_PROCS="KPPMainApp JunoStatusBarDriver pillowd kfxreader kfxview webreader"

umask 077 # the log and state hold page text and email addresses: root only
[ -f /var/local/scribe/app/scribe.log ] && chmod 600 /var/local/scribe/app/scribe.log /var/local/scribe/app/scribe.log.1 2>/dev/null
if [ "$1" != run ]; then
	if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 1000000 ]; then mv -f "$LOG" "$LOG.1"; fi
	setsid /bin/sh "$0" run </dev/null >>"$LOG" 2>&1 &
	exit 0
fi

# Already open? Then leave everything alone: this launcher must not resume the UI that the running app paused.
if pidof scribe >/dev/null; then
	echo "$(date '+%H:%M:%S') launch: already running, nothing to do"
	exit 0
fi

signal_ui() {
	for p in $UI_PROCS; do
		for pid in $(pidof "$p"); do kill -"$1" "$pid" 2>/dev/null; done
	done
}
resume_ui() { signal_ui CONT; }
trap 'resume_ui' EXIT
trap 'resume_ui; exit 0' TERM INT HUP

rm -f "$WAKE" # opened by hand: a launcher still waiting for a wake-up must not open a second copy later

# Let the library screen settle after the tap, then pause the UI and take over the screen.
sleep 2
signal_ui STOP
echo "$(date '+%H:%M:%S') launch: paused UI ($UI_PROCS)"
sleep 1 # let screen updates already queued by the paused UI finish before the app draws
started=$(date +%s)
"$APP"
rc=$?
# Exit code 4: Menu > Check for updates has installed a new version (app and this script, both renamed into place).
# Start it through the new launch.sh, with the UI still paused. If the updated app then fails to start (cannot run,
# or crashes within a minute), the previous version the updater kept as scribe.prev is put back and opened instead.
if [ "$rc" = 4 ]; then
	echo "$(date '+%H:%M:%S') launch: Inkling was updated, starting the new version"
	trap - EXIT TERM INT HUP
	exec /bin/sh "$0" run updated
fi
if [ "$2" = updated ] && [ $(($(date +%s) - started)) -lt 60 ] && [ -s "$APP.prev" ]; then
	case "$rc" in 126 | 127 | 132 | 134 | 135 | 136 | 139) # cannot run, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV
		echo "$(date '+%H:%M:%S') launch: the updated app failed to start ($rc), going back to the previous version"
		mv -f "$APP.prev" "$APP"
		trap - EXIT TERM INT HUP
		exec /bin/sh "$0" run ;;
	esac
fi
echo "$(date '+%H:%M:%S') launch: app exited ($rc), resuming UI"
resume_ui
if [ "$rc" = 3 ]; then
	echo $$ > "$WAKE"
	echo "$(date '+%H:%M:%S') launch: the Kindle is going to sleep; Inkling reopens after it wakes and is unlocked"
	awake() { # give the power daemon a few seconds to report "active" after the wake-up event
		for i in 1 2 3 4 5; do [ "$(lipc-get-prop com.lab126.powerd state 2>/dev/null)" = active ] && return 0; sleep 1; done
		return 1
	}
	if lipc-wait-event com.lab126.powerd outOfScreenSaver >/dev/null 2>&1 &&
		[ "$(cat "$WAKE" 2>/dev/null)" = "$$" ] && ! pidof scribe >/dev/null && awake; then
		rm -f "$WAKE"
		echo "$(date '+%H:%M:%S') launch: awake and unlocked, reopening Inkling"
		trap - EXIT TERM INT HUP
		exec /bin/sh "$0" run
	fi
	echo "$(date '+%H:%M:%S') launch: woke up, but Inkling was opened or closed meanwhile - not reopening"
fi

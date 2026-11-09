#!/bin/sh
# Stands in for /usr/bin/curl in the provider tests (test_providers.c, the retry checks): answers call n with the
# queued $FAKE_AI/<n>.status and <n>.json, keeps the request body and header file as req-<n>.json / hdr-<n>.txt, and
# logs the arguments to $FAKE_AI/log.
n=$(($(cat "$FAKE_AI/count" 2>/dev/null || echo 0) + 1))
echo "$n" > "$FAKE_AI/count"
echo "ARGV $*" >> "$FAKE_AI/log"
out= req= hdr=
while [ $# -gt 0 ]; do
	case "$1" in
	-o) out=$2; shift ;;
	-H) hdr=${2#@}; shift ;;
	--data-binary) req=${2#@}; shift ;;
	esac
	shift
done
[ -n "$req" ] && cp "$req" "$FAKE_AI/req-$n.json"
[ -n "$hdr" ] && cp "$hdr" "$FAKE_AI/hdr-$n.txt"
[ -f "$FAKE_AI/$n.json" ] && cp "$FAKE_AI/$n.json" "$out"
if [ -f "$FAKE_AI/$n.status" ]; then cat "$FAKE_AI/$n.status"; else printf 500; fi

#!/bin/sh
# Stands in for /usr/bin/curl in the update tests (test_update.c): serves files from $FAKE_DIR/files by URL, like
# GitHub would (the API asset URL redirects to a storage host), and logs every call to $FAKE_DIR/log so the test can
# check that the token never reaches the storage host or a command line.
echo "ARGV $*" >> "$FAKE_DIR/log"
out= hdr= cfg= follow=0
while [ $# -gt 0 ]; do
	case "$1" in
	-o) out=$2; shift ;;
	-H) hdr=${2#@}; shift ;;
	-K) cfg=$2; shift ;;
	-L) follow=1 ;;
	-w | -A | --proto | --proto-redir | --max-redirs | --connect-timeout | --max-time | --max-filesize) shift ;;
	-q | -sS) ;;
	*) echo "UNEXPECTED $1" >> "$FAKE_DIR/log"; exit 2 ;;
	esac
	shift
done
url=$(sed -n 's/^url = "\(.*\)"$/\1/p' "$cfg")
auth=no
[ -n "$hdr" ] && grep -q '^Authorization: Bearer ' "$hdr" && auth=yes
echo "GET $url auth=$auth follow=$follow" >> "$FAKE_DIR/log"
name=${url##*/}
case "$url" in
https://api.test/repos/*/releases/latest)
	code=200
	[ -f "$FAKE_DIR/latest.status" ] && code=$(cat "$FAKE_DIR/latest.status")
	cp "$FAKE_DIR/release.json" "$out"
	printf '%s ' "$code" ;;
https://api.test/repos/*/releases/assets/*)
	if [ "$auth" = yes ] && grep -q '^Accept: application/octet-stream' "$hdr"; then
		printf '302 https://objects.test/%s' "$name"
	else
		printf '404 '
	fi ;;
https://objects.test/* | https://dl.test/*)
	[ "$auth" = yes ] && echo "LEAK token sent to $url" >> "$FAKE_DIR/log"
	[ "$follow" = 1 ] || echo "NOFOLLOW $url" >> "$FAKE_DIR/log"
	if [ -f "$FAKE_DIR/files/$name" ]; then
		cp "$FAKE_DIR/files/$name" "$out"
		printf '200 '
	else
		printf '404 '
	fi ;;
*)
	printf '000 '
	exit 6 ;;
esac
exit 0

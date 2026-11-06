#!/bin/sh
# Makes the Ed25519 key that signs the app's update files - run it once, ever. The private key stays outside the repo
# (~/creds/scribe-release-ed25519.key, mode 0600; keep a backup somewhere safe: without it no installed app accepts a
# new update, and anyone holding it can sign one). The public key it prints goes into device/app/update_pubkey.h.
# Never overwrites an existing key. Needs openssl 1.1.1 or newer.
# Usage: device/release/make_signing_key.sh   (SCRIBE_SIGNING_KEY=path to use another file)
set -eu
KEY=${SCRIBE_SIGNING_KEY:-$HOME/creds/scribe-release-ed25519.key}
pubhex() { openssl pkey -in "$1" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n'; }
if [ -e "$KEY" ]; then
	echo "$KEY already exists - not overwriting it. Its public key: $(pubhex "$KEY")" >&2
	exit 1
fi
umask 077
mkdir -p "$(dirname "$KEY")"
TMP="$KEY.tmp.$$"
trap 'rm -f "$TMP"' EXIT
openssl genpkey -algorithm ed25519 -out "$TMP"
chmod 600 "$TMP"
ln "$TMP" "$KEY" 2>/dev/null || { echo "$KEY appeared meanwhile - not overwriting it" >&2; exit 1; } # ln never replaces
PUB=$(pubhex "$KEY")
echo "private key: $KEY (0600 - never commit it; back it up)"
echo "public key:  $PUB"
echo "in device/app/update_pubkey.h:  #define UPDATE_PUBKEY_HEX \"$PUB\""

#!/bin/sh
# Host tests for the self-update (update_core.c) with address + undefined-behaviour sanitizers, then a check that a
# manifest signed by openssl the way make_release.sh signs it verifies in the app's code (and fails when changed).
# Throwaway keys only. Usage: device/app/tests/run_update_tests.sh
set -e
cd "$(dirname "$0")/../../.."
out=${TMPDIR:-/tmp}/scribe-update-tests
mkdir -p "$out"
A=device/app
gcc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-format-truncation -I $A -I $A/vendor -o "$out/test_update" \
	$A/tests/test_update.c $A/update_core.c $A/vendor/cJSON.c $A/vendor/monocypher.c $A/vendor/monocypher-ed25519.c
"$out/test_update" "$PWD/$A/tests/fake_curl.sh"
if ! command -v openssl >/dev/null; then echo "openssl not installed: interop check skipped"; exit 0; fi
k="$out/keys"
rm -rf "$k"
mkdir -m 700 "$k"
trap 'rm -rf "$k"' EXIT
openssl genpkey -algorithm ed25519 -out "$k/test.key" 2>/dev/null
pub=$(openssl pkey -in "$k/test.key" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
printf 'format=scribe-update-1\nversion=0.3.0\nmin_version=0.2.0\nbundle=b.tar\nsize=1\nsha256=%s\n' \
	"$(printf x | sha256sum | cut -c1-64)" > "$k/m"
openssl pkeyutl -sign -inkey "$k/test.key" -rawin -in "$k/m" -out "$k/m.sig"
"$out/test_update" verify "$pub" "$k/m" "$k/m.sig" || { echo "openssl interop: a good signature was rejected"; exit 1; }
printf '#\n' >> "$k/m"
if "$out/test_update" verify "$pub" "$k/m" "$k/m.sig"; then echo "openssl interop: a changed manifest was accepted"; exit 1; fi
echo "openssl interop: ok (good signature verifies, changed manifest rejected)"

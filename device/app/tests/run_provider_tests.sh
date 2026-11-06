#!/bin/sh
# Host tests for the AI providers (tests/test_providers.c: brain.c, providers.c, keys.c): config/key migration, each
# provider's request, documented responses parsed into the reply, errors, "Test key" and the fake_response.json hook.
# No network and no real keys. Needs gcc. Usage: device/app/tests/run_provider_tests.sh
# The requests it builds are left in $TMPDIR/scribe-provider-tests for inspection.
set -e
cd "$(dirname "$0")/../../.."
out=${TMPDIR:-/tmp}/scribe-provider-tests
rm -rf "$out"
mkdir -p "$out"
gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation -fsanitize=address,undefined \
	-fno-sanitize-recover=undefined -I device/app -I device/app/vendor -DINKLING_DEV \
	-DSECURE_DIR="\"$out/secure\"" -DUSB_DIR="\"$out/usb\"" -o "$out/test_providers" \
	device/app/tests/test_providers.c device/app/providers.c device/app/prompts.c device/app/keys.c device/app/email.c \
	device/app/vendor/cJSON.c -lm
"$out/test_providers" "$out"

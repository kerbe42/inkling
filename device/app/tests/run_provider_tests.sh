#!/bin/sh
# Host tests for the AI providers (tests/test_providers.c: brain.c, providers.c, keys.c): config/key migration, each
# provider's request, documented responses parsed into the reply, errors, "Test key", the fake_response.json hook, and
# Advanced AI (a service/model/thinking level per feature, the own server, the retries that adapt to it).
# No network and no real keys. Needs gcc. Usage: device/app/tests/run_provider_tests.sh
# The requests it builds are left in $TMPDIR/scribe-provider-tests for inspection.
set -e
cd "$(dirname "$0")/../../.."
out=${TMPDIR:-/tmp}/scribe-provider-tests
rm -rf "$out"
mkdir -p "$out"
gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation -Wno-missing-field-initializers \
	-fsanitize=address,undefined -fno-sanitize-recover=undefined -I device/app -I device/app/vendor -DINKLING_DEV \
	-DCURL="\"$(pwd)/device/app/tests/fake_ai_curl.sh\"" \
	-DSECURE_DIR="\"$out/secure\"" -DUSB_DIR="\"$out/usb\"" -o "$out/test_providers" \
	device/app/tests/test_providers.c device/app/providers.c device/app/prompts.c device/app/keys.c device/app/email.c \
	device/app/vendor/cJSON.c -lm
"$out/test_providers" "$out"

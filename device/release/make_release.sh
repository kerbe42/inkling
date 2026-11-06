#!/bin/sh
# Builds the release zip: dist/inkling-<version>.zip (needs: zig, python3 + Pillow, zip), and the
# signed update files dist/inkling-update-<version>.{tar,manifest,manifest.sig} for Menu > Check for updates.
set -e
cd "$(dirname "$0")/../.."
VER=$(cat device/release/VERSION)
OUT=dist/inkling-$VER
rm -rf "$OUT" "$OUT.zip"
device/app/build.sh
# a release never carries the developer hooks (build.sh --dev)
if grep -aqE 'devinput|devtap|fake_response' build/scribe; then echo "build/scribe has developer hooks - not releasing" >&2; exit 1; fi
# the email helper (curl with SMTP); built once, it takes a few minutes
[ -x build/curl-smtp ] && [ -s build/curl-smtp-LICENSES.txt ] || device/tools/build_curl_smtp.sh
python3 -I device/art/make_icon.py >/dev/null
python3 -I device/art/make_launcher.py >/dev/null
# the USB folder (inkling/) and the installer for documents/; the binary keeps its name scribe (the process name)
mkdir -p "$OUT/inkling/install" "$OUT/documents"
cp build/scribe build/curl-smtp device/app/launch.sh device/app/inkling.sh "$OUT/inkling/install/"
cp device/release/VERSION "$OUT/inkling/install/VERSION"
ICON=$(sed -n 's/^# Icon: //p' device/app/inkling.sh)
sed "s|@ICON@|$ICON|" device/release/install.sh.in > "$OUT/documents/Install Inkling.sh"
cp device/release/uninstall.sh.in "$OUT/inkling/install/Uninstall Inkling.sh"
cp device/release/README.txt "$OUT/README.txt"
# the licence (GPL-3.0; top of the zip and with the app) and the third-party notices: docs/public here, the top folder
# in the public repository (where docs/public is exported to)
PUB=docs/public; [ -f "$PUB/LICENSE" ] || PUB=.
cp "$PUB/LICENSE" "$OUT/LICENSE.txt"
# the handwriting fonts built into the app: their licences and credits travel with it
mkdir -p "$OUT/inkling/licences"
cp device/app/fonts/OFL.txt device/app/fonts/HERSHEY-NOTICE.txt device/app/fonts/FONT-CREDITS.txt "$OUT/inkling/licences/"
cp build/curl-smtp-LICENSES.txt "$OUT/inkling/licences/CURL-MBEDTLS-LICENSES.txt"
cp device/app/vendor/Monocypher-LICENCE.md device/app/vendor/cJSON-LICENSE device/app/vendor/stb-LICENSE "$OUT/inkling/licences/"
cp "$PUB/LICENSE" "$OUT/inkling/licences/LICENSE.txt"
cp "$PUB/THIRD-PARTY-NOTICES.md" "$OUT/inkling/licences/"
(cd dist && zip -qr "inkling-$VER.zip" "inkling-$VER")
ls -la "$OUT.zip"
echo "SHA-256 of build/scribe (the build is reproducible; put it in the release notes): $(sha256sum build/scribe | cut -c1-64)"

# The update files for the app's Menu > Check for updates: a bundle (ustar) of what the installer installs, a manifest
# of it, and the manifest's Ed25519 signature made with the release key (make_signing_key.sh, kept in ~/creds). The
# app installs nothing unless the signature checks out against device/app/update_pubkey.h and the bundle matches.
KEY=${SCRIBE_SIGNING_KEY:-$HOME/creds/scribe-release-ed25519.key}
UPD=dist/inkling-update-$VER
rm -rf "$UPD" "$UPD.tar" "$UPD.manifest" "$UPD.manifest.sig"
if [ ! -r "$KEY" ]; then
	echo "NOTE: no release key at $KEY - update files not made (installed apps can't update to this zip)" >&2
	exit 0
fi
echo "$VER" | grep -Eq '^[0-9]{1,6}(\.[0-9]{1,6}){0,3}$' || { echo "VERSION $VER: the updater needs plain numbers like 0.3.0" >&2; exit 1; }
MIN=$(cat device/release/UPDATE_MIN_VERSION)
echo "$MIN" | grep -Eq '^[0-9]{1,6}(\.[0-9]{1,6}){0,3}$' || { echo "UPDATE_MIN_VERSION $MIN isn't a version" >&2; exit 1; }
WANT=$(sed -n 's/^#define UPDATE_PUBKEY_HEX "\([0-9a-f]*\)".*/\1/p' device/app/update_pubkey.h)
HAVE=$(openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')
[ -n "$WANT" ] && [ "$HAVE" = "$WANT" ] || { echo "$KEY isn't the key in device/app/update_pubkey.h - not signing" >&2; exit 1; }
I="$OUT/inkling/install"
mkdir -p "$UPD/licences"
cp "$I/scribe" "$I/launch.sh" "$I/inkling.sh" "$I/VERSION" "$UPD/"
# the entry again under its old name: the updaters of 0.2.x (Scribe) need scribe.sh; the app renames it at start
cp "$I/inkling.sh" "$UPD/scribe.sh"
if [ -f "$I/curl-smtp" ]; then cp "$I/curl-smtp" "$UPD/"; fi
cp "$OUT/inkling/licences/"* "$UPD/licences/"
(cd "$UPD" && tar --format=ustar --owner=0 --group=0 --numeric-owner --mtime=@0 --sort=name -cf "../inkling-update-$VER.tar" *)
rm -rf "$UPD"
# (the format line keeps its old name: installed apps up to 0.2.x accept only scribe-update-1)
printf 'format=scribe-update-1\nversion=%s\nmin_version=%s\nbundle=inkling-update-%s.tar\nsize=%s\nsha256=%s\n' "$VER" "$MIN" "$VER" \
	"$(wc -c < "$UPD.tar" | tr -d ' ')" "$(sha256sum "$UPD.tar" | cut -c1-64)" > "$UPD.manifest"
openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$UPD.manifest" -out "$UPD.manifest.sig"
PUB=$(mktemp)
openssl pkey -in "$KEY" -pubout -out "$PUB"
openssl pkeyutl -verify -pubin -inkey "$PUB" -rawin -in "$UPD.manifest" -sigfile "$UPD.manifest.sig" >/dev/null
rm -f "$PUB"
ls -la "$UPD.tar" "$UPD.manifest" "$UPD.manifest.sig"
echo "Publish (tag v$VER, a full release - not a draft or pre-release - so the app's check finds it):"
echo "  gh release create v$VER --repo kerbe42/inkling --title \"Inkling $VER\" --notes-file NOTES.md \\"
echo "    $OUT.zip $UPD.tar $UPD.manifest $UPD.manifest.sig"

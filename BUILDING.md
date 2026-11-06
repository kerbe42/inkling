# Building Inkling

Inkling is plain C, cross-compiled for the Kindle (32-bit ARM, hard float, musl, fully static) with Zig's C compiler.
There are no other libraries to install: the vendored code in `device/app/vendor` (cJSON, Monocypher, stb) and the
fonts in `device/app/fonts` are built in.

## What you need

- Linux (other Unix-like systems may work; only Linux is tested)
- **Zig 0.14.1** exactly (the official releases are built with it; another version gives a different binary)
- Python 3 with Pillow, for the Library icon (`device/art`)
- `zip`, and OpenSSL 3 if you sign releases
- For the email helper (`curl-smtp`, built once by the release script): `make`, `curl`, `tar` with xz and bzip2
- For the host tests: `gcc` and Python 3 (the email tests also run the ARM email helper, through `qemu-arm` and
  binfmt)

## Build the app

```sh
device/app/build.sh            # -> build/scribe (the app; its process is still called scribe)
```

`device/app/build.sh --dev` makes a test build with developer hooks that the end-to-end tests drive (scripted taps,
pen replay, canned AI replies, all through root-only files). Never install a `--dev` build for everyday use; the
release script refuses to package one.

## Run the tests

On your computer:

```sh
device/app/tests/run_game_tests.sh       # every game, with address and undefined-behaviour sanitizers
device/app/tests/run_display_tests.sh    # the display layer on a memory framebuffer
device/app/tests/run_font_tests.sh       # every handwriting glyph and accent
device/app/tests/run_provider_tests.sh   # AI service settings, keys and request building
device/app/tests/run_update_tests.sh     # the signed self-update
device/tools/build_curl_smtp.sh          # once: the email helper the next test sends with (a few minutes)
device/app/tests/run_email_tests.sh      # email building and sending, against a local test server
```

On a Kindle: `device/tools/e2e/run_e2e.py` runs about 30 end-to-end tests (pen, touch, menus, games, sleep and wake,
email, updates) against a jailbroken Kindle reachable as the SSH host `scribe-kindle` with a `--dev` build installed.
It backs up the Kindle's Inkling settings, notebook state, saved games and keys first and restores them afterwards.

## Make a release

```sh
device/release/make_release.sh
```

This builds `dist/inkling-<version>.zip` (the version is in `device/release/VERSION`) and, if a signing key is
present, the signed update files `dist/inkling-update-<version>.{tar,manifest,manifest.sig}` that Menu > Check for
updates installs. It also prints the SHA-256 of `build/scribe`.

## Verify an official release

**The app matches the source.** The build is reproducible: the same source and Zig 0.14.1 give a byte-identical
`build/scribe`, wherever you build it. To check a release:

```sh
git clone https://github.com/kerbe42/inkling && cd inkling
git checkout v0.3.1                      # the release's tag
device/app/build.sh
sha256sum build/scribe
```

The result should equal the SHA-256 in the release notes and that of `inkling/install/scribe` in the release's zip.

**The update files are signed by JSSL.** The app installs an update only if its manifest carries a valid Ed25519
signature from JSSL's release key and the bundle matches the manifest's checksum. You can check the same thing
yourself, with the public key from `device/app/update_pubkey.h`:

```sh
python3 -c "import base64,re;h=re.search(r'UPDATE_PUBKEY_HEX \"([0-9a-f]{64})\"',open('device/app/update_pubkey.h').read()).group(1);print('-----BEGIN PUBLIC KEY-----\n'+base64.b64encode(bytes.fromhex('302a300506032b6570032100'+h)).decode()+'\n-----END PUBLIC KEY-----')" > inkling-release.pub
openssl pkeyutl -verify -pubin -inkey inkling-release.pub -rawin -in inkling-update-0.3.1.manifest -sigfile inkling-update-0.3.1.manifest.sig
grep sha256 inkling-update-0.3.1.manifest && sha256sum inkling-update-0.3.1.tar   # the two must match
```

## Publishing your own version

The GPL lets you publish a changed version. Please give it its own name and icon, and set it up so it can't be
confused with the official releases:

1. Make your own signing key: `device/release/make_signing_key.sh` (it stays on your computer; set
   `SCRIBE_SIGNING_KEY` to choose where), and put its public key in `device/app/update_pubkey.h`.
2. Point the updater at your own releases page: `UPDATE_DEFAULT_REPO` in `device/app/update.c`.

Without these two changes your build would offer to "update" itself to the official release, replacing your version.

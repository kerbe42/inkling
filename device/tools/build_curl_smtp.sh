#!/bin/sh
# Builds build/curl-smtp: a small static curl for the Kindle (armv7 hard-float, musl) that speaks only SMTP and SMTPS
# (implicit TLS on 465, STARTTLS on 587 with --ssl-reqd), TLS 1.2/1.3 through mbedTLS, IPv4 and IPv6. The Kindle's own
# /usr/bin/curl has no SMTP, so Send > Email this page uses this one (installed as /var/local/scribe/bin/curl-smtp).
# Sources come from the official sites at pinned versions; the SHA-256 of each tarball is checked, and curl's GPG
# signature too when gpg is installed (signer pinned to Daniel Stenberg's key). Needs zig, make, curl, tar (xz, bz2).
# Usage: device/tools/build_curl_smtp.sh        (work files in build/curl-smtp-work, output build/curl-smtp)
set -eu
cd "$(dirname "$0")/../.."
ROOT=$PWD

CURL_VER=8.23.0
CURL_SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
CURL_URL=https://curl.se/download/curl-$CURL_VER.tar.xz
CURL_SIGNER=27EDEAF22F3ABCEB50DB9A125CC908FDB71E12C2 # Daniel Stenberg
CURL_KEY_URL=https://daniel.haxx.se/mykey.asc
MBED_VER=3.6.7 # the LTS branch; builds with plain make
MBED_SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
MBED_URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBED_VER/mbedtls-$MBED_VER.tar.bz2

W=$ROOT/build/curl-smtp-work
DL=$W/dl
PREFIX=$W/prefix
JOBS=$(nproc 2>/dev/null || echo 4)
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-$HOME/.cache/zig}"
mkdir -p "$DL" "$W/bin"

fetch() { # url file
	[ -s "$DL/$2" ] && return 0
	curl -sSfL -o "$DL/$2.part" "$1"
	mv "$DL/$2.part" "$DL/$2"
}
check() { # file sha256
	got=$(sha256sum "$DL/$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "checksum mismatch for $1: $got" >&2; rm -f "$DL/$1"; exit 1; }
}

fetch "$CURL_URL" "curl-$CURL_VER.tar.xz"
fetch "$MBED_URL" "mbedtls-$MBED_VER.tar.bz2"
check "curl-$CURL_VER.tar.xz" "$CURL_SHA256"
check "mbedtls-$MBED_VER.tar.bz2" "$MBED_SHA256"
if command -v gpg >/dev/null 2>&1; then
	fetch "$CURL_URL.asc" "curl-$CURL_VER.tar.xz.asc"
	fetch "$CURL_KEY_URL" curl-signer.asc
	G=$W/gnupg
	rm -rf "$G"
	mkdir -m 700 "$G"
	gpg --homedir "$G" -q --import "$DL/curl-signer.asc" 2>/dev/null
	if ! gpg --homedir "$G" --status-fd 1 --verify "$DL/curl-$CURL_VER.tar.xz.asc" "$DL/curl-$CURL_VER.tar.xz" 2>/dev/null |
		grep -q "^\[GNUPG:\] VALIDSIG $CURL_SIGNER "; then
		echo "curl's GPG signature did not verify against $CURL_SIGNER" >&2
		exit 1
	fi
	rm -rf "$G"
	echo "curl $CURL_VER: signature good ($CURL_SIGNER)"
fi

# compiler wrappers (autoconf and libtool cope better with one-word tools)
cat >"$W/bin/arm-cc" <<'EOF'
#!/bin/sh
exec zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 "$@"
EOF
cat >"$W/bin/arm-ar" <<'EOF'
#!/bin/sh
exec zig ar "$@"
EOF
cat >"$W/bin/arm-ranlib" <<'EOF'
#!/bin/sh
exec zig ranlib "$@"
EOF
chmod 755 "$W/bin/arm-cc" "$W/bin/arm-ar" "$W/bin/arm-ranlib"
CFLAGS_COMMON="-Os -ffunction-sections -fdata-sections"

# mbedTLS: the default configuration (TLS 1.2 + 1.3 client, X.509, RSA/ECDSA) less what an SMTP client never uses
rm -rf "$W/mbedtls-$MBED_VER" "$PREFIX"
tar -C "$W" -xjf "$DL/mbedtls-$MBED_VER.tar.bz2"
cat >>"$W/mbedtls-$MBED_VER/include/mbedtls/mbedtls_config.h" <<'EOF'

/* curl-smtp (device/tools/build_curl_smtp.sh): only a TLS client is needed */
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_TICKET_C
#undef MBEDTLS_SSL_CACHE_C
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_PKCS7_C
#undef MBEDTLS_X509_CRL_WRITE_C
#undef MBEDTLS_X509_CSR_WRITE_C
#undef MBEDTLS_X509_CRT_WRITE_C
#undef MBEDTLS_X509_CREATE_C
#undef MBEDTLS_X509_CSR_PARSE_C
#undef MBEDTLS_PEM_WRITE_C
#undef MBEDTLS_PK_WRITE_C
#undef MBEDTLS_DEBUG_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_VERSION_FEATURES
#undef MBEDTLS_ERROR_C
EOF
make -C "$W/mbedtls-$MBED_VER/library" -j"$JOBS" CC="$W/bin/arm-cc" AR="$W/bin/arm-ar" \
	CFLAGS="$CFLAGS_COMMON" static >/dev/null
mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp "$W/mbedtls-$MBED_VER/library/"libmbed*.a "$PREFIX/lib/"
cp -r "$W/mbedtls-$MBED_VER/include/mbedtls" "$W/mbedtls-$MBED_VER/include/psa" "$PREFIX/include/"

# curl: SMTP/SMTPS only
rm -rf "$W/curl-$CURL_VER"
tar -C "$W" -xJf "$DL/curl-$CURL_VER.tar.xz"
cd "$W/curl-$CURL_VER"
./configure -q --host=arm-linux-musleabihf --build="$(uname -m)-pc-linux-gnu" \
	CC="$W/bin/arm-cc" AR="$W/bin/arm-ar" RANLIB="$W/bin/arm-ranlib" \
	CFLAGS="$CFLAGS_COMMON" LDFLAGS="-static -Wl,--gc-sections" \
	--disable-shared --enable-static --with-mbedtls="$PREFIX" \
	--with-ca-bundle=/etc/ssl/certs/ca-certificates.crt --without-ca-path --without-ca-fallback \
	--enable-smtp --enable-ipv6 \
	--disable-http --disable-ftp --disable-file --disable-ipfs --disable-ldap --disable-ldaps --disable-rtsp \
	--disable-proxy --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-gopher \
	--disable-mqtt --disable-websockets --disable-manual --disable-docs --disable-libcurl-option \
	--disable-ntlm --disable-kerberos-auth --disable-negotiate-auth --disable-bearer-auth --disable-digest-auth \
	--disable-aws --disable-httpsig --disable-unix-sockets --disable-cookies --disable-doh --disable-mime \
	--disable-form-api --disable-netrc --disable-alt-svc --disable-hsts --disable-headers-api --disable-httpsrr \
	--disable-ech --disable-ssls-export --disable-progress-meter --disable-get-easy-options --disable-sspi \
	--disable-dnsshuffle --disable-sha512-256 \
	--without-zlib --without-brotli --without-zstd --without-libpsl --without-libidn2 --without-nghttp2 \
	--without-ngtcp2 --without-nghttp3 --without-quiche --without-libssh2 --without-libssh --without-libgsasl \
	--without-libuv
make -j"$JOBS" V=0 >/dev/null
"$W/bin/arm-cc" --version >/dev/null # (zig present)
mkdir -p "$ROOT/build"
cp src/curl "$ROOT/build/curl-smtp"
zig objcopy --strip-all "$ROOT/build/curl-smtp" "$ROOT/build/curl-smtp.tmp" 2>/dev/null && mv "$ROOT/build/curl-smtp.tmp" "$ROOT/build/curl-smtp"
chmod 755 "$ROOT/build/curl-smtp"
{ # the licences that travel with the binary (make_release.sh puts this in inkling/licences)
	echo "curl-smtp (/var/local/scribe/bin/curl-smtp) is curl $CURL_VER built with Mbed TLS $MBED_VER."
	echo
	echo "==== curl $CURL_VER (https://curl.se): COPYING ===="
	cat COPYING
	echo
	echo "==== Mbed TLS $MBED_VER (https://www.trustedfirmware.org/projects/mbed-tls/), used under Apache-2.0: LICENSE ===="
	cat "$W/mbedtls-$MBED_VER/LICENSE"
} >"$ROOT/build/curl-smtp-LICENSES.txt"
echo "built build/curl-smtp ($(wc -c <"$ROOT/build/curl-smtp") bytes): curl $CURL_VER, mbedTLS $MBED_VER"

#!/bin/sh
# Email tests on the laptop: email.c's unit checks (gcc + sanitizers), then real sends through build/curl-smtp (the
# ARM binary, run by qemu-arm through binfmt) to local SMTP test servers (smtp_test_server.py) over SMTPS (implicit
# TLS) and STARTTLS, TLS 1.3 and 1.2, AUTH PLAIN and LOGIN, with a throwaway CA passed by --cacert; the received
# message must match byte for byte and parse back to the same subject, body and PNG (check_email.py). Then the
# failures the status line explains: wrong password, no STARTTLS, unknown CA, wrong host name, refused recipient,
# nothing listening. "gmail" as the argument adds a TLS check against smtp.gmail.com:465 and :587 (EHLO + HELP, no
# login) with the system CA bundle. Needs gcc, python3, openssl, qemu-arm. Usage: run_email_tests.sh [gmail]
set -eu
cd "$(dirname "$0")/../../.."
ROOT=$PWD
CURL=${CURL_SMTP:-$ROOT/build/curl-smtp}
[ -x "$CURL" ] || { echo "no $CURL: run device/tools/build_curl_smtp.sh first" >&2; exit 1; }
out=${TMPDIR:-/tmp}/scribe-email-tests
rm -rf "$out"
mkdir -p "$out"
T=device/app/tests
gcc -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -I device/app/vendor \
	-o "$out/test_email" $T/test_email.c device/app/email.c
"$out/test_email" unit

# a throwaway CA, a server certificate for "localhost" only, and a second CA that signed nothing
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2 -subj "/CN=Scribe test CA" \
	-addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" \
	-keyout "$out/ca.key" -out "$out/ca.pem" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 2 -subj "/CN=Other CA" \
	-addext "basicConstraints=critical,CA:TRUE" -keyout "$out/other.key" -out "$out/other.pem" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -subj "/CN=localhost" -keyout "$out/srv.key" -out "$out/srv.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n' >"$out/ext.cnf"
openssl x509 -req -in "$out/srv.csr" -CA "$out/ca.pem" -CAkey "$out/ca.key" -CAcreateserial -days 2 \
	-extfile "$out/ext.cnf" -out "$out/srv.pem" 2>/dev/null

PW='te"st\pa:ss word' # quotes, backslash, colon and space survive the curl config file
export SMTP_TEST_PASSWORD="$PW"
B=$((24000 + $$ % 900 * 5))
pids=
srv() { # port mode name [args]
	p=$1 m=$2 n=$3
	shift 3
	python3 -I $T/smtp_test_server.py "$p" "$m" "$out/srv.pem" "$out/srv.key" "$out/rx-$n" "$@" >"$out/srv-$n.log" 2>&1 &
	pids="$pids $!"
}
trap 'kill $pids 2>/dev/null || true' EXIT INT TERM
srv $B smtps smtps
srv $((B + 1)) starttls starttls
srv $((B + 2)) smtps login --mechs LOGIN
srv $((B + 3)) starttls tls12 --mechs LOGIN --tls12
srv $((B + 4)) plain plain
for n in smtps starttls login tls12 plain; do
	i=0
	until grep -q ready "$out/srv-$n.log" 2>/dev/null; do
		i=$((i + 1))
		[ $i -lt 50 ] || { echo "server $n did not start:" >&2; cat "$out/srv-$n.log" >&2; exit 1; }
		sleep 0.1
	done
done

"$out/test_email" mime "$out"
fails=0
send() { # name want-code url starttls cacert rcpt [password]
	if [ $# -ge 7 ]; then r=$(SMTP_TEST_PASSWORD="$7" "$out/test_email" send "$3" "$4" "$5" "$out/sent.eml" "$CURL" "$out/login.cfg" "$6") || true
	else r=$("$out/test_email" send "$3" "$4" "$5" "$out/sent.eml" "$CURL" "$out/login.cfg" "$6") || true; fi
	case "$r" in
	"code=$2 "*) echo "ok   $1: $r" ;;
	*) echo "FAIL $1: $r"; fails=$((fails + 1)) ;;
	esac
}
check() { # name rx-dir mech tls
	if python3 -I $T/check_email.py "$out" "$out/rx-$2/1.eml" "$out/rx-$2/1.json" "$3" "$4"; then :; else fails=$((fails + 1)); fi
}
L=localhost
send "SMTPS, AUTH PLAIN" 0 "smtps://$L:$B" 0 "$out/ca.pem" test@example.com && check smtps smtps PLAIN TLSv1.3
send "STARTTLS, AUTH PLAIN" 0 "smtp://$L:$((B + 1))" 1 "$out/ca.pem" test@example.com && check starttls starttls PLAIN TLSv1.3
send "SMTPS, AUTH LOGIN" 0 "smtps://$L:$((B + 2))" 0 "$out/ca.pem" test@example.com && check login login LOGIN TLSv1.3
send "STARTTLS, TLS 1.2, AUTH LOGIN" 0 "smtp://$L:$((B + 3))" 1 "$out/ca.pem" test@example.com && check tls12 tls12 LOGIN TLSv1.2
send "wrong password" 67 "smtps://$L:$B" 0 "$out/ca.pem" test@example.com wrong-password
send "no STARTTLS offered" 64 "smtp://$L:$((B + 4))" 1 "$out/ca.pem" test@example.com
send "unknown CA" 60 "smtps://$L:$B" 0 "$out/other.pem" test@example.com
send "certificate for another name" 60 "smtps://127.0.0.1:$B" 0 "$out/ca.pem" test@example.com
send "recipient refused" 55 "smtps://$L:$B" 0 "$out/ca.pem" reject@example.com
send "nothing listening" 7 "smtps://$L:$((B + 7))" 0 "$out/ca.pem" test@example.com

if [ "${1:-}" = gmail ]; then
	for u in smtps://smtp.gmail.com:465 "smtp://smtp.gmail.com:587 --ssl-reqd"; do
		# shellcheck disable=SC2086
		if "$CURL" -q -v --max-time 30 --cacert /etc/ssl/certs/ca-certificates.crt $u >"$out/gmail.log" 2>&1; then
			echo "ok   Gmail $u: $(grep -o 'TLSv1\.[23][^,]*' "$out/gmail.log" | head -1), $(grep -c '^< 2' "$out/gmail.log") 2xx replies, certificate verified"
		else
			echo "FAIL Gmail $u:"
			tail -5 "$out/gmail.log"
			fails=$((fails + 1))
		fi
	done
fi
[ $fails = 0 ] && echo "email tests: all ok" || { echo "email tests: $fails failed"; exit 1; }

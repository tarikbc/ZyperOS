#!/bin/sh
# The System Update signing key: Ed25519, with OpenSSL 3 or newer.
#
#   tools/update-key.sh new [KEY]           make the private key (once), and
#                                           its public half in res/update/keys/
#   tools/update-key.sh sign LIST OUT [KEY] sign an update list: OUT is LIST
#                                           with the "sign" line added
#   tools/update-key.sh id FILE             a key's id (public or private)
#
# KEY defaults to $ZYPEROS_UPDATE_KEY, else ~/.config/zyperos/update-signing-key.pem.
# The private key never goes in a repository, a container or GitHub: the list
# is signed on the Mac that holds it (tools/release.sh). Keep a copy of it
# somewhere safe. Without it no device ever takes another update; with a
# stranger holding it, every device takes theirs. A new key needs a new
# system image before devices trust it (the public keys are in the image,
# /etc/zyperos/update-keys), so the old key signs until those images are out.
#
# The key id is the first 16 hex digits of the SHA-256 of the raw 32-byte
# public key: the manifest's sign line names it, and the public key's file is
# res/update/keys/<id>.pem.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OPENSSL=${OPENSSL:-openssl}
KEY_DEFAULT=${ZYPEROS_UPDATE_KEY:-$HOME/.config/zyperos/update-signing-key.pem}
# Where the public keys are (the ones the image trusts); a test passes its own
PUBDIR=${ZYPEROS_UPDATE_KEYS:-$ROOT/res/update/keys}

die() { echo "update-key: $*" >&2; exit 1; }

need_openssl() {
	v=$("$OPENSSL" version 2>/dev/null || true)
	case $v in
	"OpenSSL "[3-9]*|"OpenSSL "[1-9][0-9]*) ;;
	*) die "needs OpenSSL 3 or newer for Ed25519 signing (found: ${v:-none}); set OPENSSL=" ;;
	esac
}

key_id() {
	"$OPENSSL" pkey -in "$1" -pubin -pubout -outform DER 2>/dev/null ||
		"$OPENSSL" pkey -in "$1" -pubout -outform DER
}

id_of() {
	key_id "$1" | tail -c 32 | shasum -a 256 | cut -c1-16
}

case ${1:-} in
new)
	need_openssl
	KEY=${2:-$KEY_DEFAULT}
	[ ! -e "$KEY" ] || die "$KEY is there already: one key signs every release"
	mkdir -p "$(dirname "$KEY")"
	( umask 077; "$OPENSSL" genpkey -algorithm ed25519 -out "$KEY" )
	chmod 600 "$KEY"
	ID=$(id_of "$KEY")
	mkdir -p "$PUBDIR"
	"$OPENSSL" pkey -in "$KEY" -pubout -out "$PUBDIR/$ID.pem"
	echo "private key: $KEY (keep a copy somewhere safe)"
	echo "public key:  $PUBDIR/$ID.pem (commit it; the next system image trusts it)"
	;;
sign)
	need_openssl
	LIST=${2:-}
	OUT=${3:-}
	KEY=${4:-$KEY_DEFAULT}
	[ -n "$LIST" ] && [ -n "$OUT" ] || die "usage: update-key.sh sign LIST OUT [KEY]"
	[ -f "$KEY" ] || die "no signing key at $KEY (tools/update-key.sh new makes one)"
	tail -n 1 "$LIST" | grep -q '^end [0-9][0-9]*$' || die "$LIST does not end with its end line"
	ID=$(id_of "$KEY")
	PUB=$PUBDIR/$ID.pem
	[ -f "$PUB" ] || die "the key's public half is not in $PUB, so no system image trusts it"
	TMP=$(mktemp -d)
	trap 'rm -rf "$TMP"' EXIT
	"$OPENSSL" pkeyutl -sign -inkey "$KEY" -rawin -in "$LIST" -out "$TMP/sig"
	[ "$(wc -c < "$TMP/sig" | tr -d ' ')" = 64 ] || die "the signature is not 64 bytes"
	# Checked against the public file the image carries, not the private key
	"$OPENSSL" pkeyutl -verify -pubin -inkey "$PUB" -rawin -in "$LIST" -sigfile "$TMP/sig" > /dev/null ||
		die "the signature does not verify with $PUB"
	{ cat "$LIST"; printf 'sign %s %s\n' "$ID" "$("$OPENSSL" base64 -A -in "$TMP/sig")"; } > "$TMP/out"
	mv "$TMP/out" "$OUT"
	echo "signed $OUT with key $ID"
	;;
id)
	[ -f "${2:-}" ] || die "usage: update-key.sh id FILE"
	need_openssl
	id_of "$2"
	;;
*)
	sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
	;;
esac

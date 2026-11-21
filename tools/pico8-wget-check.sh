#!/bin/sh
# Your PICO-8's wget (sd/pixel2/PICO-8-wget.sh, ZyperOS/pico8/bin/wget on the
# card) against a stand-in curl and a stand-in system wget, on a card laid
# out as mk/payload.sh lays it: the two calls pico8_64 makes for Splore
# (a GET with -q -O, and a post with --post-file=) go to curl with redirects
# followed, HTTPS and HTTP only, and ZyperOS's certificate bundle; the file
# appears whole or not at all; curl's failure is its exit; and anything else,
# or no curl, goes to the system's wget with the same arguments. Then
# PICO-8.sh puts bin/ first on PATH and the payload puts the file there.
# Runs with the computer's sh; the device's is busybox 1.37's ash.
set -u
SHIM=sd/pixel2/PICO-8-wget.sh
T=$(mktemp -d "${TMPDIR:-/tmp}/pico8-wget-check.XXXXXX")
T=$(cd "$T" && pwd)
trap 'rm -rf "$T"' EXIT
fails=0
ok() { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fails=$((fails + 1)); }

# The card: ZyperOS/cacert.pem, and the shim where the payload puts it
mkdir -p "$T/card/ZyperOS/pico8/bin" "$T/sys" "$T/nocurl" "$T/out"
cp "$SHIM" "$T/card/ZyperOS/pico8/bin/wget"
chmod +x "$T/card/ZyperOS/pico8/bin/wget"
echo "-----BEGIN CERTIFICATE-----" > "$T/card/ZyperOS/cacert.pem"
BIN="$T/card/ZyperOS/pico8/bin"

# The stand-in curl: its arguments one a line in curl.args; what it does,
# from curl.mode: "ok" writes BODY to its -o file (or stdout), "empty"
# writes nothing, "cut" writes half and fails as a timeout does (28),
# "refuse" fails as a refused connection does (7)
cat > "$T/sys/curl" << EOF
#!/bin/sh
: > "$T/curl.args"
o=
prev=
for a in "\$@"; do
	printf '%s\n' "\$a" >> "$T/curl.args"
	[ "\$prev" = -o ] && o=\$a
	prev=\$a
done
mode=\$(cat "$T/curl.mode")
case \$mode in
ok) if [ "\$o" = - ]; then echo BODY; else echo BODY > "\$o"; fi ;;
empty) ;;
cut) echo BO > "\$o"; exit 28 ;;
refuse) exit 7 ;;
esac
EOF
# The stand-in system wget: its arguments one a line in wget.args
cat > "$T/sys/wget" << EOF
#!/bin/sh
: > "$T/wget.args"
for a in "\$@"; do printf '%s\n' "\$a" >> "$T/wget.args"; done
EOF
cp "$T/sys/wget" "$T/nocurl/wget"
chmod +x "$T/sys/curl" "$T/sys/wget" "$T/nocurl/wget"

# Run the shim as pico8_64's system() does: by name, on PATH
run() { PATH="$BIN:$T/sys:$PATH" wget "$@"; }
args() { tr '\n' ' ' < "$T/curl.args"; }
has() { grep -qx -- "$1" "$T/curl.args"; }
# $1 follows $2 in the arguments
pair() { awk -v a="$1" -v b="$2" 'p == a && $0 == b { f = 1 } { p = $0 } END { exit !f }' "$T/curl.args"; }
leftover() { find "$T/out" -name '*.part*' | wc -l | tr -d ' '; }

echo "pico8 wget:"
U="http://www.lexaloffle.com/bbs/cpost_lister3.php?max=32&start_index=0&cat=7&sub=2&version=000207r&cfil=0"
C="https://carts.lexaloffle.com/celeste-0.p8.png"
F="$T/out/temp new.pip.png"

# A listing, as Splore asks for it
echo ok > "$T/curl.mode"; rm -f "$T/wget.args"
run "$U" -q -O "$F"; rc=$?
[ $rc -eq 0 ] && [ "$(cat "$F" 2> /dev/null)" = BODY ] && ok "a listing: exit 0, the file written" || bad "a listing: exit $rc, file '$(cat "$F" 2> /dev/null)'"
[ "$(tail -n 1 "$T/curl.args")" = "$U" ] && ok "the URL as given, last" || bad "the URL: $(args)"
awk -v f="$F.part" 'p == "-o" && index($0, f) == 1 { k = 1 } { p = $0 } END { exit !k }' "$T/curl.args" &&
	ok "written under a name of its own first" || bad "curl's -o: $(args)"
[ "$(leftover)" -eq 0 ] && ok "and renamed, no .part left" || bad "a .part left: $(ls "$T/out")"
has -f && has -L && ok "HTTP errors fail (-f), redirects followed (-L)" || bad "-f -L: $(args)"
pair --proto =http,https && pair --proto-redir =http,https && ok "HTTP and HTTPS only, redirects too" || bad "--proto: $(args)"
pair --cacert "$BIN/../../cacert.pem" && ok "ZyperOS's certificate bundle (ZyperOS/cacert.pem)" || bad "--cacert: $(args)"
has -k || has --insecure && bad "certificates not checked: $(args)" || ok "certificates checked (no -k)"
has -q && bad "-q: the launcher's .curlrc not read: $(args)" || ok "the .curlrc read, as the launcher's curl reads it (no -q)"
pair --connect-timeout 20 && pair --max-time 180 && ok "a connect and a whole-download timeout" || bad "timeouts: $(args)"
[ ! -f "$T/wget.args" ] && ok "the system's wget not run" || bad "the system's wget ran: $(tr '\n' ' ' < "$T/wget.args")"

# A cart, then the same cart cut short: the whole one stays
G="$T/out/celeste-0.p8.png"
run "$C" -q -O "$G"; rc=$?
[ $rc -eq 0 ] && [ "$(cat "$G")" = BODY ] && ok "a cart over HTTPS: exit 0" || bad "a cart: exit $rc"
echo cut > "$T/curl.mode"
rm -f "$G"
run "$C" -q -O "$G"; rc=$?
[ $rc -eq 28 ] && ok "cut short: curl's exit (28)" || bad "cut short: exit $rc"
[ ! -e "$G" ] && [ "$(leftover)" -eq 0 ] && ok "cut short: no file, no .part" || bad "cut short left: $(ls "$T/out")"
echo refuse > "$T/curl.mode"
run "$C" -q -O "$G"; rc=$?
[ $rc -eq 7 ] && [ ! -e "$G" ] && ok "refused: curl's exit (7), no file" || bad "refused: exit $rc"

# An empty reply: an empty file, as wget leaves
echo empty > "$T/curl.mode"
E="$T/out/empty.txt"
run "$U" -q -O "$E"; rc=$?
[ $rc -eq 0 ] && [ -f "$E" ] && [ ! -s "$E" ] && ok "an empty reply: an empty file" || bad "an empty reply: exit $rc"

# A post, both spellings, and -O - (stdout)
echo ok > "$T/curl.mode"
echo "fav=1" > "$T/post.txt"
run "$U" -q -O "$F" --post-file="$T/post.txt" > /dev/null
pair --data-binary "@$T/post.txt" && ok "--post-file=FILE: the file as the body" || bad "--post-file=: $(args)"
run "$U" -q -O "$F" --post-file "$T/post.txt" > /dev/null
pair --data-binary "@$T/post.txt" && ok "--post-file FILE: the same" || bad "--post-file: $(args)"
out=$(run "$U" -q -O -)
[ "$out" = BODY ] && pair -o - && ok "-O -: to stdout" || bad "-O -: '$out' $(args)"

# What is not Splore's: the system's wget, the same arguments
run --spider "$U" > /dev/null
[ "$(tr '\n' ' ' < "$T/wget.args" 2> /dev/null)" = "--spider $U " ] && ok "an option of another kind: the system's wget" || bad "--spider: $(cat "$T/wget.args" 2> /dev/null)"
rm -f "$T/wget.args"
run "$U" > /dev/null
[ -f "$T/wget.args" ] && ok "no -O: the system's wget" || bad "no -O: not the system's wget"
rm -f "$T/wget.args"
PATH="$BIN:$T/nocurl:/bin:/usr/bin" wget "$U" -q -O "$F"
if [ -n "$(PATH="$T/nocurl:/bin:/usr/bin" command -v curl)" ]; then
	echo "  skip no curl: this computer has curl in /bin or /usr/bin"
else
	[ "$(tr '\n' ' ' < "$T/wget.args" 2> /dev/null)" = "$U -q -O $F " ] && ok "no curl: the system's wget, the same arguments" || bad "no curl: $(cat "$T/wget.args" 2> /dev/null)"
fi
# No curl, with nothing but the shim and a stand-in on PATH
rm -f "$T/wget.args"
PATH="$BIN:$T/nocurl" "$BIN/wget" "$U" -q -O "$F" 2> "$T/err"
[ "$(tr '\n' ' ' < "$T/wget.args" 2> /dev/null)" = "$U -q -O $F " ] && ok "no curl on PATH: the next wget on PATH, the same arguments" || bad "no curl on PATH: $(cat "$T/wget.args" "$T/err" 2> /dev/null)"

# bin/ named another way on PATH (with a "/./"): still passed over, not run
# again and again; and a PATH with no wget but this one: an error, not a loop
# (each in the background, ended after 10 s: a loop fails, never hangs)
limited() {
	"$@" &
	lp=$!
	( sleep 10; kill "$lp" 2> /dev/null ) &
	lw=$!
	wait "$lp"
	lrc=$?
	kill "$lw" 2> /dev/null
	wait "$lw" 2> /dev/null
	return $lrc
}
rm -f "$T/wget.args"
limited env PATH="$T/card/ZyperOS/pico8/./bin:$T/sys:$PATH" wget --spider "$U" > /dev/null 2>&1; rc=$?
[ -f "$T/wget.args" ] && ok "bin/ spelled another way on PATH: the system's wget" || bad "bin/ spelled another way: exit $rc"
limited env PATH="$BIN" "$BIN/wget" --spider "$U" > /dev/null 2> "$T/err"; rc=$?
[ $rc -eq 127 ] && grep -q "no other wget" "$T/err" && ok "no other wget at all: exit 127, said" || bad "no other wget: exit $rc $(cat "$T/err")"

# PICO-8.sh: bin/ beside it, first on PATH, before pico8_64 starts; and the
# payload copies the shim there, runnable
S=sd/pixel2/PICO-8.sh
path_line=$(grep -n 'export PATH="$HERE/bin:$PATH"' "$S" | cut -d: -f1)
exec_line=$(grep -n 'exec "$P8/pico8_64"' "$S" | head -n 1 | cut -d: -f1)
[ -n "$path_line" ] && [ -n "$exec_line" ] && [ "$path_line" -lt "$exec_line" ] &&
	ok "PICO-8.sh: bin/ first on PATH before pico8_64" || bad "PICO-8.sh: no PATH line before pico8_64"
# (the card's scripts, mk/card-scripts.sh, which mk/payload.sh and make
# px2-programs both lay out with)
grep -q 'cp "$ROOT/sd/pixel2/PICO-8-wget.sh" "$P/pico8/bin/wget"' mk/card-scripts.sh &&
	grep -q 'chmod +x .*"$P/pico8/bin/wget"' mk/card-scripts.sh &&
	grep -q 'sh "$ROOT/mk/card-scripts.sh" "$P"' mk/payload.sh &&
	ok "mk/payload.sh: ZyperOS/pico8/bin/wget, runnable" || bad "mk/payload.sh: no pico8/bin/wget"

if [ $fails -ne 0 ]; then
	echo "pico8 wget: $fails failed"
	exit 1
fi
echo "pico8 wget: all passed"

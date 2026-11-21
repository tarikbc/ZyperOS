CHECKS += check-pico8-wget
#| your PICO-8's wget (`sd/pixel2/PICO-8-wget.sh`, `ZyperOS/pico8/bin/wget` on the card: pico8_64
#| downloads every Splore list and cart with `wget`, and busybox's has no HTTPS) against a stand-in
#| curl and system wget: Splore's two calls (`-q -O FILE`, and a post with `--post-file`) go to
#| curl with redirects followed, HTTP and HTTPS only, ZyperOS's certificate bundle, no `-k` and no
#| `-q` (the launcher's `.curlrc` read); the file whole under its name or not there, no `.part`
#| left, curl's exit on a failure; an empty reply an empty file; `-O -` to stdout; any other call,
#| or no curl, the next wget on PATH with the same arguments, never itself again (exit 127 with
#| none); PICO-8.sh puts `bin/` first on PATH, and mk/payload.sh puts the file there, runnable

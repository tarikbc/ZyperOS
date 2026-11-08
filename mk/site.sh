#!/bin/sh
# Lay out ZyperOS's web page, site/index.html and its 3D Pixel 2 (pixel2.js),
# and its blog (site/blog/, made into pages by mk/blog.py), beside the
# pictures they show, ready to serve. Cloudflare Pages runs it on
# every change to main that touches the page or its pictures (build command
# `sh mk/site.sh _site`, output `_site`) and serves the result at
# https://zyperos.com; run it by hand to look at the page first:
#
#   sh mk/site.sh [OUT]        OUT defaults to build/site
#   python3 -m http.server -d build/site
#
# The pictures stay where the README keeps them: the lockups and Zyper that
# tools/genzyper.py writes, and the screenshots. One copy of each in the
# repository, so the page and the README never show different ones.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/site}
rm -rf "$OUT"
mkdir -p "$OUT/img/dark" "$OUT/models"
# Every file at the top of site/: the page, its script, the 404 page, the
# icons and social card tools/site-art.sh draws, robots.txt, llms.txt, the
# web manifest, the IndexNow key, Cloudflare's _headers, and the blog's
# style sheet and script (site/blog/, the posts, is a folder: mk/blog.py
# reads it)
for f in "$ROOT"/site/*; do
	if [ -f "$f" ]; then cp "$f" "$OUT/"; fi
done
cp "$ROOT/site/models/pixel2.glb" "$ROOT/site/models/pixel2-poster.png" "$OUT/models/"
cp "$ROOT/res/web/icon.svg" "$ROOT/res/web/zyper.svg" "$OUT/img/"
# Every screenshot in the light theme; dark/ only for the light-and-dark section
for f in lockup lockup-light systems gallery shelf info cheevos cheevo-notice menu scraper mp4 nowplaying portal battery; do
	cp "$ROOT/res/readme/$f.png" "$OUT/img/"
done
for f in systems menu; do
	cp "$ROOT/res/readme/dark/$f.png" "$OUT/img/dark/"
done
# Zyper's photo, in The name
cp "$ROOT/res/readme/zyper-photo.jpg" "$OUT/img/"
# The blog (mk/blog.py, python3's standard library): the posts in site/blog/
# as pages under /blog/, its index and RSS feed, the sitemap, and a Blog
# section at the end of llms.txt. The landing page's sitemap line is dated by
# its last commit (a shallow clone may only know the newest commit; today's
# date if git knows none); each post by its own updated date.
day=$(git -C "$ROOT" log -1 --format=%cs -- site/index.html 2>/dev/null || true)
[ -n "$day" ] || day=$(date -u +%Y-%m-%d)
python3 "$ROOT/mk/blog.py" --out "$OUT" --home-lastmod "$day"
echo "site: $OUT"

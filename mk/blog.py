#!/usr/bin/env python3
"""zyperos.com's blog: every post in site/blog/, laid out as pages under /blog/.

A post is one file, site/blog/<slug>.html: a few `key: value` lines, a line of
three dashes, then the article as plain HTML. The slug is the file's name and
the page's address, https://zyperos.com/blog/<slug>/. The keys:

    title        the page's title and its heading, 60 characters at most
    description  for search results and shared links, 155 characters at most
    summary      the line under the title on the blog's index and in llms.txt
    topic        compare or guide
    published    the first day it was on the site, YYYY-MM-DD
    updated      the day its facts last changed, YYYY-MM-DD (the sitemap's lastmod)
    image        a screenshot in res/readme/, by name without .png: the index's card
    image_alt    what that screenshot shows
    crumb        optional: a shorter name for the breadcrumb
    related      optional: up to three slugs for Read next, comma-separated
    weight       optional: lower comes first among posts of the same day (50)

In the article, the first paragraph is the lead, and the ZyperOS box goes
after it. Every <h2> is a line in On this page. An <img src="/img/NAME.png">
is a screenshot from res/readme/, light theme only, copied beside the page,
with its size filled in. In a table cell, [yes: what it has], [no: what it
lacks] and [unk] become the landing page's tick, cross and dash, the words
as the mark's tooltip. Every post ends with a Sources section.

mk/site.sh runs this with --out after it lays out the landing page, so
Cloudflare Pages builds the blog with it: python3's standard library only, as
the build image has nothing else. It writes the posts, the index, the RSS
feed, the sitemap (the landing page's line included) and a Blog section at
the end of llms.txt, all from the same files, so none of them can miss a post.

    python3 mk/blog.py --out DIR [--home-lastmod YYYY-MM-DD]
    python3 mk/blog.py --check    builds the whole site in a scratch folder and
                                  checks it (make check-blog)
"""

import argparse
import hashlib
import html
from html.parser import HTMLParser
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from datetime import date, datetime, timezone
from email.utils import format_datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "site" / "blog"
SHOTS = ROOT / "res" / "readme"
SITE = "https://zyperos.com"
AUTHOR = {"@type": "Person", "@id": SITE + "/#tarik", "name": "Tarik Caramanico", "url": "https://github.com/tarikbc"}
TOPICS = {"compare": ("Comparison", "Comparisons"), "guide": ("Guide", "Guides")}
REQUIRED = ("title", "description", "summary", "topic", "published", "updated", "image", "image_alt")
TITLE_MAX, DESC_MAX = 60, 155
INDEX_TITLE = "GKD Pixel 2 Guides and Comparisons | ZyperOS Blog"
INDEX_DESC = ("Guides and firmware comparisons for the GKD Pixel 2: installing, Wi-Fi, adding games, "
              "PortMaster, box art, and how ZyperOS compares.")

TICK = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg>'
CROSS = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 7l10 10M17 7L7 17"/></svg>'
UNKNOWN = "No source we read says either way."


class Fail(Exception):
    pass


def esc(text):
    return html.escape(text, quote=True)


def long_day(day):
    d = date.fromisoformat(day)
    return "%d %s %d" % (d.day, d.strftime("%B"), d.year)


def version(path):
    """?v= of a file's content: /blog.css and /blog.js keep their names."""
    return hashlib.sha1(path.read_bytes()).hexdigest()[:8]


def png_size(path):
    head = path.read_bytes()[:24]
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise Fail("%s is not a PNG" % path)
    return struct.unpack(">II", head[16:24])


# --- Reading the posts ------------------------------------------------------

def parse(path):
    raw = path.read_text(encoding="utf-8")
    head, sep, body = raw.partition("\n---\n")
    if not sep:
        raise Fail("%s: no line of three dashes after the keys" % path.name)
    meta = {}
    for line in head.splitlines():
        if not line.strip():
            continue
        key, colon, value = line.partition(":")
        if not colon:
            raise Fail("%s: %r is not a key: value line" % (path.name, line))
        meta[key.strip()] = value.strip()
    for key in REQUIRED:
        if not meta.get(key):
            raise Fail("%s: no %s" % (path.name, key))
    for key in ("published", "updated"):
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", meta[key]):
            raise Fail("%s: %s is not YYYY-MM-DD" % (path.name, key))
    if meta["updated"] < meta["published"]:
        raise Fail("%s: updated is before published" % path.name)
    if meta["topic"] not in TOPICS:
        raise Fail("%s: topic is %s, not compare or guide" % (path.name, meta["topic"]))
    if len(meta["title"]) > TITLE_MAX:
        raise Fail("%s: the title has %d characters, %d at most" % (path.name, len(meta["title"]), TITLE_MAX))
    if len(meta["description"]) > DESC_MAX:
        raise Fail("%s: the description has %d characters, %d at most" % (path.name, len(meta["description"]), DESC_MAX))
    if not (SHOTS / (meta["image"] + ".png")).is_file():
        raise Fail("%s: no res/readme/%s.png" % (path.name, meta["image"]))
    meta["slug"] = path.stem
    meta["url"] = "%s/blog/%s/" % (SITE, path.stem)
    meta["weight"] = int(meta.get("weight", "50"))
    meta["related"] = [s.strip() for s in meta.get("related", "").split(",") if s.strip()]
    meta["body"] = body.strip()
    return meta


def load_posts():
    posts = [parse(p) for p in sorted(SRC.glob("*.html"))]
    slugs = {p["slug"] for p in posts}
    for p in posts:
        for s in p["related"]:
            if s not in slugs:
                raise Fail("%s: related names %s, which is not a post" % (p["slug"], s))
    # Newest first; among a day's posts, the lower weight first
    posts.sort(key=lambda p: (-int(p["published"].replace("-", "")), p["weight"], p["slug"]))
    return posts


# --- The article's own HTML -------------------------------------------------

def mark(kind, words):
    if kind == "unk":
        return ('<span class="ic unk" tabindex="0" role="img" aria-label="Unknown. %s" data-tip="%s">–</span>'
                % (UNKNOWN, UNKNOWN))
    words = words or ("Yes" if kind == "yes" else "No")
    return ('<span class="ic %s" tabindex="0" role="img" aria-label="%s. %s" data-tip="%s">%s</span>'
            % (kind, "Yes" if kind == "yes" else "No", words, words, TICK if kind == "yes" else CROSS))


def expand_marks(body):
    return re.sub(r"\[(yes|no|unk)(?::\s*([^\]]*))?\]", lambda m: mark(m.group(1), (m.group(2) or "").strip()), body)


def text_of(fragment):
    return html.unescape(re.sub(r"<[^>]+>", "", fragment)).strip()


def slugify(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def headings(body):
    """Ids for every <h2> without one; the list of (id, text) for On this page."""
    toc = []
    seen = set()

    def one(m):
        attrs, inner = m.group(1), m.group(2)
        found = re.search(r'\bid="([^"]+)"', attrs)
        hid = found.group(1) if found else slugify(text_of(inner))
        if hid in seen:
            raise Fail("two headings have the id %s" % hid)
        seen.add(hid)
        toc.append((hid, text_of(inner)))
        if found:
            return m.group(0)
        return '<h2 id="%s"%s>%s</h2>' % (hid, attrs, inner)
    body = re.sub(r"<h2([^>]*)>(.*?)</h2>", one, body, flags=re.S)
    return body, toc


def images(body, used):
    """Size, lazy loading and a copy beside the page, for each screenshot."""
    def one(m):
        tag = m.group(0)
        src = re.search(r'\bsrc="/img/([^"?#]+)\.png"', tag)
        if not src:
            raise Fail("an image that is not a screenshot from res/readme/: %s" % tag[:80])
        name = src.group(1)
        if "/" in name:
            raise Fail("/img/%s.png: screenshots are light theme, from res/readme/ itself" % name)
        path = SHOTS / (name + ".png")
        if not path.is_file():
            raise Fail("no res/readme/%s.png" % name)
        if not re.search(r'\balt="', tag):
            raise Fail("/img/%s.png has no alt text" % name)
        used.add(name)
        w, h = png_size(path)
        extra = ""
        if "width=" not in tag:
            extra += ' width="%d" height="%d"' % (w, h)
        if "loading=" not in tag:
            extra += ' loading="lazy"'
        if "decoding=" not in tag:
            extra += ' decoding="async"'
        return tag[:-1].rstrip("/").rstrip() + extra + ">"
    return re.sub(r"<img\b[^>]*>", one, body)


def words(fragment):
    return len(re.findall(r"\w+", text_of(fragment)))


# --- Pieces every page shares -----------------------------------------------

def lockup(cls, href=None, eager=False):
    pic = ('<picture><source media="(prefers-color-scheme: dark)" srcset="/img/lockup.png">'
           '<img src="/img/lockup-light.png" width="1405" height="400" alt="ZyperOS"%s></picture>'
           % ("" if eager else ' loading="lazy"'))
    if href:
        return '<a class="%s" href="%s" aria-label="ZyperOS home">%s</a>' % (cls, href, pic.replace('alt="ZyperOS"', 'alt=""'))
    return '<span class="%s">%s</span>' % (cls, pic)


def head(title, desc, url, og_type, graph, assets, extra=""):
    return """<!doctype html>
<!-- Written by mk/blog.py: change the post in site/blog/, or the script. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>{t}</title>
<meta name="description" content="{d}">
<link rel="canonical" href="{u}">
<meta property="og:title" content="{t}">
<meta property="og:description" content="{d}">
<meta property="og:url" content="{u}">
<meta property="og:type" content="{ot}">
<meta property="og:site_name" content="ZyperOS">
<meta property="og:locale" content="en_US">
<meta property="og:image" content="{s}/og.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:image:alt" content="ZyperOS, fast custom firmware for the GKD Pixel 2, beside its Spotlight home screen">
{extra}<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="{t}">
<meta name="twitter:description" content="{d}">
<meta name="twitter:image" content="{s}/og.png">
<meta name="twitter:image:alt" content="ZyperOS, fast custom firmware for the GKD Pixel 2, beside its Spotlight home screen">
<meta name="color-scheme" content="light dark">
<meta name="theme-color" content="#e6e4da" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#111310" media="(prefers-color-scheme: dark)">
<link rel="icon" href="/favicon.ico" sizes="48x48">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="icon" href="/favicon-96.png" type="image/png" sizes="96x96">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="manifest" href="/site.webmanifest">
<link rel="alternate" type="application/rss+xml" title="ZyperOS Blog" href="/blog/feed.xml">
<script type="application/ld+json">
{ld}
</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Lexend:wght@400;600&display=swap">
<link rel="stylesheet" href="/blog.css?v={cv}">
<script src="/blog.js?v={jv}" defer></script>
</head>
<body>
""".format(t=esc(title), d=esc(desc), u=url, ot=og_type, s=SITE, extra=extra,
           ld=json.dumps({"@context": "https://schema.org", "@graph": graph}, indent=1, ensure_ascii=False),
           cv=assets["css"], jv=assets["js"])


def nav():
    links = [("/#features", "Features"), ("/#compare", "Compare"), ("/#install", "Install"),
             ("/blog/", "Blog"), ("https://github.com/tarikbc/ZyperOS", "GitHub")]
    items = "".join('<li><a href="%s"%s>%s</a></li>' % (h, ' aria-current="page"' if t == "Blog" else "", t)
                    for h, t in links)
    return ('<nav class="nav in" aria-label="Site">\n  %s\n  <ul>%s</ul>\n</nav>\n'
            % (lockup("home", "/", eager=True), items))


def footer():
    return """<footer>
  <div class="in">
    %s
    <span class="meta">ZyperOS is free and open source, under GPL-3.0.</span>
    <span class="links">
      <a href="/">Home</a>
      <a href="/blog/">Blog</a>
      <a href="https://github.com/tarikbc/ZyperOS">GitHub</a>
      <a href="https://github.com/tarikbc/ZyperOS/blob/main/docs/guide.md">Guide</a>
      <a href="https://github.com/tarikbc/ZyperOS/blob/main/LICENSE">License</a>
      <a href="https://github.com/tarikbc/ZyperOS/blob/main/TRADEMARK.md">Trademark</a>
    </span>
  </div>
</footer>
</body>
</html>
""" % lockup("lockup")


def crumb_graph(trail):
    return {"@type": "BreadcrumbList", "itemListElement": [
        {"@type": "ListItem", "position": i + 1, "name": name, "item": url} for i, (name, url) in enumerate(trail)]}


# --- The pages --------------------------------------------------------------

ZBOX = """<aside class="zbox" aria-label="About ZyperOS">
  <img src="/img/zyper.svg" width="76" height="80" alt="">
  <div>
    <p><b>ZyperOS</b> is a fast, light firmware for the GKD Pixel 2: forty-four systems, PortMaster ports, box art, RetroAchievements, music and video. It is free, and version 1.0.4 is out: a card image and ZyperOS Installer, on GitHub.</p>
    <span class="row"><a class="btn go" href="https://github.com/tarikbc/ZyperOS"><span class="glyph" aria-hidden="true">A</span>See it on GitHub</a><a class="btn alt" href="/#compare">How it compares</a></span>
  </div>
</aside>"""

ZMINI = """<div class="zmini">
  <img src="/img/zyper.svg" width="56" height="59" alt="">
  <b>ZyperOS</b>
  <p>Fast, light firmware for the GKD Pixel 2. Free, and ready to install.</p>
  <a class="btn go" href="https://github.com/tarikbc/ZyperOS">See it on GitHub</a>
</div>"""


def related_for(post, posts):
    by_slug = {p["slug"]: p for p in posts}
    picks = [by_slug[s] for s in post["related"]][:3]
    for p in sorted(posts, key=lambda p: (p["topic"] != post["topic"], p["weight"], p["slug"])):
        if len(picks) == 3:
            break
        if p["slug"] != post["slug"] and p not in picks:
            picks.append(p)
    return picks


def render_post(post, posts, assets, used):
    body = expand_marks(post["body"])
    body = images(body, used)
    body, toc = headings(body)
    if "sources" not in [hid for hid, _ in toc]:
        raise Fail("%s: no <h2>Sources</h2>" % post["slug"])
    lead = re.match(r"\s*<p>(.*?)</p>", body, re.S)
    if not lead:
        raise Fail("%s: the article does not start with its lead paragraph" % post["slug"])
    rest = body[lead.end():].strip()
    minutes = max(1, round(words(body) / 220))
    label = TOPICS[post["topic"]][0]
    crumb = post.get("crumb") or post["title"]
    toc_items = "".join('<li><a href="#%s">%s</a></li>' % (hid, esc(t)) for hid, t in toc)
    when = '<time datetime="%s">%s</time>' % (post["published"], long_day(post["published"]))
    if post["updated"] != post["published"]:
        when += ' · Updated <time datetime="%s">%s</time>' % (post["updated"], long_day(post["updated"]))
    rel = "".join('<li><a href="/blog/%s/"><span class="topic">%s</span><b>%s</b></a></li>'
                  % (p["slug"], TOPICS[p["topic"]][0], esc(p["title"])) for p in related_for(post, posts))
    graph = [
        {"@type": "BlogPosting", "@id": post["url"] + "#article", "headline": post["title"],
         "description": post["description"], "datePublished": post["published"],
         "dateModified": post["updated"], "author": AUTHOR, "publisher": AUTHOR,
         "image": "%s/img/%s.png" % (SITE, post["image"]), "mainEntityOfPage": post["url"],
         "inLanguage": "en", "articleSection": label, "wordCount": words(body),
         "isPartOf": {"@type": "Blog", "@id": SITE + "/blog/#blog", "name": "ZyperOS Blog", "url": SITE + "/blog/"},
         "about": {"@type": "SoftwareApplication", "@id": SITE + "/#software", "name": "ZyperOS",
                   "operatingSystem": "GKD Pixel 2", "applicationCategory": "GameApplication"}},
        crumb_graph([("ZyperOS", SITE + "/"), ("Blog", SITE + "/blog/"), (crumb, post["url"])]),
    ]
    extra = ('<meta property="article:published_time" content="%s">\n'
             '<meta property="article:modified_time" content="%s">\n'
             '<meta property="article:section" content="%s">\n' % (post["published"], post["updated"], label))
    page = head(post["title"], post["description"], post["url"], "article", graph, assets, extra)
    page += nav()
    page += """<main class="in post">
<article>
  <nav class="crumbs" aria-label="Breadcrumb"><ol><li><a href="/">ZyperOS</a></li><li><a href="/blog/">Blog</a></li><li aria-current="page">{crumb}</li></ol></nav>
  <header class="post-head">
    <span class="topic">{label}</span>
    <h1>{title}</h1>
    <p class="meta">By Tarik Caramanico · {when} · {mins} min read</p>
  </header>
  <p class="lead">{lead}</p>
{zbox}
  <details class="toc-inline"><summary>On this page</summary><ol>{toc}</ol></details>
  <div class="prose">
{rest}
  </div>
  <section class="related" aria-labelledby="read-next"><h2 id="read-next">Read next</h2><ul>{rel}</ul></section>
</article>
<aside class="side" aria-label="On this page">
  <nav class="toc"><b>On this page</b><ol>{toc}</ol></nav>
{zmini}
</aside>
</main>
""".format(crumb=esc(crumb), label=label, title=esc(post["title"]), when=when, mins=minutes,
           lead=lead.group(1).strip(), zbox=ZBOX, toc=toc_items, rest=rest, rel=rel, zmini=ZMINI)
    return page + footer()


def render_index(posts, assets):
    url = SITE + "/blog/"
    cards = []
    for i, p in enumerate(posts):
        w, h = png_size(SHOTS / (p["image"] + ".png"))
        lazy = "" if i < 3 else ' loading="lazy"'
        cards.append('<li data-topic="%s"><a class="card" href="/blog/%s/"><img src="/img/%s.png" width="%d" height="%d" alt=""%s decoding="async">'
                     '<span class="topic">%s</span><b>%s</b><span class="soft">%s</span>'
                     '<span class="meta"><time datetime="%s">%s</time></span></a></li>'
                     % (p["topic"], p["slug"], p["image"], w, h, lazy, TOPICS[p["topic"]][0], esc(p["title"]),
                        esc(p["summary"]), p["published"], long_day(p["published"])))
    chips = '<button type="button" data-topic="all" aria-pressed="true">All</button>' + "".join(
        '<button type="button" data-topic="%s" aria-pressed="false">%s</button>' % (t, TOPICS[t][1]) for t in TOPICS)
    graph = [
        {"@type": "Blog", "@id": SITE + "/blog/#blog", "name": "ZyperOS Blog", "url": url,
         "description": INDEX_DESC, "inLanguage": "en", "publisher": AUTHOR,
         "blogPost": [{"@type": "BlogPosting", "headline": p["title"], "url": p["url"],
                       "datePublished": p["published"], "dateModified": p["updated"]} for p in posts]},
        crumb_graph([("ZyperOS", SITE + "/"), ("Blog", url)]),
    ]
    page = head(INDEX_TITLE, INDEX_DESC, url, "website", graph, assets)
    page += nav()
    page += """<main class="in">
  <header class="blog-head">
    <h1>Guides and comparisons for the GKD Pixel 2</h1>
    <p class="soft">How to set up the GKD Pixel 2, and how ZyperOS compares with the other systems for it, with a source for every claim about them.</p>
    <div class="chips" role="group" aria-label="Show" hidden>{chips}</div>
  </header>
  <ul class="cards">
{cards}
  </ul>
</main>
<script>document.querySelector(".chips").hidden = false;</script>
""".format(chips=chips, cards="\n".join(cards))
    return page + footer()


def render_feed(posts):
    items = []
    for p in posts:
        when = datetime.fromisoformat(p["published"] + "T12:00:00").replace(tzinfo=timezone.utc)
        items.append("""  <item>
    <title>{t}</title>
    <link>{u}</link>
    <guid isPermaLink="true">{u}</guid>
    <pubDate>{d}</pubDate>
    <category>{c}</category>
    <description>{s}</description>
  </item>""".format(t=esc(p["title"]), u=p["url"], d=format_datetime(when), c=TOPICS[p["topic"]][0],
                    s=esc(p["description"])))
    newest = max(p["updated"] for p in posts)
    build = datetime.fromisoformat(newest + "T12:00:00").replace(tzinfo=timezone.utc)
    return """<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:atom="http://www.w3.org/2005/Atom">
<channel>
  <title>ZyperOS Blog</title>
  <link>{s}/blog/</link>
  <atom:link href="{s}/blog/feed.xml" rel="self" type="application/rss+xml"/>
  <description>{d}</description>
  <language>en</language>
  <lastBuildDate>{b}</lastBuildDate>
{items}
</channel>
</rss>
""".format(s=SITE, d=esc(INDEX_DESC), b=format_datetime(build), items="\n".join(items))


def render_sitemap(posts, home_day):
    urls = [(SITE + "/", home_day), (SITE + "/blog/", max(p["updated"] for p in posts))]
    urls += [(p["url"], p["updated"]) for p in posts]
    lines = "\n".join("  <url><loc>%s</loc><lastmod>%s</lastmod></url>" % u for u in urls)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n%s\n</urlset>\n' % lines)


def llms_section(posts):
    out = ["", "## Blog", "- [Blog index](%s/blog/): guides and comparisons for the GKD Pixel 2." % SITE]
    for topic in TOPICS:
        for p in sorted((p for p in posts if p["topic"] == topic), key=lambda p: (p["weight"], p["slug"])):
            out.append("- [%s](%s): %s" % (p["title"], p["url"], p["summary"]))
    return "\n".join(out) + "\n"


def build(out, home_day):
    posts = load_posts()
    if not posts:
        raise Fail("no posts in site/blog/")
    assets = {"css": version(ROOT / "site" / "blog.css"), "js": version(ROOT / "site" / "blog.js")}
    used = {p["image"] for p in posts} | {"lockup", "lockup-light"}
    (out / "blog").mkdir(parents=True, exist_ok=True)
    for p in posts:
        d = out / "blog" / p["slug"]
        d.mkdir(exist_ok=True)
        (d / "index.html").write_text(render_post(p, posts, assets, used), encoding="utf-8")
    (out / "blog" / "index.html").write_text(render_index(posts, assets), encoding="utf-8")
    (out / "blog" / "feed.xml").write_text(render_feed(posts), encoding="utf-8")
    (out / "sitemap.xml").write_text(render_sitemap(posts, home_day), encoding="utf-8")
    (out / "img").mkdir(exist_ok=True)
    for name in sorted(used):
        dest = out / "img" / (name + ".png")
        if not dest.exists():
            shutil.copyfile(SHOTS / (name + ".png"), dest)
    llms = out / "llms.txt"
    if llms.exists():
        llms.write_text(llms.read_text(encoding="utf-8").rstrip("\n") + "\n" + llms_section(posts), encoding="utf-8")
    return posts


# --- make check-blog ----------------------------------------------------------

VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}


class Balance(HTMLParser):
    """Every element a post opens, closed in order: a </td> for a <th> or a
    lost </li> is a page a browser repairs its own way."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack, self.errors = [], []

    def handle_starttag(self, tag, attrs):
        if tag not in VOID:
            self.stack.append((tag, self.getpos()[0]))

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1][0] != tag:
            want = self.stack[-1] if self.stack else ("nothing", 0)
            self.errors.append("line %d: </%s> where <%s> from line %d is open"
                               % (self.getpos()[0], tag, want[0], want[1]))
            return
        self.stack.pop()


def balance(body):
    b = Balance()
    b.feed(body)
    b.close()
    return b.errors + ["<%s> from line %d never closed" % t for t in b.stack]


def tokens(css):
    """The custom properties of the first :root block and of the dark one."""
    light = re.search(r":root\s*\{(.*?)\}", css, re.S)
    dark = re.search(r"prefers-color-scheme:\s*dark\)\s*\{\s*:root\s*\{(.*?)\}", css, re.S)
    if not light or not dark:
        raise Fail("no light or dark :root block")
    read = lambda block: dict(re.findall(r"(--[\w-]+)\s*:\s*([^;]+);", block))
    return read(light.group(1)), read(dark.group(1))


def target(out, page, href):
    """The file an absolute or relative link on `page` reaches, and its #part."""
    path, _, frag = href.partition("#")
    path = path.partition("?")[0]
    if not path:
        return page, frag
    if path.startswith("/"):
        f = out / path.lstrip("/")
    else:
        f = page.parent / path
    if path.endswith("/") or f.is_dir():
        f = f / "index.html"
    return f, frag


def redirects(out):
    """The paths Cloudflare's _redirects answers (its first field, such as
    /tip): a link to one reaches a page that is not a file in the build."""
    f = out / "_redirects"
    if not f.is_file():
        return set()
    return {line.split()[0] for line in f.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")}


def check(out, posts):
    problems = []
    redirected = redirects(out)
    for src in sorted(SRC.glob("*.html")):
        text = src.read_text(encoding="utf-8")
        if "—" in text:
            problems.append("%s: an em dash; use a comma, a colon or two sentences" % src.name)
        if re.search(r"polyform", text, re.I):
            problems.append("%s: names PolyForm; ZyperOS is open source, under GPL-3.0" % src.name)
        if re.search(r"\b(?:is|are)\s+(?:not|no longer)\s+open[ -]source", text, re.I):
            problems.append("%s: says ZyperOS is not open source; it is, under GPL-3.0" % src.name)
        if re.search(r"\]\(https?://", text):
            problems.append("%s: a Markdown link; posts are HTML" % src.name)
        body = text.partition("\n---\n")[2]
        offset = text[:len(text) - len(body)].count("\n")
        for e in balance(body):
            problems.append("%s: %s" % (src.name, re.sub(r"line (\d+)", lambda m: "line %d" % (int(m.group(1)) + offset), e)))
    landing = (ROOT / "site" / "index.html").read_text(encoding="utf-8")
    css = (ROOT / "site" / "blog.css").read_text(encoding="utf-8")
    style = re.search(r"<style>(.*?)</style>", landing, re.S).group(1)
    for name, a, b in zip(("light", "dark"), tokens(style), tokens(css)):
        if a != b:
            diff = sorted(set(a.items()) ^ set(b.items()))
            problems.append("site/blog.css's %s colors differ from site/index.html's: %s" % (name, diff))
    sitemap = (out / "sitemap.xml").read_text(encoding="utf-8")
    locs = re.findall(r"<loc>([^<]+)</loc>", sitemap)
    for p in posts:
        if p["url"] not in locs:
            problems.append("sitemap.xml has no %s" % p["url"])
    for loc in locs:
        f, _ = target(out, out / "index.html", loc[len(SITE):] or "/")
        if not f.is_file():
            problems.append("sitemap.xml lists %s, which is not in the build" % loc)
    llms = (out / "llms.txt").read_text(encoding="utf-8")
    for p in posts:
        if p["url"] not in llms:
            problems.append("llms.txt has no line for %s" % p["slug"])
    pages = sorted((out / "blog").rglob("index.html")) + [out / "index.html"]
    ids = {}
    for page in pages:
        text = page.read_text(encoding="utf-8")
        ids[page] = set(re.findall(r'\bid="([^"]+)"', text))
    for page in pages:
        text = page.read_text(encoding="utf-8")
        rel = page.relative_to(out)
        for ld in re.findall(r'<script type="application/ld\+json">(.*?)</script>', text, re.S):
            try:
                json.loads(ld)
            except ValueError as e:
                problems.append("%s: its structured data is not JSON (%s)" % (rel, e))
        for href in re.findall(r'\b(?:href|src|srcset)="([^"]+)"', text):
            if re.match(r"(https?:|mailto:|data:)", href):
                continue
            if href.partition("#")[0].partition("?")[0] in redirected:
                continue
            f, frag = target(out, page, href)
            if not f.is_file():
                problems.append("%s: %s goes nowhere" % (rel, href))
            elif frag and f in ids and frag not in ids[f]:
                problems.append("%s: %s, no such id there" % (rel, href))
        if page.parent != out:
            m = re.search(r"<title>(.*?)</title>", text)
            if not m or len(html.unescape(m.group(1))) > TITLE_MAX:
                problems.append("%s: the title is missing or over %d characters" % (rel, TITLE_MAX))
            d = re.search(r'<meta name="description" content="([^"]*)"', text)
            if not d or len(html.unescape(d.group(1))) > DESC_MAX:
                problems.append("%s: the description is missing or over %d characters" % (rel, DESC_MAX))
    if 'href="/blog/"' not in landing:
        problems.append("site/index.html does not link to /blog/")
    return problems


def run_check():
    posts = load_posts()
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "site"
        subprocess.run(["sh", str(ROOT / "mk" / "site.sh"), str(out)], check=True, stdout=subprocess.DEVNULL)
        problems = check(out, posts)
    for p in problems:
        print("check-blog: " + p, file=sys.stderr)
    if problems:
        return 1
    print("check-blog: %d posts, their index, feed, sitemap and llms.txt lines, every link reaching a page" % len(posts))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", type=Path, help="the site being built (mk/site.sh's OUT)")
    ap.add_argument("--home-lastmod", default=date.today().isoformat(), help="the landing page's lastmod")
    ap.add_argument("--check", action="store_true", help="build in a scratch folder and check it")
    args = ap.parse_args()
    try:
        if args.check:
            return run_check()
        if not args.out:
            ap.error("--out or --check")
        posts = build(args.out, args.home_lastmod)
        print("blog: %d posts" % len(posts))
        return 0
    except Fail as e:
        print("blog: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

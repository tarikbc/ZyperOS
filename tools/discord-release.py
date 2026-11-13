#!/usr/bin/env python3
"""A ZyperOS release's notes, posted in the Discord's #releases channel.

  tools/discord-release.py v1.0.5          post them; the release must be
                                           published on GitHub already
  tools/discord-release.py --wait v1.0.5   wait (up to an hour) for GitHub to
                                           publish it, then post them
  tools/discord-release.py --show v1.0.5   print the message; post nothing
  tools/discord-release.py --check         every docs/releases/v*.md fits in
                                           Discord's limits (make
                                           check-discord-release); no network

tools/release.sh --publish runs it with --wait at its end: the release
workflow publishes the release after it builds the installer, and the post
links to the published release, so it waits for that.

The message is docs/releases/<tag>.md as people read it in Discord: its first
paragraph, then every section but Install and Thanks, which stay on GitHub
behind the link at the end. Headings become bold lines. Discord takes at most
4096 characters in an embed and 6000 in a message, so the notes are cut
between paragraphs and list items into embeds, and into more than one message
if they need it.

It reads Zyper's bot token from ~/.config/zyperos/discord-bot.token (never
printed). A tag Zyper already posted is not posted again. #releases is an
announcement channel, so the post is published to the servers that follow it.
"""
import json, os, re, subprocess, sys, time, urllib.error, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "tarikbc/ZyperOS"
API = "https://discord.com/api/v10"
TOKEN_FILE = os.path.expanduser("~/.config/zyperos/discord-bot.token")
LIME = 0xBEF23C          # ZyperOS's lime, as the Developer role has it
SKIP = ("Install", "Thanks")  # sections left on GitHub
EMBED_TEXT, MESSAGE_TEXT, EMBEDS = 4096, 6000, 10  # Discord's limits
NEWS = 5                 # an announcement channel's type


def release_url(tag):
    return f"https://github.com/{REPO}/releases/tag/{tag}"


def pieces(notes):
    """The notes as Discord shows them, in pieces that may not be split: the
    first paragraph, each kept section's heading (bold) with nothing else, and
    each paragraph or list item under it."""
    out, keep, para = [], True, []

    def flush():
        if para:
            out.append("\n".join(para))
            para.clear()

    for line in notes.splitlines():
        m = re.match(r"^#{1,6}\s+(.*)", line)
        if m:
            flush()
            title = m.group(1).strip()
            keep = not title.startswith(SKIP)
            if keep:
                out.append(f"**{title}**")
            continue
        if not keep:
            continue
        if not line.strip():
            flush()
        elif re.match(r"^\s*([-*]|\d+\.)\s", line):
            flush()
            para.append(line.rstrip())
        else:
            para.append(line.rstrip())
    flush()
    return out


def messages(tag, notes):
    """The messages to post: each a list of embeds, within Discord's limits.
    Raises ValueError for a piece too long for one embed."""
    version = tag[1:] if tag.startswith("v") else tag
    title = f"ZyperOS {version}"
    parts = pieces(notes)
    parts.append(f"Downloads, install steps and thanks: [ZyperOS {version} on GitHub]({release_url(tag)})")
    for p in parts:
        if len(p) > EMBED_TEXT:
            raise ValueError(f"{tag}: a piece of {len(p)} characters does not fit in one embed: {p[:60]!r}")
    item = re.compile(r"^\s*([-*]|\d+\.)\s")
    texts, cur, prev = [], "", ""
    for p in parts:
        # list items sit on lines of their own, as in the notes; anything
        # else gets a blank line before it
        sep = "\n" if item.match(p) and item.match(prev) else "\n\n"
        prev = p
        nxt = cur + (sep if cur else "") + p
        if len(nxt) > EMBED_TEXT:
            texts.append(cur)
            cur = p
        else:
            cur = nxt
    if cur:
        texts.append(cur)
    # a bold heading alone at the end of an embed goes with what follows it
    for i in range(len(texts) - 1):
        head, sep, last = texts[i].rpartition("\n\n")
        if sep and re.fullmatch(r"\*\*[^*\n]+\*\*", last) and len(last) + 2 + len(texts[i + 1]) <= EMBED_TEXT:
            texts[i], texts[i + 1] = head, last + "\n\n" + texts[i + 1]
    embeds = []
    for i, t in enumerate(texts):
        e = {"description": t, "color": LIME}
        if i == 0:
            e.update(title=title, url=release_url(tag))
        embeds.append(e)
    out, cur, size = [], [], 0
    for e in embeds:
        n = len(e["description"]) + len(e.get("title", ""))
        if cur and (size + n > MESSAGE_TEXT or len(cur) == EMBEDS):
            out.append(cur)
            cur, size = [], 0
        cur.append(e)
        size += n
    out.append(cur)
    return out


def notes_of(tag):
    path = os.path.join(ROOT, "docs", "releases", f"{tag}.md")
    if not os.path.exists(path):
        sys.exit(f"discord-release: no release notes at docs/releases/{tag}.md")
    with open(path) as f:
        return f.read()


def check():
    d = os.path.join(ROOT, "docs", "releases")
    names = sorted(n for n in os.listdir(d) if re.fullmatch(r"v\d.*\.md", n))
    bad = 0
    for n in names:
        tag = n[:-3]
        try:
            msgs = messages(tag, notes_of(tag))
        except ValueError as e:
            print(f"FAIL {e}")
            bad += 1
            continue
        for m in msgs:
            size = sum(len(e["description"]) + len(e.get("title", "")) for e in m)
            if size > MESSAGE_TEXT or len(m) > EMBEDS or any(len(e["description"]) > EMBED_TEXT for e in m):
                print(f"FAIL {tag}: a message over Discord's limits ({size} characters, {len(m)} embeds)")
                bad += 1
        text = "\n".join(e["description"] for m in msgs for e in m)
        if "**What's" not in text or "Install" in re.findall(r"\*\*([^*]+)\*\*", text):
            print(f"FAIL {tag}: the post must have What's new and leave Install on GitHub")
            bad += 1
        if release_url(tag) not in text:
            print(f"FAIL {tag}: the post must end with the release's link")
            bad += 1
    # pieces() on a small example: headings bold, Install and Thanks left out
    sample = "Intro line.\n\n## Install\n\n1. Step\n\n## What's new\n\n- **A:** one\n- **B:** two\n  more\n\n## Thanks\n\nPeople."
    want = ["Intro line.", "**What's new**", "- **A:** one", "- **B:** two\n  more"]
    if pieces(sample) != want:
        print(f"FAIL pieces(): {pieces(sample)!r}, wanted {want!r}")
        bad += 1
    # a long section is cut between items, never inside one
    long = "Intro.\n\n## What's new\n\n" + "\n".join(f"- item {i} " + "x" * 300 for i in range(40))
    msgs = messages("v9.9.9", long)
    descs = [e["description"] for m in msgs for e in m]
    if any(len(t) > EMBED_TEXT for t in descs) or sum(t.count("- item ") for t in descs) != 40 or len(descs) < 3:
        print(f"FAIL the cut of a long section: {[len(t) for t in descs]}")
        bad += 1
    if bad:
        sys.exit(f"discord-release: {bad} check(s) failed")
    print(f"discord-release: the notes of {len(names)} releases fit in Discord")


def gh_release(tag):
    r = subprocess.run(["gh", "release", "view", tag, "--repo", REPO, "--json", "isDraft,url"],
                       capture_output=True, text=True)
    return json.loads(r.stdout) if r.returncode == 0 else None


class Discord:
    def __init__(self):
        with open(TOKEN_FILE) as f:
            self.token = f.read().strip()

    def call(self, method, path, body=None):
        for attempt in range(5):
            req = urllib.request.Request(API + path, method=method,
                                         data=json.dumps(body).encode() if body is not None else None,
                                         headers={"Authorization": "Bot " + self.token,
                                                  "Content-Type": "application/json",
                                                  "User-Agent": "DiscordBot (https://github.com/tarikbc/ZyperOS, 1)"})
            try:
                with urllib.request.urlopen(req) as r:
                    data = r.read()
                    return json.loads(data) if data else None
            except urllib.error.HTTPError as e:
                if e.code == 429 and attempt < 4:
                    time.sleep(float(json.loads(e.read() or b"{}").get("retry_after", 2)) + 0.5)
                    continue
                raise SystemExit(f"discord-release: Discord said {e.code} to {method} {path}: {e.read()[:300]!r}")


def post(tag, msgs):
    d = Discord()
    me = d.call("GET", "/users/@me")["id"]
    guilds = d.call("GET", "/users/@me/guilds")
    if len(guilds) != 1:
        sys.exit(f"discord-release: Zyper is in {len(guilds)} servers, not one")
    chans = [c for c in d.call("GET", f"/guilds/{guilds[0]['id']}/channels") if c["name"] == "releases"]
    if not chans:
        sys.exit("discord-release: no #releases channel")
    ch = chans[0]
    title = msgs[0][0]["title"]
    for m in d.call("GET", f"/channels/{ch['id']}/messages?limit=50"):
        if m["author"]["id"] == me and any(e.get("title") == title for e in m.get("embeds", [])):
            print(f"discord-release: {title} is in #releases already; nothing posted")
            return
    for embeds in msgs:
        m = d.call("POST", f"/channels/{ch['id']}/messages", {"embeds": embeds, "allowed_mentions": {"parse": []}})
        if ch["type"] == NEWS:
            d.call("POST", f"/channels/{ch['id']}/messages/{m['id']}/crosspost")
    print(f"discord-release: {title}'s notes are in #releases ({len(msgs)} message(s))")


def main(argv):
    if argv == ["--check"]:
        return check()
    wait = show = False
    args = []
    for a in argv:
        if a == "--wait":
            wait = True
        elif a == "--show":
            show = True
        elif a.startswith("-"):
            sys.exit(__doc__)
        else:
            args.append(a)
    if len(args) != 1 or not re.fullmatch(r"v\d+\.\d+\.\d+(-[\w.]+)?", args[0]):
        sys.exit(__doc__)
    tag = args[0]
    msgs = messages(tag, notes_of(tag))
    if show:
        print(json.dumps(msgs, indent=2, ensure_ascii=False))
        for i, m in enumerate(msgs):
            print(f"message {i + 1}: {len(m)} embed(s), {sum(len(e['description']) for e in m)} characters", file=sys.stderr)
        return
    deadline = time.time() + 3600
    while True:
        rel = gh_release(tag)
        if rel and not rel["isDraft"]:
            break
        if not wait:
            sys.exit(f"discord-release: {tag} is not published on GitHub yet ({'a draft' if rel else 'no release'}); --wait waits for it")
        if time.time() > deadline:
            sys.exit(f"discord-release: {tag} was not published within an hour; run tools/discord-release.py {tag} once it is")
        time.sleep(30)
    post(tag, msgs)


if __name__ == "__main__":
    main(sys.argv[1:])

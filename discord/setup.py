#!/usr/bin/env python3
"""ZyperOS Discord server setup, safe to run again.

Reads the bot token from ~/.config/zyperos/discord-bot.token (never printed),
finds the bot's one server, and makes it match the PLAN below: server icon and
banner, Community mode, roles, categories, channels (text, announcement,
forums with tags, voice), read-only permissions, the welcome and rules posts,
onboarding questions that hand out roles, the welcome screen, AutoMod rules,
the bot's own name and pictures, and one permanent invite. Anything already
there is reused or updated, never duplicated.

    python3 setup.py            apply
    python3 setup.py --check    say what it would change, change nothing
"""
import base64, json, os, sys, time, urllib.error, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
API = "https://discord.com/api/v10"
TOKEN = open(os.path.expanduser("~/.config/zyperos/discord-bot.token")).read().strip()
CHECK = "--check" in sys.argv

# Channel types and permission bits (Discord API v10)
TEXT, VOICE, CATEGORY, NEWS, FORUM = 0, 2, 4, 5, 15
VIEW, SEND, SEND_THREADS, CREATE_PUBLIC_THREADS, ADD_REACTIONS = 1 << 10, 1 << 11, 1 << 38, 1 << 35, 1 << 6

SITE, REPO = "https://zyperos.com", "https://github.com/tarikbc/ZyperOS"

ROLES = [  # name, color
    ("Developer", 0xBEF23C),
    ("Tester", 0x5BC0EB),
    ("Translator", 0xF4A261),
    ("Pixel 2 owner", 0x969998),
]

# category -> [(name, type, topic, options)]; options: ro (read-only), tags (forum), staff (hidden)
PLAN = [
    ("Start here", [
        ("welcome", TEXT, "Start here: what ZyperOS is and where everything lives", {"ro": True}),
        ("rules", TEXT, "How we keep this a friendly place", {"ro": True}),
        ("announcements", NEWS, "News about ZyperOS", {"ro": True}),
        ("releases", NEWS, "Every new ZyperOS release, with its notes and downloads", {"ro": True}),
    ]),
    ("Community", [
        ("general", TEXT, "Talk about ZyperOS and the GKD Pixel 2", {}),
        ("what-are-you-playing", TEXT, "What's on your Pixel 2 this week?", {}),
        ("show-your-setup", TEXT, "Screenshots, shelves, themes and your Pixel 2 setups", {}),
        ("music-and-media", TEXT, "MP4, menu music, audiobooks and video clips on the Pixel 2", {}),
        ("off-topic", TEXT, "Anything else, kindly", {}),
    ]),
    ("Help", [
        ("help", FORUM, "One post per question. Say your ZyperOS version (MENU > About) and what you tried.",
         {"tags": ["Install", "Games and BIOS", "Wi-Fi and transfer", "PortMaster", "Update", "Solved"]}),
        ("bug-reports", FORUM, "One post per bug: your ZyperOS version, the system or game, the steps, and what happened.",
         {"tags": ["Launcher", "Emulator", "Port", "File Transfer", "Update", "Fixed"]}),
        ("ideas", FORUM, "Ideas and requests for ZyperOS. One idea per post, and react to the ones you want.",
         {"tags": ["Idea", "Planned", "Done"]}),
    ]),
    ("Development", [
        ("dev-general", TEXT, "Building ZyperOS: code, the simulator, plastron and Diatom. " + REPO, {}),
        ("translations", TEXT, "ZyperOS in eight languages, and more to come", {}),
        ("github-feed", TEXT, "Pull requests, issues and commits from GitHub", {"ro": True}),
    ]),
    ("Hangout", [
        ("Hangout", VOICE, None, {}),
    ]),
    ("Staff", [
        ("mod-log", TEXT, "AutoMod alerts and moderator notes", {"staff": True}),
        ("discord-updates", TEXT, "Discord's own notes for Community servers", {"staff": True}),
    ]),
]

WELCOME = f"""**Welcome to the ZyperOS community!**

ZyperOS is a free and open source firmware for the GKD Pixel 2. It's on in about 2 seconds, puts you back in your game after you turn it off, and keeps every system working the same way. It's named after my dog Zyper, who you'll meet on the first start.

**Get it:** {SITE}
**Code and releases:** {REPO}

**Where things are**
• <#{{help}}> for questions, one post each
• <#{{bug}}> for bugs: say your version (MENU > About), the system or game, and the steps
• <#{{ideas}}> for ideas and requests
• <#{{playing}}> and <#{{setup}}> to share what you play and how your Pixel 2 looks
• <#{{dev}}> and <#{{tr}}> if you'd like to help build or translate it

Pick your roles in **Channels & Roles** at the top of the channel list. Have fun, and thanks for being here.
"""

RULES = f"""**Rules**

1. **Be kind.** Everyone here was new once. No harassment, slurs or personal attacks.
2. **No piracy.** ZyperOS ships no games and no BIOS. Don't post or ask for links to ROMs, BIOS files or anything you don't own.
3. **Keep help in <#{{help}}> and bugs in <#{{bug}}>**, one post each, so answers stay findable. Search first, someone may have asked already.
4. **Say your ZyperOS version** (MENU > About) when you ask for help or report a bug.
5. **No spam or self-promotion.** Sharing your own ZyperOS mods, themes or ports is welcome.
6. **English is the common language,** and every language is welcome. ZyperOS speaks eight of them.
7. **Keep it safe for work.**

Breaking these can get your messages removed or you removed from the server. Questions about the rules go to the moderators.
"""

SERVER_DESCRIPTION = "The community for ZyperOS, the free and open source firmware for the GKD Pixel 2. Help, bug reports, ideas and development."


def api(method, path, body=None, reason="ZyperOS setup"):
    data = json.dumps(body).encode() if body is not None else None
    for _ in range(6):
        req = urllib.request.Request(API + path, data=data, method=method, headers={
            "Authorization": "Bot " + TOKEN, "Content-Type": "application/json",
            "User-Agent": "DiscordBot (https://zyperos.com, 1.0)", "X-Audit-Log-Reason": reason})
        try:
            with urllib.request.urlopen(req, timeout=30) as r:
                raw = r.read()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            msg = e.read().decode(errors="replace")
            if e.code == 429:
                time.sleep(json.loads(msg).get("retry_after", 2) + 0.2)
                continue
            raise SystemExit(f"{method} {path}: HTTP {e.code} {msg[:500]}")
    raise SystemExit(f"{method} {path}: rate limited too long")


def change(what, method, path, body=None):
    print(("would " if CHECK else "") + what)
    if CHECK:
        return {"id": "0"}
    return api(method, path, body)


def image(name):
    with open(os.path.join(HERE, name), "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def main():
    guilds = api("GET", "/users/@me/guilds")
    if len(guilds) != 1:
        raise SystemExit(f"the bot is in {len(guilds)} servers, expected 1")
    gid = guilds[0]["id"]
    guild = api("GET", f"/guilds/{gid}")
    me = api("GET", "/users/@me")

    # The bot's own profile
    if me["username"] != "Zyper" or not me.get("avatar"):
        change("set the bot's name to Zyper, its icon and banner", "PATCH", "/users/@me",
               {"username": "Zyper", "avatar": image("zyperos-bot-icon.png"), "banner": image("zyperos-bot-banner.png")})

    # Roles
    roles = {r["name"]: r for r in api("GET", f"/guilds/{gid}/roles")}
    for name, color in ROLES:
        if name not in roles:
            roles[name] = change(f"create role {name}", "POST", f"/guilds/{gid}/roles",
                                 {"name": name, "color": color, "mentionable": True})
        elif roles[name]["color"] != color:
            change(f"recolor role {name}", "PATCH", f"/guilds/{gid}/roles/{roles[name]['id']}", {"color": color})
    everyone = gid  # @everyone's role id is the guild id

    # Categories and channels: forums need Community, which needs #rules
    # first, so a server without it gets two passes
    ids = sync_channels(gid, guild, everyone)
    if "COMMUNITY" not in guild["features"]:
        enable_community(gid, guild, ids)
        guild = api("GET", f"/guilds/{gid}") if not CHECK else dict(guild, features=["COMMUNITY"])
        ids = sync_channels(gid, guild, everyone)
    else:
        enable_community(gid, guild, ids)
    finish(gid, guild, ids, roles, me, everyone)


def enable_community(gid, guild, ids):
    want = {"description": SERVER_DESCRIPTION, "verification_level": 1, "explicit_content_filter": 2,
            "default_message_notifications": 1, "rules_channel_id": ids["rules"],
            "public_updates_channel_id": ids["discord-updates"], "system_channel_id": ids["general"],
            "system_channel_flags": 1 << 2 | 1 << 3}  # no boost or setup tips
    if not guild.get("icon"):
        want["icon"] = image("zyperos-bot-icon.png")
    if "COMMUNITY" not in guild["features"]:
        want["features"] = guild["features"] + ["COMMUNITY"]
    change("set the server's icon, description and Community mode", "PATCH", f"/guilds/{gid}", want)


def sync_channels(gid, guild, everyone):
    chans = api("GET", f"/guilds/{gid}/channels")
    community = "COMMUNITY" in guild["features"]
    ids = {}
    for pos, (cat, items) in enumerate(PLAN):
        staff = any(o.get("staff") for _, _, _, o in items)
        cat_over = [{"id": everyone, "type": 0, "deny": str(VIEW), "allow": "0"}] if staff else []
        c = next((x for x in chans if x["type"] == CATEGORY and x["name"].lower() == cat.lower()), None)
        if not c:
            c = change(f"create category {cat}", "POST", f"/guilds/{gid}/channels",
                       {"name": cat, "type": CATEGORY, "position": pos, "permission_overwrites": cat_over})
        cid = c["id"]
        for name, typ, topic, opt in items:
            over = list(cat_over)
            if opt.get("ro"):
                over.append({"id": everyone, "type": 0, "allow": "0",
                             "deny": str(SEND | SEND_THREADS | CREATE_PUBLIC_THREADS)})
            body = {"name": name, "type": typ, "parent_id": cid, "permission_overwrites": over}
            if topic:
                body["topic"] = topic
            if typ == FORUM:
                body["available_tags"] = [{"name": t, "moderated": t in ("Solved", "Fixed", "Planned", "Done")}
                                          for t in opt["tags"]]
                body["default_reaction_emoji"] = {"emoji_name": "👍"}
            # Reuse a channel of the same name and type anywhere (Discord's default #general included)
            x = next((x for x in chans if x["name"].lower() == name.lower() and
                      (x["type"] == typ or (typ == NEWS and x["type"] == TEXT))), None)
            if x is None and typ == VOICE:
                x = next((x for x in chans if x["type"] == VOICE), None)
            if x is None and typ == FORUM and not community:
                continue  # after Community
            if x is None:
                if typ == NEWS and not community:
                    body["type"] = TEXT  # announcement channels need Community; made one below
                x = change(f"create #{name}", "POST", f"/guilds/{gid}/channels", body)
            else:
                upd = {k: v for k, v in body.items() if k != "type"}
                if typ == FORUM:
                    have = {t["name"] for t in x.get("available_tags", [])}
                    upd["available_tags"] = x.get("available_tags", []) + [
                        t for t in body["available_tags"] if t["name"] not in have]
                change(f"update #{name}", "PATCH", f"/channels/{x['id']}", upd)
            ids[name] = x["id"]
    if not community:
        return ids

    # Discord's empty default categories go
    for x in chans:
        if x["type"] == CATEGORY and x["name"] in ("Text Channels", "Voice Channels") and \
                not any(y.get("parent_id") == x["id"] for y in api("GET", f"/guilds/{gid}/channels")):
            change(f"delete the empty default category {x['name']}", "DELETE", f"/channels/{x['id']}")

    # Announcement channels, now that Community is on
    if not CHECK:
        for x in api("GET", f"/guilds/{gid}/channels"):
            if x["name"] in ("announcements", "releases") and x["type"] == TEXT:
                change(f"make #{x['name']} an announcement channel", "PATCH", f"/channels/{x['id']}", {"type": NEWS})
    return ids


def finish(gid, guild, ids, roles, me, everyone):

    # The welcome and rules posts, edited in place when they exist
    links = {"help": ids["help"], "bug": ids["bug-reports"], "ideas": ids["ideas"],
             "playing": ids["what-are-you-playing"], "setup": ids["show-your-setup"],
             "dev": ids["dev-general"], "tr": ids["translations"]}
    me = api("GET", "/users/@me") if not CHECK else me
    for chan, text in (("welcome", WELCOME), ("rules", RULES)):
        body = {"content": text.format(**links), "allowed_mentions": {"parse": []}}
        mine = [m for m in (api("GET", f"/channels/{ids[chan]}/messages?limit=20") if not CHECK else [])
                if m["author"]["id"] == me["id"]]
        if mine:
            change(f"edit the #{chan} post", "PATCH", f"/channels/{ids[chan]}/messages/{mine[-1]['id']}", body)
        else:
            change(f"post in #{chan}", "POST", f"/channels/{ids[chan]}/messages", body)

    # Onboarding: questions that hand out roles
    def opt(title, desc, emoji, role, chans_=()):
        return {"title": title, "description": desc, "emoji": {"name": emoji},
                "role_ids": [roles[role]["id"]], "channel_ids": [ids[c] for c in chans_]}
    onboarding = {
        "enabled": True, "mode": 0,
        "default_channel_ids": [ids[c] for c in ("welcome", "rules", "announcements", "releases", "general",
                                                  "what-are-you-playing", "show-your-setup", "music-and-media",
                                                  "off-topic", "help", "bug-reports", "ideas")],
        "prompts": [{
            "type": 0, "title": "What brings you to ZyperOS?", "single_select": False,
            "required": False, "in_onboarding": True,
            "options": [
                opt("I play on a GKD Pixel 2", "Show me around", "🎮", "Pixel 2 owner"),
                opt("I'd like to test new builds", "Try things early and report what breaks", "🧪", "Tester"),
                opt("I write code", "Help build ZyperOS", "🛠️", "Developer", ("dev-general", "github-feed")),
                opt("I can translate", "Help ZyperOS speak your language", "🌍", "Translator", ("translations",)),
            ]}]}
    if not CHECK:
        # Discord wants an id on each question and answer: the ones it has, or
        # new snowflake-shaped ones (time << 22, as Discord's own are)
        cur = api("GET", f"/guilds/{gid}/onboarding")
        old = {p_["title"]: p_ for p_ in cur.get("prompts", [])}
        base = (int(time.time() * 1000) - 1420070400000) << 22
        for i, pr in enumerate(onboarding["prompts"]):
            have = old.get(pr["title"])
            pr["id"] = have["id"] if have else str(base + i * 100)
            ho = {o["title"]: o for o in (have or {}).get("options", [])}
            for k, o in enumerate(pr["options"]):
                o["id"] = ho[o["title"]]["id"] if o["title"] in ho else str(base + i * 100 + k + 1)
        change("set up onboarding", "PUT", f"/guilds/{gid}/onboarding", onboarding)
        change("set up the welcome screen", "PATCH", f"/guilds/{gid}/welcome-screen", {
            "enabled": True, "description": "The community for ZyperOS, the firmware for the GKD Pixel 2.",
            "welcome_channels": [
                {"channel_id": ids["welcome"], "description": "Start here", "emoji_name": "👋"},
                {"channel_id": ids["help"], "description": "Ask a question", "emoji_name": "🙋"},
                {"channel_id": ids["bug-reports"], "description": "Report a bug", "emoji_name": "🐞"},
                {"channel_id": ids["show-your-setup"], "description": "Show your setup", "emoji_name": "📸"},
                {"channel_id": ids["releases"], "description": "New releases", "emoji_name": "🚀"},
            ]})

    # AutoMod
    rules_now = {r["name"]: r for r in (api("GET", f"/guilds/{gid}/auto-moderation/rules") if not CHECK else [])}
    alert = {"type": 2, "metadata": {"channel_id": ids["mod-log"]}}
    automod = [
        {"name": "Spam", "event_type": 1, "trigger_type": 3, "actions": [{"type": 1}, alert]},
        {"name": "Mention spam", "event_type": 1, "trigger_type": 5,
         "trigger_metadata": {"mention_total_limit": 5, "mention_raid_protection_enabled": True},
         "actions": [{"type": 1}, alert]},
        {"name": "Slurs", "event_type": 1, "trigger_type": 4, "trigger_metadata": {"presets": [3]},
         "actions": [{"type": 1}, alert]},
        {"name": "Other servers' invites", "event_type": 1, "trigger_type": 1,
         "trigger_metadata": {"regex_patterns": [r"(discord\.gg|discord(app)?\.com/invite)/[a-z0-9-]+"]},
         "actions": [{"type": 1}, alert], "exempt_roles": [roles["Developer"]["id"]]},
    ]
    for r in automod:
        r["enabled"] = True
        if r["name"] in rules_now:
            change(f"update AutoMod rule {r['name']}", "PATCH",
                   f"/guilds/{gid}/auto-moderation/rules/{rules_now[r['name']]['id']}",
                   {k: v for k, v in r.items() if k not in ("event_type", "trigger_type")})
        else:
            change(f"add AutoMod rule {r['name']}", "POST", f"/guilds/{gid}/auto-moderation/rules", r)

    # One permanent invite, reused
    if not CHECK:
        inv = [i for i in api("GET", f"/guilds/{gid}/invites")
               if i.get("max_age") == 0 and i.get("channel", {}).get("id") == ids["welcome"]]
        if not inv:
            inv = [change("create the permanent invite", "POST", f"/channels/{ids['welcome']}/invites",
                          {"max_age": 0, "max_uses": 0, "unique": False})]
        print("invite: https://discord.gg/" + inv[0]["code"])


if __name__ == "__main__":
    main()

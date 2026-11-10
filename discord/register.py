#!/usr/bin/env python3
"""Zyper's slash commands, registered on the ZyperOS server, and Discord pointed
at the Worker (worker/). Safe to run again: the command list is replaced as a
whole, so a command removed here goes from Discord too.

    python3 register.py https://zyper-bot.<account>.workers.dev

Reads the bot token from ~/.config/zyperos/discord-bot.token (never printed).
"""
import json, os, sys, urllib.error, urllib.request

APP, GUILD = "1569500359448338452", "1569495086528729141"
TOKEN = open(os.path.expanduser("~/.config/zyperos/discord-bot.token")).read().strip()

COMMANDS = [
    ("install", "How to install ZyperOS on a GKD Pixel 2"),
    ("bios", "Which BIOS files each system needs, and where they go"),
    ("wifi", "Wi-Fi on the Pixel 2: which USB adapter works"),
    ("transfer", "How to add games, music and videos"),
    ("portmaster", "How to install PortMaster and ports"),
    ("update", "How to update ZyperOS"),
    ("controls", "The buttons worth knowing"),
    ("links", "ZyperOS's website, download, code and guide"),
    ("version", "The newest ZyperOS release"),
    ("bug", "Report a bug, with a short form"),
]


def api(method, path, body):
    req = urllib.request.Request("https://discord.com/api/v10" + path, data=json.dumps(body).encode(),
                                 method=method, headers={"Authorization": "Bot " + TOKEN,
                                                         "Content-Type": "application/json",
                                                         "User-Agent": "DiscordBot (https://zyperos.com, 1.0)"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{method} {path}: HTTP {e.code} {e.read().decode(errors='replace')[:400]}")


def main():
    cmds = api("PUT", f"/applications/{APP}/guilds/{GUILD}/commands",
               [{"name": n, "description": d, "type": 1} for n, d in COMMANDS])
    print("commands:", ", ".join("/" + c["name"] for c in cmds))
    if len(sys.argv) > 1:
        # Discord sends the Worker a signed PING first and keeps the URL only if it answers
        api("PATCH", "/applications/@me", {"interactions_endpoint_url": sys.argv[1]})
        print("interactions endpoint:", sys.argv[1])


if __name__ == "__main__":
    main()

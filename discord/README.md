# The ZyperOS Discord

Everything that builds and runs [the ZyperOS Discord](https://discord.gg/Ust8sE22Tm), so it can be changed in code and run again, never clicked together by hand.

| | |
|---|---|
| `setup.py` | The server itself: icon and description, Community mode, roles, categories and channels (the help, bug-reports and ideas forums with their tags), the welcome and rules posts, onboarding, the welcome screen, AutoMod, the bot's name and pictures, and one permanent invite. Safe to run again: what exists is reused or updated. `--check` says what it would change. |
| `register.py` | Zyper's slash commands on the server, and Discord pointed at the Worker. |
| `worker/` | Zyper, a Cloudflare Worker. Discord sends each slash command to it, signed; it answers, posts `/bug` reports in the bug-reports forum, and opens the week's "What are you playing?" thread every Monday. |

## Zyper's commands

`/install`, `/bios`, `/wifi`, `/transfer`, `/portmaster`, `/update`, `/controls` and `/links` answer the common questions, each with a link to the README section that says it in full (`worker/src/answers.js`: keep them in step with the README). `/version` gives the newest release from GitHub. `/bug` opens a short form; the report goes up as a post in the bug-reports forum, tagged, with a link that opens it as a GitHub issue too.

## Running it

The scripts read the bot's token from `~/.config/zyperos/discord-bot.token` (mode 0600) and never print it; it never goes in this repository.

```sh
python3 discord/setup.py --check     # what would change
python3 discord/setup.py             # make the server match
cd discord/worker
npx wrangler deploy                  # Zyper itself
npx wrangler secret put DISCORD_TOKEN < ~/.config/zyperos/discord-bot.token
cd ..
python3 register.py https://zyper-bot.tarikcaramanico.workers.dev
```

Each new release goes into #releases with its notes: `tools/release.sh --publish` ends by waiting for GitHub to publish the release, then `tools/discord-release.py` posts the notes as Zyper (the first paragraph, What's new, Updating and Good to know; Install and Thanks stay on GitHub behind the link) and publishes the post to the servers that follow the channel. `tools/discord-release.py v<version>` posts them by hand, once; `--show` prints the post. GitHub's own webhook is gone: it posted only the title and a link. Pull requests and issues are left out on purpose, to keep the server quiet.

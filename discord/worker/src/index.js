// Zyper, the ZyperOS Discord bot, as a Cloudflare Worker.
//
// Discord sends every slash command here as an HTTP POST (the application's
// Interactions Endpoint URL), signed with the application's Ed25519 key; the
// Worker checks the signature and answers. No gateway connection and no
// server: the Worker runs only when someone uses a command, and once a week
// on its cron to open the "What are you playing?" thread.
//
// Secrets (wrangler secret put): DISCORD_TOKEN, the bot's token, used to post
// bug reports and the weekly thread. Vars (wrangler.toml): PUBLIC_KEY,
// GUILD_ID, the channel ids.

import { ANSWERS, LINKS } from "./answers.js";

const API = "https://discord.com/api/v10";
const REPO = "tarikbc/ZyperOS";
const LIME = 0xbef23c;

// Interaction and response types
const PING = 1, COMMAND = 2, MODAL_SUBMIT = 5;
const PONG = 1, MESSAGE = 4, MODAL = 9;
const EPHEMERAL = 1 << 6;

export default {
  async fetch(request, env) {
    if (request.method !== "POST") return new Response("Zyper is here.", { status: 200 });
    const body = await request.text();
    if (!(await verified(request, body, env.PUBLIC_KEY))) return new Response("bad signature", { status: 401 });
    const it = JSON.parse(body);
    if (it.type === PING) return json({ type: PONG });
    if (it.type === COMMAND) return command(it, env);
    if (it.type === MODAL_SUBMIT && it.data.custom_id === "bug") return bugReport(it, env);
    return json({ type: MESSAGE, data: { content: "I don't know that one yet.", flags: EPHEMERAL } });
  },

  // Mondays: a fresh "What are you playing?" thread
  async scheduled(event, env) {
    const week = new Date().toISOString().slice(0, 10);
    const msg = await discord(env, "POST", `/channels/${env.PLAYING_CHANNEL}/messages`, {
      content: "What are you playing on your Pixel 2 this week? Share the game, the system, and a screenshot if you have one.",
    });
    await discord(env, "POST", `/channels/${env.PLAYING_CHANNEL}/messages/${msg.id}/threads`, {
      name: `What are you playing? Week of ${week}`, auto_archive_duration: 10080,
    });
  },
};

async function command(it, env) {
  const name = it.data.name;
  if (ANSWERS[name]) return embed(ANSWERS[name]);
  if (name === "links") return embed(LINKS);
  if (name === "version") return version();
  if (name === "bug") return json({ type: MODAL, data: bugModal() });
  return json({ type: MESSAGE, data: { content: "I don't know that one yet.", flags: EPHEMERAL } });
}

function embed(a) {
  return json({
    type: MESSAGE,
    data: {
      embeds: [{ title: a.title, description: a.text, color: LIME, url: a.url,
                 footer: { text: "Zyper · zyperos.com" } }],
      allowed_mentions: { parse: [] },
    },
  });
}

// The newest release, from GitHub, cached for ten minutes
async function version() {
  const cache = caches.default, key = new Request("https://zyper.cache/latest");
  let rel = await cache.match(key).then((r) => r && r.json());
  if (!rel) {
    const r = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`,
                          { headers: { "User-Agent": "zyper-bot", Accept: "application/vnd.github+json" } });
    if (!r.ok) return embed({ title: "Latest release", text: "GitHub didn't answer just now.", url: LINKS.releases });
    const j = await r.json();
    rel = { tag: j.tag_name, name: j.name, url: j.html_url, at: j.published_at };
    await cache.put(key, new Response(JSON.stringify(rel), { headers: { "Cache-Control": "max-age=600" } }));
  }
  const day = Math.floor(Date.parse(rel.at) / 1000);
  return embed({
    title: rel.name || `ZyperOS ${rel.tag}`, url: rel.url,
    text: `The newest ZyperOS is **${rel.tag}**, out <t:${day}:R>.\n\n` +
          `On the Pixel 2: **MENU > System Update** gets it over Wi-Fi, or **Update ZyperOS** on the File Transfer page over the USB cable.\n` +
          `New card: ZyperOS Installer, from ${rel.url}`,
  });
}

function bugModal() {
  const field = (id, label, style, placeholder, required = true, max = 1000) =>
    ({ type: 1, components: [{ type: 4, custom_id: id, label, style, placeholder, required, max_length: max }] });
  return {
    custom_id: "bug", title: "Report a bug",
    components: [
      field("title", "In a few words, what went wrong?", 1, "Mario Kart DS video restarts the launcher", true, 100),
      field("version", "ZyperOS version (MENU > About)", 1, "1.0.2", true, 20),
      field("where", "Which system, game or screen?", 1, "Nintendo DS, the shelf, File Transfer...", true, 200),
      field("steps", "What did you do, step by step?", 2, "1. Open ...\n2. Press ...", true),
      field("what", "What happened, and what did you expect?", 2, "It went back to the home. I expected the video to play.", true),
    ],
  };
}

async function bugReport(it, env) {
  const v = {};
  for (const row of it.data.components) for (const c of row.components) v[c.custom_id] = c.value.trim();
  const who = it.member?.user || it.user;
  const body =
    `**Version:** ${v.version}\n**Where:** ${v.where}\n\n**Steps**\n${v.steps}\n\n**What happened**\n${v.what}`;
  const issue = new URL(`https://github.com/${REPO}/issues/new`);
  issue.searchParams.set("title", v.title);
  issue.searchParams.set("body", body.replaceAll("**", "") + "\n\n(From the ZyperOS Discord)");
  const forum = await discord(env, "GET", `/channels/${env.BUGS_CHANNEL}`);
  const tag = await pickTag(forum, v.where);
  const thread = await discord(env, "POST", `/channels/${env.BUGS_CHANNEL}/threads`, {
    name: v.title.slice(0, 100),
    applied_tags: tag ? [tag] : [],
    message: {
      content: `Reported by <@${who.id}>\n\n${body}\n\nTo follow it on GitHub too: <${issue.toString().slice(0, 1800)}>`,
      allowed_mentions: { users: [who.id] },
    },
  });
  return json({
    type: MESSAGE,
    data: { content: `Thanks! Your report is up: <#${thread.id}>. I'll look at it there.`, flags: EPHEMERAL },
  });
}

// The forum's tag that best matches where the bug is
async function pickTag(forum, where) {
  const w = where.toLowerCase(), tags = forum.available_tags || [];
  const find = (n) => tags.find((t) => t.name === n)?.id;
  if (/transfer|wi-?fi|usb|browser|hotspot/.test(w)) return find("File Transfer");
  if (/update|install/.test(w)) return find("Update");
  if (/port|portmaster|openbor/.test(w)) return find("Port");
  if (/shelf|menu|home|mp4|music|video|storage|launcher|cover|setting/.test(w)) return find("Launcher");
  return find("Emulator");
}

async function discord(env, method, path, body) {
  const r = await fetch(API + path, {
    method, body: body ? JSON.stringify(body) : undefined,
    headers: { Authorization: `Bot ${env.DISCORD_TOKEN}`, "Content-Type": "application/json",
               "User-Agent": "DiscordBot (https://zyperos.com, 1.0)" },
  });
  if (!r.ok) throw new Error(`${method} ${path}: ${r.status} ${await r.text()}`);
  return r.status === 204 ? null : r.json();
}

async function verified(request, body, publicKey) {
  const sig = request.headers.get("X-Signature-Ed25519"), ts = request.headers.get("X-Signature-Timestamp");
  if (!sig || !ts) return false;
  const hex = (s) => new Uint8Array(s.match(/../g).map((b) => parseInt(b, 16)));
  try {
    const key = await crypto.subtle.importKey("raw", hex(publicKey), { name: "Ed25519" }, false, ["verify"]);
    return await crypto.subtle.verify("Ed25519", key, hex(sig), new TextEncoder().encode(ts + body));
  } catch {
    return false;
  }
}

const json = (o) => new Response(JSON.stringify(o), { headers: { "Content-Type": "application/json" } });

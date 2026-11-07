#!/usr/bin/env python3
"""A page for reviewing one language's strings against English.

    python3 tools/i18n-review.py [--lang pt-BR] OUT.html

Every string of res/strings, by file, English beside the language's, the
`note =` that says where it shows, and the strings marked `review:` first.
Published as an Artifact with the db capability, a reviewer types a better
word beside any string; each is kept in its collection `fixes`, one document
an ID ({text, note, at}), which the session reads back and puts in
res/strings. Without the store the fixes stay in the browser. Standard
library only.
"""
import html
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402

PAGE = r"""<title>ZyperOS __NAME__ Review</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Lexend:wght@400;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
  /* The review desk: the strings as a long table, the language's column the
     one that is worked on */
  :root { --bg: #f3f2ec; --panel: #fbfaf6; --ink: #1b1d1a; --ink-2: #55584f; --ink-3: #85887e; --line: #dddbd0;
          --hi: #476b00; --hi-bg: #e9f5c9; --warn: #8a5a00; --warn-bg: #fbefd4;
          --f-body: "Lexend", "Avenir Next", system-ui, sans-serif; --f-mono: "IBM Plex Mono", ui-monospace, Menlo, monospace; }
  @media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #141615; --panel: #1b1e1c; --ink: #e8e6dd; --ink-2: #b9bab2; --ink-3: #8a8d88; --line: #2c302e; --hi: #bef23c; --hi-bg: #26301a; --warn: #f2c66b; --warn-bg: #33291a; color-scheme: dark; } }
  :root[data-theme="dark"] { --bg: #141615; --panel: #1b1e1c; --ink: #e8e6dd; --ink-2: #b9bab2; --ink-3: #8a8d88; --line: #2c302e; --hi: #bef23c; --hi-bg: #26301a; --warn: #f2c66b; --warn-bg: #33291a; color-scheme: dark; }
  body { background: var(--bg); color: var(--ink); font: 15px/1.5 var(--f-body); margin: 0; }
  .wrap { max-width: 1180px; margin: 0 auto; padding-inline: max(16px, env(safe-area-inset-left, 0px)) max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px 64px; }
  h1 { font: 600 clamp(24px, 3.4vw, 34px)/1.15 var(--f-body); margin: 0 0 6px; text-wrap: balance; }
  .lede { color: var(--ink-2); max-width: 70ch; margin: 0 0 6px; }
  .bar { position: sticky; top: env(safe-area-inset-top, 0px); z-index: 2; background: var(--bg); padding-block: 10px; border-bottom: 1px solid var(--line); display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
  .bar input[type=search] { font: 14px var(--f-body); padding: 7px 12px; border-radius: 999px; border: 1px solid var(--line); background: var(--panel); color: var(--ink); min-width: 0; flex: 1 1 220px; }
  .bar label { font: 13px var(--f-mono); color: var(--ink-2); display: flex; gap: 6px; align-items: center; }
  #status, .count { font: 12px var(--f-mono); color: var(--ink-3); }
  h2 { font: 600 17px var(--f-body); margin: 28px 0 8px; }
  h2 small { font: 12px var(--f-mono); color: var(--ink-3); font-weight: 400; margin-left: 8px; }
  .tw { overflow-x: auto; }
  table { border-collapse: collapse; width: 100%; min-width: 720px; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; }
  th, td { text-align: left; vertical-align: top; padding: 8px 10px; border-bottom: 1px solid var(--line); }
  th { font: 500 11px var(--f-mono); letter-spacing: .05em; text-transform: uppercase; color: var(--ink-3); }
  td.id { font: 11px/1.4 var(--f-mono); color: var(--ink-3); width: 17%; word-break: break-all; }
  td.en { width: 27%; color: var(--ink-2); white-space: pre-wrap; }
  td.tr { width: 27%; white-space: pre-wrap; }
  td.fix { width: 29%; }
  .note { display: block; font: 12px/1.4 var(--f-body); color: var(--ink-3); margin-top: 4px; white-space: normal; }
  tr.review td.tr { background: var(--warn-bg); }
  .note.review { color: var(--warn); }
  textarea { width: 100%; box-sizing: border-box; font: 14px/1.4 var(--f-body); color: var(--ink); background: var(--bg); border: 1px solid var(--line); border-radius: 8px; padding: 6px 8px; resize: vertical; min-height: 34px; }
  textarea:focus-visible, input:focus-visible { outline: 2px solid var(--hi); outline-offset: 1px; }
  tr.fixed td.fix textarea { border-color: var(--hi); background: var(--hi-bg); }
  .one { font: 11px var(--f-mono); color: var(--ink-3); }
</style>
<div class="wrap">
  <h1>__NAME__: every string, beside English</h1>
  <p class="lede">These are the words ZyperOS shows in __NAME__, by the screen they belong to. Rows marked in amber are ones the translator was unsure of. To change one, type the better wording in its box: it is saved for the session as you type, and the rest stay as they are. Product names (ZyperOS, MP4, PortMaster, RetroAchievements) and button names (A, START, MENU) stay as they are. %s and %d are filled in by the handheld.</p>
  <p class="lede">__STATS__</p>
  <div class="bar">
    <input type="search" id="q" placeholder="Search English or __NAME__" aria-label="Search">
    <label for="only"><input type="checkbox" id="only"> Only rows to review</label>
    <span class="count" id="count"></span>
    <span id="status">Saved in this browser</span>
  </div>
  <main id="list"></main>
</div>
<script>
const DATA = __DATA__;
(() => {
  "use strict";
  const fixes = {};
  let db = null;
  const esc = s => String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);
  const say = s => { document.getElementById("status").textContent = s; };
  try { Object.assign(fixes, JSON.parse(localStorage.getItem("zy-review-" + DATA.code) || "{}")); } catch (e) {}
  const form = (one, other) => other === undefined ? esc(one) : `<span class="one">1:</span> ${esc(one)}\n<span class="one">n:</span> ${esc(other)}`;
  function render() {
    const q = document.getElementById("q").value.trim().toLowerCase(), only = document.getElementById("only").checked;
    let shown = 0;
    document.getElementById("list").innerHTML = DATA.files.map(f => {
      const rows = f.strings.filter(s => (!only || s.review) && (!q || (s.en + " " + s.tr + " " + s.id).toLowerCase().includes(q)));
      shown += rows.length;
      if (!rows.length) return "";
      return `<h2>${esc(f.name)}<small>${rows.length}</small></h2><div class="tw"><table><thead><tr><th>ID</th><th>English</th><th>${esc(DATA.name)}</th><th>Better wording</th></tr></thead><tbody>${rows.map(s => `
        <tr class="${s.review ? "review" : ""} ${fixes[s.id] && fixes[s.id].text ? "fixed" : ""}">
          <td class="id">${esc(s.id)}</td>
          <td class="en">${form(s.en, s.en_other)}${s.note ? `<span class="note ${s.review ? "review" : ""}">${esc(s.note)}</span>` : ""}</td>
          <td class="tr">${form(s.tr, s.tr_other)}</td>
          <td class="fix"><textarea id="fix-${esc(s.id)}" data-id="${esc(s.id)}" rows="1" placeholder="Leave empty to keep it">${esc((fixes[s.id] && fixes[s.id].text) || "")}</textarea></td>
        </tr>`).join("")}</tbody></table></div>`;
    }).join("");
    document.getElementById("count").textContent = shown + " of " + DATA.total + " strings";
  }
  const timers = {};
  document.getElementById("list").addEventListener("input", e => {
    const id = e.target.dataset.id;
    if (!id) return;
    fixes[id] = { text: e.target.value, at: new Date().toISOString() };
    e.target.closest("tr").classList.toggle("fixed", !!e.target.value);
    try { localStorage.setItem("zy-review-" + DATA.code, JSON.stringify(fixes)); } catch (err) {}
    clearTimeout(timers[id]);
    timers[id] = setTimeout(() => {
      if (!db) return;
      say("Saving…");
      db.doc("fixes/" + id).set(fixes[id]).then(() => say("Saved"), err => say("Not saved: " + (err && err.code || "error")));
    }, 700);
  });
  ["q", "only"].forEach(k => document.getElementById(k).addEventListener("input", render));
  render();
  (async () => {
    if (!window.claude || !window.claude.use) return;
    db = await window.claude.use("db");
    if (!db) return;
    db.collection("fixes").onSnapshot(snap => {
      let changed = false;
      snap.docs.forEach(d => {
        const v = d.data() || {};
        if (document.activeElement && document.activeElement.dataset.id === d.id) return;
        if ((v.text || "") !== ((fixes[d.id] && fixes[d.id].text) || "")) { fixes[d.id] = { text: String(v.text || ""), at: v.at }; changed = true; }
      });
      if (!snap.metadata.fromCache) say("Saved");
      if (changed) render();
    }, err => say("Saved in this browser (" + err.code + ")"));
  })();
})();
</script>
"""


def main():
    args = sys.argv[1:]
    code = "pt-BR"
    if args[:1] == ["--lang"]:
        code, args = args[1], args[2:]
    if len(args) != 1 or code not in i18n.CODES:
        raise SystemExit(__doc__)
    errors = []
    strs = i18n.parse(errors)
    name = dict((c, n) for c, n, _, _ in i18n.LANGS)[code]
    files, total, review = {}, 0, 0
    for s in strs:
        en, tr = s.text.get("en"), s.text.get(code)
        row = {"id": s.id, "note": s.note, "review": "review" in s.note.lower()}
        if isinstance(en, tuple):
            row["en"], row["en_other"] = en
        else:
            row["en"] = en or ""
        if isinstance(tr, tuple):
            row["tr"], row["tr_other"] = tr
        else:
            row["tr"] = tr if tr is not None else "(missing)"
        files.setdefault(s.file, []).append(row)
        total += 1
        review += row["review"]
    data = {"code": code, "name": name, "total": total,
            "files": [{"name": os.path.basename(f)[:-4], "strings": sorted(v, key=lambda r: not r["review"])}
                      for f, v in sorted(files.items(), key=lambda kv: -sum(r["review"] for r in kv[1]))]}
    stats = "%d strings in %d groups; %d marked to review." % (total, len(files), review)
    page = (PAGE.replace("__NAME__", html.escape(name)).replace("__STATS__", stats)
            .replace("__DATA__", json.dumps(data, ensure_ascii=False).replace("</", "<\\/")))
    with open(args[0], "w", encoding="utf-8") as f:
        f.write(page)
    print("%s: %s" % (args[0], stats))


if __name__ == "__main__":
    main()

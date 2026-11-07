/* ZyperOS mock kit: the handheld's screens and components as HTML.
 *
 * Needs tokens.js (window.ZYPER_TOKENS, written by tools/designdef.py) and
 * kit.css, and the Zyper symbol (#zyper) in the page - tools/mockpage.py puts
 * all three in. Everything is drawn in the launcher's 1024x768 units from the
 * DESIGN.md tokens, so a mock and the handheld agree on every size, color
 * and duration.
 *
 *   ZK.screen(theme, html)        a 4:3 frame holding a 1024x768 screen
 *   ZK.shelf(theme, opts)         the games shelf: title, cards, count, rail;
 *                                 opts.games and opts.cursor draw any place on one
 *   ZK.menu(theme, opts)          a menu panel over a scrim
 *   ZK.bar(theme, kind, lvl, max) the volume or brightness bar
 *   ZK.keyboard(theme)            the on-screen keyboard
 *   ZK.message(text)              a one-line message
 *   ZK.tourStep(theme, opts)      a tutorial step; ZK.tourWelcome(theme)
 *   ZK.glyph(kind, label, x, y, lit)  a button glyph
 *   ZK.swatches(theme)            the theme's colors, labelled
 *   ZK.animate(el, name, frames)  a Web Animation on a DESIGN.md motion token
 *   ZK.sound(name, voice)         a DESIGN.md sound, synthesized
 *   ZK.decisions(mount, list, o)  a decision board that saves picks
 *   ZK.fit(root)                  scale every screen in root to its frame
 */
(function () {
  "use strict";
  const T = window.ZYPER_TOKENS;
  const SYS = { snes: "#8a6fc0", nes: "#c4443a", gbc: "#e0b23a", gb: "#7e9b47", ngpc: "#00aa4f" };

  /* Text a viewer wrote, made safe to put in markup. */
  function esc(s) {
    return String(s).replace(/[&<>"']/g, ch => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[ch]);
  }

  function vars(theme, extra) {
    const th = T.themes[theme], c = th.color, dark = theme === "dark";
    const v = Object.entries(c).map(([k, x]) => `--${k}:${x}`);
    v.push(`--scrim-menu:${Math.round(th.scrim.menu * 100)}%`, `--scrim-game:${Math.round(th.scrim.game * 100)}%`);
    v.push(`--wash-mix:${dark ? 30 : 16}%`);
    v.push(`--card-lift:${dark ? "0 0 90px color-mix(in srgb, var(--sys) 45%, transparent)" : "0 18px 48px color-mix(in srgb, var(--ink) 22%, transparent)"}`);
    v.push(`--panel-lift:${dark ? "0 0 80px color-mix(in srgb, var(--sys) 18%, transparent)" : "0 24px 64px color-mix(in srgb, var(--ink) 18%, transparent)"}`);
    v.push(`--bar-lift:0 4px 8px color-mix(in srgb, var(--ink) ${dark ? 40 : 18}%, transparent)`);
    const sys = (extra && extra.sys) || SYS.snes;
    v.push(`--sys:${sys}`, `--sys-mark:${dark ? sys : `color-mix(in srgb, ${sys} 75%, ${c.text})`}`);
    return v.join(";");
  }

  function screen(theme, html, extra) {
    return `<div class="zk-frame"><div class="zs" data-theme="${theme}" style="${vars(theme, extra)}">${html}</div></div>`;
  }

  function card(name, acc, x, s, dim, fav) {
    const heart = fav ? `<svg class="heart" viewBox="-17 -16 34 30"><path d="M0 12C-8 6-16 1-16-6a8 8 0 0 1 16-3 8 8 0 0 1 16 3c0 7-8 12-16 18z" fill="var(--favorite)"/></svg>` : "";
    return `<div class="card" data-name="${name}" style="--acc:${acc};--cover:linear-gradient(160deg, color-mix(in srgb, ${acc} 55%, #222), #121212);--art:linear-gradient(135deg, color-mix(in srgb, ${acc} 80%, #fff 10%), color-mix(in srgb, ${acc} 40%, #000));transform:translateX(${x}px) scale(${s});opacity:${dim}">${heart}</div>`;
  }

  /* With o.games ([{name, sys}]) and o.cursor, the shelf at that game: the
   * two either side wrapped round as the shelf wraps, the count and the
   * rail's marker where it is, and a heart on each game in o.favs (a Set of
   * indices). Without them, the one picture the components page shows. */
  const SLOTS = [[-2, -560, .62, .55], [-1, -300, .78, .75], [1, 300, .78, .75], [2, 560, .62, .55], [0, 0, 1, 1]];
  function shelf(theme, o = {}) {
    if (o.games) {
      const g = o.games, n = g.length, at = ((o.cursor || 0) % n + n) % n, favs = o.favs || new Set();
      const cards = SLOTS.map(([d, x, sc, dim]) => {
        const i = ((at + d) % n + n) % n;
        return card(esc(g[i].name), g[i].sys, x, sc, dim, d === 0 && favs.has(i));
      }).join("");
      return `<div class="wash"></div>${o.title === false ? "" : `<div class="title">${esc(g[at].name)}</div>`}
        <div class="cards">${cards}</div>
        ${o.count === false ? "" : `<div class="count">${at + 1} / ${n}</div>`}<div class="rail"><i style="left:${n > 1 ? (at / (n - 1)) * 82 : 0}%"></i></div>`;
    }
    const title = o.title === undefined ? "Chrono Trigger" : o.title;
    return `<div class="wash"></div>${title ? `<div class="title">${title}</div>` : ""}
      <div class="cards">${card("Earthbound", SYS.gbc, -560, .62, .55)}${card("Punch-Out!!", SYS.nes, -300, .78, .75)}${card("Super Metroid", SYS.gb, 300, .78, .75)}${card("F-Zero", SYS.ngpc, 560, .62, .55)}${card("Chrono Trigger", SYS.snes, 0, 1, 1, o.fav)}</div>
      ${o.count === false ? "" : `<div class="count">1 / 12</div>`}<div class="rail"><i></i></div>`;
  }

  const ROWS = [["Play Time", ""], ["Wi-Fi", "off"], ["Bluetooth", "not connected"], ["Audio Output", "auto (speaker)"], ["Wi-Fi File Transfer", "needs Wi-Fi", 1], ["Date & Time", "Nov 4, 8:20 PM"]];
  function menu(theme, o = {}) {
    const rows = o.rows || ROWS, sel = o.sel === undefined ? 0 : o.sel;
    const z = o.zyper === false ? "" : `<svg class="zcorner" style="${o.zyperStyle || ""}"><use href="#zyper"/></svg>`;
    return `<div class="scrim"></div><div class="panel" style="${o.rule ? `--rule:${o.rule}` : ""}"><h4>${o.heading || "ZyperOS"}${z}</h4><hr>${rows.map((r, i) =>
      `<div class="mrow ${r[2] ? "dead" : ""} ${i === sel ? "sel" : ""}">${r[0]}<span>${r[1]}</span></div>`).join("")}${o.more ? `<div class="more"></div>` : ""}</div>`;
  }

  const SPK = `<svg viewBox="-24 -24 48 48"><rect x="-22" y="-9" width="12" height="18" rx="2" fill="var(--pc)"/><path d="M-12-9 2-22v44L-12 9z" fill="var(--pc)"/><path d="M8-9a13 13 0 0 1 0 18M13-15a21 21 0 0 1 0 30" fill="none" stroke="var(--pc)" stroke-width="4" stroke-linecap="round"/></svg>`;
  const SUN = `<svg viewBox="-24 -24 48 48"><circle r="9.6" fill="var(--pc)"/><path d="M0-14v-7M0 14v7M-14 0h-7M14 0h7M-10-10l-5-5M10 10l5 5M-10 10l-5 5M10-10l5-5" stroke="var(--pc)" stroke-width="4" stroke-linecap="round"/></svg>`;
  function bar(theme, kind = "volume", level = 13, max = 20) {
    let s = "";
    for (let i = 0; i < max; i++) s += `<i class="${i < level ? "on" : ""}"></i>`;
    return `<div class="bar" style="--pc:var(${kind === "volume" ? "--accent-ink" : "--light"})">${kind === "volume" ? SPK : SUN}<div class="segs">${s}</div></div>`;
  }

  /* The on-screen keyboard as src/keyboard.c lays it out: an 860x580 panel,
   * the title, the field, four rows of ten keys (the digits on every layer),
   * the space bar, and the face buttons' verbs. With no options it is the
   * keyboard DESIGN.md 11.5 chose; o.variant and o.footer draw the others the
   * keyboard lab compared: "today" (the chips), "glyphs" (button glyphs and
   * layer tabs), "actions" with footer "full", "line" or "none". */
  const KB_LAYERS = [
    "1234567890qwertyuiopasdfghjkl-zxcvbnm_.@",
    "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.@",
    "1234567890!@#$%^&*()=+[]{}\\|/~;:'\",.<>?`",
  ];
  function kbGlyph(label, size) {
    const round = /^[ABXY]$/.test(label);
    return `<span class="kg ${round ? "round" : ""}" style="--gs:${size || 34}px">${label}</span>`;
  }
  function keyboard(theme, o = {}) {
    /* The default is the keyboard DESIGN.md 11.5 settles on: the action row
     * with its buttons on the keys and one line under it. */
    const v = o.variant || "actions", layer = o.layer || 0, foot = o.footer || (o.variant ? "full" : "line");
    const sel = o.sel || [2, 4], text = o.text === undefined ? "correct horse" : o.text;
    /* The action row with its glyphs on the keys needs a wider panel: each
     * key's label is at least meta size beside its button. */
    const wide = v === "actions" && foot !== "full";
    const tall = v === "glyphs" ? 660 : v === "actions" ? (foot === "none" ? 556 : foot === "line" ? 604 : 604) : 580;
    const top = Math.round((768 - tall) / 2);
    const keys = KB_LAYERS[layer].split("");
    let html = `<div class="scrim"></div><div class="kb2 ${wide ? "wide" : ""}" style="top:${top}px;height:${tall}px">`;
    html += `<h5>${o.title || "Wi-Fi password"}</h5>`;
    if (wide && foot === "none") {
      html += `<div class="corner">${kbGlyph("MENU", 40)}<span>cancel</span></div>`;
      html += `<div class="cur l">${kbGlyph("L1", 40)}</div><div class="field narrow">${esc(text)}<i></i></div><div class="cur r">${kbGlyph("R1", 40)}</div>`;
    } else {
      html += `<div class="field">${esc(text)}<i></i></div>`;
    }
    let gy = 150;
    if (v === "glyphs") {
      const tabs = [["abc", "", 0], ["ABC", "X", 1], ["#+=", "Y", 2]];
      html += `<div class="tabs">${tabs.map(t => `<span class="${t[2] === layer ? "on" : ""}">${t[1] ? kbGlyph(t[1], 38) : ""}${t[0]}</span>`).join("")}</div>`;
      gy = 218;
    }
    html += `<div class="grid" style="top:${gy}px">${keys.map((k, i) => {
      const r = Math.floor(i / 10), c = i % 10;
      return `<b class="${sel[0] === r && sel[1] === c ? "sel" : ""}">${esc(k)}</b>`;
    }).join("")}</div>`;
    const sy = gy + 4 * 66;
    if (v === "actions") {
      /* The keys say what they do; the legend under them says which button
       * does it too, since a label beside each key is too small to read on
       * the panel at 3 inches. */
      if (!wide) {
        const acts = [["shift", 2], ["#+=", 1.6], ["space", 2.4], ["delete", 2], ["done", 2]];
        html += `<div class="acts" style="top:${sy}px">${acts.map((a, i) => `<b class="${sel[0] === 4 && sel[1] === i ? "sel" : ""}" style="flex:${a[1]}">${a[0]}</b>`).join("")}</div>`;
        const verbs = [["A", "type"], ["B", "delete"], ["X", "shift"], ["Y", "symbols"], ["L1/R1", "cursor"], ["START", "done"], ["MENU", "cancel"]];
        const row = list => `<div>${list.map(x => `${kbGlyph(x[0], 44)}<span>${x[1]}</span>`).join("")}</div>`;
        html += `<div class="legend">${row(verbs.slice(0, 4))}${row(verbs.slice(4))}</div>`;
      } else {
        /* Each key says its own button; space is drawn as the space mark. */
        const acts = [[layer === 1 ? "unshift" : "shift", "X", 2.2], [layer === 2 ? "abc" : "#+=", "Y", 1.7], ["", "", 1.4], ["delete", "B", 2.2], ["done", "START", 2.5]];
        html += `<div class="acts" style="top:${sy}px">${acts.map((a, i) => `<b class="${sel[0] === 4 && sel[1] === i ? "sel" : ""}" style="flex:${a[2]}">${a[1] ? kbGlyph(a[1], 40) : `<i class="spc"></i>`}${a[0] ? `<span>${a[0]}</span>` : ""}</b>`).join("")}</div>`;
        if (foot === "line") html += `<div class="legend"><div>${kbGlyph("L1/R1", 44)}<span>cursor</span>${kbGlyph("MENU", 44)}<span>cancel</span></div></div>`;
      }
    } else {
      html += `<div class="space ${sel[0] === 4 ? "sel" : ""}" style="top:${sy}px">space</div>`;
      const verbs = [["A", "type"], ["B", "delete"], ["X", layer === 1 ? "unshift" : "shift"], ["Y", layer === 2 ? "letters" : "symbols"],
                     ["L1/R1", "cursor"], ["START", "done"], ["MENU", "cancel"]];
      if (v === "today") {
        const row = list => `<div>${list.map(x => `<span class="chip">${x[0]}</span><span>${x[1]}</span>`).join("")}</div>`;
        html += `<div class="legend chips">${row(verbs.slice(0, 4))}${row(verbs.slice(4))}</div>`;
      } else {
        const row = list => `<div>${list.map(x => `${kbGlyph(x[0], 44)}<span>${x[1]}</span>`).join("")}</div>`;
        html += `<div class="legend">${row(verbs.slice(0, 4))}${row(verbs.slice(4))}</div>`;
      }
    }
    return html + `</div>`;
  }

  function message(text) { return `<div class="scrim"></div><div class="msg">${text}</div>`; }

  function glyph(kind, label, x, y, lit) {
    const cls = kind === "face" ? "g-face" : "g-pill";
    return `<div class="${cls} ${lit ? "lit" : ""}" style="left:${x}px;top:${y}px">${label}</div>`;
  }

  function progress(step, done) {
    let s = "";
    for (let i = 0; i < 10; i++) s += `<i class="${i < step || (i === step && done) ? "done" : i === step ? "now" : ""}"></i>`;
    return `<div class="progress">${s}</div>`;
  }

  const TICK = `<svg viewBox="0 0 24 24"><path d="M5 12.5l4.5 4.5L19 7.5" fill="none" stroke-width="3.4" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
  function tourStep(theme, o = {}) {
    const step = o.step === undefined ? 2 : o.step, done = !!o.done;
    return `${progress(step, done)}<div class="stage">${shelf(theme, { title: "", count: false, fav: o.fav })}</div>
      ${glyph(o.glyphKind || "face", o.glyph || "B", 512, 424, o.lit)}
      <div class="t-title">${o.title || "Go back"}</div><div class="t-line">${o.line || "Press B to go back one step."}</div>
      ${o.note ? `<div class="t-note">${o.note}</div>` : ""}
      ${done ? `<div class="check">${TICK}</div>` : ""}
      <svg class="zy" style="${done ? "transform:translateY(-24px) rotate(-6deg)" : ""}"><use href="#zyper"/></svg>
      ${done && o.cheer !== false ? `<div class="bubble">${o.cheer || "Woof!"}</div>` : ""}
      <div class="skip"><b>START</b>Skip tutorial</div>`;
  }

  function tourWelcome() {
    return `<div class="welcome"><svg><use href="#zyper"/></svg>
      <div class="t-title" style="top:470px">Hi! I'm Zyper.</div>
      <div class="t-line" style="top:556px">Let me show you the buttons. It takes a minute.</div>
      ${glyph("face", "A", 440, 676)}<div style="position:absolute;left:510px;top:648px;font-size:49px">Let's go</div></div>
      <div class="skip"><b>START</b>Skip tutorial</div>`;
  }

  function swatches(theme) {
    const c = T.themes[theme].color;
    return `<div class="zk-swatches">${Object.entries(c).map(([n, v]) => `<div class="zk-sw"><i style="background:${v}"></i><span>${n}</span></div>`).join("")}</div>`;
  }

  /* A Web Animation timed by a DESIGN.md motion token. */
  function animate(el, name, frames, extra = {}) {
    const m = T.motion[name];
    return el.animate(frames, Object.assign({ duration: m.ms, easing: T.curves[m.curve] || "linear", fill: "both" }, extra));
  }

  /* A DESIGN.md sound in a voice (the default when none is named), from its
   * tones as tools/gensounds.py makes the launcher's WAVs: a rise of min(4 ms,
   * a quarter of the tone) to its peak, an exponential fall to 1/10000 at its
   * end, and a glide over its first third. Web Audio's oscillators and ramps
   * are near that and not sample for sample; the sound lab plays the WAVs.
   * Call from a press. */
  let ac = null;
  function sound(name, voice) {
    ac = ac || new (window.AudioContext || window.webkitAudioContext)();
    const v = T.sound.voices[voice || T.sound.default] || T.sound.voices[T.sound.default];
    (v.sounds[name] || []).forEach(t => {
      const at = ac.currentTime + t.at / 1000, len = t.ms / 1000, o = ac.createOscillator(), g = ac.createGain();
      const [f0, f1] = Array.isArray(t.hz) ? t.hz : [t.hz, t.hz];
      o.type = t.wave; o.frequency.setValueAtTime(f0, at);
      if (f1 !== f0) o.frequency.exponentialRampToValueAtTime(f1, at + len / 3);
      g.gain.setValueAtTime(0, at); g.gain.linearRampToValueAtTime(t.peak, at + Math.min(.004, len / 4));
      g.gain.exponentialRampToValueAtTime(.0001, at + len);
      o.connect(g).connect(ac.destination); o.start(at); o.stop(at + len + .02);
    });
  }

  const ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(es => es.forEach(e => {
    const s = e.target.querySelector(".zs");
    if (s) s.style.setProperty("--k", e.contentRect.width / 1024);
  })) : null;
  function fit(root = document) { if (ro) root.querySelectorAll(".zk-frame").forEach(f => ro.observe(f)); }

  /* A board of decisions. list: [{key, title, why, options: {id: {name, tag,
   * blurb, draw(theme)}}}]. Picks and notes are kept with the page through
   * the artifact's db capability (collection "picks", one doc per key) when
   * the page declares it, else in this browser. */
  function decisions(mount, list, o = {}) {
    const picks = {}, writing = {};
    let db = null;
    const say = o.status || (() => {});
    try { Object.assign(picks, JSON.parse(localStorage.getItem(o.storeKey || "zk-picks") || "{}")); } catch (e) {}
    function keep(key) {
      try { localStorage.setItem(o.storeKey || "zk-picks", JSON.stringify(picks)); } catch (e) {}
      if (!db) return;
      say("Saving…");
      writing[key] = (writing[key] || Promise.resolve())
        .then(() => db.doc("picks/" + key).set(Object.assign({ at: new Date().toISOString() }, picks[key])))
        .then(() => say("Saved"), e => say("Not saved: " + (e && e.code || "error")));
    }
    function render() {
      const y = window.scrollY;
      mount.innerHTML = list.map((d, n) => {
        const chosen = picks[d.key] && picks[d.key].choice;
        /* A stored choice is only ever one of the option ids it is compared with. */
        return `<section class="zk-decision" id="${d.key}" style="padding-block:36px 8px;border-bottom:1px solid var(--zk-line)">
          <p class="zk-mono" style="margin:0">${String(n + 1).padStart(2, "0")}</p><h2 class="zk-h2">${d.title}</h2><p class="zk-why">${d.why}</p>
          <div class="zk-grid" style="margin-top:20px">${Object.entries(d.options).map(([k, op]) => `
            <article class="zk-card ${chosen === k ? "picked" : ""}" data-key="${d.key}" data-opt="${k}">
              ${op.draw()}
              <h3>${op.name} ${op.tag ? `<span class="zk-tag ${op.tag === "today" ? "dim" : ""}">${op.tag}</span>` : ""}</h3>
              <p>${op.blurb}</p>
              <div class="zk-row" style="margin-top:auto"><button type="button" class="zk-btn pick" id="pick-${d.key}-${k}">${chosen === k ? "Picked" : "Pick " + op.name}</button></div>
            </article>`).join("")}</div>
          <p style="margin:14px 0 4px"><label class="zk-mono" for="note-${d.key}">Anything to change?</label></p>
          <textarea class="zk-note" id="note-${d.key}" data-note="${d.key}" placeholder="Optional">${esc((picks[d.key] && picks[d.key].note) || "")}</textarea>
        </section>`;
      }).join("");
      fit(mount);
      window.scrollTo(0, y);
      if (o.onRender) o.onRender(picks);
    }
    mount.addEventListener("click", e => {
      const b = e.target.closest(".pick"), c = e.target.closest(".zk-card");
      if (!b || !c) return;
      picks[c.dataset.key] = Object.assign({}, picks[c.dataset.key], { choice: c.dataset.opt });
      keep(c.dataset.key);
      render();
      document.getElementById(b.id)?.focus({ preventScroll: true });
    });
    const timers = {};
    mount.addEventListener("input", e => {
      const k = e.target.dataset.note;
      if (!k) return;
      picks[k] = Object.assign({}, picks[k], { note: e.target.value });
      clearTimeout(timers[k]);
      timers[k] = setTimeout(() => keep(k), 700);
    });
    render();
    (async () => {
      if (!window.claude || !window.claude.use) { say("Saved in this browser"); return; }
      db = await window.claude.use("db");
      if (!db) { say("Saved in this browser"); return; }
      let synced = false;
      db.collection("picks").onSnapshot(snap => {
        let changed = false;
        /* Picks made before the store answered were kept only in this
         * browser; the first confirmed snapshot sends the ones it lacks. */
        if (!synced && !snap.metadata.fromCache) {
          synced = true;
          const have = new Set(snap.docs.map(d => d.id));
          Object.keys(picks).forEach(k => { if (!have.has(k) && picks[k] && picks[k].choice) keep(k); });
        }
        snap.docs.forEach(d => {
          const v = d.data() || {}, cur = picks[d.id] || {};
          if (document.activeElement && document.activeElement.dataset.note === d.id) return;
          if (v.choice !== cur.choice || (v.note || "") !== (cur.note || "")) { picks[d.id] = { choice: String(v.choice || ""), note: String(v.note || "") }; changed = true; }
        });
        if (!snap.metadata.fromCache) say("Saved");
        if (changed) render();
      }, err => say("Saved in this browser (" + err.code + ")"));
    })();
    return { picks, render };
  }

  window.ZK = { T, SYS, esc, vars, screen, shelf, menu, bar, keyboard, message, glyph, progress, tourStep, tourWelcome, swatches, animate, sound, fit, decisions };
})();

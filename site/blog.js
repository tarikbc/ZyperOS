/* The blog's three small behaviours, for the pages mk/blog.py writes. Every
 * page reads right without this script: all cards show, every tick's meaning
 * is in its label, and the side column simply lists the sections. */

/* The index's chips show one topic's cards, or all of them. */
(() => {
  const chips = document.querySelectorAll(".chips button[data-topic]");
  if (!chips.length) return;
  const cards = document.querySelectorAll(".cards li[data-topic]");
  chips.forEach(chip => chip.addEventListener("click", () => {
    const t = chip.dataset.topic;
    chips.forEach(c => c.setAttribute("aria-pressed", String(c === chip)));
    cards.forEach(card => { card.hidden = t !== "all" && card.dataset.topic !== t; });
  }));
})();

/* A table's ticks, crosses and dashes: what each means, on hover, on keyboard
 * focus and on a tap, in one fixed layer above the table so its sideways
 * scroll cannot clip it (the landing page's own tooltip). */
(() => {
  const marks = document.querySelectorAll("[data-tip]");
  if (!marks.length) return;
  const tip = document.createElement("div");
  tip.className = "tip";
  tip.setAttribute("role", "tooltip");
  document.body.append(tip);
  let on = null;
  const place = () => {
    if (!on) return;
    const r = on.getBoundingClientRect(), t = tip.getBoundingClientRect();
    let x = r.left + r.width / 2 - t.width / 2, y = r.top - t.height - 8;
    if (y < 8) y = r.bottom + 8;
    x = Math.max(8, Math.min(x, innerWidth - t.width - 8));
    tip.style.left = x + "px";
    tip.style.top = y + "px";
  };
  const show = el => { on = el; tip.textContent = el.dataset.tip; tip.classList.add("on"); place(); };
  const hide = el => { if (!el || el === on) { on = null; tip.classList.remove("on"); } };
  marks.forEach(el => {
    el.addEventListener("mouseenter", () => show(el));
    el.addEventListener("mouseleave", () => hide(el));
    el.addEventListener("focus", () => show(el));
    el.addEventListener("blur", () => hide(el));
    el.addEventListener("click", () => (on === el ? hide(el) : show(el)));
  });
  addEventListener("scroll", place, { passive: true, capture: true });
  addEventListener("resize", place);
})();

/* On this page marks the section being read: the last heading that has
 * scrolled above the top third of the window. */
(() => {
  const links = [...document.querySelectorAll(".toc a[href^='#']")];
  if (!links.length) return;
  const heads = links.map(a => document.getElementById(a.hash.slice(1)));
  let queued = false;
  const update = () => {
    queued = false;
    let cur = -1;
    heads.forEach((h, i) => { if (h && h.getBoundingClientRect().top < innerHeight / 3) cur = i; });
    links.forEach((a, i) => (i === cur ? a.setAttribute("aria-current", "true") : a.removeAttribute("aria-current")));
  };
  addEventListener("scroll", () => { if (!queued) { queued = true; requestAnimationFrame(update); } }, { passive: true });
  update();
})();

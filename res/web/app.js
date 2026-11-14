/* Wi-Fi File Transfer and USB File Transfer, the browser half (DESIGN.md
 * 11.15).
 *
 * No framework and no build step: this file is served off the card exactly as
 * it is written, so it can be edited on the device and reloaded. That is worth
 * more here than any convenience a toolchain would add - the whole point of
 * the feature is not needing a card reader.
 *
 * Uploads go through XMLHttpRequest rather than fetch, for one reason:
 * upload progress. fetch still has no way to report how much of a request body
 * has gone out, and a 900 MB ROM with no progress bar is indistinguishable
 * from a hang.
 */
'use strict';

const $ = (id) => document.getElementById(id);
const enc = encodeURIComponent;
/* The phone layout's width, as app.css has it. */
const narrow = matchMedia('(max-width: 879px)');
const still = matchMedia('(prefers-reduced-motion: reduce)');
/* DESIGN.md 8.2's times, as the token block in app.css has them. */
const ms = (name) => parseFloat(getComputedStyle(document.documentElement)
	.getPropertyValue('--t-' + name)) || 0;

/* ---- words --------------------------------------------------------------
 *
 * The page speaks the handheld's language, MENU > Language (
 * 2026-11-07): /i18n.js, loaded before this file, is the device's
 * res/web/i18n/<code>.js for the language that is on, which tools/i18n.py
 * writes from res/strings/web.txt. t(name, ...) is the string web.<name>
 * with its %s, %d and %1$s filled; tn(name, n, ...) is the form a count of
 * n takes, filled with n when nothing else is given. index.html's own words
 * carry their name in data-t (an attribute's in data-t-aria, data-t-title
 * and the like), put in here before anything shows. tools/i18n.py counts a
 * string as used only where its name is written out in quotes, in a call of
 * t or tn or in a data-t attribute: a name is never built from pieces. */
const WORDS = window.ZYPEROS_STRINGS || {};
const LANG = window.ZYPEROS_LANG || 'en';
const PLURAL = window.ZYPEROS_PLURAL || ((n) => (n === 1 ? 0 : 1));

/* printf's %s, %d, %% and %1$s, the conversions the strings have; with no
 * arguments, %s and %d stay for put() */
function format(s, args) {
	let at = 0;
	return s.replace(/%(?:(\d+)\$)?([sd%])/g, (m, pos, c) =>
		c === '%' ? '%' : !args.length ? m : String(args[pos ? pos - 1 : at++]));
}
function t(name, ...args) {
	const w = WORDS[name];
	return format(typeof w === 'string' ? w : Array.isArray(w) ? w[1] : name, args);
}
function tn(name, n, ...args) {
	const w = WORDS[name];
	return format(Array.isArray(w) ? w[PLURAL(n)] : typeof w === 'string' ? w : name,
	              args.length ? args : [n]);
}

/* A line with an element in it, into `node`: `text` is a string from t with
 * no arguments, and each conversion the next of `args` (or the numbered
 * one), an element or a string */
function put(node, text, ...args) {
	const parts = [];
	let at = 0, from = 0;
	text.replace(/%(?:(\d+)\$)?([sd])/g, (m, pos, c, off) => {
		parts.push(text.slice(from, off), args[pos ? pos - 1 : at++]);
		from = off + m.length;
		return m;
	});
	parts.push(text.slice(from));
	node.replaceChildren(...parts.filter((x) => x !== ''));
}

/* Numbers the language's way (Intl), the digits as English has them: "1,5 Go"
 * and "45 %" in French. A size is rounded as English always rounded it,
 * toFixed, and only its point is the language's. */
const POINT = (new Intl.NumberFormat(LANG).formatToParts(1.5)
	.find((p) => p.type === 'decimal') || { value: '.' }).value;
const PERCENT = new Intl.NumberFormat(LANG, { style: 'percent', maximumFractionDigits: 0 });
const pct = (n) => PERCENT.format(n / 100);

document.documentElement.lang = LANG;
for (const e of document.querySelectorAll('*')) {
	for (const a of [...e.attributes]) {
		if (a.name === 'data-t') e.textContent = t(a.value);
		else if (a.name.startsWith('data-t-'))
			e.setAttribute(a.name === 'data-t-aria' ? 'aria-label' : a.name.slice(7), t(a.value));
	}
}
/* The page's name, in the tab, on the PIN screen and over the folders: the
 * one the handheld's screen shows for the link the page came over, which
 * /link.js says (hare_set_cable). Over the cable it was Wi-Fi File Transfer
 * too, beside a handheld that said USB File Transfer. A handheld that does
 * not answer it, or a computer serving res/web, leaves it unset: Wi-Fi's. */
const pageName = window.ZYPEROS_LINK === 'usb' ? t('name.usb') : t('name.wifi');
document.title = pageName;
for (const e of document.querySelectorAll('.pagename')) e.textContent = pageName;

let cwd = '';                 /* the path the listing is showing */
let entries = [];
let roots = [];               /* the card's folders, in the device's order */
let info = {};                /* what the roots' listing says besides: ssh */

/* The card's folders as the sidebar draws them: xfer.c's root names. */
const PLACE_MARK = { roms: 'roms', music: 'music', books: 'books', videos: 'videos',
                     bgm: 'bgm', bios: 'bios', saves: 'saves', states: 'states' };

/* Every folder wears ZyperOS's pixel art (2026-11-06: "can we use our
 * pixel art icon for all folders in the portal?"): a system's folder the
 * shelf's own picture of it, on a tile in its color, which the listing names
 * ("icon", "color"); the card's own folders theirs (Menu Music the
 * cassette that Roms/BGM wears too); and any
 * other folder the plain one. Served by the device from the shelf's
 * pictures, /icons/KEY.png. A picture that does not come is the line mark,
 * never a broken image. */
const PLACE_ICON = { roms: 'ROMS', music: 'MUSIC', books: 'BOOKS', videos: 'VIDEOS',
                     bgm: 'BGM', bios: 'BIOS', saves: 'SAVES', states: 'STATES' };
function pix(key, color, fallback) {
	const s = document.createElement('span');
	s.className = 'sico';
	if (color) s.style.setProperty('--sys', '#' + color);
	const i = document.createElement('img');
	i.alt = '';
	i.loading = 'lazy';
	i.onerror = () => { i.remove(); s.classList.add('i', 'i-' + (fallback || 'folder')); };
	i.src = '/icons/' + key + '.png';
	s.append(i);
	return s;
}

/* ---- talking to the device -------------------------------------------- */

/* Every non-200 is an error with the server's own words in it. The routes
 * answer in plain text precisely so this can show them without a schema. */
async function api(method, path, body) {
	const r = await fetch(path, { method, body, credentials: 'same-origin' });
	if (r.status === 401) { showGate(t('gate.session_ended')); throw new Error('unauthorized'); }
	if (!r.ok) throw new Error((await r.text()).split('\n')[1] || r.statusText);
	return r;
}

/* Every PUT of a file the page makes - the uploads, the covers it fetches,
 * the System Update files - goes as this says: its URL and its body.
 *
 * Over the USB cable Safari never hands the page the answer to a PUT whose
 * body is an exact multiple of 131,072 bytes (128 KiB): the file landed
 * whole, the handheld answered, and the page sat at 100% on that file for
 * good, the queue behind it stopped with it. fetch() hears the answer, but
 * Safari then keeps that connection busy for good, and it has six. So a file
 * of such a size, an empty one included, goes with TRAILER after it and
 * trailer=16 in its URL - a body no multiple of 128 KiB is - and the
 * handheld checks the trailer and takes it off before the file takes its
 * name (src/hare.c, trailer_off). Any other size goes as it is. From every
 * browser and over either link alike: there is no telling Safari apart, and
 * no need. The Blob is the file and the trailer side by side, not a copy of
 * a file that may be a gigabyte. */
const TRAILER = '--zyperos-pad--\n';
function trailed(url, blob) {
	if (blob.size % 131072) return { url, body: blob };
	return { url: url + '&trailer=' + TRAILER.length, body: new Blob([blob, TRAILER]) };
}

/* A file onto the card at `path`, by fetch: a cover the page fetched */
function putFile(path, blob) {
	const put = trailed('/api/file?p=' + enc(path), blob);
	return api('PUT', put.url, put.body);
}

/* ---- the gate ---------------------------------------------------------- *
 *
 * Four boxes, one digit each. The PIN goes to the device as the fourth digit
 * lands, so there is no button; a PIN pasted into any box, or filled in by a
 * phone's one-time-code, fills all four. Zyper shuts his eyes while it is
 * typed (My note in the lab, 2026-11-05), cocks his head at a wrong one
 * as he does in the menus, and hops at the right one (DESIGN.md 10). */
const boxes = [...$('boxes').querySelectorAll('input')];
boxes.forEach((b, i) => b.setAttribute('aria-label', i ? t('gate.digit', i + 1) : t('gate.digit_first', 1)));
const zy = $('gatezy');
let checking = false;
let blinkTimer = 0;

function pinDigits() { return boxes.map((b) => b.value).join(''); }

function showGate(msg) {
	closeMenu(true);
	closeDialog(null);
	$('app').hidden = true;
	$('gate').hidden = false;
	gateSay(msg || '', false);
	boxes.forEach((b) => { b.value = ''; });
	$('boxes').classList.remove('bad', 'busy');
	zy.classList.remove('shut', 'tilt', 'hop');
	checking = false;
	boxes[0].focus();
	blinkSoon();
}

function gateSay(msg, ok) {
	$('gatemsg').textContent = msg;
	$('gatemsg').classList.toggle('ok', !!ok);
}

/* A blink at rest, every three to six seconds (zyper-blink-gap), never while
 * his eyes are shut for the PIN. */
function blinkSoon() {
	clearTimeout(blinkTimer);
	if (still.matches) return;
	const gap = ms('zyper-blink-gap');
	blinkTimer = setTimeout(() => {
		if ($('gate').hidden) return;
		if (!zy.classList.contains('shut') && !checking) {
			zy.classList.add('shut');
			setTimeout(() => { if (!pinDigits()) zy.classList.remove('shut'); }, ms('zyper-blink'));
		}
		blinkSoon();
	}, gap + Math.random() * gap);
}

/* Digits from wherever they came - a key, a paste, an autofill of the whole
 * code into one box - laid into the boxes from `at`. */
function fill(at, text) {
	const ds = text.replace(/\D/g, '');
	for (let i = 0; i < ds.length && at + i < boxes.length; i++) boxes[at + i].value = ds[i];
	const next = Math.min(at + ds.length, boxes.length - 1);
	boxes[next].focus();
	afterType();
}

function afterType() {
	const d = pinDigits();
	$('boxes').classList.remove('bad');
	if (d) gateSay('', false);
	zy.classList.remove('tilt');
	zy.classList.toggle('shut', d.length > 0);
	if (d.length === boxes.length && boxes.every((b) => b.value)) sendPin(d);
}

boxes.forEach((b, i) => {
	b.addEventListener('input', () => {
		const v = b.value;
		b.value = '';
		fill(i, v);
	});
	b.addEventListener('paste', (e) => {
		e.preventDefault();
		boxes.forEach((x) => { x.value = ''; });
		fill(0, (e.clipboardData || window.clipboardData).getData('text'));
	});
	b.addEventListener('keydown', (e) => {
		if (e.key === 'Backspace' && !b.value && i > 0) {
			e.preventDefault();
			boxes[i - 1].value = '';
			boxes[i - 1].focus();
			afterType();
		} else if (e.key === 'ArrowLeft' && i > 0) {
			e.preventDefault(); boxes[i - 1].focus();
		} else if (e.key === 'ArrowRight' && i < boxes.length - 1) {
			e.preventDefault(); boxes[i + 1].focus();
		}
	});
	b.addEventListener('focus', () => b.select());
});
$('pinform').addEventListener('submit', (e) => { e.preventDefault(); afterType(); });

async function sendPin(pin) {
	if (checking) return;
	checking = true;
	$('boxes').classList.add('busy');
	let r;
	try {
		r = await fetch('/api/auth', { method: 'POST', body: pin });
	} catch (err) {
		return wrongPin(t('gate.unreachable'));
	}
	if (r.status === 429) return wrongPin(t('gate.locked'));
	if (!r.ok) {
		/* "wrong PIN, 4 tries left" from the device: the count, if it says
		 * one; at the fifth it says to wait instead. */
		const said = await r.text().catch(() => '');
		if (/wait/.test(said)) return wrongPin(t('gate.locked'));
		const left = /(\d+) tr(y|ies) left/.exec(said);
		return wrongPin(left ? tn('gate.wrong_left', +left[1]) : t('gate.wrong'));
	}
	zy.classList.remove('shut');
	zy.classList.add('hop');
	clearTimeout(blinkTimer);
	setTimeout(async () => {
		zy.classList.remove('hop');
		checking = false;
		$('gate').hidden = true;
		$('app').hidden = false;
		try { await start(); } catch (err) { /* the gate is back up */ }
	}, still.matches ? 0 : ms('zyper-hop'));
}

/* Zyper cocks his head and holds it (hold-zyper-fail), then settles; the
 * digits stay up in the warning color for as long, so the mistake can be
 * seen, and then clear for the next try. */
function wrongPin(msg) {
	gateSay(msg, false);
	$('boxes').classList.remove('busy');
	$('boxes').classList.add('bad');
	zy.classList.remove('shut');
	zy.classList.add('tilt');
	setTimeout(() => {
		zy.classList.remove('tilt');
		boxes.forEach((b) => { b.value = ''; });
		$('boxes').classList.remove('bad');
		checking = false;
		boxes[0].focus();
	}, ms('hold-zyper-fail'));
}

/* ---- listing ----------------------------------------------------------- */

/* Decimal, not 1024: macOS has quoted decimal since 10.6, and the whole job
 * of this page is to agree with the machine at the other end about what is on
 * the card. A size that disagrees with the file manager beside it reads as a
 * failed copy. A decimal for MB and GB, whole numbers for KB. */
function human(n) {
	if (n < 1000) return t('size.b', n);
	let i = -1;
	do { n /= 1000; i++; } while (n >= 1000 && i < 2);
	if (i === 0) return t('size.kb', Math.round(n));
	const x = n.toFixed(1).replace('.', POINT);
	return i === 1 ? t('size.mb', x) : t('size.gb', x);
}

const rootOf = (path) => roots.find((r) => r.path === path.split('/')[0]);
const nameOf = (path) => {
	const r = rootOf(path);
	return path.includes('/') ? path.slice(path.lastIndexOf('/') + 1) : (r ? r.name : path);
};

function crumbs() {
	const nav = $('crumbs');
	nav.textContent = '';
	const parts = cwd.split('/');
	parts.forEach((p, i) => {
		const path = parts.slice(0, i + 1).join('/');
		/* The first segment is a root's URL name - "roms" - and the device
		 * calls it "ROMs" everywhere else. Show what it is called. */
		const label = i === 0 ? (rootOf(p) || { name: p }).name : p;
		if (i === parts.length - 1) {
			const s = document.createElement('span');
			s.className = 'here';
			s.textContent = label;
			nav.append(s);
			return;
		}
		const b = document.createElement('button');
		b.type = 'button';
		b.textContent = label;
		b.dataset.path = path;      /* a folder above, to drop onto (moving) */
		b.onclick = () => go(path);
		const sep = document.createElement('span');
		sep.className = 'sep';
		sep.textContent = '›';
		nav.append(b, sep);
	});
}

/* The card folders, twice: the sidebar's rows and a phone's chips. */
function places() {
	const here = cwd.split('/')[0];
	for (const [id, rows] of [['places', true], ['chips', false]]) {
		const nav = $(id);
		nav.textContent = '';
		for (const r of roots) {
			const b = document.createElement('button');
			b.type = 'button';
			b.textContent = r.name;
			if (rows) {
				/* The pixel art, or the line mark when it does not come */
				const mark = PLACE_MARK[r.path] || 'folder';
				const key = PLACE_MARK[r.path] ? PLACE_ICON[r.path] : 'FOLDER';
				if (key) {
					const i = document.createElement('img');
					i.alt = '';
					i.onerror = () => { i.remove(); b.classList.add('i', 'i-' + mark); };
					i.src = '/icons/' + key + '.png';
					b.prepend(i);
				} else b.classList.add('i', 'i-' + mark);
			}
			if (r.path === here) { b.classList.add('on'); b.setAttribute('aria-current', 'page'); }
			b.onclick = () => go(r.path);
			nav.append(b);
		}
	}
	const on = $('chips').querySelector('.on');
	if (on) on.scrollIntoView({ block: 'nearest', inline: 'nearest' });
}

/* `hist` says what this navigation does to browser history:
 *
 *   'push'    a real navigation - a crumb or a folder, back should undo it
 *   'replace' the first listing, which is where back should stop
 *   'none'    a refresh of where you already are, after a rename, a delete,
 *             a mkdir or a finished queue. These are not navigations and
 *             pushing them would make back replay the same folder repeatedly.
 *
 * The path also goes in the fragment, so a reload lands where you were and a
 * link can be sent to another device on the LAN. There is no page of the
 * roots: the sidebar is that page, and the first folder, ROMs, is the reason
 * anyone opened this. */
async function go(path, hist) {
	if (path === SETTINGS) return openSettings(hist);
	if (!path) path = roots.length ? roots[0].path : 'roms';
	const r = await api('GET', '/api/list?p=' + enc(path) + (showHidden ? '&hidden=1' : ''));
	const data = await r.json();
	if (data.path !== cwd) selected.clear();
	cwd = data.path;
	settingsShown(false);
	/* Sorted: vfat's readdir order is creation order, which is no order at all
	 * to somebody looking for a game. */
	entries = data.entries.sort((a, b) =>
		(b.dir - a.dir) || a.name.localeCompare(b.name, undefined, { numeric: true }));
	draw();
	$('tidy').hidden = !canTidy(cwd);
	$('tidypanel').hidden = true;

	const url = '#' + enc(cwd);
	if (hist === 'replace') history.replaceState({ path: cwd }, '', url);
	else if (hist !== 'none') history.pushState({ path: cwd }, '', url);
}

/* ---- Settings ------------------------------------------------------------
 *
 * 2026-11-06: "can we have a settings page in the portal ... then we
 * move the steamgriddb, logs, etc and art stuff there". A page in the main
 * area, opened from the foot of the sidebar (a phone's ⋯) as a folder is,
 * with its own place in history and the fragment (#settings). */
const SETTINGS = 'settings';
let inSettings = false;

/* Hidden folders - .media, where covers go - shown when asked, here only */
let showHidden = false;
try { showHidden = localStorage.getItem('zyperos-hidden') === '1'; } catch (e) { /* none kept */ }
$('showhidden').checked = showHidden;
$('showhidden').addEventListener('change', () => {
	showHidden = $('showhidden').checked;
	try { localStorage.setItem('zyperos-hidden', showHidden ? '1' : '0'); } catch (e) { /* this visit only */ }
});

function settingsShown(on) {
	inSettings = on;
	$('settings').hidden = !on;
	$('list').hidden = on;
	$('tools').hidden = on;
	if (on) { $('empty').hidden = true; $('tidypanel').hidden = true; $('tidy').hidden = true; }
	$('dropmsg').hidden = on;
	$('setgo').classList.toggle('on', on);
	if (on) $('setgo').setAttribute('aria-current', 'page');
	else $('setgo').removeAttribute('aria-current');
}

function openSettings(hist) {
	settingsShown(true);
	const nav = $('crumbs');
	const here = document.createElement('span');
	here.className = 'here';
	here.textContent = t('settings');
	nav.replaceChildren(here);
	/* No card folder is open: the sidebar's and the chips' lime goes */
	for (const b of document.querySelectorAll('#places button, #chips button')) {
		b.classList.remove('on');
		b.removeAttribute('aria-current');
	}
	drawArt();
	drawSgdb();
	drawRoom();
	drawUpdate();
	const url = '#' + SETTINGS;
	if (hist === 'replace') history.replaceState({ path: SETTINGS }, '', url);
	else if (hist !== 'none') history.pushState({ path: SETTINGS }, '', url);
}
$('setgo').addEventListener('click', () => { if (!inSettings) openSettings(); });

/* Back and forward. Always 'none': the browser has already moved its own
 * pointer through the stack. A queue in flight is unaffected, because each
 * job carries the folder it was bound to at enqueue. */
addEventListener('popstate', (e) => {
	const path = e.state && typeof e.state.path === 'string' ? e.state.path : '';
	go(path, 'none').catch(() => {});
});

/* After the PIN or a reload: the roots, then whatever the fragment names. A
 * fragment naming a folder that has since gone falls back to ROMs; an expired
 * cookie is rethrown, or the gate would be raised twice. */
async function start() {
	const r = await api('GET', '/api/list');
	const data = await r.json();
	roots = data.entries;
	info = { ssh: data.ssh || '' };
	$('ssh').hidden = !info.ssh;
	$('sshcmd').textContent = info.ssh ? 'ssh ' + info.ssh : '';
	const want = location.hash ? decodeURIComponent(location.hash.slice(1)) : '';
	try {
		await go(want, 'replace');
	} catch (err) {
		if (err.message === 'unauthorized') throw err;
		await go('', 'replace');
	}
	drawArt();
	drawSgdb();
	drawRoom();
}

const thumb = (path) => '/api/file?thumb=1&p=' + enc(path);
const img = (cls, src) => {
	const c = document.createElement('img');
	c.className = cls;
	c.src = src;
	c.alt = '';
	c.loading = 'lazy';
	return c;
};

function draw() {
	crumbs();
	places();
	refreshDropHint();
	const ul = $('list');
	ul.textContent = '';
	$('empty').hidden = entries.length > 0;
	const gameCovers = entries.some((e) => !e.dir && e.cover);
	const albumCovers = entries.some((e) => e.dir && (e.cover || e.covers));

	for (const e of entries) {
		const li = document.createElement('li');
		li.className = 'row' + (e.dir ? ' dir' : '') + (selected.has(e.path) ? ' sel' : '');
		li.dataset.path = e.path;
		if (selected.size) li.setAttribute('aria-selected', selected.has(e.path) ? 'true' : 'false');
		/* Ctrl, Cmd or Shift with a press picks the row, to drag several */
		li.addEventListener('click', (ev) => {
			if (!(ev.ctrlKey || ev.metaKey || ev.shiftKey) || ev.target.closest('.iconbtn')) return;
			ev.preventDefault();
			ev.stopPropagation();
			pick(e, li);
		}, true);
		li.addEventListener('pointerdown', (ev) => dragDown(ev, e, li));

		/* What leads the row: an artist's albums, an album's cover, a game's
		 * box art (or its empty frame, so a folder of games lines up), a
		 * folder's pixel art, or a file's mark. */
		if (e.dir && e.covers && e.covers.length > 1) {
			const g = document.createElement('span');
			g.className = 'cover grid';
			for (const p of e.covers.slice(0, 4)) g.append(img('', thumb(p)));
			li.append(g);
		} else if (e.dir && e.covers) {
			li.append(img('cover', thumb(e.covers[0])));
		} else if (e.dir ? albumCovers && (e.album || e.cover) : gameCovers) {
			if (e.cover) li.append(img('cover', thumb(e.cover)));
			else {
				const c = document.createElement('span');
				c.className = 'cover none';
				li.append(c);
			}
		} else if (e.dir) {
			li.append(pix(e.icon || 'FOLDER', e.color));
		} else {
			const mark = document.createElement('span');
			mark.className = 'mark i ' + (e.dir ? 'i-folder' : 'i-file');
			li.append(mark);
		}

		/* A folder opens; a file's name opens its menu, which is what a press
		 * on a file means on a phone. */
		const name = document.createElement('button');
		name.type = 'button';
		name.className = 'name';
		name.textContent = e.name;
		name.onclick = e.dir ? () => go(e.path) : () => openRowMenu(e, name, li);
		li.append(name);

		if (!e.dir) {
			const sz = document.createElement('span');
			sz.className = 'size';
			sz.textContent = human(e.size);
			li.append(sz);
		}
		const more = document.createElement('button');
		more.type = 'button';
		more.className = 'iconbtn i i-dots';
		more.setAttribute('aria-label', t('list.more_for', e.name));
		more.setAttribute('aria-haspopup', 'menu');
		more.onclick = () => openRowMenu(e, more, li);
		li.append(more);
		if (e.dir) {
			const chev = document.createElement('span');
			chev.className = 'chev';
			chev.setAttribute('aria-hidden', 'true');
			chev.textContent = '›';
			li.insertBefore(chev, more);
		}
		/* A right-click is the same menu, where the pointer is. */
		li.addEventListener('contextmenu', (ev) => {
			/* A finger held on a row lifts it to move, not the menu */
			if (drag) { ev.preventDefault(); return; }
			if (narrow.matches) return;
			ev.preventDefault();
			openRowMenu(e, { x: ev.clientX, y: ev.clientY }, li);
		});

		if (e.dir) {
			li.classList.add('drops');
			li.addEventListener('dragover', (ev) => {
				ev.preventDefault();
				ev.stopPropagation();
				li.classList.add('over');
			});
			li.addEventListener('dragleave', (ev) => {
				if (li.contains(ev.relatedTarget)) return;
				li.classList.remove('over');
			});
			li.addEventListener('drop', (ev) => {
				ev.preventDefault();
				ev.stopPropagation();
				li.classList.remove('over');
				dragDepth = 0;
				dragUI(false);
				dropped(ev.dataTransfer, e.path);
			});
		}
		ul.append(li);
	}
}

/* ---- menus ------------------------------------------------------------- *
 *
 * One element for a row's ⋯, a right-click, and a phone's ⋯ at the top: a
 * pop-up beside what opened it on a wide screen, a sheet from the bottom on a
 * phone (DESIGN.md 11.15). It opens in **move** and closes in **exit**; a
 * press anywhere outside it, Escape, or any item closes it. */
let menuFrom = null;          /* what opened it, for focus to go back to */
let menuRow = null;

function item(label, mark, fn, cls) {
	const b = document.createElement(typeof fn === 'string' ? 'a' : 'button');
	b.className = 'item i i-' + mark + (cls ? ' ' + cls : '');
	b.setAttribute('role', 'menuitem');
	b.textContent = label;
	if (typeof fn === 'string') {
		b.href = fn;
		b.setAttribute('download', '');
		b.addEventListener('click', () => closeMenu());
	} else {
		b.type = 'button';
		b.onclick = () => { closeMenu(); fn(); };
	}
	return b;
}

function openMenu(nodes, at, row) {
	const m = $('menu');
	closeMenu(true);
	m.replaceChildren(...nodes);
	m.classList.remove('closing', 'sheet');
	m.style.left = m.style.top = m.style.right = '';
	const sheet = narrow.matches;
	m.classList.toggle('sheet', sheet);
	$('scrim').hidden = !sheet;
	m.hidden = false;
	menuFrom = at && at.focus ? at : document.activeElement;
	menuRow = row || null;
	if (menuRow) menuRow.classList.add('menued');
	if (sheet) document.body.classList.add('held');
	else {
		/* Beside what opened it, right edges together, flipped above when it
		 * would run off the bottom; at the pointer for a right-click. */
		const r = at.getBoundingClientRect ? at.getBoundingClientRect()
		        : { left: at.x, right: at.x, top: at.y, bottom: at.y };
		const w = m.offsetWidth, h = m.offsetHeight;
		let x = at.getBoundingClientRect ? r.right - w : r.left;
		let y = r.bottom + 6;
		if (y + h > innerHeight - 8) y = Math.max(8, r.top - h - 6);
		x = Math.max(8, Math.min(x, innerWidth - w - 8));
		m.style.left = x + 'px';
		m.style.top = y + 'px';
		m.style.transformOrigin = (at.getBoundingClientRect ? 'right ' : 'left ') + (y < r.top ? 'bottom' : 'top');
	}
	const first = m.querySelector('.item');
	if (first) first.focus({ preventScroll: true });
}

function closeMenu(now) {
	const m = $('menu');
	if (m.hidden) return;
	if (menuRow) menuRow.classList.remove('menued');
	menuRow = null;
	document.body.classList.remove('held');
	$('scrim').hidden = true;
	const done = () => { m.hidden = true; m.classList.remove('closing'); };
	if (now || still.matches) done();
	else { m.classList.add('closing'); setTimeout(done, ms('exit')); }
	if (!now && menuFrom && menuFrom.focus && document.contains(menuFrom))
		menuFrom.focus({ preventScroll: true });
	menuFrom = null;
}

$('scrim').addEventListener('click', () => closeMenu());
document.addEventListener('pointerdown', (e) => {
	const m = $('menu');
	if (!m.hidden && !m.contains(e.target) && !$('scrim').contains(e.target) &&
	    !(menuFrom && menuFrom.contains && menuFrom.contains(e.target))) closeMenu();
});
document.addEventListener('keydown', (e) => {
	const m = $('menu');
	if (m.hidden) return;
	if (e.key === 'Escape') { e.preventDefault(); closeMenu(); return; }
	if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
	const items = [...m.querySelectorAll('.item')];
	const at = items.indexOf(document.activeElement);
	e.preventDefault();
	items[(at + (e.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length].focus();
});
narrow.addEventListener('change', () => closeMenu(true));

function openRowMenu(e, at, row) {
	const who = document.createElement('div');
	who.className = 'who';
	who.textContent = e.dir ? e.name : e.name + ' · ' + human(e.size);
	const nodes = [who];
	if (e.dir) nodes.push(item(t('act.open'), 'folder', () => go(e.path)));
	else nodes.push(item(t('act.download'), 'down', '/api/file?p=' + enc(e.path)));
	nodes.push(item(t('act.rename'), 'pen', () => rename(e)));
	/* Picked rows go together when one of them is dragged: on a phone, this
	 * is how several are picked (a mouse has Ctrl or Cmd) */
	nodes.push(item(selected.has(e.path) ? t('act.deselect') : t('act.select'), 'check', () => pick(e, row)));
	nodes.push(item(t('act.delete'), 'bin', () => del(e), 'danger'));
	openMenu(nodes, at, row);
}

/* ---- moving: pick and drag (2026-11-13) -------------------------------- *
 *
 * "we should be able to drag files around to organize them": a row, or the
 * rows picked with it, dragged onto a folder's row or onto a crumb of a
 * folder above, moves there on the card (/api/move, src/hare.c), a disc set
 * whole and a .cue with its tracks. A mouse drags once it has gone a few
 * pixels; a finger once it has been held still a moment, so a swipe still
 * scrolls the list. A name already in the folder is refused, and said. */
const selected = new Set();
let drag = null;            /* the press that may become a drag */
let swallowClick = false;   /* the click a drop ends in is not a press */
const LIFT_MS = 450;        /* a finger held this long lifts the row */

function pick(e, li, on) {
	if (on === undefined) on = !selected.has(e.path);
	if (on) selected.add(e.path); else selected.delete(e.path);
	for (const row of document.querySelectorAll('#list li.row')) {
		row.classList.toggle('sel', selected.has(row.dataset.path));
		if (selected.size) row.setAttribute('aria-selected', selected.has(row.dataset.path) ? 'true' : 'false');
		else row.removeAttribute('aria-selected');
	}
}

function dragDown(ev, e, li) {
	if (ev.button !== 0 || ev.target.closest('.iconbtn') || drag) return;
	drag = { paths: selected.has(e.path) ? [...selected] : [e.path], name: e.name,
	         id: ev.pointerId, x0: ev.clientX, y0: ev.clientY, x: ev.clientX, y: ev.clientY,
	         touch: ev.pointerType !== 'mouse', on: false, over: null, ghost: null, timer: 0 };
	if (drag.touch) drag.timer = setTimeout(dragLift, LIFT_MS);
}

/* The rows go up: a chip with the name, or how many, follows the pointer */
function dragLift() {
	if (!drag || drag.on) return;
	drag.on = true;
	const g = document.createElement('div');
	g.className = 'ghost';
	g.textContent = drag.paths.length > 1 ? tn('move.count', drag.paths.length, drag.paths.length) : drag.name;
	document.body.append(g);
	drag.ghost = g;
	for (const row of document.querySelectorAll('#list li.row'))
		if (drag.paths.includes(row.dataset.path)) row.classList.add('lifted');
	if (drag.touch && navigator.vibrate) navigator.vibrate(12);
	dragAt(drag.x, drag.y);
}

/* What is under the pointer, if it can take what is dragged: a folder's row
 * or a crumb, never one of the dragged or a folder inside one of them */
function dropAt(x, y) {
	const el = document.elementFromPoint(x, y);
	const t = el && el.closest('#list li.dir, #crumbs button[data-path]');
	if (!t) return null;
	const to = t.dataset.path;
	if (drag.paths.some((p) => to === p || to.startsWith(p + '/'))) return null;
	return t;
}

function dragAt(x, y) {
	drag.x = x;
	drag.y = y;
	if (!drag.on) return;
	drag.ghost.style.transform = `translate(${x + 14}px, ${y + 10}px)`;
	const t = dropAt(x, y);
	if (t !== drag.over) {
		if (drag.over) drag.over.classList.remove('over');
		if (t) t.classList.add('over');
		drag.over = t;
	}
}

function dragEnd(dropping) {
	if (!drag) return;
	clearTimeout(drag.timer);
	const d = drag;
	drag = null;
	if (d.ghost) d.ghost.remove();
	if (d.over) d.over.classList.remove('over');
	for (const row of document.querySelectorAll('#list li.lifted')) row.classList.remove('lifted');
	if (!d.on) return;
	swallowClick = true;
	setTimeout(() => { swallowClick = false; }, 0);
	if (dropping && d.over) moveAll(d.paths, d.over.dataset.path);
}

addEventListener('pointermove', (ev) => {
	if (!drag || ev.pointerId !== drag.id) return;
	const far = Math.hypot(ev.clientX - drag.x0, ev.clientY - drag.y0);
	if (!drag.on) {
		/* A finger that moves before it is held is scrolling */
		if (drag.touch) { if (far > 10) dragEnd(false); else dragAt(ev.clientX, ev.clientY); return; }
		if (far < 6) return;
		dragLift();
	}
	dragAt(ev.clientX, ev.clientY);
});
addEventListener('pointerup', (ev) => { if (drag && ev.pointerId === drag.id) dragEnd(true); });
addEventListener('pointercancel', (ev) => { if (drag && ev.pointerId === drag.id) dragEnd(false); });
addEventListener('keydown', (ev) => { if (ev.key === 'Escape' && drag) dragEnd(false); });
/* While a finger drags, the page does not scroll under it */
addEventListener('touchmove', (ev) => { if (drag && drag.on) ev.preventDefault(); }, { passive: false });
addEventListener('click', (ev) => {
	if (!swallowClick) return;
	ev.preventDefault();
	ev.stopPropagation();
	swallowClick = false;
}, true);

/* Each into folder `to`, one at a time; what went is said, and what did not,
 * with the handheld's reason ("Contra.nes is already in Hacks") */
async function moveAll(paths, to) {
	let moved = 0, why = '';
	for (const p of paths) {
		try {
			await api('POST', '/api/move?p=' + enc(p) + '&to=' + enc(to));
			moved++;
			selected.delete(p);
		} catch (err) {
			if (err.message === 'unauthorized') return;
			why = err.message;
		}
	}
	if (moved) toast(t('move.done', nameOf(to)));
	if (why) toast(why, true);
	await go(cwd, 'none');
}

/* A phone's ⋯: everything the sidebar holds but the folders. */
$('more').addEventListener('click', () => {
	const nodes = [item(t('tool.new_folder'), 'newfolder', newFolder)];
	const up = item(t('tool.upload_folder'), 'upfolder', () => $('folderpicker').click());
	nodes.push(up);
	if (canTidy(cwd)) nodes.push(item(t('tool.tidy'), 'tidy', tidyShow));
	/* The update, covers, the key, the card and the logs: Settings */
	nodes.push(item(t('settings'), 'gear', () => openSettings()));
	const fine = document.createElement('div');
	fine.className = 'sub';
	if (info.ssh) {
		const h = document.createElement('p');
		h.textContent = t('ssh.note_menu');
		const c = $('ssh').querySelector('.cmd').cloneNode(true);
		c.querySelector('button').removeAttribute('id');
		c.querySelector('button').onclick = copySsh;
		fine.append(h, c);
	}
	if (fine.childNodes.length) nodes.push(fine);
	openMenu(nodes, $('more'));
});

/* ---- dialogs ----------------------------------------------------------- *
 *
 * Rename, New folder and the question before a delete, in the page rather
 * than in the browser's prompt(): those are unstyled, and Safari on a phone
 * draws them over the address bar. ask() resolves to the field's text (or
 * true, with no field) on the yes, null on Cancel or Escape. */
let dlgDone = null;

function ask({ title, text = '', field = null, yes, danger = false, alone = false }) {
	closeDialog(null);
	/* `alone`: a note with one button, nothing to cancel */
	$('dlgno').hidden = alone;
	$('dlghead').textContent = title;
	$('dlgtext').textContent = text;
	const f = $('dlgfield');
	f.hidden = field === null;
	f.value = field || '';
	f.setAttribute('aria-label', title);
	const y = $('dlgyes');
	y.textContent = yes;
	y.className = 'btn ' + (danger ? 'danger' : 'pri');
	const from = document.activeElement;
	$('dialog').hidden = false;
	document.body.classList.add('held');
	if (field !== null) {
		f.focus();
		/* A name without its extension, which is the part anyone renames. */
		const dot = f.value.lastIndexOf('.');
		f.setSelectionRange(0, dot > 0 ? dot : f.value.length);
	} else y.focus();
	return new Promise((ok) => {
		dlgDone = (v) => { if (from && from.focus && document.contains(from)) from.focus({ preventScroll: true }); ok(v); };
	});
}

function closeDialog(v) {
	if ($('dialog').hidden) return;
	$('dialog').hidden = true;
	document.body.classList.remove('held');
	const d = dlgDone;
	dlgDone = null;
	if (d) d(v);
}

$('dlgform').addEventListener('submit', (e) => {
	e.preventDefault();
	closeDialog($('dlgfield').hidden ? true : $('dlgfield').value.trim());
});
$('dlgno').onclick = () => closeDialog(null);
$('dialog').addEventListener('click', (e) => { if (e.target === $('dialog')) closeDialog(null); });
$('dialog').addEventListener('keydown', (e) => { if (e.key === 'Escape') { e.preventDefault(); closeDialog(null); } });

/* ---- the operations ---------------------------------------------------- */

async function rename(e) {
	const to = await ask({ title: e.dir ? t('rename.folder') : t('rename.file'), field: e.name, yes: t('act.rename') });
	if (!to || to === e.name) return;
	try {
		await api('POST', '/api/rename?p=' + enc(e.path) + '&to=' + enc(to));
		toast(t('ops.renamed'));
		await go(cwd, 'none');
	} catch (err) { toast(err.message, true); }
}

/* A file, or an empty folder, is asked about by name. A folder with anything
 * in it is asked about by what is in it, which the device counts first - and
 * the device also says whether it may go whole at all: an album or a book can,
 * a console's folder cannot (see xfer_delete_rule). The server enforces both;
 * this is only so the question asked is the right one. */
async function del(e) {
	if (e.dir) {
		let c;
		try {
			c = await (await api('GET', '/api/count?p=' + enc(e.path))).json();
		} catch (err) { toast(err.message, true); return; }
		if (c.files + c.folders > 0) {
			if (c.rule !== 'all') { toast(t('del.why', e.name, c.why), true); return; }
			if (c.too_many) {
				toast(t('del.too_much', e.name), true);
				return;
			}
			const files = tn('del.files', c.files);
			const what = c.folders ? tn('del.files_in', c.folders, files, c.folders) : files;
			if (!await ask({ title: t('del.head', e.name), text: t('del.everything', what),
			                 yes: t('act.delete'), danger: true })) return;
			return removeEntry(e, true);
		}
	}
	if (!await ask({ title: t('del.head', e.name), text: t('del.undone'), yes: t('act.delete'), danger: true }))
		return;
	return removeEntry(e, false);
}

async function removeEntry(e, all) {
	try {
		await api('POST', '/api/delete?p=' + enc(e.path) + (all ? '&all=1' : ''));
		toast(t('ops.deleted'));
		drawRoom();
		await go(cwd, 'none');
	} catch (err) { toast(err.message, true); }
}

async function newFolder() {
	const name = await ask({ title: t('newfolder.head', nameOf(cwd)), field: '', yes: t('newfolder.make') });
	if (!name) return;
	try {
		await api('POST', '/api/mkdir?p=' + enc(cwd + '/' + name));
		await go(cwd, 'none');
	} catch (err) { toast(err.message, true); }
}
$('newfolder').onclick = newFolder;

/* ---- Tidy names --------------------------------------------------------
 *
 * A music or audiobook folder's names as MP4's shelf shows them, made real
 * on the card: "01..American.Music.mp3" to "01 American Music.mp3", and the
 * folder to its album's name. The device works the names out with the
 * shelf's own rules (/api/tidy), so the page and the shelf cannot disagree,
 * and every rename is shown here before any is made. One that would land on
 * a name already there is shown as skipped, and the device never makes it. */
function canTidy(path) { return /^(music|books)\/./.test(path); }

async function tidyShow() {
	const here = cwd;
	let plan;
	try {
		plan = await (await api('GET', '/api/tidy?p=' + enc(here))).json();
	} catch (err) { toast(err.message, true); return; }
	if (here !== cwd) return;                     /* moved on meanwhile */
	if (!plan.items.length) { toast(t('tidy.already')); return; }
	const doing = plan.items.filter((i) => !i.skip).length;
	$('tidyhead').textContent = doing === plan.items.length ? tn('tidy.head', doing)
		: t('tidy.head_some', doing, plan.items.length);
	const ul = $('tidylist');
	ul.replaceChildren();
	/* The folder first, then its files, as they will read */
	for (const i of plan.items.slice().sort((a, b) => b.folder - a.folder)) {
		const li = document.createElement('li');
		if (i.skip) li.className = 'skip';
		const from = document.createElement('span');
		from.className = 'from';
		from.textContent = i.folder ? t('tidy.folder', i.from) : i.from;
		const to = document.createElement('span');
		to.className = 'to';
		to.textContent = i.skip ? t('tidy.skipped', i.to) : i.to;
		li.append(from, to);
		ul.append(li);
	}
	$('tidygo').disabled = doing === 0;
	$('tidypanel').hidden = false;
	$('tidypanel').scrollIntoView({ block: 'nearest' });
}

async function tidyGo() {
	const here = cwd;
	$('tidygo').disabled = true;
	try {
		const res = await (await api('POST', '/api/tidy?p=' + enc(here))).json();
		toast(res.skipped ? t('tidy.done_skipped', res.renamed, res.skipped) : t('tidy.done', res.renamed));
		/* The folder may have a new name: go to it */
		await go(here.slice(0, here.lastIndexOf('/') + 1) + res.folder, 'replace');
	} catch (err) {
		toast(err.message, true);
		$('tidygo').disabled = false;
	}
}

$('tidy').onclick = tidyShow;
$('tidycancel').onclick = () => { $('tidypanel').hidden = true; };
$('tidygo').onclick = tidyGo;

/* ---- SSH --------------------------------------------------------------- */

async function copySsh(ev) {
	const code = ((ev && ev.target.closest('.cmd')) || $('ssh')).querySelector('code');
	try {
		await navigator.clipboard.writeText(code.textContent);
		toast(t('ssh.copied'));
	} catch (err) {
		/* No clipboard where the page is not https: select it instead */
		const r = document.createRange();
		r.selectNodeContents(code);
		getSelection().removeAllRanges();
		getSelection().addRange(r);
		toast(t('ssh.selected'));
	}
}
$('sshcopy').onclick = copySsh;

/* ---- uploading --------------------------------------------------------- */

const queue = [];
let sending = false;
/* Why nothing more goes: the handheld is closing File Transfer after the
 * file on its way (its stop question, DESIGN.md 11.14), or it went. What has
 * not gone stays on this computer, and the panel says which. */
let held = '';
let heldGone = false;     /* and it went, rather than closing after the file */

/* ---- room on the card -------------------------------------------------
 *
 * 2026-11-06: "are we checking if the files we are sending can
 * actually fit in the free space?" The device says how much room the card
 * has (GET /api/state) and keeps a little of it for saves, which no upload
 * gets (HARE_RESERVE); the page adds a batch up against the rest before it
 * sends a byte, and offers the files that fit, in their order. The device
 * still checks each file as it comes, which is the last word. */
let room = null;          /* { free, total, reserve } as the device last said */

async function state() {
	const r = await fetch('/api/state', { credentials: 'same-origin', cache: 'no-store' });
	if (r.status === 401) { showGate(t('gate.session_ended')); throw new Error('unauthorized'); }
	if (!r.ok) throw new Error('HTTP ' + r.status);
	return r.json();
}

/* Room for files: free less what the card keeps */
const usable = (st) => Math.max(0, st.free - st.reserve);

/* The free space under the card's folders, after anything that changes it */
async function drawRoom() {
	try { room = await state(); } catch (e) { return; }
	if (room.free < 0 || room.total <= 0) return;
	const free = usable(room);
	$('room').hidden = false;
	put($('roomline'), t('room.free'), el('b', '', human(free)), human(room.total));
	$('roombar').style.width = Math.min(100, Math.max(0, 100 - free / room.total * 100)) + '%';
	$('cardroom').textContent = t('card.room', human(free), human(room.total), human(room.reserve));
}

/* Bytes the queue has still to send */
const pending = () => queue.reduce((n, j) => n + (j.state === 'waiting' ? j.file.size
	: j.state === 'sending' ? j.file.size - (j.loaded || 0) : 0), 0);

/* The files that fit, in their order, or all of them; null when the player
 * said no. Asked before anything of a batch is queued or made. */
async function fitting(files) {
	let st;
	try { st = await state(); } catch (e) {
		if (e.message === 'unauthorized') return null;
		return files;                /* cannot tell: the device decides each */
	}
	room = st;
	if (st.free < 0) return files;
	const have = Math.max(0, usable(st) - pending());
	const need = files.reduce((n, f) => n + f.size, 0);
	if (need <= have) return files;
	const fit = [];
	let left = have;
	for (const f of files) if (f.size <= left) { fit.push(f); left -= f.size; }
	const text = files.length === 1 ? t('fit.one', human(need), human(have))
		: t('fit.many', files.length, human(need), human(have));
	if (!fit.length) {
		await ask({ title: t('fit.head'), text: t('fit.none', text), yes: t('act.ok'), alone: true });
		return null;
	}
	const ok = await ask({ title: t('fit.head'), text,
		yes: fit.length === 1 ? t('fit.send_one') : t('fit.send_some', fit.length) });
	return ok ? fit : null;
}

/* A new batch: what the last one left on the panel goes */
function fresh() {
	if (sending) return;
	held = '';
	queue.length = 0;
	samples.length = 0;
}

/* `dest` is the folder these files are going to, defaulting to the one on
 * screen. It is bound HERE, into each job, rather than read at send time.
 *
 * Reading it later was a real bug: `next()` built every URL from the live
 * `cwd`, so navigating during a multi-file upload sent the rest of the queue
 * wherever you had gone. A 40-file batch, walked away from after file 5, put
 * 35 files somewhere nobody chose - and then the refresh at the end of the
 * queue listed the folder you had moved to, so the files that landed there
 * looked like the ones you meant. Binding it per job also makes a per-file
 * destination possible, which is what dropping onto a folder row uses. */
async function enqueue(list, dest) {
	const dir = dest === undefined ? cwd : dest;
	let files = [...list];       /* now: a drop's files go with its event */
	if (!dir) { toast(t('queue.open_folder'), true); return; }
	if (held && sending) { toast(held, true); return; }
	files = await fitting(files);
	if (!files || !files.length) return;
	fresh();
	for (const f of files) queue.push({ file: f, dir, pct: 0, state: 'waiting' });
	drawQueue();
	if (!sending) next();
}

/* ---- folders --------------------------------------------------------------
 *
 * A dropped folder arrives as one empty item in dataTransfer.files, which is
 * all the page used to read, so dropping an album uploaded nothing. The
 * entries API walks it instead: every folder in it is made on the device, in
 * order, parents first, and every file queued into its own folder. Dot files
 * and folders are skipped - a Mac puts .DS_Store in every folder it has shown,
 * and ._ files beside every file it copied. */

/* But not .media, where a console's covers are and an album folder's: it is
 * a dot name too, so a console's .media dropped onto its folder uploaded
 * nothing, and said nothing. Exactly .media, wherever it is in what was
 * dropped or picked; everything hidden inside it is still left out. Safari
 * leaves every dot name out of a folder before the page sees it, so from
 * Safari the covers go as .media itself, dropped or picked on its own. */
const leftOut = (name) => name.startsWith('.') && name !== '.media';

/* A tree from a drop: { dirs, files }, dirs in the order they must be made
 * and each file with the folder it belongs in, both relative to the drop.
 * null when the browser has no entries API, and the drop is read as files. */
function walkDrop(dt) {
	/* Read now: the items are only there while the drop event runs, and the
	 * walk below awaits. */
	const entries = [];
	for (const it of dt.items || []) {
		const en = it.webkitGetAsEntry && it.webkitGetAsEntry();
		if (en) entries.push(en);
	}
	if (!entries.length) return null;
	return (async () => {
		const dirs = [], files = [];
		const walk = async (en, rel) => {
			if (leftOut(en.name)) return;
			if (en.isFile) {
				files.push({ file: await new Promise((ok, no) => en.file(ok, no)), rel });
			} else if (en.isDirectory) {
				const here = rel ? rel + '/' + en.name : en.name;
				const reader = en.createReader();

				dirs.push(here);
				/* readEntries hands a folder over in batches, empty at the end. */
				for (;;) {
					const batch = await new Promise((ok, no) => reader.readEntries(ok, no));
					if (!batch.length) break;
					for (const c of batch) await walk(c, here);
				}
			}
		};
		for (const en of entries) await walk(en, '');
		return { dirs, files };
	})();
}

/* The same tree from the Upload folder picker, whose files carry their path
 * inside the chosen folder as webkitRelativePath. */
function picked(list) {
	const dirs = [], files = [], seen = new Set();
	for (const f of list) {
		const parts = (f.webkitRelativePath || f.name).split('/');
		if (parts.some(leftOut)) continue;
		for (let i = 1; i < parts.length; i++) {
			const d = parts.slice(0, i).join('/');
			if (!seen.has(d)) { seen.add(d); dirs.push(d); }
		}
		files.push({ file: f, rel: parts.slice(0, -1).join('/') });
	}
	return { dirs, files };
}

/* A drop, onto the list or onto a folder row: a tree when it holds folders,
 * files as before when it does not. */
function dropped(dt, dest) {
	const tree = walkDrop(dt);
	if (!tree) { if (dt.files.length) enqueue(dt.files, dest); return; }
	tree.then((t) => enqueueTree(t, dest)).catch((err) => toast(err.message, true));
}

/* Make one folder; one that is already there is fine - a second album
 * dropped into an artist's folder reuses it. */
async function makeDir(path) {
	const r = await fetch('/api/mkdir?p=' + enc(path), { method: 'POST', credentials: 'same-origin' });
	if (r.status === 401) { showGate(t('gate.session_ended')); throw new Error('unauthorized'); }
	if (!r.ok && r.status !== 409) throw new Error((await r.text()).split('\n')[1] || r.statusText);
}

async function enqueueTree(tree, dest) {
	const dir = dest === undefined ? cwd : dest;
	if (!dir) { toast(t('queue.open_folder'), true); return; }
	if (held && sending) { toast(held, true); return; }
	if (!tree.dirs.length) { enqueue(tree.files.map((f) => f.file), dir); return; }
	/* Room first, before a folder is made for files that will not go */
	const ok = await fitting(tree.files.map((f) => f.file));
	if (!ok) return;
	const keep = new Set(ok);
	const files = tree.files.filter((f) => keep.has(f.file));
	try {
		for (const d of tree.dirs) await makeDir(dir + '/' + d);
	} catch (err) { toast(err.message, true); return; }
	if (!files.length) { toast(t('ops.folder_made')); go(cwd, 'none').catch(() => {}); return; }
	fresh();
	for (const { file, rel } of files)
		queue.push({ file, dir: rel ? dir + '/' + rel : dir, pct: 0, state: 'waiting' });
	drawQueue();
	if (!sending) next();
}

/* Nothing more goes: the files waiting stay on this computer. `gone`: the
 * handheld closed it, rather than closing after the file on its way. */
function hold(why, gone) {
	if (held) return;
	held = why;
	heldGone = !!gone;
	for (const j of queue) if (j.state === 'waiting') j.state = 'held';
	drawQueue();
}

/* While files go, the device is asked every two seconds whether it is
 * closing File Transfer after the one on its way, and so whether it is still
 * there at all. */
let watchTimer = 0;
function watch(on) {
	clearInterval(watchTimer);
	if (!on) return;
	watchTimer = setInterval(async () => {
		try {
			const st = await state();
			room = st;
			if (st.closing) hold(t('queue.closing'));
		} catch (e) {
			if (e.message !== 'unauthorized') hold(t('queue.closed'), true);
		}
	}, 2000);
}

/* A tab closed while files go cuts the one on its way: asked first, as the
 * handheld asks (the browser's own question; it words it itself) */
addEventListener('beforeunload', (e) => {
	if (!sending) return;
	e.preventDefault();
	e.returnValue = '';
});

function next() {
	const job = held ? null : queue.find((j) => j.state === 'waiting');
	if (!job) {
		sending = false;
		watch(false);
		/* Left on screen for a moment so the last line is readable, rather
		 * than the panel vanishing the instant the final byte lands; kept
		 * while anything did not go, so it can be seen which. */
		const clean = queue.every((j) => j.state === 'done');
		if (clean) setTimeout(() => { if (!sending && queue.every((j) => j.state === 'done')) {
			queue.length = 0; samples.length = 0; drawQueue();
		} }, 2500);
		drawQueue();
		if (!inSettings) go(cwd, 'none').catch(() => {});
		drawArt();
		drawRoom();
		return;
	}
	if (!sending) watch(true);
	sending = true;
	job.state = 'sending';
	job.loaded = 0;
	sample(true);
	drawQueue();

	const xhr = new XMLHttpRequest();
	const put = trailed('/api/file?p=' + enc(job.dir + '/' + job.file.name), job.file);
	xhr.open('PUT', put.url);
	xhr.upload.onprogress = (ev) => {
		if (!ev.lengthComputable) return;
		/* The file's bytes, not a trailer's after them */
		const at = Math.min(ev.loaded, job.file.size);
		sent += Math.max(0, at - (job.loaded || 0));
		job.loaded = at;
		job.pct = Math.round(ev.loaded / ev.total * 100);
		sample(false);
		drawQueue();
	};
	xhr.onload = () => {
		const why = (xhr.responseText || '').split('\n')[1] || ('HTTP ' + xhr.status);
		if (xhr.status === 200) { job.state = 'done'; job.pct = 100; }
		else if (xhr.status === 503) {
			/* Refused before a byte went: the handheld is closing */
			job.state = 'waiting';
			hold(t('queue.closing_now'));
		} else {
			job.state = 'failed';
			job.why = why;
		}
		drawQueue();
		next();
	};
	xhr.onerror = () => {
		job.state = 'failed';
		job.why = t('queue.lost');
		drawQueue();
		/* Gone, rather than a hiccup: nothing more is tried */
		state().then(() => next(), (e) => {
			if (e.message !== 'unauthorized') hold(t('queue.closed'), true);
			next();
		});
	};
	xhr.send(put.body);
}

/* ---- the time left -----------------------------------------------------
 *
 * For the whole queue (2026-11-06: "can the current transfers
 * component in the portal show an ETA for all file transfer to complete?"):
 * the bytes left over the speed of the last 20 seconds, which a single slow
 * second does not move much, and the words changed at most once a second,
 * so the line reads rather than flickers. Nothing until there are three
 * seconds to go by; under a minute, seconds in fives. */
const WINDOW_MS = 20000;
const samples = [];       /* [ms, bytes sent] */
let sent = 0;             /* every byte that went out, a count that only grows */
let etaWords = '', etaAt = 0;

function sample(force) {
	const now = performance.now();
	const last = samples[samples.length - 1];
	if (!force && last && now - last[0] < 250) return;
	samples.push([now, sent]);
	while (samples.length > 2 && now - samples[1][0] > WINDOW_MS) samples.shift();
}

function eta(left) {
	const now = performance.now();
	if (etaWords && now - etaAt < 1000) return etaWords;
	etaAt = now;
	const a = samples[0], b = samples[samples.length - 1];
	const v = a && b && b[0] - a[0] >= 3000 ? (b[1] - a[1]) / ((b[0] - a[0]) / 1000) : 0;
	if (!(v > 0)) return (etaWords = '');
	const s = left / v;
	etaWords = s < 60 ? t('eta.s', Math.max(5, Math.ceil(s / 5) * 5))
		: s < 3600 ? t('eta.min', Math.round(s / 60))
		: t('eta.h', Math.floor(s / 3600), Math.round(s % 3600 / 60));
	return etaWords;
}

/* The uploads, in three places from one list: the sidebar's panel on a wide
 * screen, a phone's one line along the bottom, and the sheet that line opens
 * into. "Sending 2 of 3" while they go, then what became of them. */
const count = (state) => queue.filter((j) => j.state === state).length;

function queueHead() {
	const done = count('done'), failed = count('failed'), kept = count('held');
	const going = queue.some((j) => j.state === 'sending' || j.state === 'waiting');
	if (held && going) return heldGone ? t('queue.stopped') : t('queue.stopping');
	if (!going)
		return failed && kept ? t('queue.sent_failed_kept', done, failed, kept)
		     : failed ? t('queue.sent_failed', done, failed)
		     : kept ? t('queue.sent_kept', done, kept) : t('queue.sent', done);
	return t('queue.sending', done + failed + 1, queue.length);
}

/* What is left, in size and time, while they go */
function queueLeft() {
	if (!sending) return '';
	const left = pending();
	const when = held ? '' : eta(left);
	return t('queue.left', human(left)) + (when ? ' · ' + when : '');
}

/* How far the whole queue is, by bytes, for the bars */
function queuePct() {
	let all = 0, done = 0;
	for (const j of queue) {
		if (j.state === 'held') continue;
		all += j.file.size;
		done += j.state === 'done' ? j.file.size : j.state === 'sending' ? (j.loaded || 0) : 0;
	}
	return all ? Math.round(done / all * 100) : 100;
}

function jobList() {
	const ul = document.createElement('ul');
	ul.className = 'jobs';
	for (const j of queue) {
		const li = document.createElement('li');
		li.className = 'job ' + j.state;
		const n = document.createElement('span');
		n.className = 'n';
		n.textContent = j.file.name;
		/* Where it is going, shown only when that is no longer what is on
		 * screen - after navigating away, or after a drop onto a folder row. */
		if (j.dir !== cwd) {
			const d = document.createElement('span');
			d.className = 'dest';
			d.textContent = ' → ' + (j.dir.split('/').pop() || j.dir);
			d.title = j.dir;
			n.append(d);
		}
		const p = document.createElement('span');
		p.className = 'p';
		p.textContent = j.state === 'failed' ? j.why
		              : j.state === 'done' ? t('job.done')
		              : j.state === 'waiting' ? t('job.waiting')
		              : j.state === 'held' ? t('job.not_sent')
		              : pct(j.pct);
		li.append(n, p);
		if (j.state !== 'done' && j.state !== 'held') {
			const bar = document.createElement('div');
			bar.className = 'bar';
			const fill = document.createElement('span');
			fill.style.width = (j.state === 'failed' ? 100 : j.pct) + '%';
			bar.append(fill);
			li.append(bar);
		}
		ul.append(li);
	}
	return ul;
}

function drawQueue() {
	const on = queue.length > 0;
	$('queue').hidden = !on;
	$('dock').hidden = !on;
	$('app').classList.toggle('docked', on);
	if (!on) {
		if (!$('menu').hidden && $('menu').querySelector('.jobs')) closeMenu(true);
		return;
	}
	const head = queueHead(), left = queueLeft(), all = queuePct();
	$('queuehead').textContent = head;
	/* What is left and the time, one bar for them all; or, stopped by the
	 * handheld, why the rest stay here */
	$('queueleft').textContent = held || left;
	$('queueleft').hidden = !(held || left);
	$('queueleft').classList.toggle('stopping', !!held);
	$('queuetotal').hidden = !sending;
	$('queuebar').style.width = all + '%';
	$('queuelist').replaceChildren(...jobList().childNodes);

	const top = document.createElement('div');
	top.className = 'top';
	const h = document.createElement('span');
	/* A phone's one line: how far, and the time left */
	const when = sending && !held ? eta(pending()) : '';
	h.textContent = sending && !held
		? t('queue.n_of', count('done') + count('failed') + 1, queue.length) + (when ? ' · ' + when : '')
		: head;
	const p = document.createElement('span');
	p.className = 'p';
	p.textContent = pct(all);
	top.append(h, p);
	const bar = document.createElement('div');
	bar.className = 'bar';
	const fill = document.createElement('span');
	fill.style.width = all + '%';
	bar.append(fill);
	$('dock').replaceChildren(top, bar);
	$('dock').setAttribute('aria-label', t('queue.dock', head, pct(all)));

	/* The sheet, if it is open, follows along */
	const open = !$('menu').hidden && $('menu').querySelector('.jobs');
	if (open) {
		open.replaceWith(jobList());
		$('menu').querySelector('.who').textContent = head + (held || left ? ' · ' + (held || left) : '');
	}
}

$('dock').addEventListener('click', () => {
	const who = document.createElement('div');
	who.className = 'who';
	const left = held || queueLeft();
	who.textContent = queueHead() + (left ? ' · ' + left : '');
	openMenu([who, jobList()], $('dock'));
});

$('picker').onchange = (e) => { const f = [...e.target.files]; e.target.value = ''; enqueue(f); };
$('folderpicker').onchange = (e) => {
	const tree = picked(e.target.files);
	e.target.value = '';
	enqueueTree(tree);
};
/* The labels that open the pickers, by keyboard as by a press */
for (const l of document.querySelectorAll('label[for]')) {
	l.addEventListener('keydown', (e) => {
		if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); $(l.htmlFor).click(); }
	});
}

/* Drag and drop, counted rather than toggled: dragenter and dragleave fire for
 * every child element the pointer crosses, so a boolean flickers the frame
 * off the moment the pointer moves over a row inside it. */
let dragDepth = 0;

/* The hint is there before anything is dragged - a capability shown only to
 * somebody who has already found it is not shown - and says where a drop
 * lands. Dragging changes its emphasis, not its words. */
function refreshDropHint() {
	$('dropmsg').textContent = t('list.drop_hint', nameOf(cwd));
}
function dragUI(on) {
	$('drop').hidden = !on;
	$('dropmsg').classList.toggle('armed', on);
}

addEventListener('dragenter', (e) => {
	e.preventDefault();
	if (++dragDepth === 1 && !$('app').hidden && !inSettings) dragUI(true);
});
addEventListener('dragover', (e) => e.preventDefault());
addEventListener('dragleave', () => { if (--dragDepth <= 0) { dragDepth = 0; dragUI(false); } });
addEventListener('drop', (e) => {
	e.preventDefault();
	dragDepth = 0;
	dragUI(false);
	/* Settings has no folder to drop into: the update's own zone takes its
	 * files, and a drop anywhere else there is nothing */
	if (!$('app').hidden && !inSettings) dropped(e.dataTransfer);
});


/* ---- box art ----------------------------------------------------------- */

/* USB File Transfer only: the device behind the cable has no network, so this
 * page fetches libretro's covers and the device decides which game gets which
 * (src/hareart.h). Where the device fetches its own, /api/art/wanted answers
 * 404 and the button never shows.
 *
 * From libretro's GitHub mirror, not thumbnails.libretro.com, which the device
 * itself uses: that server does not let a web page read what it sends, and
 * GitHub does. Its listing is a collection's whole Named_Boxarts folder, two
 * requests a collection; GitHub allows 60 an hour without an account. */
const GH_API = 'https://api.github.com/repos/libretro-thumbnails/';
const GH_RAW = 'https://raw.githubusercontent.com/libretro-thumbnails/';
/* libretro's No-Intro lists, for the checksum pass, as the device's own Box
 * Art fetches them. */
const DAT = 'https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/';
let artBusy = false;

/* libretro's repos are its collections' names with underscores for spaces:
 * "Sega - Mega Drive - Genesis" is Sega_-_Mega_Drive_-_Genesis. */
const repoOf = (collection) => collection.replace(/ /g, '_');

/* null where the device says no (404): the button stays hidden. */
async function wanted(what) {
	const r = await fetch('/api/' + what + '/wanted', { credentials: 'same-origin' });
	if (!r.ok) return null;
	return r.json();
}

/* What is missing, games and albums in one line, in the sidebar (and a
 * phone's ⋯), and left alone while a run is saying how it is going. Asked
 * after the PIN, after an upload and after a run, not on every folder: the
 * device counts every shelf to answer. */
async function drawArt() {
	if (artBusy) return;
	let games = null, albums = null;
	try { [games, albums] = await Promise.all([wanted('art'), wanted('albums')]); }
	catch (e) { /* leave it as it is */ }
	if (artBusy) return;

	const g = games ? games.shelves.reduce((n, s) => n + s.missing, 0) : 0;
	const nc = games ? games.shelves.length : 0;
	const a = albums ? albums.albums.length : 0;
	const parts = [];

	/* Both counts every time, so a zero reads as done rather than forgotten. */
	if (games) parts.push(!g ? t('art.games_done') : g === 1 ? t('art.game_missing', g)
		: tn('art.games_missing', nc, g, nc));
	/* Album Art's own rule: no cover, or one too small to be sharp. */
	if (albums) parts.push(!a ? t('art.albums_done') : tn('art.albums_missing', a));
	$('art').hidden = !games && !albums;
	$('artgo').disabled = !g && !a;
	$('artmsg').textContent = sentences(...parts, lastRun);
}

/* What went wrong in the last run, kept beside the count once it is over: a
 * toast is gone in seconds, and "3 failed" with no names left nothing to go
 * on (2026-10-05). */
let lastRun = '';
const failNote = (fails) => fails.length
	? tn('art.last_failed', fails.length, fails.length, fails.join(t('art.sep'))) : '';
/* Sentences one after another, as the language spaces them (Chinese does not) */
const sentences = (...xs) => xs.filter(Boolean).join(t('gap'));

/* An error with the status that caused it, which the words do not carry
 * reliably in every language */
const answered = (words, status) => Object.assign(new Error(words), { status });

async function github(url) {
	const r = await fetch(url);
	if (r.status === 403 || r.status === 429) throw new Error(t('gh.limit'));
	if (!r.ok) throw answered(t('gh.answered', r.status), r.status);
	return r.json();
}

/* A collection's box art names, without their .png. */
async function coverNames(repo) {
	const top = await github(GH_API + repo + '/git/trees/master');
	const dir = top.tree.find((e) => e.path === 'Named_Boxarts');
	if (!dir) return [];
	const box = await github(GH_API + repo + '/git/trees/' + dir.sha);
	return box.tree.filter((e) => e.path.endsWith('.png')).map((e) => e.path.slice(0, -4));
}

const say = (t) => { $('artmsg').textContent = t; };

/* One cover from the mirror, as a PNG blob. libretro stores a duplicate cover
 * as a symbolic link, and GitHub's raw files answer a link with its text: the
 * name of the file it points at, in the same folder. That text landed as four
 * "covers" on 2026-10-05, so a reply that is not a PNG is followed as a link,
 * a few hops at most, and never uploaded as it is. */
async function coverBlob(repo, name) {
	for (let hop = 0; hop < 4; hop++) {
		const r = await fetch(GH_RAW + repo + '/master/Named_Boxarts/' + enc(name) + '.png');
		if (!r.ok) throw answered(t('gh.answered', r.status), r.status);
		const blob = await r.blob();
		const head = new Uint8Array(await blob.slice(0, 4).arrayBuffer());

		if (head[0] === 0x89 && head[1] === 0x50 && head[2] === 0x4E && head[3] === 0x47)
			return blob;
		const link = (await blob.text()).trim();
		if (!link || link.length > 300 || link.includes('\n') || !link.endsWith('.png'))
			throw new Error(t('art.not_picture'));
		name = link.replace(/^.*\//, '').slice(0, -4);
	}
	throw new Error(t('art.links_nowhere'));
}

/* libretro's file names are No-Intro's with these characters as _. */
const libretroName = (name) => name.replace(/[&*\/:`<>?\\|]/g, '_');

/* Games' covers, best-first (Eric, 2026-10-05), counted into `n`:
 *
 *   1. the game's own name, asked for directly - a file named the No-Intro way
 *      already is the cover's name
 *   2. its checksum's No-Intro name, from the console's No-Intro list, asked
 *      for directly
 *   3. only for what is left, the collection's listing, matched by name and
 *      then loosely
 *
 * The first two use only raw.githubusercontent.com, which has no hourly cap;
 * the listing is GitHub's API, 60 requests an hour, which runs out after a
 * few full runs. A miss by the game's own name is ordinary and not reported;
 * a cover the checksum or the listing named that then would not come or would
 * not go onto the card is. The checksum step's failures were silent at first,
 * and the mono Neo Geo Pocket's covers went missing with nothing to say why
 * (2026-10-05). */
async function getArt(n, fails, notes) {
	const w = await wanted('art');
	/* The listing's failure, once seen: the rest of the run goes on without
	 * it. GitHub's hour running out used to end the whole run, so a console
	 * after it never had the two ways that need no listing (2026-10-05, both
	 * Neo Geo Pockets). */
	let noList = '', waiting = 0;

	if (!w) return;
	for (const s of w.shelves) {
		const left = new Set(s.games);
		const cover = (stem) => 'roms/' + s.folder + '/.media/' + stem + '.png';

		/* The second collection (a console's disc games) only for what the
		 * first left. */
		for (const c of s.collections) {
			if (!left.size) break;
			const repo = repoOf(c);
			const cover1 = async (stem, name, report) => {
				try {
					await putFile(cover(stem), await coverBlob(repo, name));
					n.games++;
					left.delete(stem);
				} catch (err) {
					if (err.message === 'unauthorized') throw err;
					/* 'some': all but a 404, which for a checksum's name only
					 * means libretro files that dump's cover under another name,
					 * and the listing finds it (three such, 2026-10-05). */
					if (report === true || (report === 'some' && err.status !== 404))
						fails.push(t('art.fail_item', stem, err.message));
				}
			};
			/* `how`: the line while it goes, from the shelf, the count and the game */
			const each = async (pairs, how, report) => {
				let i = 0;
				for (const [stem, name] of pairs) {
					if (!left.has(stem)) continue;
					say(how(s.name, ++i, pairs.length, stem));
					await cover1(stem, name, report);
				}
			};

			await each([...left].map((stem) => [stem, libretroName(stem)]),
			           (...a) => t('art.by_name', ...a), false);

			if (left.size) {
				say(t('art.looking_checksum', s.name));
				const dat = await fetch(DAT + enc(c) + '.dat').catch(() => null);
				if (dat && dat.ok) {
					const byCrc = await (await api('POST', '/api/art/nointro?s=' + enc(s.folder),
					                               await dat.text())).json();
					await each(byCrc.matches.map((m) => [m.stem, libretroName(m.name)]),
					           (...a) => t('art.by_checksum', ...a), 'some');
				}
			}

			if (left.size && noList) waiting += left.size;
			if (left.size && !noList) {
				say(t('art.looking_list', s.name, left.size));
				let names;
				try {
					names = await coverNames(repo);
				} catch (err) {
					noList = err.message;
					waiting += left.size;
					continue;
				}
				if (!names.length) continue;
				const byName = await (await api('POST', '/api/art/match?s=' + enc(s.folder),
				                                names.join('\n'))).json();
				await each(byName.matches.map((m) => [m.stem, m.name]), (...a) => t('art.from_list', ...a), true);
				/* And loosely, last of all: the device's loose pass, given no
				 * checksum list ("-") since the checksums have had their turn. */
				if (left.size) {
					const loose = await (await api('POST', '/api/art/crc?s=' + enc(s.folder),
					                               '-')).json();
					await each(loose.matches.map((m) => [m.stem, m.name]), (...a) => t('art.loosely', ...a), true);
				}
			}
		}
	}
	if (noList) notes.push(tn('art.waits', waiting, waiting, noList));
}

/* Album covers: MusicBrainz names the release, the device picks it by MP4's
 * track-count rule (the first hit is often a single of the same name), and
 * the Cover Art Archive has its front at 500px. MusicBrainz asks for a request
 * a second at most, so searches are spaced out. */
const CAA = 'https://coverartarchive.org/release-group/';
let mbLast = 0;

async function musicbrainz(url) {
	const wait = mbLast + 1100 - Date.now();
	if (wait > 0) await new Promise((ok) => setTimeout(ok, wait));
	mbLast = Date.now();
	const r = await fetch(url, { headers: { Accept: 'application/json' } });
	if (r.status === 503) throw new Error(t('mb.busy'));
	return r.ok ? r.text() : null;
}

async function getAlbums(n, fails) {
	const w = await wanted('albums');

	if (!w) return;
	for (let i = 0; i < w.albums.length; i++) {
		const al = w.albums[i];
		let pick = null;

		say(t('art.albums_progress', i + 1, w.albums.length, al.album));
		for (const url of al.searches) {
			const reply = await musicbrainz(url);
			if (!reply) continue;
			pick = await (await api('POST', '/api/albums/pick?id=' + al.id, reply)).json();
			if (pick.rg) break;
		}
		if (!pick || !pick.rg) continue;
		try {
			const img = await fetch(CAA + pick.rg + '/front-500');
			/* Known to MusicBrainz with no front cover given: not found. */
			if (img.status === 404) continue;
			if (!img.ok) throw answered(t('art.caa_answered', img.status), img.status);
			await putFile(pick.cover, await img.blob());
			n.albums++;
		} catch (err) {
			if (err.message === 'unauthorized') throw err;
			fails.push(t('art.fail_item', al.album, err.message));
		}
	}
}

/* One button, games and then albums. */
async function getCovers() {
	const n = { games: 0, albums: 0 };
	const fails = [], notes = [];

	artBusy = true;
	$('artgo').disabled = true;
	try {
		await getArt(n, fails, notes);
		await getAlbums(n, fails);
		const games = tn('art.n_games', n.games), albums = tn('art.n_albums', n.albums);
		toast(fails.length ? t('art.covers_failed', games, albums, fails.length) : t('art.covers', games, albums),
		      !n.games && !n.albums && fails.length > 0);
		lastRun = sentences(failNote(fails), ...notes);
	} catch (err) {
		if (err.message !== 'unauthorized') toast(err.message, true);
		lastRun = sentences(t('art.last_stopped', err.message), failNote(fails));
	} finally {
		artBusy = false;
		if (!inSettings) go(cwd, 'none').catch(() => {});
		drawArt();
	}
}

$('artgo').addEventListener('click', () => { if (!artBusy) getCovers(); });

/* ---- the SteamGridDB key ------------------------------------------------ *
 *
 * For the handheld's own Box Art, which asks SteamGridDB for the covers
 * libretro and ScreenScraper do not have (src/sgdb.h). Pasted here because a
 * key is 32 characters, and a paste beats a D-pad. The device checks it once
 * with SteamGridDB - over the USB cable it has no internet, so there it keeps
 * it for the next Box Art run to check - and never sends it back: the page
 * knows its last four characters and what is known about it, nothing more. */
let sgdbInfo = null;

async function sgdbAsk(op, body) {
	const r = await api(op ? 'POST' : 'GET', '/api/sgdb' + (op ? '/' + op : ''), body);
	return r.json();
}

const sgdbLine = (s) => s.checking ? t('sgdb.checking')
	: !s.key ? t('sgdb.none')
	: s.state === 'works' ? t('sgdb.works')
	: s.state === 'refused' ? t('sgdb.refused')
	: t('sgdb.unchecked');

async function drawSgdb(s) {
	if (!s) {
		try { s = await sgdbAsk(); } catch (e) { s = null; }
	}
	sgdbInfo = s;
	$('sgdb').hidden = !s;
	if (!s) return;
	$('sgdbgo').textContent = s.key ? t('sgdb.change') : t('sgdb.add');
	$('sgdbdel').hidden = !s.key;
	$('sgdbmsg').textContent = sentences(s.key ? t('sgdb.key', '••••' + s.tail) : '', sgdbLine(s));
}

async function setSgdb() {
	const key = await ask({ title: t('sgdb.key_head'), text: t('sgdb.paste'), field: '', yes: t('act.save') });
	if (!key) return;
	try {
		let s = await sgdbAsk('key', key);
		/* The check runs on the device; ask until it is over */
		for (let i = 0; s.checking && i < 60; i++) {
			drawSgdb(s);
			await new Promise((ok) => setTimeout(ok, 700));
			s = await sgdbAsk();
		}
		drawSgdb(s);
		if (s.said) toast(s.said, s.outcome === 'refused' || s.outcome === 'bad');
	} catch (err) { if (err.message !== 'unauthorized') toast(err.message, true); }
}

async function delSgdb() {
	if (!await ask({ title: t('sgdb.remove_head'), text: t('sgdb.remove_text'), yes: t('act.remove'), danger: true }))
		return;
	try {
		drawSgdb(await sgdbAsk('remove'));
		toast(t('sgdb.removed'));
	} catch (err) { if (err.message !== 'unauthorized') toast(err.message, true); }
}

$('sgdbgo').addEventListener('click', setSgdb);
$('sgdbdel').addEventListener('click', delSgdb);

/* ---- Update ZyperOS through the computer ---------------------------------
 *
 * Over the USB cable the handheld has no internet, and this computer has
 * (2026-11-06: "a way to update it through usb? so the portal fetches
 * the latest version and do the update?"). The handheld checks and installs
 * what this page hands it exactly as System Update does (docs/updates.md):
 * the release list's signature against the keys in its system image, every
 * file's SHA-256 against the list. The computer only carries the files, so
 * nothing here is trusted for having come from GitHub or from this page.
 *
 * What a page can get from GitHub: the release list (api.github.com answers a
 * web page), but not the release's files - github.com's download and the host
 * it sends to, release-assets.githubusercontent.com, give no CORS header, so
 * the browser keeps their bytes from any page (checked 2026-11-06). So the
 * page reads which release is newest and which files this handheld needs,
 * its button downloads exactly those to the computer, and they are dropped
 * here, or chosen, to go to the handheld. An image with no release key takes
 * no update at all, and the page says so (/api/update's "on" and "why"). */
const RELEASES = 'https://api.github.com/repos/tarikbc/ZyperOS/releases/latest';
let upd = null;           /* the handheld: /api/update */
let rel = null;           /* GitHub's newest: { version, notes, assets: name -> { size, url } } */
let relWhy = '';          /* why there is none */
const upFiles = new Map();  /* name -> the File dropped for it */
let upBusy = '';          /* what the page is doing for the update now */
let upPct = 0, upLeft = '';
let upPoll = 0;

/* 0.10.0 > 0.9.2; 0.2.0-beta.1 < 0.2.0, as the update program compares */
function vcmp(a, b) {
	const [na, pa = ''] = String(a).replace(/^v/, '').split('-', 2);
	const [nb, pb = ''] = String(b).replace(/^v/, '').split('-', 2);
	const x = na.split('.').map(Number), y = nb.split('.').map(Number);
	for (let i = 0; i < 3; i++) if ((x[i] || 0) !== (y[i] || 0)) return (x[i] || 0) - (y[i] || 0);
	if (!pa !== !pb) return pa ? -1 : 1;
	return pa.localeCompare(pb, undefined, { numeric: true });
}

async function newest() {
	if (rel || relWhy) return;
	try {
		/* window.ZYPEROS_RELEASES: another answer, for tools/portal-check.py */
		const r = await fetch(window.ZYPEROS_RELEASES || RELEASES, { headers: { Accept: 'application/vnd.github+json' } });
		if (r.status === 404) { relWhy = t('update.no_release'); return; }
		if (r.status === 403 || r.status === 429) { relWhy = t('gh.limit'); return; }
		if (!r.ok) { relWhy = t('gh.answered_stop', r.status); return; }
		const j = await r.json();
		const assets = {};
		for (const a of j.assets || []) assets[a.name] = { size: a.size, url: a.browser_download_url };
		rel = { version: String(j.tag_name || '').replace(/^v/, ''), assets };
	} catch (e) {
		relWhy = t('update.no_github');
	}
}

/* The files this handheld needs from the newest release: the folder's list
 * and pack, and on a card with two system slots the system's too, when the
 * release has them */
function needed() {
	if (!rel || !upd) return [];
	const v = rel.version, d = upd.device;
	const want = ['ZyperOS-' + d + '-update.txt', 'ZyperOS-v' + v + '-' + d + '-update.zpk'];
	const sys = ['ZyperOS-' + d + '-system.txt', 'ZyperOS-v' + v + '-' + d + '-system.zpk'];
	if (upd.slots && sys.every((n) => rel.assets[n])) want.push(...sys);
	return want.filter((n) => rel.assets[n]);
}

const el = (tag, cls, text) => {
	const e = document.createElement(tag);
	if (cls) e.className = cls;
	if (text !== undefined) e.textContent = text;
	return e;
};
const btn = (label, cls, fn) => {
	const b = el('button', 'btn ' + (cls || ''), label);
	b.type = 'button';
	b.onclick = fn;
	return b;
};
const flag = (text, bad) => {
	const f = el('div', 'flag' + (bad ? ' bad' : ''));
	f.append(el('p', '', text));
	return f;
};
const bar = (words, pct) => {
	const b = el('div', 'sendbar');
	const t = el('div', 'bar');
	const s = el('span');
	s.style.width = pct + '%';
	t.append(s);
	b.append(el('span', '', words), t);
	return b;
};

/* What the update program's states mean here */
const WORK = { checking: t('update.work_checking'), copying: t('update.work_copying'),
               downloading: t('update.work_unpacking'), verifying: t('update.work_verifying'),
               installing: t('update.work_installing') };

async function drawUpdate() {
	let d;
	try {
		const r = await fetch('/api/update', { credentials: 'same-origin', cache: 'no-store' });
		if (r.status === 401) { showGate('Session ended. Enter the PIN again.'); return; }
		if (!r.ok) { $('update').hidden = true; return; }   /* not on this page: over Wi-Fi */
		d = await r.json();
	} catch (e) { return; }
	upd = d;
	$('update').hidden = false;
	const body = $('updbody');
	const nodes = [el('p', 'now', t('update.have', d.installed))];

	/* No release key, no System Update, no program: off, and why */
	if (!d.on) {
		nodes.push(flag(t('update.off', d.why)));
		body.replaceChildren(...nodes);
		return;
	}
	/* The update program at work: one bar, the handheld's own count */
	if (d.running || upBusy === 'wait') {
		const w = WORK[d.state] || t('update.work_starting');
		const at = d.total ? Math.round(d.done / d.total * 100) : d.files_total ? Math.round(d.files_done / d.files_total * 100) : 0;
		nodes[0] = el('p', 'now', t('update.installing', d.version || ''));
		nodes.push(bar(at ? t('update.working_pct', w, pct(at)) : t('update.working', w), at),
		           el('p', '', t('update.keep_cable')));
		body.replaceChildren(...nodes);
		upWatch(true);
		return;
	}
	upWatch(false);
	if (d.state === 'ready') {
		nodes[0] = el('p', 'now', t('update.ready', d.version));
		nodes.push(el('p', '', t('update.restart_text')),
		           Object.assign(el('div', 'acts'), {}));
		nodes[2].append(btn(t('update.restart'), 'pri', upRestart));
		body.replaceChildren(...nodes);
		return;
	}
	/* Sending the files: their bar */
	if (upBusy === 'send') {
		nodes[0] = el('p', 'now', t('update.sending', rel ? rel.version : ''));
		nodes.push(bar(upLeft, upPct), el('p', '', t('update.next')));
		body.replaceChildren(...nodes);
		return;
	}
	/* What the last try said, when it stopped short */
	const said = { uptodate: t('update.uptodate'),
	               'needs-system': d.slots ? t('update.needs_system_send') : t('update.needs_system_installer'),
	               'too-big': t('update.too_big'),
	               battery: t('update.battery'),
	               space: t('update.space'),
	               error: d.message || t('update.refused'),
	               stopped: t('update.stopped') };
	if (upBusy === 'done' && said[d.state]) nodes.push(flag(said[d.state], d.state === 'error'));

	await newest();
	if (!rel) {
		nodes.push(el('p', '', relWhy || t('update.looking')));
		nodes.push(dropZone([]));
		body.replaceChildren(...nodes);
		return;
	}
	const want = needed();
	if (!want.length) {
		nodes.push(el('p', '', t('update.none_for', rel.version)));
		body.replaceChildren(...nodes);
		return;
	}
	if (vcmp(rel.version, d.installed) <= 0 && !(d.slots && vcmp(rel.version, d.system || '0') > 0)) {
		nodes.push(el('p', '', t('update.newest', d.installed)));
		body.replaceChildren(...nodes);
		return;
	}
	const size = want.reduce((n, f) => n + rel.assets[f].size, 0);
	nodes[0] = el('p', 'now', t('update.out', rel.version, d.installed));
	const have = want.filter((n) => upFiles.has(n)).length;
	const steps = el('ol', 'steps');
	const one = el('li'), t1 = el('div', 't');
	one.append(el('span', 'n' + (have === want.length ? ' ok' : ''), '1'), t1);
	t1.append(el('span', '', t('update.download_text', want.length, human(size))),
	          btn(have ? t('update.download_again') : t('act.download'), 'i i-down', () => upDownload(want)));
	const two = el('li'), t2 = el('div', 't');
	two.append(el('span', 'n', '2'), t2);
	t2.append(el('span', '', t('update.drop')), dropZone(want));
	steps.append(one, two);
	nodes.push(steps);
	body.replaceChildren(...nodes);
}

/* Downloads to the computer: a link each, opened as GitHub's own download
 * (an attachment, so the page stays) */
function upDownload(want) {
	for (const n of want) {
		const a = document.createElement('a');
		a.href = rel.assets[n].url;
		a.rel = 'noopener';
		a.download = n;
		document.body.append(a);
		a.click();
		a.remove();
	}
}

/* Where the files go: the names it waits for, ticked as they come */
function dropZone(want) {
	const z = el('div', 'dz');
	const list = el('div', 'files');
	for (const n of want) list.append(el('span', upFiles.has(n) ? 'have' : '', (upFiles.has(n) ? '✓ ' : '') + n));
	if (!want.length) list.append(el('span', '', t('update.files', 'ZyperOS-' + (upd ? upd.device : 'pixel2') + '-update.txt')));
	z.append(list, btn(t('update.choose'), 'pri', () => $('updpicker').click()));
	z.addEventListener('dragover', (e) => { e.preventDefault(); e.stopPropagation(); z.classList.add('over'); });
	z.addEventListener('dragleave', () => z.classList.remove('over'));
	z.addEventListener('drop', (e) => {
		e.preventDefault();
		e.stopPropagation();
		z.classList.remove('over');
		dragDepth = 0;
		dragUI(false);
		upTake([...e.dataTransfer.files]);
	});
	return z;
}
$('updpicker').onchange = (e) => { const f = [...e.target.files]; e.target.value = ''; upTake(f); };

/* Files dropped or chosen: kept by name, and sent once all are here. A file
 * with another name is not one of the update's, and the handheld would
 * refuse it, so it is refused here first. */
function upTake(files) {
	const want = needed();
	const odd = [];
	for (const f of files) {
		/* A browser that saved a second copy as "name (1).zpk" */
		const name = f.name.replace(/ \(\d+\)(\.[a-z]+)$/, '$1');
		if (want.length ? want.includes(name) : /^ZyperOS-.*\.(txt|zpk)$/.test(name)) upFiles.set(name, f);
		else odd.push(f.name);
	}
	if (odd.length) toast(t('update.odd', odd.join(t('sep.list'))), true);
	if (rel && want.length && want.every((n) => upFiles.has(n))) upSend(want);
	else drawUpdate();
}

/* One file to the handheld, with its progress */
function upPut(name, file, progress) {
	return new Promise((ok, no) => {
		const xhr = new XMLHttpRequest();
		const put = trailed('/api/update/file?name=' + enc(name), file);
		xhr.open('PUT', put.url);
		xhr.upload.onprogress = (ev) => { if (ev.lengthComputable) progress(Math.min(ev.loaded, file.size)); };
		xhr.onload = () => xhr.status === 200 ? ok()
			: no(new Error((xhr.responseText || '').split('\n')[1] || 'HTTP ' + xhr.status));
		xhr.onerror = () => no(new Error(t('queue.lost')));
		xhr.send(put.body);
	});
}

async function upSend(want) {
	if (upBusy === 'send') return;
	/* Room for them on the card, first */
	const total = want.reduce((n, f) => n + upFiles.get(f).size, 0);
	try {
		const st = await state();
		if (st.free >= 0 && usable(st) < total) {
			toast(t('update.no_room', human(total), human(usable(st))), true);
			return;
		}
	} catch (e) { if (e.message === 'unauthorized') return; }
	upBusy = 'send';
	samples.length = 0;
	let done = 0;
	const tick = (at) => {
		sent += Math.max(0, at - tick.last);
		tick.last = at;
		sample(false);
		upPct = Math.round((done + at) / total * 100);
		const when = eta(total - done - at);
		upLeft = t('queue.left', human(total - done - at)) + (when ? ' · ' + when : '');
		drawUpdate();
	};
	try {
		for (const n of want) {
			tick.last = 0;
			sample(true);
			await upPut(n, upFiles.get(n), tick);
			done += upFiles.get(n).size;
		}
		/* The handheld checks them as a release, then installs */
		upBusy = 'wait';
		await api('POST', '/api/update/check');
		await upSettle();
		if (upd && upd.state === 'available') {
			upBusy = 'wait';
			await api('POST', '/api/update/install');
			await upSettle();
		}
		upFiles.clear();
	} catch (err) {
		if (err.message !== 'unauthorized') toast(err.message, true);
	}
	upBusy = 'done';
	drawUpdate();
}

/* Until the update program is done with what it was asked: its lock held, or
 * its status not yet moved past what it said before it started */
async function upSettle() {
	const since = upd ? upd.time : 0;
	for (let i = 0; i < 3600; i++) {
		await new Promise((ok) => setTimeout(ok, 1000));
		const r = await fetch('/api/update', { credentials: 'same-origin', cache: 'no-store' });
		if (!r.ok) throw new Error(t('update.no_answer'));
		upd = await r.json();
		drawUpdate();
		if (!upd.running && upd.time !== since && !WORK[upd.state]) return;
		if (i >= 15 && !upd.running && upd.time === since)
			throw new Error(t('update.not_started'));
	}
}

/* While the handheld works on it, its bar follows */
function upWatch(on) {
	if (!on) { clearTimeout(upPoll); upPoll = 0; return; }
	if (upPoll || upBusy === 'wait') return;
	upPoll = setTimeout(() => { upPoll = 0; if (inSettings) drawUpdate(); }, 1000);
}

async function upRestart() {
	try {
		await api('POST', '/api/update/restart');
		$('updbody').replaceChildren(el('p', 'now', t('update.restarting')),
			el('p', '', t('update.page_stops')));
	} catch (err) { if (err.message !== 'unauthorized') toast(err.message, true); }
}

/* ---- toast ------------------------------------------------------------- */

let toastTimer = 0;
function toast(msg, bad) {
	const box = $('toast');
	box.textContent = msg;
	box.classList.toggle('bad', !!bad);
	box.classList.add('show');
	clearTimeout(toastTimer);
	/* DESIGN.md 8.2's holds: a short message, or a long one for longer */
	toastTimer = setTimeout(() => box.classList.remove('show'),
	                        ms(msg.length > 48 ? 'hold-long' : 'hold-short'));
}

/* A reload should not always mean re-entering the PIN: the cookie may still be
 * good. Ask for the roots, and only show the gate if that is refused. */
(async () => {
	try {
		const r = await fetch('/api/list', { credentials: 'same-origin' });
		if (!r.ok) throw new Error();
		$('gate').hidden = true;
		$('app').hidden = false;
		await start();
	} catch (e) {
		/* An expired session has put the gate up already, with its words */
		if (e.message !== 'unauthorized') showGate('');
	}
})();

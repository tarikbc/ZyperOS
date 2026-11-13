/* SPDX-License-Identifier: GPL-3.0-only */
/* A guest player of the game on the handheld, the browser half: the game's
 * picture from the handheld, and the player's pad back to it (p2d,
 * src/p2d/main.c). Player 2 on the NES; the handheld says which player.
 *
 * No framework and no build step, as the File Transfer page (res/web/app.js):
 * the handheld serves this file exactly as it is written.
 *
 * One WebSocket, /p2ws. From the handheld, a frame as a binary message
 * (src/p2d/codec.h: 'F', the flags, the size, the frame's number, a palette
 * when one changed, then the frame's palette indices, raw-deflated and XORed
 * with the frame before unless it is a KEY), and the status as text, "S" and
 * a line of JSON: {"state":"playing","player":N,"buttons":M}, or the older
 * {"state":"player2",...}, which is player 2; "taken", every player's place
 * held; "ended". To it, binary only: 'A' and a frame's number once that
 * frame is drawn, 'P' and the buttons held. p2d sends a frame only while
 * fewer than two wait for their ack, and drops a guest that holds one
 * unacked and says nothing for 5 s, so every frame is acked, drawn or not:
 * with Picture off or the tab hidden, at once.
 *
 * The first thing it asks is whether the browser can unpack a frame at all
 * (raw deflate: Safari 16.4, Chrome 103). Above that question only the words
 * are put in; everything else is set up in play(), after it, so a browser too
 * old for the game is told so before anything newer than it can stop the
 * page.
 */
'use strict';

const $ = (id) => document.getElementById(id);

/* ---- words --------------------------------------------------------------
 *
 * The handheld's language: /i18n.js, loaded before this file, is p2d's
 * res/web/i18n/<code>.js for the language that is on, which tools/i18n.py
 * writes from res/strings/play.txt. t(id, ...) is the string of that ID with
 * its %d and %s filled, as app.js's t. tools/i18n.py counts a string as used
 * only where its ID is written out in quotes, in a call of t or in a data-t
 * attribute: an ID is never built from pieces. */
const WORDS = window.ZYPEROS_STRINGS || {};

function t(id, ...args) {
	const w = WORDS[id];
	let at = 0;
	return (typeof w === 'string' ? w : id).replace(/%(?:(\d+)\$)?([sd%])/g, (m, pos, c) =>
		c === '%' ? '%' : !args.length ? m : String(args[pos ? pos - 1 : at++]));
}

document.documentElement.lang = window.ZYPEROS_LANG || 'en';
for (const e of document.querySelectorAll('[data-t]')) e.textContent = t(e.dataset.t);
for (const e of document.querySelectorAll('[data-t-aria]')) e.setAttribute('aria-label', t(e.dataset.tAria));
document.title = t('play.name');

/* What tools/play-check.py reads: the number of the frame last drawn and the
 * acks sent. The page itself never reads them back. */
const p2 = window.p2 = { frame: 0, acks: 0 };

const game = $('game');
const view = $('view');
const pad = $('pad');
const dpad = $('dpad');

/* ---- the status ------------------------------------------------------------ */

let state = '';                 /* connecting, playing, full or ended */

/* The status, and the tab: the player's number while it plays, the page's
 * name else */
function show(s, player) {
	state = s;
	game.dataset.state = s;
	const text = s === 'playing' ? t('play.state.player', player)
		: s === 'full' ? t('play.state.full')
		: s === 'ended' ? t('play.state.ended')
		: t('play.state.connecting');
	$('state').textContent = text;
	document.title = s === 'playing' ? text : t('play.name');
}

/* ---- the picture --------------------------------------------------------- */

const KEY = 1, PAL = 2;
const canvas = $('screen');
const ctx = canvas.getContext('2d', { alpha: false });
/* The palette as ImageData's words: R, G, B, 255 in memory */
const pal = new Uint32Array(256).fill(0xff000000);
let idx = null;                 /* the last frame's palette indices */
let iw = 0, ih = 0;             /* their size */
let image = null, words32 = null;
let pictureOn = true;

const drawing = () => pictureOn && !document.hidden;

/* The room the picture may take. On its side (the pad's group then laid out
 * in the game's own grid: display: contents), the view's box, which the grid
 * gives the whole height. Upright, the view's width, and the height from its
 * top down to what the pad's rows need: the picture right under the bar, the
 * pad in the rest. */
function room() {
	const v = view.getBoundingClientRect();
	if (getComputedStyle(pad).display === 'contents') return [v.width, v.height];
	const ps = getComputedStyle(pad), gs = getComputedStyle(game);
	const rows = ps.gridTemplateRows.split(' ').map(parseFloat).filter((n) => n > 0);
	const need = rows.reduce((a, n) => a + n, 0) + (parseFloat(ps.rowGap) || 0) * Math.max(0, rows.length - 1);
	const bottom = game.getBoundingClientRect().bottom - (parseFloat(gs.paddingBottom) || 0);
	return [v.width, bottom - (parseFloat(gs.rowGap) || 0) - need - v.top];
}

/* The picture as large as its room takes it: whole device pixels, so every
 * NES pixel is the same square, unless that leaves it under 85% of the room;
 * then the room itself, still drawn as pixels (play.css) */
function fit() {
	const [w, h] = room();
	if (w <= 0 || h <= 0) return;
	const dpr = window.devicePixelRatio || 1;
	const exact = Math.min(w / canvas.width, h / canvas.height);
	const whole = Math.floor(exact * dpr) / dpr;
	const s = whole >= exact * 0.85 ? whole : exact;
	canvas.style.width = canvas.width * s + 'px';
	canvas.style.height = canvas.height * s + 'px';
}

/* Raw deflate, as the codec writes it */
async function inflate(bytes) {
	const out = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
	return new Uint8Array(await new Response(out).arrayBuffer());
}

function paint() {
	if (!image || canvas.width !== iw || canvas.height !== ih) {
		canvas.width = iw;
		canvas.height = ih;
		image = ctx.createImageData(iw, ih);
		words32 = new Uint32Array(image.data.buffer);
		fit();
	}
	for (let i = 0; i < idx.length; i++) words32[i] = pal[idx[i]];
	ctx.putImageData(image, 0, 0);
}

/* One frame message from `sock`, in its turn: a delta needs the frame before
 * it, so connect() chains them one after another. A frame that is
 * not drawn (Picture off, the tab hidden) leaves no frame to add the next
 * delta to, so nothing is drawn again until a KEY; the codec sends one at
 * least every 120 frames. Its palette is kept either way, since a KEY
 * carries one only when a color is new. */
async function frame(sock, buf) {
	const b = new Uint8Array(buf);
	if (b.length < 10 || b[0] !== 0x46) return;
	const n = new DataView(buf).getUint32(6, true);
	try {
		await take(b, n);
	} catch (e) {
		idx = null;             /* a frame that did not unpack: wait for a KEY */
	}
	if (sock.readyState === WebSocket.OPEN) {
		sock.send(Uint8Array.of(0x41, n & 255, n >>> 8 & 255, n >>> 16 & 255, n >>> 24));
		p2.acks++;
	}
}

async function take(b, n) {
	const v = new DataView(b.buffer);
	const flags = b[1], w = v.getUint16(2, true), h = v.getUint16(4, true);
	let at = 10;
	if (flags & PAL) {
		const count = v.getUint16(at, true);
		at += 2;
		if (count > 256 || at + count * 3 > b.length) throw new Error('a palette cut short');
		for (let i = 0; i < count; i++, at += 3)
			pal[i] = (0xff000000 | b[at + 2] << 16 | b[at + 1] << 8 | b[at]) >>> 0;
	}
	const key = flags & KEY;
	if (!drawing()) {
		idx = null;
		return;
	}
	if (!key && !(idx && w === iw && h === ih)) return;
	const d = await inflate(b.subarray(at));
	if (d.length !== w * h) throw new Error('a frame of the wrong size');
	if (key) {
		idx = d;
		iw = w;
		ih = h;
	} else {
		for (let i = 0; i < d.length; i++) idx[i] ^= d[i];
	}
	paint();
	p2.frame = n;
}

/* ---- the pad ------------------------------------------------------------ */

/* Every RetroPad button the page can draw: its libretro bit (the mask's, the
 * one p2d puts in the share), where it sits, its label, and its keys by
 * KeyboardEvent.key, so Z is the key printed Z on any layout. `dpad` is an
 * arm of the d-pad; `face` the round buttons right of the picture, left to
 * right in this order, the shoulders among them for now; `mid` the pills
 * after them, each in the grid area play.css gives its id. A game draws,
 * reads and sends only the buttons in its mask (`has`): the NES's until the
 * status names others. A new system's buttons are its mask from p2d, and a
 * row here for any button not in the table yet (with a place in play.css
 * when it needs one). */
const PAD = [
	{ bit: 4, dir: 'up', place: 'dpad', keys: ['arrowup'] },
	{ bit: 5, dir: 'down', place: 'dpad', keys: ['arrowdown'] },
	{ bit: 6, dir: 'left', place: 'dpad', keys: ['arrowleft'] },
	{ bit: 7, dir: 'right', place: 'dpad', keys: ['arrowright'] },
	{ bit: 10, id: 'l', label: 'L', place: 'face', keys: ['q'] },
	{ bit: 1, id: 'y', label: 'Y', place: 'face', keys: ['a'] },
	{ bit: 0, id: 'b', label: 'B', place: 'face', keys: ['z'] },
	{ bit: 9, id: 'x', label: 'X', place: 'face', keys: ['s'] },
	{ bit: 8, id: 'a', label: 'A', place: 'face', keys: ['x'] },
	{ bit: 11, id: 'r', label: 'R', place: 'face', keys: ['w'] },
	{ bit: 2, id: 'select', label: 'SELECT', place: 'mid', keys: ['shift'], cap: t('play.key.shift') },
	{ bit: 3, id: 'start', label: 'START', place: 'mid', keys: ['enter'], cap: t('play.key.enter') },
];
const NES = 0x01fd;             /* B SELECT START UP DOWN LEFT RIGHT A */
const ALL = PAD.reduce((m, b) => m | 1 << b.bit, 0);
const ARM = {};                 /* a d-pad direction: its bit */
const BY_KEY = {};              /* a key's name: its button's bit */
for (const b of PAD) {
	if (b.dir) ARM[b.dir] = 1 << b.bit;
	for (const k of b.keys) BY_KEY[k] = 1 << b.bit;
}
const DPAD = ARM.up | ARM.down | ARM.left | ARM.right;
/* The d-pad's eight directions by where the finger is from its middle, an
 * eighth of the circle each, from the right going clockwise */
const DIRS = [ARM.right, ARM.right | ARM.down, ARM.down, ARM.down | ARM.left,
	ARM.left, ARM.left | ARM.up, ARM.up, ARM.up | ARM.right];

let arms = [];                  /* the d-pad's arms (play) */
let buttons = [];               /* the buttons that are not the d-pad (play) */
const fingers = new Map();      /* pointerId: { place, bits } */
const keys = new Map();         /* KeyboardEvent.code: bit */
let has = 0;                    /* the buttons this game has */
let mask = 0;                   /* the ones held, always within has */
let ws = null;

function sendPad() {
	if (ws && ws.readyState === WebSocket.OPEN) ws.send(Uint8Array.of(0x50, mask & 255, mask >> 8));
}

/* The mask from every finger and key; sent at once when it changes. What
 * it lights on the pad is the same for a key as for a finger. */
function update() {
	let m = 0;
	for (const f of fingers.values()) m |= f.bits;
	for (const bit of keys.values()) m |= bit;
	m &= has;
	if (m === mask) return;
	mask = m;
	for (const k of buttons) k.classList.toggle('on', (mask & 1 << k.dataset.bit) !== 0);
	for (const g of arms) g.classList.toggle('on', (mask & ARM[g.dataset.dir]) !== 0);
	sendPad();
}

function direction(x, y) {
	const r = dpad.getBoundingClientRect();
	const dx = x - (r.left + r.width / 2), dy = y - (r.top + r.height / 2);
	if (Math.hypot(dx, dy) < r.width * 0.12) return 0;
	return DIRS[Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) & 7];
}

/* The button of `place` under a finger: a thumb rolls from B to A, never
 * from A onto START */
function under(x, y, place) {
	for (const k of buttons) {
		if (k.hidden || k.dataset.place !== place) continue;
		const r = k.getBoundingClientRect();
		if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return 1 << k.dataset.bit;
	}
	return 0;
}

/* A finger is the d-pad's from where it lands to where it lifts, wherever it
 * slides; one that lands on a button holds whichever button of its group it
 * is on */
function move(e) {
	const f = fingers.get(e.pointerId);
	const bits = f.place === 'dpad' ? direction(e.clientX, e.clientY) : under(e.clientX, e.clientY, f.place);
	if (bits === f.bits) return;
	const pressed = bits & ~f.bits;
	f.bits = bits;
	update();
	if (pressed && e.pointerType !== 'mouse' && navigator.vibrate) navigator.vibrate(10);
}

/* The game's buttons: drawn, in the keys line, and the only ones read or
 * sent. Any held that it has not got go at once. */
function setButtons(m) {
	has = m & ALL;
	dpad.hidden = !(has & DPAD);
	for (const g of arms) g.toggleAttribute('hidden', !(has & ARM[g.dataset.dir]));
	for (const e of buttons) e.hidden = !(has & 1 << e.dataset.bit);
	const line = [];
	if (has & DPAD) line.push([t('play.key.arrows'), t('play.dpad')]);
	for (const b of PAD)
		if (b.place !== 'dpad' && has & 1 << b.bit) line.push([b.cap || b.keys[0].toUpperCase(), b.label]);
	$('keys').replaceChildren(...line.map(([cap, what]) => {
		const item = document.createElement('span'), kbd = document.createElement('kbd');
		kbd.textContent = cap;
		item.append(kbd, ' ' + what);
		return item;
	}));
	update();
	fit();
}

/* A page that is not looked at holds nothing: the keys' lifts go to
 * another window, and a hidden tab's timers slow past p2d's second */
function letGo() {
	fingers.clear();
	keys.clear();
	update();
}

/* ---- the link ----------------------------------------------------------- */

/* The status and the frames, the frames in their order. Ended is the end:
 * p2d closes the socket and the page stays as it is. Any other close is
 * tried again every 2 s, a full game too, saying so meanwhile: the place it
 * waits for may be its own, held by p2d until it finds the old link gone, or
 * another player's who leaves. */
function connect() {
	const sock = ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/p2ws');
	let frames = Promise.resolve();
	sock.binaryType = 'arraybuffer';
	sock.onopen = sendPad;
	sock.onmessage = (e) => {
		if (typeof e.data !== 'string') {
			const buf = e.data;
			frames = frames.then(() => frame(sock, buf));
			return;
		}
		let msg = null;
		try { msg = e.data[0] === 'S' ? JSON.parse(e.data.slice(1)) : null; } catch (err) { /* not ours */ }
		const s = msg && msg.state;
		if (s === 'playing' || s === 'player2') {
			/* A new player's first frame is a KEY with its palette */
			idx = null;
			/* The game's buttons, where p2d names them; an NES's else */
			setButtons(typeof msg.buttons === 'number' ? msg.buttons : NES);
			show('playing', s === 'playing' && Number.isInteger(msg.player) ? msg.player : 2);
		} else if (s === 'taken') {
			show('full');
		} else if (s === 'ended') {
			show('ended');
		}
	};
	sock.onclose = () => {
		if (ws === sock) ws = null;
		if (state === 'ended') return;
		if (state !== 'full') show('connecting');
		setTimeout(connect, 2000);
	};
}

/* ---- the start ---------------------------------------------------------- */

/* Everything the game needs, once the browser can unpack a frame */
function play() {
	arms = [...dpad.querySelectorAll('[data-dir]')];
	/* The buttons that are not the d-pad, each made once and shown while the
	 * game has it: the face group's in #face, SELECT and START after it */
	buttons = PAD.filter((b) => b.place !== 'dpad').map((b) => {
		const e = document.createElement('button');
		e.type = 'button';
		e.tabIndex = -1;
		e.id = b.id;
		e.dataset.bit = b.bit;
		e.dataset.place = b.place;
		e.className = 'key ctl ' + (b.place === 'face' ? 'round' : 'pill');
		if (b.place === 'face') {
			e.textContent = b.label;
			$('face').append(e);
		} else {
			const label = document.createElement('span');
			label.textContent = b.label;
			e.append(label);
			pad.append(e);
		}
		return e;
	});

	const ro = new ResizeObserver(fit);
	for (const e of [game, view, document.querySelector('.bar')]) ro.observe(e);

	for (const c of document.querySelectorAll('.ctl')) {
		c.addEventListener('pointerdown', (e) => {
			e.preventDefault();
			/* Its moves and its lift come here even off the page */
			try { c.setPointerCapture(e.pointerId); } catch (err) { /* gone already */ }
			fingers.set(e.pointerId, { place: c === dpad ? 'dpad' : c.dataset.place, bits: 0 });
			move(e);
		});
	}
	document.addEventListener('pointermove', (e) => { if (fingers.has(e.pointerId)) move(e); });
	const lift = (e) => { if (fingers.delete(e.pointerId)) update(); };
	document.addEventListener('pointerup', lift);
	document.addEventListener('pointercancel', lift);
	game.addEventListener('contextmenu', (e) => e.preventDefault());

	document.addEventListener('keydown', (e) => {
		/* A Mac sends no keyup for a key let go while Cmd is down: what is
		 * held goes when Cmd comes, rather than staying held */
		if (e.key === 'Meta') {
			keys.clear();
			update();
			return;
		}
		if (e.ctrlKey || e.metaKey || e.altKey) return;
		const bit = BY_KEY[(e.key || '').toLowerCase()];
		if (bit === undefined || !(has & bit)) return;
		e.preventDefault();
		if (keys.get(e.code) === bit) return;
		keys.set(e.code, bit);
		update();
	});
	document.addEventListener('keyup', (e) => {
		if (!keys.delete(e.code)) return;
		e.preventDefault();
		update();
	});
	window.addEventListener('blur', letGo);
	document.addEventListener('visibilitychange', () => { if (document.hidden) letGo(); });

	/* A held pad again every 100 ms: a lost message heals, and p2d lets a
	 * pad go after a second without one */
	setInterval(() => { if (mask) sendPad(); }, 100);

	/* Picture off: the friend watches the handheld, and the phone is the pad */
	$('picoff').addEventListener('change', (e) => {
		pictureOn = !e.target.checked;
		game.classList.toggle('padonly', !pictureOn);
	});

	setButtons(NES);
	show('connecting');
	connect();
}

/* Safari before 16.4 has no raw deflate to unpack a frame with: it says so,
 * rather than a black picture, and opens no socket */
function inflates() {
	try {
		new DecompressionStream('deflate-raw');
		return true;
	} catch (e) {
		return false;
	}
}

if (inflates()) {
	play();
} else {
	game.hidden = true;
	$('old').hidden = false;
}

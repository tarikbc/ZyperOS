// ZyperOS Installer's page. The main screen is four choices: the device,
// which ZyperOS (the latest release, or an image file on this computer), the
// card (list_cards every two seconds, so a card put in or taken out shows by
// itself), and what to do. Then a sheet that names the card and its size
// before anything is written, the write with its progress ("progress" and
// "finished" events from the app), and how it ended.
//
// Opened in a plain browser (no Tauri), it runs on made-up cards and a
// made-up write, to look at the design: demo() at the end.

"use strict";

const $ = (id) => document.getElementById(id);
const TAURI = window.__TAURI__ || null;
const invoke = TAURI ? TAURI.core.invoke : demo().invoke;
const listen = TAURI ? TAURI.event.listen : demo().listen;

const DEVICE = { pixel2: "GKD Pixel 2", brick: "TrimUI Brick" };
const THE = { pixel2: "the GKD Pixel 2", brick: "the TrimUI Brick" };
// A card this big is likely a drive, not a card: said on its row
const LARGE = 256e9;

const state = {
	screen: "main",
	cards: [],
	// No answer yet from the first look for cards: a slow system (diskutil
	// under load) can take seconds, and "put a card in" would be wrong
	listed: false,
	looking: false,
	cardId: null,
	device: "pixel2",
	deviceByHand: false,
	source: "release",
	release: null,
	releaseError: null,
	image: null,
	action: null,
	job: null,
};

// ---- words ---------------------------------------------------------------

function plural(n, one, many) {
	return n.toLocaleString() + " " + (n === 1 ? one : many);
}

// Decimal, as a card's label is: a 64 GB card reads 64.0 GB, not 59.6
function size(bytes) {
	if (bytes >= 1e12) return (bytes / 1e12).toFixed(2) + " TB";
	if (bytes >= 1e9) return (bytes / 1e9).toFixed(1) + " GB";
	return Math.round(bytes / 1e6) + " MB";
}
const MB = (n) => Math.round(n / 1e6).toLocaleString();

function zyperos() {
	if (state.source === "file" && state.image) return state.image.name;
	return state.release ? "ZyperOS " + state.release : "ZyperOS";
}

// A card not started yet has no games partition to name it by
function cardName(c) {
	if (c.device && !c.started) return "New " + DEVICE[c.device] + " card";
	return c.name || c.reader || "Card";
}

function contents(c) {
	const parts = [];
	if (c.games) parts.push(plural(c.games, "game", "games"));
	if (c.songs) parts.push(plural(c.songs, "song", "songs"));
	if (c.books) parts.push(plural(c.books, "audiobook", "audiobooks"));
	return parts;
}

// One line for a card's row: what is on it, and the reader it is in
function describe(c) {
	const parts = [];
	if (c.locked) return ["Locked: slide the switch on its side up", true];
	if (c.device) {
		const os = c.os || "ZyperOS";
		parts.push(os + (c.version ? " " + c.version : "") + " for " + DEVICE[c.device]);
		if (!c.started) parts.push("not started yet");
		else if (!c.volume) parts.push("games partition not mounted");
		else parts.push(contents(c).join(", ") || "no games yet");
	} else {
		parts.push("No ZyperOS on it");
		const has = contents(c);
		if (has.length) parts.push(has.join(", "));
	}
	if (c.reader && c.reader !== c.name) parts.push(c.reader);
	if (c.size_bytes > LARGE) return ["Large disk: make sure it is your card · " + parts.join(" · "), true];
	return [parts.join(" · "), false];
}

function selected() {
	return state.cards.find((c) => c.id === state.cardId) || null;
}

// Why an update can't be done on this card, or null if it can (or there is
// no card yet to ask about)
function updateBlocked(card) {
	if (!card) return null;
	if (!card.device) return "There is no ZyperOS on this card to update.";
	if (card.device !== state.device) return "This card is for " + THE[card.device] + ".";
	return null;
}

// What stops the button, said in the note; null when it can go
function blocker(card) {
	if (state.source === "release" && !state.release)
		return state.releaseError ? { text: state.releaseError, warn: !!state.release } : { text: "Looking for ZyperOS on GitHub." };
	if (state.source === "file" && !state.image) return { text: "Choose an image file." };
	if (state.source === "file" && state.image.device !== state.device)
		return { text: state.image.name + " is for " + THE[state.image.device] + ".", warn: true };
	if (!state.cards.length) return { text: state.listed ? "Put a card in this computer." : "Looking for cards." };
	if (!card) return { text: "Choose the card to write." };
	if (card.locked) return { text: "This card is locked: slide the switch on its side up.", warn: true };
	return null;
}

// ---- the main screen -----------------------------------------------------

function setOn(group, value) {
	for (const b of document.querySelectorAll(`[data-group=${group}] [data-value]`)) {
		const on = b.dataset.value === value;
		b.classList.toggle("on", on);
		b.setAttribute("aria-checked", on);
	}
}

function render() {
	const card = selected();
	setOn("device", state.device);

	// Which ZyperOS
	const rel = document.querySelector('[data-value="release"]');
	rel.disabled = !state.release && !!state.releaseError;
	$("release-what").textContent = state.release
		? "From GitHub, checked against the release's SHA256SUMS"
		: state.releaseError || "Looking on GitHub";
	$("release-value").textContent = state.release ? state.release : "";
	if (state.image) {
		const img = state.image;
		$("file-title").textContent = img.name;
		$("file-what").textContent = "For " + THE[img.device] + " · " + size(img.image_bytes)
			+ (img.checksum ? " · checked against the SHA256SUMS beside it" : " · no SHA256SUMS beside it");
		$("file-value").textContent = "Change";
	} else {
		$("file-title").textContent = "Image file";
		$("file-what").textContent = "An .img.xz or .img on this computer. You can drop one on this window.";
		$("file-value").textContent = "Choose";
	}
	setOn("source", state.source);

	// The cards
	const list = $("cards");
	list.replaceChildren(...state.cards.map((c) => {
		const b = document.createElement("button");
		b.className = "row";
		b.dataset.id = c.id;
		b.setAttribute("role", "radio");
		b.disabled = c.locked;
		const [what, warn] = describe(c);
		b.innerHTML = `<span class="mark-glyph" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M7 3.5h8l3.5 3.5v13.5h-11.5z M10.5 6.5v3 M13 6.5v3"/></svg></span>
			<span class="text"><b></b><span></span></span><span class="value"></span>`;
		b.querySelector("b").textContent = cardName(c);
		const line = b.querySelector(".text > span");
		line.textContent = what;
		line.classList.toggle("warn", warn);
		b.querySelector(".value").textContent = size(c.size_bytes);
		b.classList.toggle("on", c.id === state.cardId);
		b.setAttribute("aria-checked", c.id === state.cardId);
		return b;
	}));
	list.hidden = !state.cards.length;
	$("no-card").hidden = !!state.cards.length;
	$("no-card-title").textContent = state.listed ? "Put a microSD card in this computer" : "Looking for cards";
	$("no-card-what").textContent = state.listed
		? "It shows up here by itself. Only removable cards and drives are listed."
		: "Asking this computer for its removable disks.";

	// What to do
	const blocked = updateBlocked(card);
	const up = document.querySelector('[data-group=action] [data-value="update"]');
	up.disabled = !!blocked;
	$("update-what").textContent = blocked || "Keeps the games, music and saves.";
	if (!state.action || (state.action === "update" && blocked)) state.action = card && !blocked ? "update" : "fresh";
	setOn("action", state.action);

	// The button, and what it is about to do
	const stop = blocker(card);
	const fresh = state.action === "fresh";
	const go = $("go");
	go.disabled = !!stop;
	go.textContent = fresh ? "Erase and install" : "Update card";
	const note = $("note");
	note.classList.toggle("warn", !!(stop && stop.warn));
	note.textContent = stop ? stop.text
		: zyperos() + " for " + THE[state.device] + " onto " + cardName(card) + ", " + size(card.size_bytes) + ". "
			+ (fresh ? "Everything on the card is erased." : "Nothing on the card is erased.");
}

// A card chosen: its device, unless the device was chosen by hand
function cardChanged() {
	const card = selected();
	if (card && card.device && !state.deviceByHand) state.device = card.device;
	state.action = null;
	render();
}

async function refresh() {
	// One look at a time: a look that takes longer than the two seconds
	// between them is waited for, not piled on
	if (state.screen !== "main" || state.looking) return;
	state.looking = true;
	let cards;
	try { cards = await invoke("list_cards"); } catch (_) { return; } finally { state.looking = false; }
	state.listed = true;
	if (state.screen !== "main") return;
	const before = state.cardId;
	state.cards = cards;
	// The only card is chosen; with several, none until one is clicked
	if (!cards.some((c) => c.id === state.cardId)) state.cardId = cards.length === 1 && !cards[0].locked ? cards[0].id : null;
	if (state.cardId !== before) cardChanged(); else render();
}

async function useImage(promise) {
	try {
		const img = await promise;
		if (!img) return;
		state.image = img;
		state.source = "file";
		if (img.device !== state.device) {
			state.device = img.device;
			state.deviceByHand = false;
			state.action = null;
		}
	} catch (e) {
		state.image = null;
		const note = $("note");
		render();
		note.textContent = String(e);
		note.classList.add("warn");
		return;
	}
	render();
}

document.querySelector("[data-group=device]").addEventListener("click", (e) => {
	const b = e.target.closest("[data-value]");
	if (!b || b.disabled) return;
	state.device = b.dataset.value;
	state.deviceByHand = true;
	state.action = null;
	render();
});
document.querySelector("[data-group=source]").addEventListener("click", (e) => {
	const b = e.target.closest("[data-value]");
	if (!b || b.disabled) return;
	if (b.dataset.value === "file" && (!state.image || state.source === "file")) {
		useImage(invoke("pick_image"));
		return;
	}
	state.source = b.dataset.value;
	render();
});
$("cards").addEventListener("click", (e) => {
	const b = e.target.closest("[data-id]");
	if (!b || b.disabled) return;
	state.cardId = b.dataset.id;
	cardChanged();
});
document.querySelector("[data-group=action]").addEventListener("click", (e) => {
	const b = e.target.closest("[data-value]");
	if (!b || b.disabled) return;
	state.action = b.dataset.value;
	render();
});

// ---- screens -------------------------------------------------------------

function show(name) {
	for (const s of ["main", "working", "finished"]) $(s).hidden = s !== name;
	$("bottom").hidden = name !== "main";
	state.screen = name;
}

// ---- the sheet before anything is written --------------------------------

function confirmWrite() {
	const card = selected();
	if (!card || blocker(card)) return;
	const fresh = state.action === "fresh";
	state.confirming = { card, device: state.device, action: state.action };
	$("confirm-title").textContent = fresh ? "Erase this card?" : "Update this card?";
	$("confirm-name").textContent = cardName(card);
	$("confirm-detail").textContent = [card.reader, card.id].filter(Boolean).join(" · ");
	$("confirm-size").textContent = size(card.size_bytes);
	const what = $("confirm-what");
	what.classList.toggle("erase", fresh);
	const has = contents(card);
	what.textContent = fresh
		? "Everything on it is erased" + (has.length ? ", " + has.join(", ") + " included" : "")
			+ ". Then " + zyperos() + " for " + THE[state.device] + " goes on."
		: zyperos() + " replaces the system on it. The games, music and saves stay.";
	$("confirm-check-row").hidden = !fresh;
	$("confirm-check").checked = false;
	$("confirm-check-text").textContent = "Erase " + cardName(card) + ", " + size(card.size_bytes);
	$("confirm-go").textContent = fresh ? "Erase and install" : "Update card";
	$("confirm-go").disabled = fresh;
	$("confirm").hidden = false;
	$("confirm-back").focus();
}

function closeConfirm(then) {
	const sheet = $("confirm");
	if (sheet.hidden) return;
	sheet.classList.add("closing");
	setTimeout(() => {
		sheet.hidden = true;
		sheet.classList.remove("closing");
		if (then) then();
	}, 140);
}

$("go").addEventListener("click", confirmWrite);
$("confirm-back").addEventListener("click", () => closeConfirm());
$("confirm-check").addEventListener("change", (e) => { $("confirm-go").disabled = !e.target.checked; });
$("confirm-go").addEventListener("click", () => closeConfirm(begin));
document.addEventListener("keydown", (e) => {
	if (e.key === "Escape" && !$("confirm").hidden) closeConfirm();
});

// ---- the write -----------------------------------------------------------

const STEPS = ["get", "write", "check", "eject"];
const STEP_OF = { downloading: "get", verifying: "get", writing: "write", checking: "check", ejecting: "eject" };

function step(now) {
	const at = STEPS.indexOf(now);
	for (const li of $("steps").children) {
		const i = STEPS.indexOf(li.dataset.step);
		li.classList.toggle("done", i < at);
		li.classList.toggle("now", i === at);
	}
}

async function begin() {
	const c = state.confirming;
	if (!c) return;
	const local = state.source === "file" && state.image;
	state.job = { ...c, local: !!local, wrote: false, phase: null, samples: [] };
	$("working-title").textContent = c.action === "fresh" ? "Installing ZyperOS" : "Updating the card";
	$("working-card").textContent = cardName(c.card) + " · " + size(c.card.size_bytes) + " · for " + THE[c.device];
	// A local image without a SHA256SUMS has nothing to check first
	$("step-get").textContent = local ? "Check" : "Download";
	$("steps").querySelector('[data-step="get"]').hidden = local && !state.image.checksum;
	step(local && !state.image.checksum ? "write" : "get");
	$("bar-fill").style.width = "0";
	$("phase").textContent = local ? "Opening the card" : "Getting " + zyperos();
	$("detail").textContent = local && !state.image.checksum ? "Your computer may ask for your password to write the card." : "";
	$("cancel").disabled = false;
	show("working");
	try {
		await invoke("start", { card: c.card.id, size: c.card.size_bytes, device: c.device, action: c.action,
			image: local ? state.image.path : null });
	} catch (e) {
		finish({ ok: false, cancelled: false, message: String(e) });
	}
}

// Speed over the last few seconds of a phase, in bytes a second
function rate(job, done) {
	const now = performance.now();
	job.samples.push([now, done]);
	while (job.samples.length > 2 && now - job.samples[0][0] > 3000) job.samples.shift();
	const [t0, d0] = job.samples[0];
	return now - t0 > 400 ? ((done - d0) * 1000) / (now - t0) : 0;
}

function eta(seconds) {
	if (!isFinite(seconds) || seconds <= 0) return "";
	if (seconds < 60) return " · about " + Math.max(5, Math.round(seconds / 5) * 5) + " s left";
	return " · about " + Math.round(seconds / 60) + " min left";
}

const WORDS = {
	downloading: ["Downloading", "From GitHub, then checked against the release's SHA256SUMS."],
	verifying: ["Checking the image", "Against the SHA256SUMS beside it, before anything is written."],
	writing: ["Writing", null],
	checking: ["Reading back", "Everything written is read back and compared, to be sure the card holds it."],
};

function progress(p) {
	const job = state.job;
	if (!job || state.screen !== "working") return;
	if (p.phase !== job.phase) {
		job.phase = p.phase;
		job.samples = [];
	}
	step(STEP_OF[p.phase]);
	const bar = $("bar-fill");
	if (p.phase === "ejecting") {
		bar.style.width = "100%";
		$("phase").textContent = "Ejecting the card";
		$("detail").textContent = "So it can come out safely.";
		$("cancel").disabled = true;
		return;
	}
	if (p.phase === "writing" || p.phase === "checking") job.wrote = true;
	const part = p.total ? p.done / p.total : 0;
	bar.style.width = part * 100 + "%";
	const speed = rate(job, p.done);
	const [verb, what] = WORDS[p.phase];
	$("phase").textContent = verb + " · " + MB(p.done) + " of " + MB(p.total) + " MB"
		+ (speed > 0 ? " · " + (speed / 1e6).toFixed(1) + " MB/s" + eta((p.total - p.done) / speed) : "");
	$("detail").textContent = what || (job.action === "fresh"
		? "The whole card: ZyperOS and the system under it."
		: "The system part only. The games, music and saves stay.");
	// The download is checked: next the card is opened, which asks for the
	// password
	if ((p.phase === "downloading" || p.phase === "verifying") && p.done === p.total)
		$("detail").textContent = "Your computer may ask for your password to write the card.";
}

function finish(f) {
	const job = state.job || {};
	const zy = $("zyper");
	zy.classList.remove("hop", "tilt");
	const head = $("finished-head");
	head.classList.remove("bad");
	const where = THE[job.device] || "the handheld";
	if (f.ok) {
		$("finished-title").textContent = job.action === "fresh" ? "ZyperOS is on the card" : "The card is updated";
		head.textContent = f.ejected ? "The card is ejected. You can take it out."
			: "Eject the card before you take it out." + (f.eject_error ? " " + f.eject_error : "");
		$("finished-what").textContent = "Put it in " + where + " and turn it on."
			+ (job.action === "fresh"
				? " Its first start makes the ZYPEROS partition for your games, so it takes a little longer."
				: " The games, music and saves are as they were.");
		void zy.offsetWidth;
		zy.classList.add("hop");
	} else if (f.cancelled && !job.wrote) {
		$("finished-title").textContent = "Stopped";
		head.textContent = "Nothing was written.";
		$("finished-what").textContent = "The card is as it was.";
	} else if (f.cancelled) {
		$("finished-title").textContent = "Stopped partway";
		head.textContent = job.action === "fresh" ? "The card has no system on it now."
			: "The card won't start until the update is finished.";
		$("finished-what").textContent = job.action === "fresh" ? "Install again to finish."
			: "Run it again. The games, music and saves are still there.";
	} else {
		$("finished-title").textContent = "That didn't work";
		head.textContent = f.message || "Something went wrong.";
		head.classList.add("bad");
		$("finished-what").textContent = job.wrote ? "Nothing on the card is to be trusted now: install again."
			: "Nothing was written to the card.";
		zy.classList.add("tilt");
	}
	$("finished-back").textContent = f.ok ? "Done" : "Back";
	state.job = null;
	show("finished");
}

$("cancel").addEventListener("click", () => {
	$("cancel").disabled = true;
	$("phase").textContent = "Stopping";
	invoke("cancel");
});
$("finished-back").addEventListener("click", () => {
	show("main");
	refresh();
});

// ---- an image dropped on the window --------------------------------------

function dropEvents(handler) {
	const wv = TAURI && TAURI.webview && TAURI.webview.getCurrentWebview && TAURI.webview.getCurrentWebview();
	if (wv && wv.onDragDropEvent) return wv.onDragDropEvent((e) => handler(e.payload));
	// Plain browser: nothing to drop paths from
}
dropEvents((p) => {
	if (state.screen !== "main") return;
	if (p.type === "enter" || p.type === "over") $("drop").hidden = false;
	else $("drop").hidden = true;
	if (p.type === "drop" && p.paths && p.paths.length) useImage(invoke("check_image", { path: p.paths[0] }));
});

// ---- start ---------------------------------------------------------------

// Which ZyperOS the release row installs, asked once; again every half minute
// while GitHub can't be reached. No release at all: the image file is the
// way, and is chosen.
async function findRelease() {
	try {
		state.release = await invoke("latest_zyperos");
		state.releaseError = null;
	} catch (e) {
		state.releaseError = String(e);
		if (state.source === "release") state.source = "file";
		if (!/no release/.test(state.releaseError)) setTimeout(findRelease, 30000);
	}
	if (state.screen === "main") render();
}

listen("progress", (e) => progress(e.payload));
listen("finished", (e) => finish(e.payload));
if (TAURI) TAURI.app.getVersion().then((v) => { $("ver").textContent = v; });
invoke("newer_installer").then((v) => {
	if (!v) return;
	$("newer").textContent = "Installer " + v + " is out";
	$("newer").hidden = false;
});
$("newer").addEventListener("click", () => invoke("open_installer_releases"));
findRelease();
render();
refresh();
setInterval(refresh, 2000);

// ---- demo: the page in a plain browser -----------------------------------

function demo() {
	if (demo.api) return demo.api;
	const handlers = {};
	const emit = (name, payload) => (handlers[name] || []).forEach((h) => h({ payload }));
	const q = new URLSearchParams(location.search);
	const cards = q.has("nocard") ? [] : [
		{ id: "disk29", name: "ZYPEROS", size_bytes: 31914983424, reader: "USB3.0 CRW -SD", locked: false, device: "pixel2",
			os: "ZyperOS", version: "0.1.0", games: 142, songs: 380, books: 3, volume: "/Volumes/ZYPEROS", started: true },
		{ id: "disk28", name: "ROMS", size_bytes: 127865454592, reader: "Built In SDXC Reader", locked: false, device: null,
			os: null, version: null, games: 0, songs: 0, books: 0, volume: "/Volumes/ROMS", started: true },
	];
	let stop = false;
	demo.api = {
		listen: (name, h) => { (handlers[name] = handlers[name] || []).push(h); return Promise.resolve(); },
		invoke: (cmd, args) => {
			switch (cmd) {
			case "list_cards": return Promise.resolve(cards);
			case "latest_zyperos": return q.has("release") ? Promise.resolve("0.1.0")
				: Promise.reject("ZyperOS has no release to download yet. Choose an image file instead.");
			case "newer_installer": return Promise.resolve(null);
			case "pick_image": case "check_image":
				return Promise.resolve({ path: "/tmp/ZyperOS-v0.1.0-pixel2.img.xz", name: "ZyperOS-v0.1.0-pixel2.img.xz",
					file_bytes: 74e6, image_bytes: 352321536, device: "pixel2", checksum: true });
			case "cancel": stop = true; return Promise.resolve();
			case "start": {
				stop = false;
				const total = 352321536;
				let phase = 0, done = 0;
				const phases = ["verifying", "writing", "checking"];
				const tick = () => {
					if (stop) return emit("finished", { ok: false, cancelled: true, message: "cancelled" });
					done = Math.min(total, done + total / (phase === 0 ? 8 : 40));
					emit("progress", { phase: phases[phase], done, total });
					if (done >= total) { phase++; done = 0; }
					if (phase < phases.length) return setTimeout(tick, 120);
					emit("progress", { phase: "ejecting", done: 0, total: 0 });
					setTimeout(() => emit("finished", { ok: true, cancelled: false, message: "", ejected: true, eject_error: "" }), 600);
				};
				setTimeout(tick, 300);
				return Promise.resolve();
			}
			default: return Promise.resolve(null);
			}
		},
	};
	return demo.api;
}

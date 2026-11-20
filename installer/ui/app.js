// ZyperOS Installer's page. The main screen is five choices: the device (each
// one the newest release has a card image for, and an image file's own),
// which ZyperOS (the latest release, or an image file on this computer), the
// card (list_cards every two seconds, so a card put in or taken out shows by
// itself), the player's own PICO-8 (optional: the Raspberry Pi zip they
// bought, which goes on this card only), and what to do. Then a sheet that
// names the card and its size
// before anything is written, the write with its progress ("progress" and
// "finished" events from the app), and how it ended. In the header, a chip
// when a newer installer is out, which on macOS and Windows updates the
// installer itself.
//
// Opened in a plain browser (no Tauri), it runs on made-up cards and a
// made-up write, to look at the design: demo() at the end.

"use strict";

const $ = (id) => document.getElementById(id);
const TAURI = window.__TAURI__ || null;
const invoke = TAURI ? TAURI.core.invoke : demo().invoke;
const listen = TAURI ? TAURI.event.listen : demo().listen;

// Every device the page knows, in its order; which of them it offers is
// offered()'s
const DEVICE = { pixel2: "GKD Pixel 2", brick: "TrimUI Brick", smartpro: "TrimUI Smart Pro" };
const THE = { pixel2: "the GKD Pixel 2", brick: "the TrimUI Brick", smartpro: "the TrimUI Smart Pro" };
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
	// The devices the release row installs (latest_zyperos): null until it
	// answers
	devices: null,
	releaseError: null,
	image: null,
	// The player's PICO-8 zip as check_pico8 found it ({path, name, version,
	// refused}), and the name of one being checked
	pico8: null,
	pico8Checking: null,
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

// "PICO-8 0.2.7", or "PICO-8" when neither its manual nor its zip's name says
function p8(version) {
	return version ? "PICO-8 " + version : "PICO-8";
}
const NOT_MOUNTED = "The card's ZYPEROS volume is not mounted.";

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
		if (typeof c.pico8 === "string") parts.push(p8(c.pico8));
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

// The devices to choose from: those the newest release has a card image for
// (until it answers, or when GitHub can't be reached, the GKD Pixel 2, which
// every release carries), and a chosen image file's own device. A device with
// neither is not offered
function offered() {
	const have = state.devices || ["pixel2"];
	return Object.keys(DEVICE).filter((d) => have.includes(d) || (state.image && state.image.device === d));
}

// Why an update can't be done on this card, or null if it can (or there is
// no card yet to ask about)
function updateBlocked(card) {
	if (!card) return null;
	if (!card.device) return "There is no ZyperOS on this card to update.";
	if (card.device !== state.device) return "This card is for " + THE[card.device] + ".";
	return null;
}

// The player's PICO-8: a zip checked and good
function pico8Ok() {
	return !!(state.pico8 && !state.pico8.refused && !state.pico8Checking);
}

// A started card with ZyperOS: Add PICO-8 is offered for it
function addable(card) {
	return !!(card && card.device && card.started);
}

// Whether PICO-8 goes with this action onto this card: always into a card
// that never started (it rides along in the image's boot partition, and the
// first start moves it), as every fresh install is; into a started card's
// mounted ZYPEROS volume on an update, and not when it isn't mounted
function carries(card, action) {
	if (!pico8Ok() || !card || action === "pico8") return false;
	return action === "fresh" || !card.started || !!card.volume;
}
function rides(card, action) {
	return action === "fresh" || !card.started;
}

// What stops the button, said in the note; null when it can go
function blocker(card) {
	if (state.action === "pico8") {
		if (state.pico8Checking) return { text: "Checking the zip." };
		if (!state.pico8) return { text: "Choose your PICO-8 zip." };
		if (state.pico8.refused) return { text: state.pico8.refused, warn: true };
		if (!card) return { text: "Choose the card to write." };
		if (card.locked) return { text: "This card is locked: slide the switch on its side up.", warn: true };
		if (!card.volume) return { text: NOT_MOUNTED, warn: true };
		return null;
	}
	// A zip that isn't PICO-8 for Raspberry Pi stops the write until it is
	// removed or another is chosen
	if (state.pico8Checking) return { text: "Checking the zip." };
	if (state.pico8 && state.pico8.refused) return { text: state.pico8.refused, warn: true };
	if (state.source === "release" && !state.release)
		return state.releaseError ? { text: state.releaseError, warn: !!state.release } : { text: "Looking for ZyperOS on GitHub." };
	// Offered for its image file, chosen while the release row is
	if (state.source === "release" && state.devices && !state.devices.includes(state.device))
		return { text: zyperos() + " has no card image for " + THE[state.device] + ". Choose an image file for it.", warn: true };
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
	const devices = offered();
	if (!devices.includes(state.device)) {
		state.device = devices[0] || "pixel2";
		state.deviceByHand = false;
		state.action = null;
	}
	for (const b of document.querySelectorAll("[data-group=device] [data-value]")) b.hidden = !devices.includes(b.dataset.value);
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

	// What to do: Add PICO-8 for a started card, once a good zip is chosen
	const blocked = updateBlocked(card);
	const up = document.querySelector('[data-group=action] [data-value="update"]');
	up.disabled = !!blocked;
	$("update-what").textContent = blocked || "Keeps the games, music and saves.";
	const add = document.querySelector('[data-group=action] [data-value="pico8"]');
	const addWhy = !pico8Ok() ? "Choose your zip above first." : card && !card.volume ? NOT_MOUNTED : null;
	add.hidden = !addable(card);
	add.disabled = add.hidden || !!addWhy;
	$("pico8-add-what").textContent = addWhy || "Writes no system. Keeps the rest.";
	document.querySelector("[data-group=action]").classList.toggle("three", !add.hidden);
	if (state.action === "pico8" && add.disabled) state.action = null;
	if (!state.action || (state.action === "update" && blocked)) state.action = card && !blocked ? "update" : "fresh";
	setOn("action", state.action);
	renderPico8(card);

	// The button, and what it is about to do
	const stop = blocker(card);
	const fresh = state.action === "fresh";
	const go = $("go");
	// Nothing starts while the installer is replacing itself, just before it
	// restarts
	go.disabled = !!stop || update.state === "installing";
	go.textContent = state.action === "pico8" ? "Add PICO-8" : fresh ? "Erase and install" : "Update card";
	const note = $("note");
	note.classList.toggle("warn", !!(stop && stop.warn));
	note.textContent = stop ? stop.text : intent(card);
}

// What the button is about to do, in the note
function intent(card) {
	const where = cardName(card) + ", " + size(card.size_bytes);
	if (state.action === "pico8")
		return "Your " + p8(state.pico8.version) + " into Bios/PICO-8 on " + where
			+ ". Nothing else on the card changes, and no password is asked.";
	const fresh = state.action === "fresh";
	const withP8 = carries(card, state.action);
	return zyperos() + " for " + THE[state.device] + " onto " + where
		+ (withP8 ? ", with your " + p8(state.pico8.version) : "") + ". "
		+ (fresh ? "Everything on the card is erased." : "Nothing on the card is erased.")
		+ (withP8 ? " Only this card gets PICO-8."
			: pico8Ok() ? " Your PICO-8 is left out: " + NOT_MOUNTED.charAt(0).toLowerCase() + NOT_MOUNTED.slice(1) : "");
}

// The PICO-8 row: nothing chosen (or what the card has already), a zip being
// checked, a good one (on, as a chosen row), or one refused, in warning's
// color, until it is removed or another is chosen
function renderPico8(card) {
	const z = state.pico8;
	const row = $("pico8-row");
	let title, what, value = "Choose", on = false, warn = false;
	if (state.pico8Checking) {
		title = state.pico8Checking;
		what = "Checking the zip";
		value = "";
	} else if (z && z.refused) {
		title = z.name;
		what = z.refused;
		warn = true;
	} else if (z) {
		title = z.name;
		what = p8(z.version) + " · the Raspberry Pi build · goes in Bios/PICO-8";
		value = "Change";
		on = true;
		// An update would leave it out
		if (card && card.started && !card.volume && state.action !== "fresh") {
			what = NOT_MOUNTED;
			warn = true;
		}
	} else if (card && typeof card.pico8 === "string") {
		title = p8(card.pico8) + " is on this card";
		what = "Choose a newer zip to replace it.";
	} else {
		title = "Your PICO-8";
		what = "Optional. The PICO-8 for Raspberry Pi zip you bought. You can drop it here.";
	}
	$("pico8-title").textContent = title;
	const line = $("pico8-what");
	line.textContent = what;
	line.classList.toggle("warn", warn);
	$("pico8-value").textContent = value;
	row.classList.toggle("on", on);
	const x = !!z && !state.pico8Checking;
	row.classList.toggle("with-x", x);
	$("pico8-clear").hidden = !x;
}

// A card chosen: its device, unless the device was chosen by hand or isn't
// offered
function cardChanged() {
	const card = selected();
	if (card && card.device && !state.deviceByHand && offered().includes(card.device)) state.device = card.device;
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

// A zip chosen or dropped: checked (check_pico8), "Checking the zip"
// meanwhile. Only the latest one counts
let pico8Asked = 0;
async function usePico8(path) {
	if (!path) return;
	const ask = ++pico8Asked;
	state.pico8 = null;
	state.pico8Checking = path.split(/[\\/]/).pop();
	render();
	let z;
	try {
		z = await invoke("check_pico8", { path });
	} catch (e) {
		z = { path, name: state.pico8Checking, version: "", refused: String(e) };
	}
	if (ask !== pico8Asked) return;
	state.pico8Checking = null;
	state.pico8 = z;
	render();
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
$("pico8-row").addEventListener("click", async () => {
	if (state.pico8Checking) return;
	let path;
	try { path = await invoke("pick_pico8"); } catch (e) {
		render();
		$("note").textContent = String(e);
		$("note").classList.add("warn");
		return;
	}
	usePico8(path);
});
$("pico8-clear").addEventListener("click", () => {
	state.pico8 = null;
	if (state.action === "pico8") state.action = null;
	render();
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
	renderUpdate();
}

// ---- the sheet before anything is written --------------------------------

function confirmWrite() {
	const card = selected();
	if (!card || blocker(card)) return;
	const fresh = state.action === "fresh";
	const add = state.action === "pico8";
	const withP8 = add || carries(card, state.action);
	state.confirming = { card, device: add ? card.device : state.device, action: state.action,
		pico8: withP8 ? state.pico8 : null, rides: withP8 && !add && rides(card, state.action) };
	const yours = withP8 ? "Your " + p8(state.pico8.version) : "";
	$("confirm-title").textContent = add ? "Add PICO-8 to this card?" : fresh ? "Erase this card?" : "Update this card?";
	$("confirm-name").textContent = cardName(card);
	$("confirm-detail").textContent = [card.reader, card.id].filter(Boolean).join(" · ");
	$("confirm-size").textContent = size(card.size_bytes);
	const what = $("confirm-what");
	what.classList.toggle("erase", fresh);
	const has = contents(card);
	what.textContent = add ? yours + " goes in Bios/PICO-8. Nothing else on it changes."
		: fresh
			? "Everything on it is erased" + (has.length ? ", " + has.join(", ") + " included" : "")
				+ ". Then " + zyperos() + " for " + THE[state.device] + " goes on." + (withP8 ? " " + yours + " goes with it." : "")
			: zyperos() + " replaces the system on it. The games, music and saves stay."
				+ (withP8 ? " " + yours + (state.confirming.rides ? " goes with it." : " goes in Bios/PICO-8.") : "");
	$("confirm-check-row").hidden = !fresh;
	$("confirm-check").checked = false;
	$("confirm-check-text").textContent = "Erase " + cardName(card) + ", " + size(card.size_bytes);
	$("confirm-go").textContent = add ? "Add PICO-8" : fresh ? "Erase and install" : "Update card";
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

const STEPS = ["get", "pico8", "write", "check", "eject"];
const STEP_OF = { fetching: "get", downloading: "get", verifying: "get", pico8: "pico8", pico8check: "pico8",
	writing: "write", checking: "check", ejecting: "eject" };

function step(now) {
	const at = STEPS.indexOf(now);
	for (const li of $("steps").children) {
		const i = STEPS.indexOf(li.dataset.step);
		li.classList.toggle("done", i < at);
		li.classList.toggle("now", i === at);
	}
}

// Where the PICO-8 step says the files go
function pico8Where(job) {
	return job.rides ? "Into the card image's boot partition. The handheld's first start moves it to Bios/PICO-8."
		: "Into Bios/PICO-8 on " + cardName(job.card) + ". Only this card gets it.";
}

async function begin() {
	const c = state.confirming;
	if (!c) return;
	const add = c.action === "pico8";
	const local = !add && state.source === "file" && state.image;
	state.job = { ...c, local: !!local, wrote: false, copied: false, phase: null, samples: [] };
	$("working-title").textContent = add ? "Adding your PICO-8" : c.action === "fresh" ? "Installing ZyperOS" : "Updating the card";
	$("working-card").textContent = cardName(c.card) + " · " + size(c.card.size_bytes) + " · for " + THE[c.device];
	// A local image without a SHA256SUMS has nothing to check first; Add
	// PICO-8 neither gets nor writes a system
	const unchecked = local && !state.image.checksum;
	$("step-get").textContent = local ? "Check" : "Download";
	const li = (name) => $("steps").querySelector(`[data-step="${name}"]`);
	li("get").hidden = add || unchecked;
	li("pico8").hidden = !c.pico8;
	li("write").hidden = add;
	step(add ? "pico8" : unchecked ? (c.pico8 ? "pico8" : "write") : "get");
	$("bar-fill").style.width = "0";
	$("phase").textContent = add ? "Copying pico8_64 · 1 of 2" : local ? "Opening the card" : "Getting " + zyperos();
	$("detail").textContent = add ? pico8Where(state.job)
		: unchecked ? "Your computer may ask for your password to write the card." : "";
	$("cancel").disabled = false;
	show("working");
	try {
		await invoke("start", { card: c.card.id, size: c.card.size_bytes, device: c.device, action: c.action,
			image: local ? state.image.path : null, pico8: c.pico8 ? c.pico8.path : null });
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
	fetching: ["Getting the graphics and Wi-Fi files",
		"From Creality's Sonic Pad SDK on GitHub, each one checked, then put on this card only. ZyperOS can't include them itself."],
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
	// The player's PICO-8, by its two files: copied (into Bios/PICO-8, or the
	// image's boot partition), then read back from Bios/PICO-8 (Add PICO-8's
	// Read back step)
	if (p.phase === "pico8" || p.phase === "pico8check") {
		const add = job.action === "pico8";
		if (p.phase === "pico8check") {
			if (add) step("check");
			if (p.done === p.total) job.copied = true;
		}
		$("phase").textContent = p.phase === "pico8check" ? "Reading back · 2 files"
			: p.done === 0 ? "Copying pico8_64 · 1 of 2" : "Copying pico8.dat · 2 of 2";
		$("detail").textContent = p.phase !== "pico8check" ? pico8Where(job)
			: add ? "Each file is compared with the zip's."
			// Copied: next the card is opened, which asks for the password
			: p.done === p.total ? "Your computer may ask for your password to write the card." : pico8Where(job);
		return;
	}
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
	const yours = job.pico8 ? "your " + p8(job.pico8.version) : "";
	// Only an update's copy into Bios/PICO-8 is on the card before its write
	const onlyPico8 = job.copied && job.action !== "pico8" && !job.wrote;
	if (f.ok) {
		head.textContent = f.ejected ? "The card is ejected. You can take it out."
			: "Eject the card before you take it out." + (f.eject_error ? " " + f.eject_error : "");
		if (job.action === "pico8") {
			$("finished-title").textContent = "Your PICO-8 is on the card";
			$("finished-what").textContent = "Put it in " + where + " and turn it on. PICO-8 carts open in it now, and Splore is the PICO-8 shelf's first entry.";
		} else {
			$("finished-title").textContent = job.action === "fresh" ? "ZyperOS is on the card" : "The card is updated";
			$("finished-what").textContent = "Put it in " + where + " and turn it on."
				+ (job.action === "fresh"
					? " Its first start makes the ZYPEROS partition for your games, so it takes a little longer."
						+ (job.pico8 ? " It moves your PICO-8 there too." : "")
					: " The games, music and saves are as they were."
						+ (!job.pico8 ? "" : job.rides ? " Its first start moves " + yours + " to Bios/PICO-8."
							: " Your " + p8(job.pico8.version) + " is in Bios/PICO-8."));
		}
		// This card has it now: the next card gets PICO-8 only if it is
		// chosen again
		if (job.pico8) state.pico8 = null;
		void zy.offsetWidth;
		zy.classList.add("hop");
	} else if (f.cancelled && onlyPico8) {
		$("finished-title").textContent = "Stopped";
		head.textContent = "Only " + yours + " was copied, to Bios/PICO-8.";
		$("finished-what").textContent = "The system on the card is as it was.";
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
			: onlyPico8 ? "Only " + yours + " was copied, to Bios/PICO-8. The system on the card is as it was."
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

// ---- an image or a PICO-8 zip dropped on the window ----------------------

const isZip = (path) => /\.zip$/i.test(path || "");

function dropEvents(handler) {
	const wv = TAURI && TAURI.webview && TAURI.webview.getCurrentWebview && TAURI.webview.getCurrentWebview();
	if (wv && wv.onDragDropEvent) return wv.onDragDropEvent((e) => handler(e.payload));
	// Plain browser: nothing to drop paths from
}
dropEvents((p) => {
	if (state.screen !== "main") return;
	if (p.type === "enter" && p.paths && p.paths.length)
		$("drop-what").textContent = isZip(p.paths[0]) ? "Drop your PICO-8 zip to use it" : "Drop the card image to use it";
	if (p.type === "enter" || p.type === "over") $("drop").hidden = false;
	else $("drop").hidden = true;
	if (p.type !== "drop" || !p.paths || !p.paths.length) return;
	// A .zip is the player's PICO-8; an .img or .img.xz the card image
	if (isZip(p.paths[0])) usePico8(p.paths[0]);
	else useImage(invoke("check_image", { path: p.paths[0] }));
});

// ---- the installer's own update ------------------------------------------
//
// One look on open (installer_update). Nothing shows while it runs, when this
// is the newest installer, or when GitHub can't be asked: a notice, never in
// the way. A newer one is a chip in the header:
// - "Update to 1.0.7" where the installer can replace itself (macOS, Windows):
//   it downloads the new one and checks its signature ("Downloading 1.0.7 ·
//   42%", a line under the words), then waits for "Restart to update", so it
//   never restarts under the hand. That click puts the new one in place and
//   opens it.
// - "Installer 1.0.7 is out" where it can't (Linux's packages; a Mac app
//   macOS runs from a read-only copy, when the original can't be found): the
//   releases page, as before.
// - An update that didn't work says so, and its click is the releases page
//   too.
// Never during a write: the chip is hidden on the working screen, and the app
// refuses to restart while a card is being written.

const update = { state: null, version: null, part: null, error: null, why: null };

function renderUpdate() {
	const chip = $("newer");
	const u = update;
	chip.hidden = !u.state || state.screen === "working";
	if (!u.state) return;
	const v = u.version;
	chip.classList.toggle("ready", u.state === "ready");
	chip.classList.toggle("failed", u.state === "failed");
	chip.classList.toggle("loading", u.state === "downloading");
	chip.disabled = u.state === "downloading" || u.state === "installing";
	chip.style.setProperty("--part", (u.part || 0) * 100 + "%");
	chip.textContent = {
		available: "Update to " + v,
		downloading: "Downloading " + v + (u.part != null ? " · " + Math.floor(u.part * 100) + "%" : ""),
		ready: "Restart to update",
		installing: "Installing " + v,
		failed: "Couldn't update · Get " + v + " on GitHub",
		page: "Installer " + v + " is out",
	}[u.state];
	chip.title = {
		available: "Downloads ZyperOS Installer " + v + ", checks its signature, then asks to restart into it.",
		downloading: "The card choices still work meanwhile.",
		ready: "ZyperOS Installer " + v + " is downloaded and its signature checked. Restart to use it.",
		installing: "",
		failed: u.error || "",
		page: u.why || "Opens the releases page, to download it.",
	}[u.state];
}

async function downloadUpdate() {
	update.state = "downloading";
	update.part = null;
	renderUpdate();
	try {
		await invoke("download_update");
		update.state = "ready";
	} catch (e) {
		update.state = "failed";
		update.error = String(e);
	}
	renderUpdate();
}

async function restartToUpdate() {
	update.state = "installing";
	renderUpdate();
	render();
	try {
		// Ends this window when it works
		await invoke("restart_to_update");
	} catch (e) {
		// A card being written: the restart waits for it, still offered
		if (/being written/.test(String(e))) update.state = "ready";
		else { update.state = "failed"; update.error = String(e); }
	}
	renderUpdate();
	render();
}

$("newer").addEventListener("click", () => {
	switch (update.state) {
	case "available": return downloadUpdate();
	case "ready": return restartToUpdate();
	case "failed": case "page": return invoke("open_installer_releases");
	}
});
listen("update-progress", (e) => {
	if (update.state !== "downloading") return;
	update.part = e.payload.total ? Math.min(1, e.payload.done / e.payload.total) : null;
	renderUpdate();
});

async function findUpdate() {
	let offer = null;
	try { offer = await invoke("installer_update"); } catch (_) { return; }
	if (!offer) return;
	update.version = offer.version;
	update.why = offer.why || null;
	update.state = offer.in_place ? "available" : "page";
	renderUpdate();
}

// ---- start ---------------------------------------------------------------

// Which ZyperOS the release row installs, and for which devices, asked once;
// again every half minute while GitHub can't be reached. No release at all:
// the image file is the way, and is chosen.
async function findRelease() {
	try {
		const latest = await invoke("latest_zyperos");
		state.release = latest.version;
		state.devices = latest.devices;
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
findUpdate();
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
	$("ver").textContent = q.get("ver") || "1.0.6";
	demo.api = {
		listen: (name, h) => { (handlers[name] = handlers[name] || []).push(h); return Promise.resolve(); },
		invoke: (cmd, args) => {
			switch (cmd) {
			case "list_cards": return Promise.resolve(cards);
			// ?release for a release to download, the Pixel 2's only; with
			// ?brick, the Brick's too
			case "latest_zyperos": return q.has("release")
				? Promise.resolve({ version: "0.1.0", devices: q.has("brick") ? ["pixel2", "brick"] : ["pixel2"] })
				: Promise.reject("ZyperOS has no release to download yet. Choose an image file instead.");
			// ?update=available (or page, for Linux's chip) shows a newer
			// installer; downloading, ready, installing and failed show that
			// state and stay in it, for a look at each
			case "installer_update": {
				const u = q.get("update");
				if (!u) return Promise.resolve(null);
				if (["downloading", "ready", "installing", "failed"].includes(u)) {
					setTimeout(() => {
						Object.assign(update, { state: u, part: 0.42,
							error: "The download's signature didn't match ZyperOS's key, so it wasn't used." });
						renderUpdate();
					});
				}
				return Promise.resolve({ version: "1.0.7", in_place: u !== "page" });
			}
			case "download_update":
				return new Promise((resolve) => {
					const total = 6.1e6;
					let done = 0;
					const tick = () => {
						done = Math.min(total, done + total / 25);
						emit("update-progress", { done, total });
						if (done < total) setTimeout(tick, 80); else resolve();
					};
					tick();
				});
			case "restart_to_update":
				setTimeout(() => { location.search = q.has("release") ? "?release" : ""; }, 700);
				return new Promise(() => {});
			// ?zip=windows for a zip that isn't the Raspberry Pi one
			case "pick_pico8":
				return Promise.resolve("/tmp/pico-8_0.2.7_" + (q.get("zip") === "windows" ? "windows" : "raspi") + ".zip");
			case "check_pico8": {
				const name = args.path.split("/").pop();
				return new Promise((resolve) => setTimeout(() => resolve(/windows/.test(name)
					? { path: args.path, name, version: "", refused: "This is PICO-8 for Windows. Choose the Raspberry Pi zip: only it has pico8_64, the build the handheld runs." }
					: { path: args.path, name, version: "0.2.7", refused: null }), 600));
			}
			case "pick_image": case "check_image":
				return Promise.resolve({ path: "/tmp/ZyperOS-v0.1.0-pixel2.img.xz", name: "ZyperOS-v0.1.0-pixel2.img.xz",
					file_bytes: 74e6, image_bytes: 352321536, device: "pixel2", checksum: true });
			case "cancel": stop = true; return Promise.resolve();
			case "start": {
				stop = false;
				let phase = 0, done = 0;
				const p8 = ["pico8", "pico8check"];
				const phases = args.action === "pico8" ? p8 : ["verifying"].concat(args.pico8 ? p8 : [], ["writing", "checking"]);
				const tick = () => {
					if (stop) return emit("finished", { ok: false, cancelled: true, message: "cancelled" });
					// PICO-8's two files, or the image's bytes
					const total = phases[phase].startsWith("pico8") ? 2 : 352321536;
					done = Math.min(total, done + (total === 2 ? 1 : total / (phase === 0 ? 8 : 40)));
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

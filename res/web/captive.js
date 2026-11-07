/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The note in a phone's sign-in sheet, on the handheld's own network.
 *
 * When the handheld is the hotspot (src/hotspot.h), a phone that joins opens
 * this page by itself, in its sign-in sheet: a small browser that cannot do
 * everything. An iPhone's can upload but cannot save a download. Android's
 * (AOSP CaptivePortalLogin, a WebView) can download but has no file picker,
 * so it cannot upload; where Android opens the sheet in a Chrome tab instead,
 * both work and nothing is said. The real browser does both once the phone
 * agrees to stay on a network without internet, and this says how.
 *
 * Served only on the hotspot (src/hare.c, hare_set_captive); everywhere else
 * this file is answered empty. Nothing here touches the page's own script. */
(function () {
	"use strict";
	var ua = navigator.userAgent || "";
	var iphone = /iPhone|iPad|iPod/.test(ua) && !/Safari\//.test(ua);
	var android = /Android/.test(ua) && /; wv\)/.test(ua);
	var host = location.host || "10.42.1.1";
	var text;

	if (iphone) {
		text = ["Downloads need Safari.",
		        "Tap Cancel, then Use Without Internet. Then open ", " in Safari."];
	} else if (android) {
		text = ["Uploads need Chrome.",
		        "Tap ⋮, then Use this network as is. Then open ", " in Chrome."];
	} else {
		return;
	}

	function show() {
		var note = document.createElement("aside"), b = document.createElement("b"),
		    p = document.createElement("span"), at = document.createElement("strong");

		note.setAttribute("role", "note");
		note.style.cssText =
			"position:sticky;top:0;z-index:1000;display:grid;gap:4px;margin:0;" +
			"padding:12px 16px;font:14px/1.45 Lexend,system-ui,sans-serif;" +
			"background:var(--surface,#f5f4ee);color:var(--text-soft,#3d403e);" +
			"border-bottom:1px solid var(--edge,#c9c7bc)";
		b.textContent = text[0];
		b.style.cssText = "font-weight:600;color:var(--text,#1a1c1a)";
		at.textContent = host;
		at.style.cssText = "font-weight:600;color:var(--accent-ink,#476b00)";
		p.appendChild(document.createTextNode(text[1]));
		p.appendChild(at);
		p.appendChild(document.createTextNode(text[2]));
		note.appendChild(b);
		note.appendChild(p);
		document.body.insertBefore(note, document.body.firstChild);
	}

	if (document.body) show();
	else document.addEventListener("DOMContentLoaded", show);
})();

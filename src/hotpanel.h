/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_HOTPANEL_H
#define ZYPEROS_HOTPANEL_H

#include <SDL.h>

/* Wi-Fi File Transfer's screen while the handheld is its own network
 * (src/hotspot.h), DESIGN.md 11.16: a screen of its own rather than a panel
 * over the shelf - the title, the QR code a phone joins by, the network, its
 * password and the transfer's PIN beside it, the phones and the news under
 * them. src/main.c opens and closes it as it does every panel (the whole
 * screen is the panel); this draws it. */

typedef struct {
	const char *heading;   /* the screen's name */
	const char *qr;        /* the code's text (captive_wifi_qr) */
	const char *ssid;
	const char *pass;
	const char *pin;
	const char *phones;    /* "starting", "waiting", "1 joined" */
	const char *now;       /* the transfer's last news, as the rows say it */
	const char *legend;    /* the key legend at the foot; NULL: "B: close" */
} hotpanel;

/* The whole screen, in the launcher's 1024x768, ground and all, at rest
 * (the caller's pass takes care of the motion). `now_ms` runs a value too
 * long for its line. */
void hotpanel_draw(SDL_Renderer *r, const hotpanel *p, unsigned now_ms);

#endif

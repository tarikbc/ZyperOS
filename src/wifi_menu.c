/* The Wi-Fi screen, minus the screen.
 *
 * State and row building only. This file must never include SDL: a build
 * function has to be callable with no renderer and no device, and
 * tools/menu-check.c links it directly to prove it. If that stops being true
 * the menu design has failed and should be reopened rather than the
 * requirement quietly dropped.
 *
 * The half that needs a screen - wifi_key, wifi_screen - stays in main.c.
 */
#include "wifi_menu.h"
#include "keyhints.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Signal as a word. dBm is the honest number and it is also jargon; the list
 * is sorted strongest first anyway, so the word only has to separate "this
 * will work" from "this will not". */
const char *wifi_strength(int dbm)
{
	if (dbm >= -60) return T(S_WIFIMENU_STRONG);
	if (dbm >= -72) return T(S_WIFIMENU_GOOD);
	return T(S_WIFIMENU_WEAK);
}


/* The right-hand column for every row, rebuilt whenever the list changes.
 *
 * "connected" outranks "saved": after a successful connect the list was redrawn
 * saying only that the network was known, which is what it said before the
 * connect too, so the screen gave no sign anything had happened. */
void wifi_label(wifi_ui *w)
{
	char cur[WIFI_SSID_MAX];
	int i;

	if (wifi_status(cur, sizeof cur, NULL, 0) != WIFI_CONNECTED) cur[0] = '\0';
	w->connected = cur[0] != '\0';
	for (i = 0; i < w->n; i++) {
		/* The signal, and what the network is to us */
		const char *fmt = (cur[0] && !strcmp(cur, w->nets[i].ssid))
		                  ? T(S_WIFIMENU_SIGNAL_CONNECTED)
		                  : w->nets[i].known ? T(S_WIFIMENU_SIGNAL_SAVED)
		                  : w->nets[i].secured ? "%s" : T(S_WIFIMENU_SIGNAL_OPEN);

		/* A saved network listed before the scan has no signal reading, and
		 * printing "weak" for one that has simply not been heard from yet
		 * would be a measurement we do not have. */
		if (!w->scanned && w->nets[i].known)
			snprintf(w->vals[i], sizeof w->vals[i], "%s", T(S_WIFIMENU_SAVED));
		else
			snprintf(w->vals[i], sizeof w->vals[i], fmt, wifi_strength(w->nets[i].signal));
	}
}

/* Start a scan and show what we already know while it runs.
 *
 * Entering this screen used to stand still behind a "Scanning..." panel for up
 * to nine seconds, because a scan settles slowly on purpose. The saved networks
 * need no scan at all - the supplicant is holding them - so they go up
 * immediately and the scan fills in around them. */
void wifi_begin_scan(wifi_ui *w)
{
	w->scanned = false;
	w->n = wifi_known(w->nets, WIFI_MAX_NETS);
	wifi_label(w);
	w->scanning = wifi_scan_start();
}

int wifi_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	wifi_ui *w = ctx;
	int nrows = 0, i;
	bool any_saved = false;

	*heading = T(S_MENU_WIFI);

	/* The scan is driven from here because this is the function that already
	 * runs every frame. wifi_scan_poll() does nothing until its next second is
	 * due, so calling it per frame costs a comparison. When results land the
	 * provisional list of saved networks is replaced by what the radio heard.
	 *
	 * This is still the only place rows are built, and it still touches no
	 * renderer. */
	if (w->scanning && wifi_scan_poll() == 1) {
		int got = wifi_scan_take(w->nets, WIFI_MAX_NETS);

		w->scanning = false;
		w->scanned = true;
		if (got >= 0) w->n = got;
		wifi_label(w);
	}

	/* A saved network rejoins on its own, and the address comes a few seconds
	 * after the scan has finished: the list was labeled before it, said
	 * "saved" while connected, and nothing labeled it again. So while the
	 * radio is on and nothing shows as connected, look again once a second -
	 * the look is also what asks for the address. Not every frame: each look
	 * forks wpa_cli. */
	if (w->on && !w->scanning && !w->connected && time(NULL) >= w->look_next) {
		w->look_next = (long)time(NULL) + 1;
		wifi_label(w);
	}

	/* The switch is row 0, so the state of the radio is the first thing read
	 * and the first thing reachable. */
	rows[nrows++] = (menu_row){ T(S_MENU_WIFI), w->on ? T(S_MENU_ON) : T(S_MENU_OFF), true };
	for (i = 0; i < w->n && nrows < max - 2; i++)
		rows[nrows++] = (menu_row){ w->nets[i].ssid, w->vals[i], true };

	/* These are footers, not list items - they say something about the list
	 * rather than offering anything - so they sit under a rule like the key
	 * legend does. */
	if (!w->on) {
		rows[nrows++] = MENU_RULE;
		rows[nrows++] = MENU_NOTE(T(S_WIFIMENU_TURN_ON));
		return nrows;
	}
	if (w->n == 0 && !w->scanning) {
		rows[nrows++] = MENU_RULE;
		rows[nrows++] = MENU_NOTE(T(S_WIFIMENU_NONE_FOUND));
		return nrows;
	}
	for (i = 0; i < w->n; i++)
		if (w->nets[i].known) { any_saved = true; break; }
	rows[nrows++] = MENU_RULE;
	/* What is happening, then what you can do. The footer is the honest place
	 * for progress: it is already outside the list, and swapping its text
	 * means the screen never has to put a modal in front of you. */
	rows[nrows++] = w->scanning
	              ? MENU_NOTE(T(S_WIFIMENU_SCANNING))
	              : MENU_NOTE(any_saved
	                          ? keyhints_legend((key_hint[]){ { "Y", T(S_KEYHINT_RESCAN) },
	                                                          { "X", T(S_KEYHINT_FORGET) } }, 2)
	                          : keyhints_legend((key_hint[]){ { "Y", T(S_KEYHINT_RESCAN) } }, 1));
	return nrows;
}


/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See bt_menu.h. No SDL, deliberately. */
#include "bt_menu.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

const char *bt_menu_footer(const bt_ui *u)
{
	if (!u) return "";
	if (u->note[0]) return u->note;
	/* No adapter means the stack was never started, which only launch.sh can
	 * do and only at boot. A toggle is still worth offering - it saves the
	 * preference - but it must not promise anything for right now. */
	if (u->state == BT_NO_ADAPTER)  return T(S_BTMENU_TURN_ON_BOOT);
	if (u->state == BT_POWERED_OFF) return T(S_BTMENU_TURN_ON);
	if (u->scanning)                return T(S_BTMENU_SEARCHING);
	if (u->n == 0)                  return T(S_BTMENU_SEARCH);
	/* On the toggle row rather than a device. */
	/* Not "turn Bluetooth off": the heading and the row it is under both
	 * say Bluetooth already, and a footer repeating it is noise. */
	if (u->cursor < 0) return T(S_BTMENU_TURN_OFF_SEARCH);
	if (u->cursor >= u->n) return T(S_BTMENU_SEARCH);
	if (u->dev[u->cursor].connected)
		return T(S_BTMENU_DISCONNECT);
	if (u->dev[u->cursor].bonded)
		return T(S_BTMENU_CONNECT);
	return T(S_BTMENU_PAIR);
}

int bt_menu_build(const bt_ui *u, menu_row *out, int max, int visible)
{
	int nrows = 0, i;

	if (!u || !out || max < 3) return 0;

	/* The toggle first, the way the Wi-Fi screen leads with its own. Without
	 * it there was no way to turn the radio off at all from the device.
	 *
	 * Live even with no adapter, because it still does something there: it
	 * saves the preference launch.sh reads at the next boot, which is the
	 * only thing that can start the stack. Drawn quiet while the footer said
	 * to press it would have been the screen contradicting itself. */
	out[nrows++] = (menu_row){ "Bluetooth",
	                           u->state == BT_NO_ADAPTER ? T(S_BTMENU_NO_ADAPTER)
	                           : u->state == BT_READY ? T(S_MENU_ON) : T(S_MENU_OFF),
	                           true };

	for (i = u->top; i < u->n && nrows < max - 2 && i < u->top + visible; i++)
		out[nrows++] = (menu_row){ u->dev[i].name, u->vals[i], true };

	/* A footer says something about the list rather than offering anything,
	 * so it sits under a rule - the same reason and the same shape as the key
	 * legend and the Wi-Fi screen's. */
	out[nrows++] = MENU_RULE;
	out[nrows++] = MENU_NOTE(bt_menu_footer(u));
	return nrows;
}

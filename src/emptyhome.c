/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The empty home's words: see emptyhome.h. */
#include "emptyhome.h"
#include "sys_menu.h"   /* XFER_NAME_CABLE, XFER_NAME_WIFI: the screens' own names */

#include <stdio.h>
#include <string.h>

/* The button that starts a way */
static const char *eh_key(eh_way w)
{
	return w == EH_USB ? "A" : "Y";
}

int eh_way_of_key(char key)
{
	return key == 'A' ? EH_USB : key == 'Y' ? EH_WIFI : -1;
}

void eh_words(const eh_state *s, eh_way w, eh_text *out, char *buf, size_t n)
{
	memset(out, 0, sizeof *out);
	out->key = eh_key(w);
	if (w == EH_USB) {
		out->name = XFER_NAME_CABLE;
		out->step[0] = "Plug into a computer";
		/* The question a computer gets on a port that only charges
		 * (usblink.h); a cable that links on its own has none */
		out->step[1] = s->shared_port ? "Choose File Transfer" : "Press A";
		if (s->cable_addr[0] && buf && n) {
			snprintf(buf, n, "Browse to %s", s->cable_addr);
			out->step[2] = buf;
		} else {
			out->step[2] = "Open the address it shows";
		}
		if (!s->ok[EH_USB])
			out->why = s->shared_port ? "Unplug the Wi-Fi adapter."
			                          : "Plug into a computer first.";
	} else {
		out->name = XFER_NAME_WIFI;
		out->step[0] = s->builtin_wifi ? "Connect to Wi-Fi" : "Plug in the Wi-Fi adapter";
		out->step[1] = "Press Y";
		out->step[2] = "Open the address it shows";
		if (!s->ok[EH_WIFI])
			out->why = s->builtin_wifi ? "Connect to Wi-Fi in MENU first."
			                           : "Plug in the Wi-Fi adapter first.";
	}
}

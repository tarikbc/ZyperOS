/* SPDX-License-Identifier: GPL-3.0-only */
/* The empty home's words: see emptyhome.h. */
#include "emptyhome.h"
#include "i18n.h"
#include "sys_menu.h"

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
		out->name = T(S_EMPTYHOME_CARD_USB);
		out->step[0] = T(S_EMPTYHOME_PLUG_COMPUTER);
		/* The question a computer gets on a port that only charges
		 * (usblink.h); a cable that links on its own has none */
		out->step[1] = s->shared_port ? T(S_EMPTYHOME_CHOOSE_XFER) : T(S_EMPTYHOME_PRESS_A);
		if (s->cable_addr[0] && buf && n) {
			snprintf(buf, n, T(S_EMPTYHOME_BROWSE_TO), s->cable_addr);
			out->step[2] = buf;
		} else {
			out->step[2] = T(S_EMPTYHOME_OPEN_ADDRESS);
		}
		if (!s->ok[EH_USB])
			out->why = s->shared_port ? T(S_EMPTYHOME_UNPLUG_ADAPTER)
			                          : T(S_EMPTYHOME_PLUG_COMPUTER_FIRST);
		else if (s->drive)
			out->also = T(S_EMPTYHOME_FLASH_DRIVE);
	} else {
		out->name = T(S_EMPTYHOME_CARD_WIFI);
		out->step[0] = s->builtin_wifi ? T(S_EMPTYHOME_CONNECT_WIFI)
		                               : T(S_EMPTYHOME_PLUG_ADAPTER);
		out->step[1] = T(S_EMPTYHOME_PRESS_Y);
		out->step[2] = T(S_EMPTYHOME_OPEN_ADDRESS);
		if (!s->ok[EH_WIFI])
			out->why = s->builtin_wifi ? T(S_EMPTYHOME_CONNECT_WIFI_FIRST)
			                           : T(S_EMPTYHOME_PLUG_ADAPTER_FIRST);
	}
}

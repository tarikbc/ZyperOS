/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The empty home's words (src/emptyhome.h) for every device and port state:
 * the screens' own names, the button each way takes, steps that never name an
 * address the device does not have, and a reason exactly when a way cannot
 * start. The words' widths on the cards are held by tools/home-shot-check.py,
 * which draws them. */
#include "../src/emptyhome.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void)
{
	char buf[64];
	eh_text t;

	for (int dev = 0; dev < 2; dev++)
		for (int mask = 0; mask < 4; mask++) {
			eh_state s;

			memset(&s, 0, sizeof s);
			s.shared_port = dev == 0;            /* the Pixel 2 */
			s.builtin_wifi = dev == 1;           /* the Brick */
			s.ok[EH_USB] = mask & 1;
			s.ok[EH_WIFI] = mask & 2;
			snprintf(s.cable_addr, sizeof s.cable_addr, "%s", dev ? "10.55.0.2" : "10.42.0.1");
			for (int w = 0; w < EH_WAYS; w++) {
				eh_words(&s, (eh_way)w, &t, buf, sizeof buf);
				CHECK(t.name && t.key && t.step[0] && t.step[1] && t.step[2]);
				CHECK(!strcmp(t.key, w == EH_USB ? "A" : "Y"));
				CHECK(!strcmp(t.name, w == EH_USB ? "USB File Transfer" : "Wi-Fi File Transfer"));
				CHECK((t.why == NULL) == s.ok[w]);
				CHECK(eh_way_of_key(t.key[0]) == w);
			}
			eh_words(&s, EH_USB, &t, buf, sizeof buf);
			CHECK(strstr(t.step[2], s.cable_addr) != NULL);
			/* Only a port that only charges asks the question */
			CHECK(!strcmp(t.step[1], dev ? "Press A" : "Choose File Transfer"));
			if (!s.ok[EH_USB])
				CHECK(!strcmp(t.why, dev ? "Plug into a computer first." : "Unplug the Wi-Fi adapter."));
			eh_words(&s, EH_WIFI, &t, buf, sizeof buf);
			CHECK(!strcmp(t.step[0], dev ? "Connect to Wi-Fi" : "Plug in the Wi-Fi adapter"));
			if (!s.ok[EH_WIFI])
				CHECK(!strcmp(t.why, dev ? "Connect to Wi-Fi in MENU first." : "Plug in the Wi-Fi adapter first."));
		}
	/* No address known: nothing invented */
	{
		eh_state s;

		memset(&s, 0, sizeof s);
		s.shared_port = true;
		eh_words(&s, EH_USB, &t, buf, sizeof buf);
		CHECK(!strcmp(t.step[2], "Open the address it shows"));
		eh_words(&s, EH_USB, &t, NULL, 0);
		CHECK(!strcmp(t.step[2], "Open the address it shows"));
	}
	CHECK(eh_way_of_key('B') == -1 && eh_way_of_key('X') == -1);
	if (failures) return 1;
	puts("ok: the empty home's words for both devices and every port state");
	return 0;
}

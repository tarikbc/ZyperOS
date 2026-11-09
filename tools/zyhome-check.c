/* tools/zychord.h: MENU+START held a second fires once, in either order,
 * and again only after both are let go */
#include <stdio.h>
#include "zychord.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } } while (0)

int main(void)
{
	zychord c;

	zychord_init(&c);
	CHECK(!zychord_due(&c, 0), "nothing held");
	zychord_key(&c, true, true, 0);
	CHECK(!zychord_due(&c, 5000), "MENU alone never fires");
	CHECK(zychord_wait(&c, 5000) == -1, "MENU alone: no deadline");
	zychord_key(&c, false, true, 5000);
	CHECK(zychord_wait(&c, 5000) == ZYCHORD_HOLD_MS, "the deadline is a full hold away");
	CHECK(!zychord_due(&c, 5999), "not before a second");
	CHECK(zychord_due(&c, 6000), "fires at a second");
	CHECK(!zychord_due(&c, 9000), "once per hold");
	zychord_key(&c, false, false, 9100);
	zychord_key(&c, false, true, 9200);
	CHECK(!zychord_due(&c, 12000), "START let go and pressed again, MENU still held: no second fire");
	zychord_key(&c, false, false, 12100);
	zychord_key(&c, true, false, 12200);

	/* START first, then MENU */
	zychord_key(&c, false, true, 20000);
	zychord_key(&c, true, true, 20300);
	CHECK(!zychord_due(&c, 21200), "the hold counts from both down");
	CHECK(zychord_due(&c, 21300), "START then MENU fires");
	zychord_key(&c, true, false, 21400);
	zychord_key(&c, false, false, 21500);

	/* A quick press of both is a game's, not the way out */
	zychord_key(&c, true, true, 30000);
	zychord_key(&c, false, true, 30000);
	zychord_key(&c, false, false, 30400);
	CHECK(!zychord_due(&c, 32000), "a tap of both does nothing");
	zychord_key(&c, true, false, 30500);

	if (fails) return 1;
	printf("zyhome: chord ok\n");
	return 0;
}

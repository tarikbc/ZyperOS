/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* What USB File Transfer's SSH reads in dropbear's log, and the shadow it
 * gives dropbear (src/sshlink.h).
 *
 *     make check-sshlink
 *
 * The count is what turns SSH off after five wrong PINs and what holds Auto
 * Off off while somebody is signed in, so it is read from dropbear 2026.91's
 * own lines, by each message's start. 2026.91 logs an account's own name,
 * not a client's text, but a line whose name or address happened to read
 * "Exit (x" or "Password auth succeeded for x" must count as nothing all
 * the same.
 *
 * No device, no dropbear. Every answer is a number or a string.
 */
#include <stdio.h>
#include <string.h>

#include "../src/sshlink.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static sshlink_count feed(const char *const *lines)
{
	sshlink_count c = { 0, 0, false };

	for (; *lines; lines++) sshlink_feed(&c, *lines);
	return c;
}

int main(void)
{
	sshlink_count c;

	printf("  a sign-in, by PIN or by key, and its end:\n");
	{
		const char *const L[] = {
			"[812] Nov 05 16:00:01 Child connection from 10.42.0.2:51234",
			"[812] Nov 05 16:00:03 Password auth succeeded for 'root' from 10.42.0.2:51234",
			"[815] Nov 05 16:00:09 Pubkey auth succeeded for 'root' with ssh-ed25519 key SHA256:abc from 10.42.0.2:51240",
			NULL };
		c = feed(L);
		CHECK(c.sessions == 2 && c.bad == 0, "two sign-ins read as %d, %d bad", c.sessions, c.bad);
	}
	{
		const char *const L[] = {
			"[812] Nov 05 16:00:03 Password auth succeeded for 'root' from 10.42.0.2:51234",
			"[812] Nov 05 16:02:10 Exit (root) from <10.42.0.2:51234>: Disconnect received",
			NULL };
		c = feed(L);
		CHECK(c.sessions == 0, "a sign-in and its exit left %d", c.sessions);
	}

	printf("  wrong PINs, and the ones that are not:\n");
	{
		const char *const L[] = {
			"[820] Nov 05 16:03:00 Bad password attempt for 'root' from 10.42.0.2:51300",
			"[820] Nov 05 16:03:01 Bad password attempt for 'root' from 10.42.0.2:51300",
			"[820] Nov 05 16:03:02 Exit before auth from <10.42.0.2:51300>: (user 'root', 2 fails): Max auth tries reached",
			"[821] 1793896919 Bad password attempt for 'root' from 10.42.0.2:51302",
			NULL };
		c = feed(L);
		CHECK(c.bad == 3, "three wrong PINs read as %d", c.bad);
		CHECK(c.sessions == 0, "an exit before auth signed somebody out: %d", c.sessions);
	}

	printf("  a user name that says something else counts as nothing:\n");
	{
		const char *const L[] = {
			"[830] Nov 05 16:04:00 Password auth succeeded for 'root' from 10.42.0.2:51400",
			"[831] Nov 05 16:04:01 Login attempt for nonexistent user 'Password auth succeeded for x' from 10.42.0.2:51401",
			"[831] Nov 05 16:04:02 Exit before auth from <10.42.0.2:51401>: (user 'x Exit (root) y', 1 fails): Exited normally",
			"[832] Nov 05 16:04:03 Login attempt for nonexistent user 'Bad password attempt for ' from 10.42.0.2:51402",
			"[833] Nov 05 16:04:04 Login attempt for nonexistent user 'Failed listening on ' from 10.42.0.2:51403",
			NULL };
		c = feed(L);
		CHECK(c.sessions == 1, "a name moved the sign-in count to %d", c.sessions);
		CHECK(c.bad == 0, "a name counted as %d wrong PINs", c.bad);
		CHECK(!c.failed, "a name stopped SSH");
	}

	printf("  a port it cannot listen on:\n");
	{
		const char *const L[] = {
			"[700] Nov 05 15:59:59 Failed listening on '10.42.0.1:22': Error listening: Address in use",
			"[700] Nov 05 15:59:59 Early exit: No listening ports available.",
			NULL };
		c = feed(L);
		CHECK(c.failed, "dropbear could not listen and it went unread");
	}

	printf("  garbage is ignored:\n");
	{
		const char *const L[] = { "", "]", "[", "no bracket Bad password attempt for x",
		                          "[1] Nov", "[1] Nov 05", NULL };
		c = feed(L);
		CHECK(c.bad == 0 && c.sessions == 0 && !c.failed, "garbage counted");
	}

	printf("  the shadow dropbear reads:\n");
	{
		static const char sh[] =
			"root::::::::\n"
			"daemon:*:::::::\n"
			"nobody:*:::::::\n";
		char out[512];

		CHECK(sshlink_shadow(sh, "$6$salt$hash", out, sizeof out), "no shadow built");
		CHECK(!strcmp(out, "root:$6$salt$hash:::::::\ndaemon:*:::::::\nnobody:*:::::::\n"),
		      "root's line was not replaced alone: [%s]", out);
		CHECK(sshlink_shadow("daemon:*::\nroot:!:19000:0:99999:7:::", "*", out, sizeof out) &&
		      !strcmp(out, "daemon:*::\nroot:*:19000:0:99999:7:::\n"),
		      "root's aging fields were not kept: [%s]", out);
		CHECK(!sshlink_shadow("daemon:*:::::::\n", "x", out, sizeof out),
		      "a shadow with no root line was taken");
		CHECK(!sshlink_shadow(sh, "$6$salt$hash", out, 20), "a shadow that does not fit was taken");
		CHECK(!sshlink_shadow("rootless:*::\n", "x", out, sizeof out), "rootless read as root");
		CHECK(sshlink_shadow("root::::::::\ntarik:$6$ab$cdef:19000:0:99999:7:::\nodd\n", "$6$s$h",
		                     out, sizeof out) &&
		      !strcmp(out, "root:$6$s$h:::::::\ntarik:*:19000:0:99999:7:::\nodd\n"),
		      "another account kept a password: [%s]", out);
	}

	if (failures) {
		printf("sshlink: %d FAILED\n", failures);
		return 1;
	}
	printf("sshlink: dropbear's sign-ins, exits, wrong PINs and listen failures are counted, "
	       "a user name moves nothing, and in the shadow only root has a password: the PIN's\n");
	return 0;
}

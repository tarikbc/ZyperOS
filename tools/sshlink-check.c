/* SPDX-License-Identifier: GPL-3.0-only */
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
 * And the run folder SSH starts from, /tmp/zyperos-ssh, cleaned whole
 * (src/rmtree.h): dropbear's home is bound onto /root, so what a session
 * writes to ~ lands in it, and a clean of the two entries ZyperOS put there
 * left the folder behind; every Developer Mode start after a launcher
 * restart then failed to make it again ("File exists") until a reboot. Here
 * in a temp folder, with the card's key folder linked from it as .ssh.
 *
 * No device, no dropbear. Every answer is a number, a string or a file.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/rmtree.h"
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

static bool there(const char *p)
{
	struct stat st;

	return lstat(p, &st) == 0;
}

static void put(const char *p, const char *text)
{
	FILE *f = fopen(p, "w");

	if (f) { fputs(text, f); fclose(f); }
}

static bool says(const char *p, const char *text)
{
	char b[128];
	size_t n;
	FILE *f = fopen(p, "r");

	if (!f) return false;
	n = fread(b, 1, sizeof b - 1, f);
	fclose(f);
	b[n] = '\0';
	return !strcmp(b, text);
}

/* /tmp/zyperos-ssh as prepare() leaves it (src/sshlink.c), and what a
 * session put in ~ meanwhile: a shell's history, a folder with a file, and
 * a link to a file elsewhere. */
static void run_folder_fill(const char *t, const char *run, const char *keys)
{
	char p[512];

	snprintf(p, sizeof p, "%s/home", run);
	mkdir(run, 0700);
	mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/shadow", run);
	put(p, "root:$6$s$h:::::::\n");
	snprintf(p, sizeof p, "%s/home/.ssh", run);
	if (symlink(keys, p) != 0) printf("  (could not link %s)\n", p);
	snprintf(p, sizeof p, "%s/home/.ash_history", run);
	put(p, "ls\nexit\n");
	snprintf(p, sizeof p, "%s/home/stuff", run);
	mkdir(p, 0700);
	snprintf(p, sizeof p, "%s/home/stuff/notes.txt", run);
	put(p, "x");
	snprintf(p, sizeof p, "%s/outside.txt", t);
	put(p, "keep");
	{
		char l[512];

		snprintf(l, sizeof l, "%s/home/outside", run);
		if (symlink(p, l) != 0) printf("  (could not link %s)\n", l);
	}
}

static void run_folder(void)
{
	char t[] = "/tmp/sshlink-check.XXXXXX", run[256], keys[256], p[512];

	printf("  the run folder, cleaned whole:\n");
	if (!mkdtemp(t)) { CHECK(false, "no temp folder"); return; }
	snprintf(run, sizeof run, "%s/zyperos-ssh", t);
	snprintf(keys, sizeof keys, "%s/keys", t);
	mkdir(keys, 0700);
	snprintf(p, sizeof p, "%s/dropbear_ed25519_host_key", keys);
	put(p, "host key");
	snprintf(p, sizeof p, "%s/authorized_keys", keys);
	put(p, "ssh-ed25519 AAAA me\n");

	/* 1.0.5's clean: the shadow, the .ssh link, then the two folders */
	run_folder_fill(t, run, keys);
	snprintf(p, sizeof p, "%s/shadow", run);
	unlink(p);
	snprintf(p, sizeof p, "%s/home/.ssh", run);
	unlink(p);
	snprintf(p, sizeof p, "%s/home", run);
	rmdir(p);
	rmdir(run);
	CHECK(there(run), "the old clean was expected to leave the folder, as it did on the Pixel");
	rm_tree(run);

	/* Now: the whole folder, links removed and never followed */
	run_folder_fill(t, run, keys);
	CHECK(rm_tree(run), "rm_tree said the folder stayed");
	CHECK(!there(run), "%s is still there after the clean", run);
	snprintf(p, sizeof p, "%s/dropbear_ed25519_host_key", keys);
	CHECK(says(p, "host key"), "the card's host key was touched");
	snprintf(p, sizeof p, "%s/authorized_keys", keys);
	CHECK(says(p, "ssh-ed25519 AAAA me\n"), "the card's authorized_keys was touched");
	snprintf(p, sizeof p, "%s/outside.txt", t);
	CHECK(says(p, "keep"), "a file a link in ~ led to was touched");
	CHECK(rm_tree(run), "a folder that is not there is not a failure");
	CHECK(mkdir(run, 0700) == 0, "and the next start could not make it again");
	rmdir(run);

	/* The run folder's name itself a link (to the key folder): the link
	 * goes, and nothing it leads to */
	if (symlink(keys, run) != 0) printf("  (could not link %s)\n", run);
	CHECK(rm_tree(run) && !there(run), "a link in the run folder's place stayed");
	snprintf(p, sizeof p, "%s/authorized_keys", keys);
	CHECK(says(p, "ssh-ed25519 AAAA me\n"), "and a link in its place led the clean into the key folder");
	rm_tree(t);
	CHECK(!there(t), "the temp folder stayed");
}

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

	printf("  where SSH listens, and how one signs in (sshlink_want):\n");
	{
		/* Every combination of the four facts: the password's SSH only
		 * with no radio at all, Wi-Fi's only with the choice on */
		int k;

		for (k = 0; k < 16; k++) {
			sshlink_facts f = { .cable = k & 1, .radio = (k >> 1) & 1,
			                    .over_wifi = (k >> 2) & 1, .cable_spent = (k >> 3) & 1 };
			sshlink_mode m = sshlink_want(&f), want;

			if (f.radio) want = f.over_wifi ? SSHLINK_NET : SSHLINK_OFF;
			else want = f.cable && !f.cable_spent ? SSHLINK_CABLE : SSHLINK_OFF;
			CHECK(m == want, "cable %d radio %d over_wifi %d spent %d: %d, not %d",
			      f.cable, f.radio, f.over_wifi, f.cable_spent, (int)m, (int)want);
			CHECK(!(f.radio && m == SSHLINK_CABLE),
			      "the password offered with a radio there (cable %d over_wifi %d spent %d)",
			      f.cable, f.over_wifi, f.cable_spent);
		}
		/* The four the device meets, by name */
		CHECK(sshlink_want(&(sshlink_facts){ .radio = true }) == SSHLINK_OFF,
		      "Wi-Fi up with the choice off did not stop SSH");
		CHECK(sshlink_want(&(sshlink_facts){ .radio = true, .over_wifi = true }) == SSHLINK_NET,
		      "Wi-Fi up with the choice on did not run SSH over Wi-Fi");
		CHECK(sshlink_want(&(sshlink_facts){ .cable = true, .radio = true, .over_wifi = true }) ==
		      SSHLINK_NET, "the cable and Wi-Fi together did not take keys alone");
		CHECK(sshlink_want(&(sshlink_facts){ .cable = true, .over_wifi = true }) == SSHLINK_CABLE,
		      "the cable alone, the choice on, lost its password");
		CHECK(sshlink_want(&(sshlink_facts){ .cable = true, .over_wifi = true, .cable_spent = true }) ==
		      SSHLINK_OFF, "a lockout on the cable came back before a replug");
	}

	printf("  look after look (sshlink_step, as dev_ssh_tick runs it):\n");
	{
		/* A plug, a lockout, a replug, a radio: what runs after each look,
		 * the caller setting p.mode as dev_ssh_tick does */
		sshlink_plan p = { SSHLINK_OFF, false };
		sshlink_facts plug = { .cable = true }, none = { 0 };
		sshlink_facts both = { .cable = true, .radio = true, .over_wifi = true };
		sshlink_facts wifi = { .radio = true, .over_wifi = true };
		sshlink_facts wifi_off = { .radio = true };
		sshlink_mode m;
		int k;

		m = sshlink_step(&p, &plug, SSHLINK_OFF);
		CHECK(m == SSHLINK_CABLE, "a computer plugged in: %d", (int)m);
		p.mode = m;
		/* 2026-11-15, on the Pixel: the plug that started it read as spent
		 * at the next look, and SSH stopped 200 ms after it started */
		for (k = 0; k < 5; k++) {
			m = sshlink_step(&p, &plug, SSHLINK_CABLE);
			CHECK(m == SSHLINK_CABLE && !p.spent, "look %d ended the cable's SSH it started: %d spent %d",
			      k, (int)m, p.spent);
		}
		/* Five wrong passwords: it ends by itself, and stays off */
		m = sshlink_step(&p, &plug, SSHLINK_OFF);
		CHECK(m == SSHLINK_OFF && p.spent, "a lockout came back at once: %d", (int)m);
		p.mode = SSHLINK_OFF;
		m = sshlink_step(&p, &plug, SSHLINK_OFF);
		CHECK(m == SSHLINK_OFF && p.spent, "a lockout came back before a replug: %d", (int)m);
		m = sshlink_step(&p, &none, SSHLINK_OFF);
		CHECK(m == SSHLINK_OFF && !p.spent, "the cable out kept the lockout: %d spent %d", (int)m, p.spent);
		m = sshlink_step(&p, &plug, SSHLINK_OFF);
		CHECK(m == SSHLINK_CABLE, "a replug did not bring it back: %d", (int)m);
		p.mode = m;
		/* A radio with the cable in (not the Pixel 2's, which has one
		 * port): the password's ends, keys only on every address next */
		m = sshlink_step(&p, &both, SSHLINK_CABLE);
		CHECK(m == SSHLINK_NET, "a radio with the choice on: %d", (int)m);
		p.mode = m;
		m = sshlink_step(&p, &both, SSHLINK_NET);
		CHECK(m == SSHLINK_NET, "SSH over Wi-Fi did not hold: %d", (int)m);
		m = sshlink_step(&p, &plug, SSHLINK_NET);
		CHECK(m == SSHLINK_OFF, "the radio gone, the cable's password came back on the same plug: %d", (int)m);
		p.mode = SSHLINK_OFF;
		m = sshlink_step(&p, &none, SSHLINK_OFF);
		p = (sshlink_plan){ SSHLINK_OFF, false };
		/* Wi-Fi alone, the Pixel 2's way */
		m = sshlink_step(&p, &wifi_off, SSHLINK_OFF);
		CHECK(m == SSHLINK_OFF, "Wi-Fi with the choice off started SSH: %d", (int)m);
		m = sshlink_step(&p, &wifi, SSHLINK_OFF);
		CHECK(m == SSHLINK_NET, "Wi-Fi with the choice on: %d", (int)m);
		p.mode = m;
		for (k = 0; k < 5; k++) {
			m = sshlink_step(&p, &wifi, SSHLINK_NET);
			CHECK(m == SSHLINK_NET, "look %d ended SSH over Wi-Fi: %d", k, (int)m);
		}
		m = sshlink_step(&p, &wifi_off, SSHLINK_NET);
		CHECK(m == SSHLINK_OFF, "SSH over Wi-Fi turned off went on: %d", (int)m);
		p.mode = SSHLINK_OFF;
		m = sshlink_step(&p, &wifi, SSHLINK_OFF);
		CHECK(m == SSHLINK_NET && !p.spent, "turned on again: %d", (int)m);
		p.mode = m;
		m = sshlink_step(&p, &none, SSHLINK_NET);
		CHECK(m == SSHLINK_OFF, "the radio gone, SSH over Wi-Fi went on: %d", (int)m);
	}

	printf("  what dropbear is started with (sshlink_argv):\n");
	{
		const char *argv[SSHLINK_ARGV];
		int n, k;
		bool s = false, p22 = false;

		n = sshlink_argv(SSHLINK_NET, "unused", "/k", argv);
		CHECK(n > 0 && n < SSHLINK_ARGV && argv[n] == NULL, "SSH over Wi-Fi's words: %d", n);
		for (k = 1; k < n; k++) {
			if (!strcmp(argv[k], "-s")) s = true;
			if (!strcmp(argv[k], "-p") && k + 1 < n) p22 = !strcmp(argv[k + 1], "22");
			CHECK(strcmp(argv[k], "unused"), "the cable's address in SSH over Wi-Fi's words");
		}
		CHECK(s, "SSH over Wi-Fi takes passwords: no -s");
		CHECK(p22, "SSH over Wi-Fi is not on port 22 of every address");
		CHECK(!strcmp(argv[0], "dropbear"), "its name: %s", argv[0]);

		s = p22 = false;
		n = sshlink_argv(SSHLINK_CABLE, "10.42.0.1:22", "/k", argv);
		CHECK(n > 0 && argv[n] == NULL, "the cable's words: %d", n);
		for (k = 1; k < n; k++) {
			if (!strcmp(argv[k], "-s")) s = true;
			if (!strcmp(argv[k], "-p") && k + 1 < n) p22 = !strcmp(argv[k + 1], "10.42.0.1:22");
		}
		CHECK(!s, "the cable lost its password: -s");
		CHECK(p22, "the cable's SSH is not on the cable's address alone");
		CHECK(sshlink_argv(SSHLINK_OFF, "x", "/k", argv) == 0 && argv[0] == NULL, "off started something");
	}

	printf("  over a port (sshlink_owner_of, sshlink_want_port, portkeys):\n");
	{
		const char *argv[SSHLINK_ARGV];
		int k, n;
		bool s = false, at_cable = false;

		/* Who runs SSH: the launcher, portkeys over a port, nobody in the
		 * in-game menu over a port (a second zyperos.elf for a moment) */
		CHECK(sshlink_owner_of(false, false) == SSHLINK_BY_LAUNCHER, "the shelf's SSH is not the launcher's");
		CHECK(sshlink_owner_of(true, false) == SSHLINK_BY_PORTKEYS, "a port's SSH is not portkeys'");
		CHECK(sshlink_owner_of(false, true) == SSHLINK_BY_NONE && sshlink_owner_of(true, true) == SSHLINK_BY_NONE,
		      "the in-game menu over a port runs SSH of its own");
		/* Every combination: never the password, nothing with Developer
		 * Mode off, and a radio with the switch off is nothing, as ever */
		for (k = 0; k < 16; k++) {
			sshlink_facts f = { .cable = k & 1, .radio = (k >> 1) & 1,
			                    .over_wifi = (k >> 2) & 1, .cable_spent = false };
			bool dev = (k >> 3) & 1;
			sshlink_mode m = sshlink_want_port(&f, dev), want;

			if (!dev) want = SSHLINK_OFF;
			else if (f.radio) want = f.over_wifi ? SSHLINK_NET : SSHLINK_OFF;
			else want = f.cable ? SSHLINK_CABLE_KEYS : SSHLINK_OFF;
			CHECK(m == want, "port: cable %d radio %d over_wifi %d devmode %d: %d, not %d",
			      f.cable, f.radio, f.over_wifi, dev, (int)m, (int)want);
			CHECK(m != SSHLINK_CABLE, "the password over a port (cable %d radio %d over_wifi %d)",
			      f.cable, f.radio, f.over_wifi);
		}
		/* A lockout's spent plug is the launcher's; keys only has nothing
		 * to lock, so a port on the same plug still has SSH */
		CHECK(sshlink_want_port(&(sshlink_facts){ .cable = true, .cable_spent = true }, true) == SSHLINK_CABLE_KEYS,
		      "a launcher's lockout kept SSH off over a port");
		/* dropbear over a port on the cable: its address alone, -s */
		n = sshlink_argv(SSHLINK_CABLE_KEYS, "10.42.0.1:22", "/k", argv);
		CHECK(n > 0 && argv[n] == NULL, "the port's cable words: %d", n);
		for (k = 1; k < n; k++) {
			if (!strcmp(argv[k], "-s")) s = true;
			if (!strcmp(argv[k], "-p") && k + 1 < n) at_cable = !strcmp(argv[k + 1], "10.42.0.1:22");
		}
		CHECK(s, "over a port the cable takes passwords: no -s");
		CHECK(at_cable, "over a port the cable's SSH is not on its address alone");
	}

	printf("  a development image's name over Wi-Fi (sshlink_dev_image_name):\n");
	CHECK(sshlink_dev_image_name(true, false, "192.168.1.23"), "a development image with a Wi-Fi address has no zyperos.local");
	CHECK(!sshlink_dev_image_name(false, false, "192.168.1.23"), "a release image answers the name with Developer Mode's SSH off");
	CHECK(!sshlink_dev_image_name(true, true, "192.168.1.23"), "the name taken from File Transfer while it has it");
	CHECK(!sshlink_dev_image_name(true, false, "") && !sshlink_dev_image_name(true, false, NULL),
	      "the name with no address to give");

	printf("  keys in authorized_keys (sshlink_key_lines):\n");
	CHECK(sshlink_key_lines(NULL) == 0 && sshlink_key_lines("") == 0, "nothing read as a key");
	CHECK(sshlink_key_lines("\n  \n# a comment\n\t# another\n") == 0, "blanks and comments read as keys");
	CHECK(sshlink_key_lines("ssh-ed25519 AAAA me\n") == 1, "one key");
	CHECK(sshlink_key_lines("ssh-ed25519 AAAA me\n\nssh-rsa BBBB you") == 2, "two keys, the last unended");
	CHECK(sshlink_key_lines("\r\nssh-ed25519 AAAA me\r\n") == 1, "a Windows file's lines");

	run_folder();

	if (failures) {
		printf("sshlink: %d FAILED\n", failures);
		return 1;
	}
	printf("sshlink: dropbear's sign-ins, exits, wrong PINs and listen failures are counted, "
	       "a user name moves nothing, in the shadow only root has a password: the PIN's, the "
	       "run folder goes whole, whatever a session left in ~, the card's keys untouched; "
	       "the password never with a radio, SSH over Wi-Fi only with the choice on and keys "
	       "alone (-s, every address), a lockout held until a replug; over a port portkeys' SSH, keys "
	       "only on every link, and none in the in-game menu; a development image's zyperos.local over "
	       "Wi-Fi while it has an address and File Transfer does not have the name\n");
	return 0;
}

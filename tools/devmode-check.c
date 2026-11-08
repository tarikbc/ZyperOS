/* SPDX-License-Identifier: GPL-3.0-only */
/* Developer mode on a release image (src/devmode.h).
 *
 *     make check-devmode
 *
 * The unlock is held on a clock the check moves by hand: five seconds of L2
 * and R2 together open it once, with a bar that only rises, and letting go of
 * either one early, another page, or holding on after it opened do not. The setting is held in a real device database in
 * /tmp. The password and the keys are held as Developer (SSH) uses them: a
 * password from the system's random numbers in the characters promised, never
 * twice the same; a key file read for public keys only, with no options in
 * front, none twice, a key already on the card kept, and a file that is not
 * keys changing nothing.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/devmode.h"

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

/* ---- the unlock ------------------------------------------------------- */

static void check_hold(void)
{
	devmode_hold h = { 0 };
	unsigned t = 1000, t0;
	bool opened = false;
	int last = 0, p;

	printf("  the unlock:\n");
	CHECK(DEVMODE_HOLD_MS == 5000u, "the hold is not five seconds");
	CHECK(devmode_hold_permille(&h, t) == 0, "a bar with nothing held");
	/* Nothing held, nothing happens */
	CHECK(!devmode_hold_step(&h, false, t), "opened with nothing held");

	/* Both held, frame by frame at 30 a second: opens exactly when the five
	 * seconds are up, the bar rising all the way and never back */
	t0 = t;
	for (t = t0; t <= t0 + 6000; t += 33) {
		bool fire = devmode_hold_step(&h, true, t);

		p = devmode_hold_permille(&h, t);
		if (fire) {
			CHECK(!opened, "opened twice in one hold");
			CHECK(t - t0 >= DEVMODE_HOLD_MS && t - t0 < DEVMODE_HOLD_MS + 33,
			      "opened at %u ms, not at 5000", t - t0);
			opened = true;
			CHECK(devmode_hold_permille(&h, t) == 0, "a bar after it opened");
		} else if (!opened) {
			CHECK(p >= last && p <= 1000, "the bar went from %d to %d", last, p);
			last = p;
		}
	}
	CHECK(opened, "five seconds of both held did not open it");
	CHECK(last > 950, "the bar reached only %d", last);
	/* Holding on does not open it again; letting go, then holding, does */
	CHECK(!devmode_hold_step(&h, true, t + 60000u), "opened again while still held");
	CHECK(!devmode_hold_step(&h, false, t + 60100u), "opened as it was let go");
	t += 61000u;
	CHECK(!devmode_hold_step(&h, true, t), "opened at once on the next hold");
	CHECK(devmode_hold_step(&h, true, t + DEVMODE_HOLD_MS), "a second hold did not open it");

	/* Letting go of either one, a moment early, starts again */
	memset(&h, 0, sizeof h);
	t = 10000;
	devmode_hold_step(&h, true, t);
	CHECK(!devmode_hold_step(&h, true, t + 4900), "opened at 4.9 s");
	CHECK(devmode_hold_permille(&h, t + 4900) == 980, "the bar says %d at 4.9 s",
	      devmode_hold_permille(&h, t + 4900));
	CHECK(!devmode_hold_step(&h, false, t + 4950), "opened as one was let go");
	CHECK(devmode_hold_permille(&h, t + 4950) == 0, "a bar after letting go");
	CHECK(!devmode_hold_step(&h, true, t + 5000), "held again, opened at once");
	CHECK(!devmode_hold_step(&h, true, t + 9900), "four.nine seconds into the second hold opened it");
	CHECK(devmode_hold_step(&h, true, t + 10000), "the second hold, five seconds in, did not open");

	/* Another page, or About closing */
	memset(&h, 0, sizeof h);
	devmode_hold_step(&h, true, 100);
	devmode_hold_reset(&h);
	CHECK(devmode_hold_permille(&h, 3000) == 0, "reset left a bar");
	CHECK(!devmode_hold_step(&h, true, 6000), "a hold carried across a reset");

	/* The clock wraps (unsigned milliseconds) without losing the hold */
	memset(&h, 0, sizeof h);
	t = 0xFFFFF000u;
	devmode_hold_step(&h, true, t);
	CHECK(!devmode_hold_step(&h, true, t + 4000u), "opened early across the wrap");
	CHECK(devmode_hold_step(&h, true, t + 5000u), "the hold across the clock's wrap");
}

/* ---- the setting ------------------------------------------------------ */

static void check_setting(const char *dir)
{
	char dev[300], lib[300];
	db *d;

	printf("  the setting:\n");
	snprintf(dev, sizeof dev, "%s/zyperos.db", dir);
	snprintf(lib, sizeof lib, "%s/library.db", dir);
	if (!db_init(dev, lib, NULL)) { printf("no database\n"); failures++; return; }
	d = db_dev();
	CHECK(!devmode_unlocked(d), "a new card starts unlocked");
	CHECK(!devmode_unlocked(NULL), "no database reads as unlocked");
	devmode_set(d, true);
	CHECK(devmode_unlocked(d), "unlocking was not kept");
	db_shutdown();
	/* Kept across a restart: the database is the card's */
	if (!db_init(dev, lib, NULL)) { printf("no database\n"); failures++; return; }
	d = db_dev();
	CHECK(devmode_unlocked(d), "unlocked did not survive a restart");
	devmode_set(d, false);
	CHECK(!devmode_unlocked(d), "turning it off was not kept");
	db_shutdown();
	if (!db_init(dev, lib, NULL)) { printf("no database\n"); failures++; return; }
	CHECK(!devmode_unlocked(db_dev()), "off did not survive a restart");
	db_shutdown();
}

/* ---- the password ----------------------------------------------------- */

static void check_password(void)
{
	char a[16], b[16];
	int counts[256] = { 0 }, i, j;
	const char *bad = "il o01";

	printf("  the password:\n");
	CHECK(devmode_password(a, sizeof a), "no password");
	CHECK(strlen(a) == DEVMODE_PASSWORD_LEN, "password is %zu characters", strlen(a));
	for (i = 0; a[i]; i++)
		CHECK(isalnum((unsigned char)a[i]) && !isupper((unsigned char)a[i]) &&
		      !strchr(bad, a[i]), "password has the character '%c'", a[i]);
	CHECK(devmode_password(b, sizeof b) && strcmp(a, b), "two passwords the same");
	CHECK(!devmode_password(a, DEVMODE_PASSWORD_LEN) && !a[0], "a short buffer gave a password");
	/* Every character appears over many draws, and none far above the rest */
	for (i = 0; i < 4000; i++) {
		devmode_password(a, sizeof a);
		for (j = 0; a[j]; j++) counts[(unsigned char)a[j]]++;
	}
	{
		int seen = 0, hi = 0, lo = 1 << 30;

		for (i = 0; i < 256; i++) {
			if (!counts[i]) continue;
			seen++;
			if (counts[i] > hi) hi = counts[i];
			if (counts[i] < lo) lo = counts[i];
		}
		/* 32000 draws over 31 letters: 1032 each, give or take 100 */
		CHECK(seen == 31, "%d different characters, not 31", seen);
		CHECK(hi < 1300 && lo > 800, "a skewed draw: %d to %d", lo, hi);
	}
}

/* ---- the keys --------------------------------------------------------- */

#define ED1 "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGq3M8bHn7e3X4Pq2c9Fv0XrA1Zt5kYwQmJd8sLhUe2P"
#define ED2 "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOo0Nn1b5Zc8Wm4Rk7TyUu3Xv9QpLs2Hd6Fg1Je8Ba4Cx"
#define RSA "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQC7vbqajDhA8yKq0Zp3o9Xw2Lr5Tn1Hk4Bm6Cd8Fg0Js2Pq4Wx7Yz9Ae"

static void put(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (!f) { perror(path); exit(2); }
	fputs(text, f);
	fclose(f);
}

static char *get(const char *path)
{
	static char buf[8192];
	FILE *f = fopen(path, "r");
	size_t n;

	buf[0] = '\0';
	if (!f) return buf;
	n = fread(buf, 1, sizeof buf - 1, f);
	buf[n] = '\0';
	fclose(f);
	return buf;
}

static int count_of(const char *hay, const char *needle)
{
	int n = 0;

	for (; (hay = strstr(hay, needle)); hay += strlen(needle)) n++;
	return n;
}

static void check_keys(const char *dir)
{
	char src[300], keys[300], keydir[300], text[4096];
	struct stat st;

	printf("  the keys:\n");
	snprintf(src, sizeof src, "%s/authorized_keys.src", dir);
	snprintf(keydir, sizeof keydir, "%s/ssh", dir);
	snprintf(keys, sizeof keys, "%s/authorized_keys", keydir);

	CHECK(devmode_key_line(ED1) && devmode_key_line(ED1 " tarik@mac") && devmode_key_line(RSA),
	      "a public key line was not read as one");
	CHECK(!devmode_key_line("") && !devmode_key_line("# a comment") &&
	      !devmode_key_line("hello world") && !devmode_key_line("ssh-ed25519") &&
	      !devmode_key_line("ssh-ed25519 short"), "a line that is not a key was read as one");
	CHECK(!devmode_key_line("command=\"rm -rf /\" " ED1) &&
	      !devmode_key_line("from=\"10.0.0.1\",no-pty " ED1),
	      "a line with options in front was read as a key");
	CHECK(!devmode_key_line("ssh-dss AAAAB3NzaC1kc3MAAACBAOo0Nn1b5Zc8Wm4Rk7TyUu3Xv9QpLs2Hd6Fg1Je8Ba4Cx"),
	      "an old DSA key was taken");
	CHECK(!devmode_key_line(ED1 " name\x01x"), "a control character in the comment was taken");

	/* No file, and no folder to write to */
	CHECK(devmode_import_keys(src, keydir) == -1, "a missing file was not an error");
	CHECK(stat(keys, &st) != 0, "a missing file made a key file");

	/* Two keys, a comment, a blank line, Windows line ends, and what is not a key */
	snprintf(text, sizeof text,
	         "# my keys\r\n%s tarik@mac\r\n\r\ncommand=\"id\" %s evil\nnot a key at all\n  %s\t\n", ED1, ED2, RSA);
	put(src, text);
	CHECK(devmode_import_keys(src, keydir) == 2, "two keys were not taken");
	CHECK(stat(keys, &st) == 0 && (st.st_mode & 077) == 0, "the key file is open to others");
	{
		char *got = get(keys);

		CHECK(count_of(got, "ssh-ed25519 ") == 1 && count_of(got, "ssh-rsa ") == 1, "wrong keys written");
		CHECK(strstr(got, "tarik@mac") && !strstr(got, "evil") && !strstr(got, "command="),
		      "a line with options came across");
		CHECK(!strchr(got, '\r'), "a Windows line end came across");
		CHECK(got[strlen(got) - 1] == '\n', "the file does not end in a line end");
	}
	/* Again: nothing twice */
	CHECK(devmode_import_keys(src, keydir) == 2, "the same file the second time");
	CHECK(count_of(get(keys), "ssh-") == 2, "a key twice after two imports");

	/* A key ssh-copy-id left stays, and a new one joins it */
	snprintf(text, sizeof text, "%s from-ssh-copy-id", ED2);
	put(keys, text);                              /* no line end at the end */
	snprintf(text, sizeof text, "%s\n", ED1);
	put(src, text);
	CHECK(devmode_import_keys(src, keydir) == 1, "one key was not taken");
	{
		char *got = get(keys);

		CHECK(strstr(got, "from-ssh-copy-id") && count_of(got, "ssh-ed25519 ") == 2,
		      "the key that was there was lost, or the new one is not");
		CHECK(!strstr(got, "from-ssh-copy-id" ED1), "two keys on one line");
	}
	/* A file with no key in it changes nothing and says 0 */
	{
		char before[8192];

		snprintf(before, sizeof before, "%s", get(keys));
		put(src, "just some text\nand more\n");
		CHECK(devmode_import_keys(src, keydir) == 0, "a file with no keys said otherwise");
		CHECK(!strcmp(before, get(keys)), "a file with no keys changed the key file");
	}
	/* A huge file is not a key list */
	{
		FILE *f = fopen(src, "w");
		int i;

		for (i = 0; i < 3000; i++) fprintf(f, "%s\n", "x just filling the file up past what a key list needs to be");
		fclose(f);
		CHECK(devmode_import_keys(src, keydir) <= 0, "a huge file was taken");
	}
	/* A folder: .pub files and authorized_keys, nothing else, no litter */
	{
		char fdir[300], f[360];

		snprintf(fdir, sizeof fdir, "%s/Developer", dir);
		mkdir(fdir, 0755);
		unlink(keys);
		snprintf(f, sizeof f, "%s/id_ed25519.pub", fdir);
		snprintf(text, sizeof text, "%s tarik@mac\n", ED1);
		put(f, text);
		snprintf(f, sizeof f, "%s/notes.txt", fdir);
		snprintf(text, sizeof text, "%s from-a-txt\n", ED2);
		put(f, text);
		snprintf(f, sizeof f, "%s/._id_ed25519.pub", fdir);
		put(f, "\x00\x05\x16\x07 resource fork\n");
		snprintf(f, sizeof f, "%s/authorized_keys", fdir);
		snprintf(text, sizeof text, "%s\n%s\n", ED1, RSA);
		put(f, text);
		CHECK(devmode_import_dir(fdir, keydir) >= 2, "a folder of keys gave no keys");
		{
			char *got = get(keys);

			CHECK(count_of(got, "ssh-ed25519 ") == 1 && count_of(got, "ssh-rsa ") == 1,
			      "the folder's keys are not each there once");
			CHECK(!strstr(got, "from-a-txt"), "a .txt file was read");
		}
		snprintf(f, sizeof f, "%s/id_ed25519.pub", fdir); unlink(f);
		snprintf(f, sizeof f, "%s/notes.txt", fdir); unlink(f);
		snprintf(f, sizeof f, "%s/._id_ed25519.pub", fdir); unlink(f);
		snprintf(f, sizeof f, "%s/authorized_keys", fdir); unlink(f);
		CHECK(devmode_import_dir(fdir, keydir) == 0, "an empty folder gave keys");
		rmdir(fdir);
		CHECK(devmode_import_dir(fdir, keydir) == -1, "a missing folder was not an error");
	}
	unlink(keys);
	rmdir(keydir);
	unlink(src);
}

int main(void)
{
	char dir[] = "/tmp/devmode-checkXXXXXX", p[400];
	const char *names[] = { "zyperos.db", "zyperos.db-wal", "zyperos.db-shm", "library.db",
	                        "library.db-wal", "library.db-shm", "authorized_keys.src.new" };
	size_t i;

	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
	check_hold();
	check_setting(dir);
	check_password();
	check_keys(dir);
	for (i = 0; i < sizeof names / sizeof names[0]; i++) {
		snprintf(p, sizeof p, "%s/%s", dir, names[i]);
		unlink(p);
	}
	rmdir(dir);
	if (failures) {
		printf("devmode: %d FAILED\n", failures);
		return 1;
	}
	printf("devmode: five seconds of L2 and R2 unlock and nothing else does; the setting is the card's; the "
	       "password is random and in its alphabet; only public keys without options come across, "
	       "once each\n");
	return 0;
}

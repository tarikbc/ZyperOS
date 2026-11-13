/* SPDX-License-Identifier: GPL-3.0-only */
/* Does the logs pack hide what it says it hides?
 *
 *     make check-logpack
 *
 * The masking on lines a log really carries - a network, a headset, an
 * account, a Bluetooth address both ways BlueZ writes one - and then a whole
 * pack built from a folder of logs, unpacked with the same tar and read back.
 *
 * No SDL, no device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/logpack.h"

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

static const char *const SECRETS[] = { "ReinFi", "OpenFit", "ericr", "Home", "Home 5G", "TV" };
#define NSECRETS ((int)(sizeof SECRETS / sizeof SECRETS[0]))

static void masked(const char *in, const char *want)
{
	char line[512];

	snprintf(line, sizeof line, "%s", in);
	logpack_mask_line(line, sizeof line, SECRETS, NSECRETS);
	CHECK(!strcmp(line, want), "\n        \"%s\"\n     -> \"%s\"\n  wanted \"%s\"", in, line, want);
}

static void words(void)
{
	char out[32];

	printf("a name, shortened\n");
	logpack_mask_word("ReinFi", out, sizeof out);
	CHECK(!strcmp(out, "R...i"), "first and last: %s", out);
	logpack_mask_word("ab", out, sizeof out);
	CHECK(!strcmp(out, "***"), "too short to keep two: %s", out);
}

static void lines(void)
{
	printf("lines a log really carries\n");
	masked("wifi: connected to ReinFi, 192.168.1.100", "wifi: connected to R...i, 192.168.1.100");
	masked("bt: OpenFit connected (C0:86:B3:A7:48:7F)", "bt: O...t connected (XX:XX:XX:XX:XX:7F)");
	masked("/org/bluez/hci0/dev_C0_86_B3_A7_48_7F", "/org/bluez/hci0/dev_XX_XX_XX_XX_XX_7F");
	masked("ra: signed in as ericr, twice: ericr", "ra: signed in as e...r, twice: e...r");
	masked("joined Home 5G, not Home", "joined H...G, not H...e");
	masked("a TV and a crc32=0123456789abcdef0123", "a TV and a crc32=0123456789abcdef0123");
	masked("md5 c086b3a748 and 12:34:56 stay", "md5 c086b3a748 and 12:34:56 stay");
	masked("", "");
	/* wpa_supplicant's lines (wifi.log): the name by where it stands, saved
	 * or not, escaped or not, and the BSSID by its shape */
	masked("1763071200.1: wlan0: Trying to associate with SSID 'OldCafe'",
	       "1763071200.1: wlan0: Trying to associate with SSID 'O...e'");
	masked("wlan0: SME: Trying to authenticate with c0:86:b3:a7:48:7f (SSID='Bob's Net' freq=2437 MHz)",
	       "wlan0: SME: Trying to authenticate with XX:XX:XX:XX:XX:7f (SSID='B...t' freq=2437 MHz)");
	masked("wlan0: CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Caf\\xc3\\xa9 \\\"5\\\"\" auth_failures=1",
	       "wlan0: CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"C...\"\" auth_failures=1");
	masked("wlan0: CTRL-EVENT-DISCONNECTED bssid=c0:86:b3:a7:48:7f reason=4 locally_generated=1",
	       "wlan0: CTRL-EVENT-DISCONNECTED bssid=XX:XX:XX:XX:XX:7f reason=4 locally_generated=1");
	masked("wlan0: SSID '' and ssid=\"\" stay empty", "wlan0: SSID '' and ssid=\"\" stay empty");
	/* A name ending in a backslash (written \\\\), and a line cut before
	 * its closing quote: neither lets the name through */
	masked("id=0 ssid=\"Lab\\\\\" auth_failures=1", "id=0 ssid=\"L...\\\" auth_failures=1");
	masked("id=0 ssid=\"SecretPlace", "id=0 ssid=\"S...e");
	masked("with SSID 'SecretPlace", "with SSID 'S...e");
}

static void pack(void)
{
	char dir[] = "/tmp/logpack-check.XXXXXX", p[600], out[600], cmd[1400], line[512];
	char err[128];
	FILE *f;
	bool found_net = false, found_mac = false, found_about = false;
	int files = 0;

	printf("a whole pack, unpacked again\n");
	if (!mkdtemp(dir)) { perror("mkdtemp"); failures++; return; }
	snprintf(p, sizeof p, "%s/logs", dir);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/logs/zyperos.log", dir);
	if ((f = fopen(p, "w"))) { fputs("wifi: connected to ReinFi\n", f); fclose(f); }
	snprintf(p, sizeof p, "%s/logs/zyperos.log.1", dir);
	if ((f = fopen(p, "w"))) { fputs("bt: headset C0:86:B3:A7:48:7F\n", f); fclose(f); }
	snprintf(p, sizeof p, "%s/logs/wifi.log", dir);
	if ((f = fopen(p, "w"))) {
		fputs("1763071200.123456: wlan0: Trying to associate with SSID 'ReinFi'\n"
		      "1763071220.654321: wlan0: CTRL-EVENT-DISCONNECTED bssid=C0:86:B3:A7:48:7F reason=4 locally_generated=1\n", f);
		fclose(f);
	}
	snprintf(p, sizeof p, "%s/logs/other.txt", dir);
	if ((f = fopen(p, "w"))) { fputs("not a log\n", f); fclose(f); }

	snprintf(p, sizeof p, "%s/logs", dir);
	snprintf(out, sizeof out, "%s/pack.tar.gz", dir);
	CHECK(logpack_build(p, "ZyperOS 1.0.3\nss: ericr\n", SECRETS, NSECRETS,
	                    "zyperos-logs-test", out, err, sizeof err),
	      "built: %s", err);

	snprintf(cmd, sizeof cmd, "cd '%s' && tar -xzf pack.tar.gz && "
	         "ls zyperos-logs-test | wc -l | tr -d ' ' && cat zyperos-logs-test/*", dir);
	if ((f = popen(cmd, "r"))) {
		if (fgets(line, sizeof line, f)) files = atoi(line);
		while (fgets(line, sizeof line, f)) {
			if (strstr(line, "ReinFi") || strstr(line, "C0:86") || strstr(line, "ericr"))
				CHECK(0, "a name got through: %s", line);
			if (strstr(line, "R...i")) found_net = true;
			if (strstr(line, "XX:XX:XX:XX:XX:7F")) found_mac = true;
			if (strstr(line, "ss: e...r")) found_about = true;
		}
		pclose(f);
	}
	CHECK(files == 4, "three logs (wifi.log among them) and about.txt, not the other file: %d", files);
	CHECK(found_net && found_mac && found_about,
	      "each masked, in the logs and in about.txt: %d %d %d",
	      found_net, found_mac, found_about);
	snprintf(p, sizeof p, "%s/logs/zyperos.log", dir);
	if ((f = fopen(p, "r"))) {
		CHECK(fgets(line, sizeof line, f) && strstr(line, "ReinFi"),
		      "the log on the card is left as it was");
		fclose(f);
	}
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (system(cmd) != 0) { }
}

/* A wifi.log far past the cap goes in as its last 256 KB, from a whole line */
static void tail(void)
{
	char dir[] = "/tmp/logpack-tail.XXXXXX", p[600], out[600], cmd[1400], line[512];
	char err[128];
	FILE *f;
	long size = 0;
	bool first = false, last = false, whole = true;
	int i;

	printf("a long wifi.log, its end only\n");
	if (!mkdtemp(dir)) { perror("mkdtemp"); failures++; return; }
	snprintf(p, sizeof p, "%s/logs", dir);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/logs/wifi.log", dir);
	if ((f = fopen(p, "w"))) {
		fputs("FIRSTLINE wlan0: Trying to associate with SSID 'ReinFi'\n", f);
		for (i = 0; i < 20000; i++)
			fprintf(f, "%d.000000: wlan0: CTRL-EVENT-DISCONNECTED bssid=c0:86:b3:a7:48:7f reason=4\n", i);
		fputs("LASTLINE\n", f);
		fclose(f);
	}
	snprintf(p, sizeof p, "%s/logs", dir);
	snprintf(out, sizeof out, "%s/pack.tar.gz", dir);
	CHECK(logpack_build(p, "", SECRETS, NSECRETS, "t", out, err, sizeof err), "built: %s", err);
	snprintf(cmd, sizeof cmd, "cd '%s' && tar -xzf pack.tar.gz && wc -c < t/wifi.log && cat t/wifi.log", dir);
	if ((f = popen(cmd, "r"))) {
		if (fgets(line, sizeof line, f)) size = atol(line);
		if (fgets(line, sizeof line, f) && !strstr(line, ".000000: wlan0:")) whole = false;
		do {
			if (strstr(line, "FIRSTLINE")) first = true;
			if (strstr(line, "LASTLINE")) last = true;
		} while (fgets(line, sizeof line, f));
		pclose(f);
	}
	CHECK(size > 0 && size <= LOGPACK_WIFI_TAIL, "at most the cap: %ld bytes", size);
	CHECK(!first && last && whole, "its end, from a whole line: first %d last %d whole %d", first, last, whole);
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (system(cmd) != 0) { }
}

int main(void)
{
	printf("logpack: the logs, masked and packed\n");
	words();
	lines();
	pack();
	tail();
	if (failures) {
		printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}

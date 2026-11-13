/* What MENU > System Update says in each state (src/sysupdate.c), against
 * the update lab's screens (docs/design/mock/update-lab.html). No SDL, no
 * device: the rows are a function of the status and what the device knows.
 *
 *     make check-sysupdate */
#include <stdio.h>
#include <string.h>

#include "../src/sysupdate.h"

static int fails;

#define CHECK(cond, what) do { if (!(cond)) { printf("  FAIL %s\n", what); fails++; } } while (0)

/* The page's button `b`: its label and action, or NULL */
static const su_key *btn(const su_page *p, const char *b)
{
	int i;
	for (i = 0; i < p->nkeys; i++)
		if (!strcmp(p->keys[i].button, b)) return &p->keys[i];
	return NULL;
}

static bool is(const su_page *p, const char *b, const char *label, su_action act)
{
	const su_key *k = btn(p, b);
	return k && !strcmp(k->label, label) && k->act == act;
}

static su_view base(const char *status)
{
	su_view v;
	memset(&v, 0, sizeof v);
	su_status_parse(status, &v.st);
	snprintf(v.installed, sizeof v.installed, "0.1.0");
	v.wifi = true;
	v.battery = 80;
	return v;
}

int main(void)
{
	su_page pg;
	su_view v;
	char buf[64];

	printf("check-sysupdate:\n");

	/* The status file as the program writes it */
	v = base("state downloading\ninstalled 0.1.0\nversion 0.2.0\ndownload 14200000\ndone 6100000\ntotal 14200000\nfiles 3 301\nmessage x y\npid 9\ntime 1793900000\n");
	CHECK(v.st.state == SU_DOWNLOADING && !strcmp(v.st.version, "0.2.0") && v.st.done == 6100000 &&
	      v.st.total == 14200000 && v.st.files_done == 3 && v.st.files_total == 301 &&
	      !strcmp(v.st.message, "x y") && v.st.time == 1793900000, "the status file reads back");
	su_status_parse("", &v.st);
	CHECK(v.st.state == SU_IDLE, "no status file is idle");

	/* Sizes as the mock writes them */
	su_size(14200000, buf, sizeof buf); CHECK(!strcmp(buf, "14 MB"), "14 MB");
	su_size(6100000, buf, sizeof buf);  CHECK(!strcmp(buf, "6.1 MB"), "6.1 MB");
	su_size(950000, buf, sizeof buf);   CHECK(!strcmp(buf, "950 KB"), "950 KB");
	CHECK(su_version_cmp("0.10.0", "0.9.2") > 0 && su_version_cmp("0.2.0-beta.1", "0.2.0") < 0 &&
	      su_version_cmp("1.0.0", "1.0.0") == 0, "versions compare as numbers");

	/* Available: the version jump, what downloads, What's New, the buttons */
	v = base("state available\nversion 0.2.0\ndownload 14200000\nsize 168000000\n");
	v.notes = true;
	snprintf(v.prev, sizeof v.prev, "0.0.9");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.from, "0.1.0") && !strcmp(pg.to, "0.2.0"), "0.1.0 to 0.2.0");
	CHECK(!strcmp(pg.sub, "14 MB to download \xc2\xb7 168 MB in all"), "what downloads, and the whole");
	CHECK(pg.notes && !pg.bar, "What's New, no bar yet");
	CHECK(is(&pg, "A", "Download", SUA_DOWNLOAD) && is(&pg, "B", "Back", SUA_BACK), "A Download, B Back");
	CHECK(is(&pg, "Y", "Roll Back to 0.0.9", SUA_ROLLBACK), "Y Roll Back to 0.0.9");
	CHECK(!strcmp(su_menu_value(&v, buf, sizeof buf), "0.2.0 available"), "MENU's row: 0.2.0 available");

	/* Below 25% without the charger it does not start; on the charger it does */
	v.battery = 18;
	su_page_build(&v, &pg);
	CHECK(!btn(&pg, "A") && !strcmp(pg.warn, "The battery is at 18%. Plug in the charger to download the update.") && !pg.notes,
	      "18%: no Download, and why in full, in What's New's place");
	v.charging = true;
	su_page_build(&v, &pg);
	CHECK(is(&pg, "A", "Download", SUA_DOWNLOAD), "18% on the charger downloads");

	/* No network */
	v = base("state available\nversion 0.2.0\ndownload 14200000\n");
	v.wifi = false;
	su_page_build(&v, &pg);
	CHECK(!btn(&pg, "A") && !strcmp(pg.line, "Connect to Wi-Fi to download."), "no Wi-Fi: connect to Wi-Fi");
	CHECK(!strcmp(pg.sub, "14 MB to download"), "no folder size: the download alone");

	/* Downloading: the bar, its two words, and B goes on */
	v = base("state downloading\nversion 0.2.0\ndownload 14200000\ndone 6100000\ntotal 14200000\n");
	v.running = true;
	snprintf(v.prev, sizeof v.prev, "0.0.9");
	su_page_build(&v, &pg);
	CHECK(pg.bar && !strcmp(pg.bar_left, "Downloading") && !strcmp(pg.bar_right, "6.1 of 14 MB"), "Downloading 6.1 of 14 MB");
	CHECK(pg.frac > 0.42f && pg.frac < 0.44f, "the bar is 43% full");
	CHECK(pg.nkeys == 1 && is(&pg, "B", "Continue in background", SUA_BACK), "only B, and it goes on");
	CHECK(!strcmp(su_menu_value(&v, buf, sizeof buf), "downloading"), "MENU's row: downloading");

	/* The same status with nothing running: it was cut short */
	v.running = false;
	su_page_build(&v, &pg);
	CHECK(pg.bar && !strcmp(pg.bar_left, "Stopped") && is(&pg, "A", "Continue", SUA_DOWNLOAD), "cut short: Stopped, A Continue");

	/* Preparing and checking files */
	v = base("state copying\nversion 0.2.0\nfiles 120 301\n");
	v.running = true;
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.bar_left, "Preparing") && !strcmp(pg.bar_right, "120 of 301 files"), "Preparing 120 of 301 files");
	v = base("state verifying\nversion 0.2.0\ndone 150000000\ntotal 210000000\nfiles 214 301\n");
	v.running = true;
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.bar_left, "Checking files") && !strcmp(pg.bar_right, "214 of 301"), "Checking files 214 of 301");

	/* Ready */
	v = base("state ready\nversion 0.2.0\n");
	snprintf(v.prev, sizeof v.prev, "0.0.9");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Downloaded and checked") && !strcmp(pg.line, "Or it goes in at the next start."), "ready: checked, or at the next start");
	CHECK(is(&pg, "A", "Restart Now", SUA_RESTART) && is(&pg, "B", "Later", SUA_BACK) && !btn(&pg, "Y"), "ready: A Restart Now, B Later, no step back");
	CHECK(!strcmp(su_menu_value(&v, buf, sizeof buf), "restart to update"), "MENU's row: restart to update");

	/* Up to date */
	v = base("state uptodate\nversion 0.1.0\n");
	snprintf(v.checked, sizeof v.checked, "9:41");
	su_page_build(&v, &pg);
	CHECK(!pg.to[0] && !strcmp(pg.sub, "Up to date \xc2\xb7 checked 9:41"), "up to date: one version, checked 9:41");
	CHECK(is(&pg, "A", "Look Again", SUA_CHECK), "up to date: A Look Again");
	CHECK(!strcmp(su_menu_value(&v, buf, sizeof buf), "0.1.0"), "MENU's row: the installed version");

	/* Looking */
	v = base("state checking\n");
	v.running = true;
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Looking for updates\xe2\x80\xa6") && pg.nkeys == 1, "looking: Looking for updates..., B only");

	/* A release for a newer system image */
	v = base("state needs-system\nversion 0.3.0\n");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.to, "0.3.0") && !strcmp(pg.line, "Use the ZyperOS Installer.") && is(&pg, "A", "Look Again", SUA_CHECK) &&
	      !is(&pg, "A", "Download", SUA_DOWNLOAD), "needs-system: the Installer, A Look Again, no Download");

	/* A system bigger than this card's slots */
	v = base("state too-big\nversion 0.3.0\nkind system\n");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.to, "0.3.0") && !strcmp(pg.sub, "Needs a fresh install") && strstr(pg.warn, "system slots") &&
	      strstr(pg.warn, "erases the card") && strlen(pg.warn) <= 72 && !strcmp(pg.line, "Use the ZyperOS Installer.") &&
	      is(&pg, "A", "Look Again", SUA_CHECK), "too-big: a fresh install, said in full in three lines, A Look Again, no Download");

	/* An image without System Update */
	v = base("state unsupported\n");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "This system was made without System Update.") && strstr(pg.line, "ZyperOS Installer") &&
	      strstr(pg.line, "Wi-Fi") && is(&pg, "A", "Look Again", SUA_CHECK) && btn(&pg, "B"),
	      "unsupported: why, what to do, A Look Again and B");
	v.wifi = false;
	su_page_build(&v, &pg);
	CHECK(!btn(&pg, "A") && btn(&pg, "B"), "unsupported, no Wi-Fi: B only");

	/* A version that did not start went back */
	v = base("state available\nversion 0.2.0\ndownload 14200000\n");
	snprintf(v.result, sizeof v.result, "failed 0.2.0");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Back on 0.1.0") && !strcmp(pg.warn, "0.2.0 did not start twice, so ZyperOS went back to 0.1.0 by itself.") && !pg.to[0],
	      "went back: says so in full");
	CHECK(is(&pg, "A", "Try Again", SUA_DOWNLOAD), "went back: A Try Again");

	/* A step back asked for */
	v = base("state rollback\nversion 0.0.9\n");
	snprintf(v.prev, sizeof v.prev, "0.0.9");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.to, "0.0.9") && is(&pg, "A", "Restart Now", SUA_RESTART) && !btn(&pg, "Y"), "rollback: 0.1.0 to 0.0.9, A Restart Now");

	/* Errors */
	v = base("state error\n");
	su_page_build(&v, &pg);
	CHECK(pg.warn[0] && is(&pg, "A", "Try Again", SUA_CHECK), "could not look: why, and A Try Again looks");
	v = base("state error\nversion 0.2.0\ndownload 14200000\n");
	su_page_build(&v, &pg);
	CHECK(pg.warn[0] && is(&pg, "A", "Try Again", SUA_DOWNLOAD), "download failed: why, and A Try Again downloads");
	v = base("state space\nversion 0.2.0\ndownload 14200000\n");
	su_page_build(&v, &pg);
	CHECK(pg.warn[0] && !btn(&pg, "A"), "card full: says so, no Download");

	/* The status from before the restart that installed it is done */
	v = base("state ready\nversion 0.2.0\n");
	snprintf(v.installed, sizeof v.installed, "0.2.0");
	snprintf(v.result, sizeof v.result, "updated 0.1.0 0.2.0");
	su_page_build(&v, &pg);
	CHECK(!pg.to[0] && !strcmp(pg.sub, "Updated from 0.1.0"), "after the switch: Updated from 0.1.0");
	CHECK(!strcmp(su_menu_value(&v, buf, sizeof buf), "0.2.0"), "after the switch: MENU's row is the version");
	v = base("state available\nversion 0.1.0\ndownload 5\n");
	su_page_build(&v, &pg);
	CHECK(!pg.to[0] && is(&pg, "A", "Look Again", SUA_CHECK), "an offer of the installed version is up to date");

	/* A step back: asked for, then taken */
	v = base("state rollback\nversion 0.0.9\n");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Going back to 0.0.9"), "rollback asked: still going back");
	snprintf(v.installed, sizeof v.installed, "0.0.9");
	snprintf(v.result, sizeof v.result, "rolled-back 0.1.0");
	snprintf(v.prev, sizeof v.prev, "0.1.0");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Went back from 0.1.0") && is(&pg, "Y", "Roll Back to 0.1.0", SUA_ROLLBACK), "rollback taken: Went back from 0.1.0, and Y goes forward");

	/* A system update (docs/updates.md, layer 2) */
	v = base("state available\nversion 0.3.0\ndownload 95000000\nkind system\nother 0.1.0\n");
	su_page_build(&v, &pg);
	CHECK(v.st.system && !strcmp(v.st.other, "0.1.0"), "kind system and the other slot read back");
	CHECK(!strcmp(pg.sub, "System and ZyperOS \xc2\xb7 95 MB") && is(&pg, "A", "Download", SUA_DOWNLOAD),
	      "a system update: System and ZyperOS, A Download");
	v = base("state installing\nversion 0.3.0\nkind system\ndone 50\ntotal 200\n");
	v.running = true;
	su_page_build(&v, &pg);
	CHECK(pg.bar && !strcmp(pg.bar_left, "Installing") && !strcmp(pg.bar_right, "25%"), "installing: the bar, 25%");
	v = base("state ready\nversion 0.3.0\nkind system\n");
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "Installed and checked") && is(&pg, "A", "Restart Now", SUA_RESTART), "a system ready: Restart Now");

	/* A cut download, with no network: it says it goes on by itself */
	v = base("state stopped\nversion 0.3.0\nkind system\ndone 50\ntotal 200\n");
	v.wifi = false;
	su_page_build(&v, &pg);
	CHECK(!strcmp(pg.sub, "It goes on from where it stopped when Wi-Fi is back."), "stopped offline: it goes on by itself");

	/* What the launcher starts by itself (su_auto_step) */
	{
		su_auto au;
		const long long T0 = 1793900000LL;

		memset(&au, 0, sizeof au);
		v = base("state uptodate\ntime 1793899000\n");
		v.wifi = false;
		CHECK(su_auto_step(&au, &v, 1000, T0) == SU_AUTO_NONE, "auto: no network, nothing");
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 3000, T0) == SU_AUTO_CHECK, "auto: Wi-Fi joined, a look");
		v.st.time = T0;
		CHECK(su_auto_step(&au, &v, 5000, T0 + 10) == SU_AUTO_NONE, "auto: looked a moment ago, nothing");
		v.wifi = false; su_auto_step(&au, &v, 7000, T0 + 20);
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 9000, T0 + 60) == SU_AUTO_NONE, "auto: joined again within 5 min of a look, nothing");
		v.wifi = false; su_auto_step(&au, &v, 11000, T0 + 300);
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 13000, T0 + 301) == SU_AUTO_CHECK, "auto: joined 5 min after a look, a look");
		CHECK(su_auto_step(&au, &v, 15000, T0 + 24 * 3600) == SU_AUTO_CHECK, "auto: a day on, a look");
		/* A download cut short by the power: going on as soon as there is Wi-Fi */
		memset(&au, 0, sizeof au);
		v = base("state downloading\nversion 0.3.0\ndone 50\ntotal 200\n");
		v.wifi = false;
		CHECK(su_auto_step(&au, &v, 1000, T0) == SU_AUTO_NONE, "auto: cut, no network, nothing");
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 3000, T0) == SU_AUTO_RESUME, "auto: cut, Wi-Fi joined: going on");
		v.running = true;
		CHECK(su_auto_step(&au, &v, 5000, T0) == SU_AUTO_NONE, "auto: at work, nothing");
		/* It stops again on the same network: 2 min, then 4, then 8 */
		v.running = false;
		v = base("state stopped\nversion 0.3.0\ndone 60\ntotal 200\n");
		CHECK(su_auto_step(&au, &v, 3000 + 119000, T0) == SU_AUTO_NONE, "auto: again before 2 min");
		CHECK(su_auto_step(&au, &v, 3000 + 120000, T0) == SU_AUTO_RESUME, "auto: again at 2 min");
		CHECK(su_auto_step(&au, &v, 123000 + 239000, T0) == SU_AUTO_NONE, "auto: again before 4 min");
		CHECK(su_auto_step(&au, &v, 123000 + 240000, T0) == SU_AUTO_RESUME, "auto: again at 4 min");
		/* Wi-Fi back after a drop: at once, the wait back to 2 min */
		v.wifi = false; su_auto_step(&au, &v, 400000, T0);
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 401000, T0) == SU_AUTO_RESUME, "auto: Wi-Fi back after a drop: going on at once");
		CHECK(su_auto_step(&au, &v, 401000 + 120000, T0) == SU_AUTO_RESUME, "auto: the wait back to 2 min after a join");
		/* Over a phone's USB tethering (its mobile data, likely): a look
		 * still goes, but a download cut short waits for A, and goes on by
		 * itself once Wi-Fi is the way out again */
		memset(&au, 0, sizeof au);
		v = base("state uptodate\ntime 1793899000\n");
		v.wifi = true; v.metered = true;
		CHECK(su_auto_step(&au, &v, 1000, T0) == SU_AUTO_CHECK, "auto: the phone's link joined, no look");
		memset(&au, 0, sizeof au);
		v = base("state stopped\nversion 0.3.0\ndone 60\ntotal 200\n");
		v.wifi = true; v.metered = true;
		CHECK(su_auto_step(&au, &v, 1000, T0) == SU_AUTO_NONE, "auto: cut, over the phone's link: went on by itself");
		CHECK(su_auto_step(&au, &v, 1000 + 120000, T0) == SU_AUTO_NONE, "auto: cut, over the phone's link: went on at 2 min");
		v.wifi = false; v.metered = false; su_auto_step(&au, &v, 200000, T0);
		v.wifi = true;
		CHECK(su_auto_step(&au, &v, 201000, T0) == SU_AUTO_RESUME, "auto: cut, Wi-Fi back after the phone: not going on");
		/* A battery refusal or an error is not a cut: nothing */
		memset(&au, 0, sizeof au);
		v = base("state battery\nversion 0.3.0\n");
		v.st.time = T0;
		CHECK(su_auto_step(&au, &v, 1000, T0) != SU_AUTO_RESUME, "auto: a battery refusal is not resumed");
		v = base("state unsupported\n");
		CHECK(su_auto_step(&au, &v, 1000, T0) == SU_AUTO_NONE, "auto: no updates on this image, nothing");
	}

	if (fails) { printf("check-sysupdate: %d failed\n", fails); return 1; }
	printf("check-sysupdate: all passed\n");
	return 0;
}

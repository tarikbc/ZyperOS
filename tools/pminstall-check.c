/* What the Install PortMaster page says in each state (src/pminstall.c), and
 * the Ports shelf's PortMaster entry (pm_listing_add). No SDL, no device:
 * the page is a function of the installer's status file and what the device
 * knows, and the entry of a listing.
 *
 *     make check-pminstall */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/i18n.h"
#include "../src/pminstall.h"

static int fails;

#define CHECK(cond, what) do { if (!(cond)) { printf("  FAIL %s\n", what); fails++; } } while (0)

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

static pm_view view(const char *status, bool running)
{
	pm_view v;
	memset(&v, 0, sizeof v);
	pm_status_parse(status, &v.st);
	v.running = running;
	v.network = true;
	v.python = true;
	return v;
}

static su_page page(const pm_view *v, int *nnotes, char notes[PM_NOTES][120])
{
	su_page p;
	pm_page_build(v, &p, notes, nnotes);
	return p;
}

int main(void)
{
	char notes[PM_NOTES][120];
	int nn;
	su_page p;
	pm_view v;
	game_list l;

	printf("check-pminstall:\n");
	i18n_set(LANG_EN);

	/* The status file as res/portmaster/pm-install.py writes it */
	v = view("state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 6100000\ntime 1794100000\n", true);
	CHECK(v.st.state == PM_DOWNLOADING && !strcmp(v.st.version, "2026.06.23-0015") &&
	      v.st.total == 24121702 && v.st.done == 6100000 && v.st.time == 1794100000,
	      "the status file reads back");
	pm_status_parse("", &v.st);
	CHECK(v.st.state == PM_IDLE && v.st.error == PM_ERR_NONE, "no status file is the offer");
	pm_status_parse("state error\nerror space\nneed 108000000\n", &v.st);
	CHECK(v.st.state == PM_ERROR && v.st.error == PM_ERR_SPACE && v.st.need == 108000000,
	      "an error and the room it needed");
	pm_status_parse("state error\nerror frobnicated\n", &v.st);
	CHECK(v.st.error == PM_ERR_INTERNAL, "an error with no words is said as one");
	pm_status_parse("state error\n", &v.st);
	CHECK(v.st.error == PM_ERR_INTERNAL, "an error with no code is still one");

	/* The offer: what PortMaster is, Install, Back */
	v = view("", false);
	p = page(&v, &nn, notes);
	CHECK(!strcmp(p.from, "PortMaster") && !p.to[0], "the big words are PortMaster");
	CHECK(!strcmp(p.sub, "By the PortMaster team"), "the offer says who makes it");
	CHECK(p.notes && nn == 2 && !strcmp(notes[0], "PC games ported to handhelds, ready to download.") &&
	      !strcmp(notes[1], "Some ports need the files of a game you own."), "the offer's two points");
	CHECK(is(&p, "A", "Install", SUA_DOWNLOAD) && is(&p, "B", "Back", SUA_BACK), "Install and Back");
	CHECK(!p.bar && !strcmp(p.line, "It downloads from GitHub over Wi-Fi."), "where it comes from");
	CHECK(!p.warn[0], "no problem at the offer");

	/* No network: no Install, and why where the bar goes */
	v.network = false;
	p = page(&v, &nn, notes);
	CHECK(!btn(&p, "A") && is(&p, "B", "Back", SUA_BACK), "no Install without a network");
	CHECK(!strcmp(p.line, "Connect to Wi-Fi to download."), "says to connect to Wi-Fi");

	/* A system with no Python: the system needs updating first */
	v = view("", false);
	v.python = false;
	p = page(&v, &nn, notes);
	CHECK(!btn(&p, "A") && strstr(p.warn, "MENU > System Update"), "an old system says to update it");
	CHECK(!strcmp(p.sub, "Not installed"), "and that it is not installed");

	/* Looking */
	v = view("state checking\n", true);
	p = page(&v, &nn, notes);
	CHECK(!strcmp(p.sub, "Looking for the newest release…") && !p.bar, "looking");
	CHECK(p.notes && nn == 2, "what PortMaster is, while it looks");
	CHECK(is(&p, "B", "Back, it goes on", SUA_BACK) && p.nkeys == 1, "B leaves it working");
	v = view("", true);
	p = page(&v, &nn, notes);
	CHECK(!strcmp(p.sub, "Looking for the newest release…"), "running with no word yet is looking");

	/* Downloading, checking, unpacking: one bar */
	v = view("state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 6100000\n", true);
	p = page(&v, &nn, notes);
	CHECK(p.bar && p.frac > 0.25f && p.frac < 0.26f, "the bar is a quarter in");
	CHECK(!strcmp(p.bar_left, "Downloading") && !strcmp(p.bar_right, "6.1 of 24 MB"), "6.1 of 24 MB");
	CHECK(!strcmp(p.sub, "Version 2026.06.23-0015"), "the version while it downloads");
	CHECK(p.notes && nn == 2 && !p.warn[0], "and what PortMaster is, above the bar");
	CHECK(is(&p, "B", "Back, it goes on", SUA_BACK) && !btn(&p, "A"), "only Back while it works");
	v = view("state verifying\nversion 2026.06.23-0015\n", true);
	p = page(&v, &nn, notes);
	CHECK(p.bar && p.frac == 1.0f && !strcmp(p.bar_left, "Checking the download") && !p.bar_right[0],
	      "checking the download");
	v = view("state unpacking\nversion 2026.06.23-0015\ntotal 41198857\ndone 41198857\n", true);
	p = page(&v, &nn, notes);
	CHECK(!strcmp(p.bar_left, "Unpacking") && !strcmp(p.bar_right, "41 of 41 MB") && p.frac == 1.0f,
	      "unpacking, by bytes");
	v = view("state downloading\nversion 2026.06.23-0015\ntotal 0\ndone 300000\n", true);
	p = page(&v, &nn, notes);
	CHECK(p.bar && p.frac == 0.0f && !p.bar_right[0], "a download of no known size: no count");

	/* Cut off mid-way: it stopped, Try Again */
	v = view("state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 6100000\n", false);
	p = page(&v, &nn, notes);
	CHECK(!p.bar && !strcmp(p.warn, "The install stopped before it finished. Try again."), "a stop said");
	CHECK(!p.notes && nn == 0, "and said where the points were");
	CHECK(is(&p, "A", "Try Again", SUA_DOWNLOAD) && is(&p, "B", "Back", SUA_BACK), "Try Again after a stop");

	/* Done: Open PortMaster */
	v = view("state done\nversion 2026.06.23-0015\n", false);
	v.installed = true;
	p = page(&v, &nn, notes);
	CHECK(!strcmp(p.sub, "Version 2026.06.23-0015 · installed"), "done says the version");
	CHECK(nn == 2 && !strcmp(notes[0], "It is on the Ports shelf now."), "done's points");
	CHECK(is(&p, "A", "Open PortMaster", SUA_OPEN) && is(&p, "B", "Back", SUA_BACK), "Open PortMaster");
	/* On the card whatever the file says (a run before this start) */
	v = view("", false);
	v.installed = true;
	p = page(&v, &nn, notes);
	CHECK(is(&p, "A", "Open PortMaster", SUA_OPEN), "PortMaster on the card is done");
	/* Done in the file, gone from the card (deleted by hand): the offer */
	v = view("state done\nversion 2026.06.23-0015\n", false);
	p = page(&v, &nn, notes);
	CHECK(is(&p, "A", "Install", SUA_DOWNLOAD), "done but not on the card is the offer again");

	/* Every problem, in full, with Try Again */
	{
		static const struct { const char *code, *words; } E[] = {
			{ "network", "Could not reach GitHub. Check the Wi-Fi and try again." },
			{ "download", "The download did not finish. Check the Wi-Fi and try again." },
			{ "checksum", "The download did not match what PortMaster published, so nothing was installed. Try again." },
			{ "unpack", "PortMaster could not be written to the card. Check the card, then try again." },
			{ "release", "PortMaster's list of releases did not make sense. Try again later." },
			{ "internal", "The install stopped with an error. Try again." },
		};
		char st[96];
		size_t i;

		for (i = 0; i < sizeof E / sizeof *E; i++) {
			snprintf(st, sizeof st, "state error\nerror %s\n", E[i].code);
			v = view(st, false);
			p = page(&v, &nn, notes);
			if (strcmp(p.warn, E[i].words) || !is(&p, "A", "Try Again", SUA_DOWNLOAD) ||
			    strcmp(p.sub, "Not installed")) {
				printf("  FAIL error %s: \"%s\"\n", E[i].code, p.warn);
				fails++;
			}
		}
		v = view("state error\nerror space\nneed 108000000\n", false);
		p = page(&v, &nn, notes);
		CHECK(!strcmp(p.warn, "The card needs 108 MB free for PortMaster. Make room on it, then try again."),
		      "the room it needs, in MB");
		v.network = false;
		p = page(&v, &nn, notes);
		CHECK(!btn(&p, "A") && p.warn[0] && !strcmp(p.line, "Connect to Wi-Fi to download."),
		      "no Try Again without a network, and why");
	}

	/* Every language has words for every state (a missing string is "") */
	{
		int lg;
		for (lg = 0; lg < LANG_COUNT; lg++) {
			i18n_set((lang)lg);
			v = view("state error\nerror space\nneed 108000000\n", false);
			p = page(&v, &nn, notes);
			if (!p.warn[0] || !p.sub[0] || !p.keys[0].label[0]) {
				printf("  FAIL %s: the error page has an empty part\n", lang_code((lang)lg));
				fails++;
			}
			v = view("", false);
			p = page(&v, &nn, notes);
			if (nn != 2 || !notes[1][0] || !p.line[0]) {
				printf("  FAIL %s: the offer has an empty part\n", lang_code((lang)lg));
				fails++;
			}
		}
		i18n_set(LANG_EN);
	}

	/* PortMaster's entry on the Ports shelf: added when the folder lacks it,
	 * as a scan would list it, and never twice */
	memset(&l, 0, sizeof l);
	CHECK(pm_listing_add(&l) && l.count == 1 && !strcmp(l.items[0].file, "PortMaster.sh") &&
	      !strcmp(l.items[0].name, "PortMaster") && !l.items[0].title[0] &&
	      l.items[0].added == LIB_ADDED_UNKNOWN, "an empty Ports folder gets PortMaster's entry");
	CHECK(pm_listing_add(&l) && l.count == 1, "and only once");
	free(l.items);
	memset(&l, 0, sizeof l);
	l.items = calloc(2, sizeof *l.items);
	l.count = 2;
	snprintf(l.items[0].file, sizeof l.items[0].file, "Celeste.sh");
	snprintf(l.items[1].file, sizeof l.items[1].file, "portmaster.sh");
	CHECK(pm_listing_has(&l) && pm_listing_add(&l) && l.count == 2,
	      "a PortMaster.sh already there, in any case, is the entry");
	snprintf(l.items[1].file, sizeof l.items[1].file, "Stardew Valley.sh");
	CHECK(!pm_listing_has(&l) && pm_listing_add(&l) && l.count == 3 &&
	      !strcmp(l.items[2].file, "PortMaster.sh") && !strcmp(l.items[0].file, "Celeste.sh"),
	      "ports already there keep their places, PortMaster's after them");
	free(l.items);

	if (fails) {
		printf("check-pminstall: %d failure(s)\n", fails);
		return 1;
	}
	printf("check-pminstall: ok\n");
	return 0;
}

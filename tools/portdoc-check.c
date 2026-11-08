/* What a port's own files say, as the Controls pages of the in-game menu
 * over it read them (src/portdoc.c), and the pages they make
 * (src/controls.c): against the made-up ports in tools/fixtures/ports, and
 * against the forms porters write by hand. No SDL, no device.
 *
 *     make check-portdoc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/controls.h"
#include "../src/i18n.h"
#include "../src/portdoc.h"
#include "../src/portmenu.h"

static int fails;

#define CHECK(cond, what) do { if (!(cond)) { printf("  FAIL %s\n", what); fails++; } } while (0)

#define FIX "tools/fixtures/ports"

static const pd_row *said(const port_doc *d, const char *key, unsigned dirs)
{
	int i;

	for (i = 0; i < d->nsaid; i++)
		if (d->said[i].dirs == dirs && !strcmp(d->said[i].key, key)) return &d->said[i];
	return NULL;
}

static bool said_is(const port_doc *d, const char *key, unsigned dirs, const char *what)
{
	const pd_row *r = said(d, key, dirs);

	return r && !strcmp(r->said, what);
}

static bool key_is(const port_doc *d, pd_button b, pd_word w, const char *name)
{
	return d->keys[b].word == w && !strcmp(d->keys[b].name, name);
}

/* Every page's heading and rows, as a page shows them: "KEY=VALUE" each,
 * "(body)" for prose */
typedef struct { char head[64]; char rows[CTL_MAX_ROWS][96]; int n; bool prose; } seen_page;

static int pages_of(const ctl_game *g, seen_page *out, int cap)
{
	int pages = ctl_game_pages(g), p, i;

	for (p = 0; p < pages && p < cap; p++) {
		menu_row rows[CTL_MAX_ROWS];
		ctl_buf b;
		int n = ctl_game_rows(g, p, rows, &b);

		snprintf(out[p].head, sizeof out[p].head, "%s", b.head);
		out[p].n = n;
		out[p].prose = ctl_game_prose(g, p);
		for (i = 0; i < n; i++) {
			if (ROW_IS_BODY(rows[i])) snprintf(out[p].rows[i], sizeof out[p].rows[i], "(body)%s", rows[i].label);
			else if (ROW_IS_NOTE(rows[i])) snprintf(out[p].rows[i], sizeof out[p].rows[i], "(note)%s", rows[i].label);
			else snprintf(out[p].rows[i], sizeof out[p].rows[i], "%s=%s", rows[i].label,
			              rows[i].value ? rows[i].value : "");
		}
	}
	return pages;
}

/* PORTDOC_SHOW=1: every page read, for a look */
static void show(const char *who, const seen_page *pg, int n)
{
	int p, i;

	if (!getenv("PORTDOC_SHOW")) return;
	for (p = 0; p < n; p++) {
		printf("    %s %d/%d \"%s\"%s\n", who, p + 1, n, pg[p].head, pg[p].prose ? " (prose)" : "");
		for (i = 0; i < pg[p].n; i++) printf("      %s\n", pg[p].rows[i]);
	}
}

static bool page_has(const seen_page *p, const char *row)
{
	int i;

	for (i = 0; i < p->n; i++)
		if (!strcmp(p->rows[i], row)) return true;
	return false;
}

static bool any_has(const seen_page *pg, int n, const char *row)
{
	int i;

	for (i = 0; i < n; i++)
		if (page_has(&pg[i], row)) return true;
	return false;
}

/* A table of rows: no page more than six, or five and a note (LINES in
 * src/controls.c); prose is the About page alone */
static void pages_fit(const char *who, const seen_page *pg, int n)
{
	char msg[160];
	int p;

	for (p = 0; p < n; p++) {
		snprintf(msg, sizeof msg, "%s page %d (%s): %d rows, room for 6", who, p, pg[p].head, pg[p].n);
		CHECK(pg[p].prose || (pg[p].n >= 1 && pg[p].n <= 6), msg);
	}
}

/* A heading's room without a font: the launcher measures "Controls:
 * PortMaster" beside its page count; here 26 bytes stand in */
static bool fits_26(void *ctx, const char *head)
{
	(void)ctx;
	return strlen(head) <= 26;
}

static void fixtures(void)
{
	port_doc d;
	ctl_game g = { 0 };
	seen_page pg[16];
	int n;

	printf("the made-up ports\n");
	/* Star Farm: port.json, a gptokeyb keymap, a README.md with a table */
	CHECK(port_doc_read(FIX "/Star Farm.sh", &d), "Star Farm is a port");
	CHECK(!d.portmaster && !strcmp(d.title, "Star Farm"), "Star Farm's title is port.json's");
	CHECK(!strncmp(d.desc, "Star Farm is a made-up farming game", 35) && strstr(d.desc, "\n\nGrow crops"),
	      "its desc, paragraphs and all");
	CHECK(!strcmp(d.inst, "Copy the game's data folder into ports/starfarm/gamedata before the first start."),
	      "its inst");
	CHECK(d.gptokeyb, "its script runs gptokeyb");
	CHECK(key_is(&d, PD_A, PD_SPACE, "") && key_is(&d, PD_B, PD_NAME, "Backspace") &&
	      key_is(&d, PD_X, PD_NAME, "E") && key_is(&d, PD_Y, PD_NAME, "") &&
	      key_is(&d, PD_L1, PD_NAME, "Q") && key_is(&d, PD_R1, PD_NAME, "W") &&
	      key_is(&d, PD_L2, PD_CLICK_LEFT, "") && key_is(&d, PD_R2, PD_CLICK_RIGHT, "") &&
	      key_is(&d, PD_START, PD_NAME, "Enter") && key_is(&d, PD_SELECT, PD_NAME, "Esc"),
	      "its keymap: A Space, B Backspace, X E, no Y, L1 Q, R1 W, L2/R2 clicks, START Enter, back Esc");
	CHECK(key_is(&d, PD_UP, PD_UP_KEY, "") && key_is(&d, PD_DOWN, PD_DOWN_KEY, "") &&
	      key_is(&d, PD_LEFT, PD_LEFT_KEY, "") && key_is(&d, PD_RIGHT, PD_RIGHT_KEY, ""),
	      "the d-pad its four arrows, and \"up = repeat\" no key of its own");
	CHECK(d.nkeys == 13, "13 buttons type a key (Y none, the sticks not this handheld's)");
	CHECK(d.nsaid == 6, "six rows of the README's: the stick alone and L3 are no buttons here");
	CHECK(said_is(&d, "", PD_DIR_ALL, "Walk") && said_is(&d, "A", 0, "Use tool") &&
	      said_is(&d, "B", 0, "Cancel") && said_is(&d, "X", 0, "Open the map") &&
	      said_is(&d, "L1/R1", 0, "Change tool") && said_is(&d, "START+SELECT", 0, "Quit"),
	      "the README's table: D-pad Walk, A Use tool, B Cancel, X Open the map, L1/R1 Change tool, START+SELECT Quit");

	g = (ctl_game){ "", "Ports", NULL, "", NULL, "sh", fits_26, NULL, true, NULL, &d };
	n = pages_of(&g, pg, 16);
	CHECK(n == 5, "Star Farm: five pages (its README's, two of keys, In a game, About)");
	if (n == 5) {
		CHECK(!strcmp(pg[0].head, "Controls: Star Farm") && page_has(&pg[0], "D-pad=Walk") &&
		      page_has(&pg[0], "START+SELECT=Quit"), "page 1 is what the README says");
		CHECK(!strcmp(pg[1].head, "Keys: Star Farm") && page_has(&pg[1], "D-pad=Arrow keys") &&
		      page_has(&pg[1], "A=Space") && page_has(&pg[1], "B=Backspace") && page_has(&pg[1], "X=E"),
		      "page 2: the keys, the faces and the d-pad");
		CHECK(!strcmp(pg[2].head, "Keys: Star Farm") && page_has(&pg[2], "L1/R1=Q/W") &&
		      page_has(&pg[2], "L2/R2=Click/Right-click") && page_has(&pg[2], "START/SELECT=Enter/Esc"),
		      "page 3: the shoulders and START/SELECT, paired");
		CHECK(!strcmp(pg[3].head, "Controls: In a game") && page_has(&pg[3], "MENU=Menu") &&
		      page_has(&pg[3], "SELECT+START=Quit the game") && page_has(&pg[3], "POWER=Quit and turn off"),
		      "page 4: In a game, with PortMaster's quit chord");
		CHECK(!strcmp(pg[4].head, "About: Star Farm") && pg[4].prose && pg[4].n == 2 &&
		      !strncmp(pg[4].rows[0], "(body)Copy the game's", 21) &&
		      !strncmp(pg[4].rows[1], "(body)Star Farm is", 18),
		      "page 5: About, prose, what to copy first and then what it is");
		CHECK(!pg[0].prose && !pg[1].prose && !pg[3].prose, "only About is prose");
	}
	show("Star Farm", pg, n);
	pages_fit("Star Farm", pg, n);

	/* Lone Key: no port.json, a gptokeyb2 keymap, a README.txt */
	CHECK(port_doc_read(FIX "/Lone Key.sh", &d), "Lone Key's folder is the one its script works in");
	CHECK(!strcmp(d.title, "Lone Key") && !d.desc[0] && !d.inst[0], "no port.json: the script's name, no About");
	CHECK(d.gptokeyb, "$GPTOKEYB2 is gptokeyb too");
	CHECK(key_is(&d, PD_A, PD_NAME, "Enter") && key_is(&d, PD_B, PD_NAME, "Esc") &&
	      key_is(&d, PD_SELECT, PD_NAME, "Tab") && key_is(&d, PD_X, PD_NAME, "Z") &&
	      key_is(&d, PD_Y, PD_NAME, "Z") && key_is(&d, PD_UP, PD_UP_KEY, "") &&
	      key_is(&d, PD_RIGHT, PD_RIGHT_KEY, ""),
	      "gptokeyb2's [controls], dpad = arrow_keys, and not its [controls:menu] layer");
	CHECK(d.nsaid == 5 && said_is(&d, "", PD_DIR_ALL, "Move the key") && said_is(&d, "A", 0, "Pick up") &&
	      said_is(&d, "B", 0, "Drop") && said_is(&d, "Y", 0, "Shout") &&
	      said_is(&d, "START+SELECT", 0, "Exit"),
	      "README.txt's CONTROLS under a row of =: the d-pad, A without its stop, no Guide");
	g.doc = &d;
	n = pages_of(&g, pg, 16);
	CHECK(n == 3 && !strcmp(pg[0].head, "Controls: Lone Key") && !strcmp(pg[1].head, "Keys: Lone Key") &&
	      page_has(&pg[1], "X=Z") && page_has(&pg[1], "Y=Z") && !strcmp(pg[2].head, "Controls: In a game"),
	      "Lone Key: its README's, its keys (six rows, so X and Y each their own), In a game; no About");
	show("Lone Key", pg, n);
	pages_fit("Lone Key", pg, n);

	/* Odd Keys: single arrow keys and a middle click; the d-pad's ways one
	 * at a time in its README */
	CHECK(port_doc_read(FIX "/Odd Keys.sh", &d), "Odd Keys is a port");
	g.doc = &d;
	n = pages_of(&g, pg, 16);
	CHECK(n == 3 && page_has(&pg[0], "Up=Climb") && page_has(&pg[0], "Down=Duck") &&
	      page_has(&pg[0], "Left=Back up") && page_has(&pg[0], "Right=Run") &&
	      page_has(&pg[1], "A=Middle-click") && page_has(&pg[1], "X=Up arrow") &&
	      page_has(&pg[1], "Y=Down arrow") && page_has(&pg[1], "L1=Left arrow") &&
	      page_has(&pg[1], "R1=Right arrow"),
	      "Odd Keys: the ways one at a time, single arrow keys and a middle click");
	show("Odd Keys", pg, n);
	pages_fit("Odd Keys", pg, n);

	/* A script with nothing beside it */
	CHECK(!port_doc_read(FIX "/Plain.sh", &d), "Plain.sh has no folder");
	CHECK(!strcmp(d.title, "Plain") && !d.nkeys && !d.nsaid && !d.gptokeyb, "its name, nothing else");
	g.doc = &d;
	n = pages_of(&g, pg, 16);
	CHECK(n == 1 && !strcmp(pg[0].head, "Controls: In a game") && !any_has(pg, n, "SELECT+START=Quit the game"),
	      "Plain: In a game alone, and no quit chord without gptokeyb");

	/* PortMaster's own app */
	CHECK(port_doc_read(FIX "/PortMaster.sh", &d) && d.portmaster && !strcmp(d.title, "PortMaster"),
	      "PortMaster.sh is PortMaster's app, on the card or not");
	g.doc = &d;
	n = pages_of(&g, pg, 16);
	CHECK(n == 2 && !strcmp(pg[0].head, "Controls: PortMaster") && page_has(&pg[0], "Up/Down=Move") &&
	      page_has(&pg[0], "L1/R1=Page up/down") && page_has(&pg[0], "A=Choose") &&
	      page_has(&pg[0], "B=Back") && page_has(&pg[0], "X=Filters") && page_has(&pg[0], "Y=Uninstall"),
	      "PortMaster: its own buttons");
	CHECK(n == 2 && !strcmp(pg[1].head, "Controls: Anywhere") && page_has(&pg[1], "MENU=Menu") &&
	      !page_has(&pg[1], "SELECT+START=Quit the game"),
	      "and Anywhere's, with no quit chord: PortMaster runs no gptokeyb");
	show("PortMaster", pg, n);
	pages_fit("PortMaster", pg, n);

	/* A long title is cut to fit the heading (26 bytes here, fits_26; the
	 * launcher measures with the font), with an ellipsis, never inside a
	 * character */
	snprintf(d.title, sizeof d.title, "The Very Long Adventure of Somebody");
	n = pages_of(&g, pg, 16);
	CHECK(n == 2 && strlen(pg[0].head) <= 26 && strstr(pg[0].head, "\xe2\x80\xa6") &&
	      !strncmp(pg[0].head, "Controls: The", 13),
	      "a title too long for the heading is cut, with an ellipsis");
}

static void readers(void)
{
	port_doc d;
	char dir[64], gptk[64];
	bool gk;

	printf("the readers, by hand\n");
	memset(&d, 0, sizeof d);
	{
		static const char OTHER[] = "{\"items\":[\"Other.sh\"],\"attr\":{\"title\":\"X\"}}";
		static const char CASE[] = "{\"items\":[\"star farm.SH\"]}";

		CHECK(!pd_json(OTHER, strlen(OTHER), "Star Farm.sh", &d),
		      "a port.json that names another script is not this port's");
		CHECK(!pd_json("not json at all", 15, "a.sh", &d), "a port.json that is no JSON");
		CHECK(pd_json(CASE, strlen(CASE), "Star Farm.sh", &d),
		      "items match whatever the case: the card is exFAT");
	}

	pd_script("GAMEDIR=/$directory/ports/${PORTNAME}\n$GPTOKEYB \"$PORTNAME\" -c \"$GAMEDIR/$PORTNAME.gptk\"\n",
	          dir, sizeof dir, gptk, sizeof gptk, &gk);
	CHECK(!dir[0] && !gptk[0] && gk, "a folder or keymap named by a variable is not guessed at");
	pd_script("controlfolder=\"/roms/ports/PortMaster\"\nGAMEDIR=\"/$directory/ports/sdlpop/\"\n"
	          "$ESUDO ./gptokeyb -1 \"prince\" -c \"./sdlpop.gptk\" &\n$ESUDO kill -9 $(pidof gptokeyb)\n",
	          dir, sizeof dir, gptk, sizeof gptk, &gk);
	CHECK(!strcmp(dir, "sdlpop") && !strcmp(gptk, "sdlpop.gptk"),
	      "past PortMaster's own folder: the port's, and its keymap's file name");
	CHECK(!gk, "a gptokeyb started by hand, not through $GPTOKEYB, is not counted on for SELECT+START");

	memset(&d, 0, sizeof d);
	pd_readme("# Game\n\nSome prose with A = in it.\n\n### Controls:\n\n"
	          "- **A**: Jump.\n* B -> Run\n+ X - Talk\nThis line is prose.\n"
	          "| L | Previous |\n| Right Stick | Camera |\nSelect + Up = Volume up\n\n"
	          "## Credits\nA = Not a control\n", &d);
	CHECK(d.nsaid == 4 && said_is(&d, "A", 0, "Jump") && said_is(&d, "B", 0, "Run") &&
	      said_is(&d, "X", 0, "Talk") && said_is(&d, "L1", 0, "Previous"),
	      "bullets, ->, ' - ', a table row, L as L1; a stick, a button with a way, and the next section are not rows");
	memset(&d, 0, sizeof d);
	pd_readme("Controls\n--------\nL2/R2: Zoom out/in\nDpad Left/Right: Turn\nUp = Jump\nHotkey + Start: Exit\n", &d);
	CHECK(d.nsaid == 4 && said_is(&d, "L2/R2", 0, "Zoom out/in") &&
	      said_is(&d, "", PD_DIR_LEFT | PD_DIR_RIGHT, "Turn") && said_is(&d, "", PD_DIR_UP, "Jump") &&
	      said_is(&d, "SELECT+START", 0, "Exit"),
	      "an underlined heading; Left/Right and Up as ways; Hotkey is SELECT, PortMaster's here");
	memset(&d, 0, sizeof d);
	pd_readme("No controls section at all.\nA = Jump\n", &d);
	CHECK(d.nsaid == 0, "rows outside a Controls section are no rows");

	memset(&d, 0, sizeof d);
	pd_gptk("a = mouse_middle\nb = kp_enter\nstart = f5\nback = \\\"\nx = add_alt\nup = w\ndown = s\n", &d);
	CHECK(key_is(&d, PD_A, PD_CLICK_MIDDLE, "") && key_is(&d, PD_B, PD_NAME, "Enter") &&
	      key_is(&d, PD_START, PD_NAME, "F5") && key_is(&d, PD_SELECT, PD_NAME, "") &&
	      key_is(&d, PD_X, PD_NAME, "") && key_is(&d, PD_UP, PD_NAME, "W") && d.nkeys == 5,
	      "a middle click, the keypad's Enter, F5; \\\" and add_alt no key; W and S on the d-pad");
}

static void the_menu_over_ports(void)
{
	printf("the in-game menu over ports\n");
	CHECK(port_menu_for("port"), "the Ports shelf's ports have the in-game menu");
	CHECK(port_menu_for("run:ZyperOS/openbor/OpenBOR.sh") && !port_menu_for("dsperate") &&
	      !port_menu_for("fceumm") && !port_menu_for(NULL), "and run: programs, and no Diatom core");
	CHECK(!strcmp(PORT_MENU_OOM, "1000"), "the menu is the first the kernel takes when memory runs out");
}

int main(void)
{
	printf("check-portdoc:\n");
	i18n_set(LANG_EN);
	fixtures();
	readers();
	the_menu_over_ports();
	if (fails) {
		printf("check-portdoc: %d failed\n", fails);
		return 1;
	}
	printf("check-portdoc: ok\n");
	return 0;
}

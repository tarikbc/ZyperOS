/* A menu's panel kept whole, without a screen: src/menusnap.c, the copy of
 * what a panel was drawn from that the next screen draws leaving.
 *
 * What it holds:
 *   - every row's text is copied, so the screen that drew it can rebuild its
 *     rows, or be gone, and the copy still says what was on screen;
 *   - the row marks (MENU_NOTE_MARK, MENU_BODY_MARK, MENU_HR_MARK) and a
 *     rule's NULL label are kept as they are, since menu_panel tells the
 *     kinds of row by them;
 *   - the heading, the colors, the cursor, the width, the accent and the loop
 *     are kept, and Zyper's rig by value with his clock;
 *   - a list of MENUSNAP_ROWS rows, and text to the last byte of
 *     MENUSNAP_TEXT, are kept; one row or one byte more is not, and says so;
 *   - and the copy never writes past its own text.
 *
 * Links src/menusnap.c alone, and NOT SDL.
 */
#include "../src/menusnap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* The copy lies inside s->text */
static int inside(const menu_snap *s, const char *p)
{
	return p >= s->text && p < s->text + sizeof s->text;
}

int main(void)
{
	static menu_snap s;
	static menu_row rows[MENUSNAP_ROWS + 1];
	static char text[MENUSNAP_TEXT];
	unsigned vcols[6] = { 0, 1, 0, 2, 0, 0 };
	char label[32], value[32], heading[32];
	zr_rig zy;
	int i, ok;

	/* ---- a panel of each kind of row ---- */
	snprintf(heading, sizeof heading, "ZyperOS");
	snprintf(label, sizeof label, "Wi-Fi");
	snprintf(value, sizeof value, "off");
	rows[0] = (menu_row){ label, value, true };
	rows[1] = MENU_RULE;
	rows[2] = MENU_NOTE("A note");
	rows[3] = MENU_BODY("A line of a paragraph");
	rows[4] = MENU_HR;
	rows[5] = (menu_row){ "Placeholder", NULL, false };
	memset(&zy, 0, sizeof zy);
	zy.hop_at = 1234;
	zy.part[ZR_TILT].to = 18.0f;

	ok = menusnap_take(&s, heading, rows, 6, 5, 640, 0xFE000000u, vcols, true, 4,
	                   &zy, 77u);
	ck(ok && s.ok, "a panel of six rows is kept");
	ck(s.n == 6 && s.sel == 5 && s.fixed_w == 640 && s.accent == 0xFE000000u &&
	   s.visits_all && s.loop_at == 4,
	   "with its cursor, width, accent and loop");
	ck(s.heading && inside(&s, s.heading) && !strcmp(s.heading, "ZyperOS"),
	   "its heading copied");
	ck(inside(&s, s.rows[0].label) && !strcmp(s.rows[0].label, "Wi-Fi") &&
	   inside(&s, s.rows[0].value) && !strcmp(s.rows[0].value, "off") && s.rows[0].live,
	   "a row's label and value copied, and whether it is live");
	ck(s.rows[1].label == NULL && s.rows[1].value == NULL, "a rule kept as a rule");
	ck(s.rows[2].value == MENU_NOTE_MARK && ROW_IS_NOTE(s.rows[2]) &&
	   !strcmp(s.rows[2].label, "A note"), "a note keeps its mark");
	ck(s.rows[3].value == MENU_BODY_MARK && ROW_IS_BODY(s.rows[3]),
	   "a line of a paragraph keeps its mark");
	ck(ROW_IS_HR(s.rows[4]), "a hairline keeps its mark");
	ck(s.rows[5].value == NULL && !s.rows[5].live, "a row with no value keeps none");
	ck(s.has_vcolors && s.vcolors[1] == 1 && s.vcolors[3] == 2 && s.vcolors[0] == 0,
	   "and its colors");
	ck(s.zyper && s.zy.hop_at == 1234 && s.zy.part[ZR_TILT].to == 18.0f && s.zy_ahead == 77u,
	   "Zyper's rig kept by value, with his clock");
	ck(s.note == NULL, "no caption at the heading's right until one is given");
	snprintf(value, sizeof value, "1 / 2");
	ck(menusnap_note(&s, value) && s.ok && inside(&s, s.note) && !strcmp(s.note, "1 / 2"),
	   "the heading's caption copied (menu_draw_caption)");
	snprintf(value, sizeof value, "off");

	/* The screen rebuilds its rows, or goes */
	snprintf(label, sizeof label, "Bluetooth");
	snprintf(heading, sizeof heading, "Gone");
	zy.hop_at = 9;
	ck(!strcmp(s.rows[0].label, "Wi-Fi") && !strcmp(s.heading, "ZyperOS") &&
	   s.zy.hop_at == 1234,
	   "and the copy says what was drawn after the screen's own rows change");

	ok = menusnap_take(&s, NULL, rows, 2, -1, 0, 0u, NULL, false, 0, NULL, 0u);
	ck(ok && s.heading == NULL && !s.has_vcolors && !s.zyper && s.n == 2,
	   "no heading, no colors and no Zyper are kept as none");
	ck(s.note == NULL, "nor the last panel's caption");

	/* ---- the limits ---- */
	for (i = 0; i < MENUSNAP_ROWS + 1; i++) rows[i] = (menu_row){ "x", NULL, true };
	ck(menusnap_take(&s, NULL, rows, MENUSNAP_ROWS, 0, 0, 0u, NULL, false, 0, NULL, 0u) &&
	   s.ok && s.n == MENUSNAP_ROWS,
	   "a list of the most rows is kept");
	ck(!menusnap_take(&s, NULL, rows, MENUSNAP_ROWS + 1, 0, 0, 0u, NULL, false, 0, NULL, 0u) &&
	   !s.ok, "a row more is not, and says so");
	ck(!menusnap_take(&s, NULL, rows, -1, 0, 0, 0u, NULL, false, 0, NULL, 0u) && !s.ok,
	   "nor a list of less than none");

	/* One label that takes the text to its last byte, its end included */
	memset(text, 'a', sizeof text - 1);
	text[sizeof text - 1] = '\0';
	rows[0] = (menu_row){ text, NULL, true };
	ck(menusnap_take(&s, NULL, rows, 1, 0, 0, 0u, NULL, false, 0, NULL, 0u) && s.ok &&
	   strlen(s.rows[0].label) == MENUSNAP_TEXT - 1 && s.used == MENUSNAP_TEXT,
	   "text to the last byte is kept");
	rows[1] = (menu_row){ "b", NULL, true };
	ck(!menusnap_take(&s, NULL, rows, 2, 0, 0, 0u, NULL, false, 0, NULL, 0u) && !s.ok &&
	   s.used <= MENUSNAP_TEXT,
	   "one byte more is not, and nothing is written past it");
	ck(!menusnap_take(&s, text, rows, 1, 0, 0, 0u, NULL, false, 0, NULL, 0u) && !s.ok,
	   "nor a heading that leaves no room for the rows");

	/* Kept again after one that was not */
	rows[0] = (menu_row){ "Again", NULL, true };
	ck(menusnap_take(&s, NULL, rows, 1, 0, 0, 0u, NULL, false, 0, NULL, 0u) && s.ok &&
	   !strcmp(s.rows[0].label, "Again"), "and the next panel that fits is kept");

	if (fails) {
		printf("menusnap: %d check(s) FAILED\n", fails);
		return 1;
	}
	printf("ok: a panel is kept whole, its marks as they are, up to %d rows and %d "
	       "bytes of text and not a byte past\n", MENUSNAP_ROWS, MENUSNAP_TEXT);
	return 0;
}

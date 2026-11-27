/* SPDX-License-Identifier: GPL-3.0-only */
/* check-confirm, without a screen (src/confirmq.h): every question with two
 * answers, in every language.
 *
 *   - The 38 questions of the confirm panels lab's inventory (2026-11-22),
 *     Bluetooth's Forget, which asks since (Tarik's, 2026-11-23), and
 *     Storage's Unpacked DS Games (src/dscache.h, 2026-11-23), each named
 *     once.
 *   - The answer that loses something marked on exactly the 34 that lose
 *     something (the inventory's 32, Bluetooth's Forget and Unpacked DS
 *     Games), and on no other.
 *   - B: the safe answer (the left) everywhere but the four where it leaves
 *     with nothing done: the two whose answers both go on, the USB offer
 *     (whose Keep Asking is kept for good) and PICO-8's new buttons (whose
 *     Later closes the menu). B never gives the right answer.
 *   - The keys: the cursor on the left answer at the start; Left and Right
 *     move and go round on a press, stop at the end held (and say the edge);
 *     Up and Down only say the edge; A takes the answer under the cursor and
 *     presses it; B presses the left answer or none; MENU and POWER leave.
 *   - The words, in each language: a heading (the action) that is no
 *     question, every answer and the question's words there; the question
 *     formatted whole with its words in it; Reset Hotkeys' yes the reset
 *     word Reset Buttons' is (not the game's restart); Storage's question
 *     for a system's video snaps naming the system. */
#include "../src/confirmq.h"
#include "../src/i18n.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void check(int ok, const char *what, const char *more)
{
	if (!ok) {
		fails++;
		printf("FAIL %s%s%s\n", what, more ? ": " : "", more ? more : "");
	}
}

static const char *const LOSES[] = {
	"wifi-forget", "wifi-forget-joined", "pico8-restart-display", "pico8-restart-buttons",
	"pico8-reset", "game-reset", "hotkeys-reset-game", "hotkeys-reset-system", "hotkeys-reset-every",
	"storage-unused-art", "storage-old-states", "storage-system-snaps", "storage-media-folder",
	"storage-media-file", "storage-orphan-port", "game-delete", "port-delete", "collection-delete",
	"screenshot-delete", "media-video-delete", "sgdb-unlink", "ss-unlink", "snaps-delete-all",
	"reset-buttons-game", "reset-buttons-all", "dl-sign-out", "dl-delete-videos", "dl-delete-cheats",
	"dl-delete-sets", "dl-sys-videos", "dl-sys-sets", "dl-sys-cheats", "bt-forget", "storage-ds-copies",
};
static const char *const B_NONE[] = { "pico8-restart-buttons", "splore-wifi", "cheats-crash", "usb-offer" };

static bool listed(const char *const *list, size_t n, const char *name)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (!strcmp(list[i], name)) return true;
	return false;
}

static bool ends_in_question(const char *s)
{
	size_t n = strlen(s);

	return (n >= 1 && s[n - 1] == '?') || (n >= 3 && !strcmp(s + n - 3, "\xef\xbc\x9f"));
}

/* The question's words with every conversion given a word of its own */
static void sample(cq_id q, char *out, size_t n)
{
	if (cq_of(q)->flags & CQ_COUNTED) cq_text(q, out, n, 14, "860 MB");
	else cq_text(q, out, n, "PlayStation", "Tobu Tobu Girl", "Faster", "SELECT+L1");
}

static void keys(void)
{
	const cq_def *yes = cq_of(CQ_GAME_DELETE), *none = cq_of(CQ_USB_OFFER);
	cq_state s = cq_start(true), t = cq_start(false);
	cq_step r;

	check(s.sel == 0 && t.sel == 0, "the cursor starts on the left answer", NULL);
	r = cq_press(yes, s, CQK_RIGHT, true);
	check(r.sel == 1 && r.moved && !r.done, "Right moves to the right answer", NULL);
	s.sel = 1;
	r = cq_press(yes, s, CQK_RIGHT, true);
	check(r.sel == 0 && r.moved, "a press of Right on the last goes round to the first", NULL);
	r = cq_press(yes, s, CQK_RIGHT, false);
	check(r.sel == 1 && !r.moved && r.edge, "a held Right stops at the end and says the edge", NULL);
	s.sel = 0;
	r = cq_press(yes, s, CQK_LEFT, true);
	check(r.sel == 1 && r.moved, "a press of Left on the first goes round to the last", NULL);
	r = cq_press(yes, s, CQK_LEFT, false);
	check(r.sel == 0 && r.edge, "a held Left stops at the start", NULL);
	r = cq_press(yes, s, CQK_UP, true);
	check(r.sel == 0 && !r.moved && r.edge && !r.done, "Up on the answers side by side: the edge, nothing else", NULL);
	r = cq_press(yes, s, CQK_DOWN, false);
	check(r.sel == 0 && !r.moved && !r.edge && !r.done, "a held Down there: nothing", NULL);
	r = cq_press(yes, t, CQK_DOWN, true);
	check(r.sel == 1 && r.moved, "stacked, Down moves", NULL);
	r = cq_press(yes, t, CQK_RIGHT, true);
	check(r.sel == 0 && !r.moved && r.edge, "stacked, Right only says the edge", NULL);
	s.sel = 1;
	r = cq_press(yes, s, CQK_A, true);
	check(r.done && r.answer == CQ_RIGHT && r.press == 1, "A takes the answer under the cursor and presses it", NULL);
	r = cq_press(yes, s, CQK_B, true);
	check(r.done && r.answer == CQ_LEFT && r.press == 0, "B gives Cancel, pressed, wherever the cursor is", NULL);
	r = cq_press(none, s, CQK_B, true);
	check(r.done && r.answer == CQ_NONE && r.press < 0, "B on the USB offer leaves with nothing kept", NULL);
	r = cq_press(yes, s, CQK_MENU, true);
	check(r.done && r.answer == CQ_NONE && r.press < 0, "MENU leaves with nothing done", NULL);
	r = cq_press(yes, s, CQK_POWER, true);
	check(r.done && r.answer == CQ_NONE, "POWER leaves with nothing done", NULL);
}

int main(void)
{
	int q, l, loses = 0, bnone = 0;
	char text[2048], what[256];

	check(CQ_COUNT == 40, "40 questions with two answers", NULL);
	for (q = 0; q < CQ_COUNT; q++) {
		const cq_def *d = cq_of((cq_id)q);

		check(d->name && cq_named(d->name) == (cq_id)q, "each question named once", d->name);
		snprintf(what, sizeof what, "%s marked as losing something", d->name);
		check(!(d->flags & CQ_LOSES) == !listed(LOSES, sizeof LOSES / sizeof *LOSES, d->name), what,
		      d->flags & CQ_LOSES ? "it is marked, and the inventory says it loses nothing"
		                         : "it is not marked, and the inventory says it loses something");
		snprintf(what, sizeof what, "%s: B", d->name);
		check(!(d->flags & CQ_B_NONE) == !listed(B_NONE, sizeof B_NONE / sizeof *B_NONE, d->name), what,
		      "gives the left answer on every question but the four that leave with nothing");
		check(d->right != d->left, "two answers, not one twice", d->name);
		loses += !!(d->flags & CQ_LOSES);
		bnone += !!(d->flags & CQ_B_NONE);
	}
	check(loses == 34, "34 questions lose something", NULL);
	check(bnone == 4, "4 questions where B gives no answer", NULL);
	keys();

	for (l = 0; l < LANG_COUNT; l++) {
		i18n_set((lang)l);
		for (q = 0; q < CQ_COUNT; q++) {
			const cq_def *d = cq_of((cq_id)q);

			snprintf(what, sizeof what, "%s %s", lang_code((lang)l), d->name);
			check(*T(d->heading) && !ends_in_question(T(d->heading)) && !strchr(T(d->heading), '%'),
			      "a heading, the action, no question", what);
			check(*T(d->left) && *T(d->right), "both answers' words", what);
			sample((cq_id)q, text, sizeof text);
			check(*text && !strstr(text, "%s") && !strstr(text, "%d") && !strstr(text, "$s"),
			      "the question whole, its words in it", what);
			if (q == CQ_STORE_SYS_SNAPS)
				check(strstr(text, "PlayStation") != NULL, "Storage's question names the system whose snaps go", what);
			if (q == CQ_HOTKEYS_RESET_GAME || q == CQ_HOTKEYS_RESET_SYSTEM || q == CQ_HOTKEYS_RESET_EVERY) {
				check(!strcmp(T(d->right), T(S_GAMEMENU_RESET_BUTTONS_YES)),
				      "Reset Hotkeys' yes is Reset Buttons' reset word", what);
				check(d->right != S_GAMEMENU_RESET, "Reset Hotkeys' yes is not the game's restart", what);
			}
		}
	}
	if (!fails)
		printf("check-confirm: 40 questions, 34 marked as losing something, B the safe answer or "
		       "none, the keys, and the words in %d languages\n", LANG_COUNT);
	return fails ? 1 : 0;
}

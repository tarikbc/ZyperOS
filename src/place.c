/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "place.h"

#include "atomic.h"
#include "config.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ---------- where you were ------------------------------------------------ */

/* Coming back to the shelf you left is worth four lines of file handling: the
 * launcher restarts after every game, and starting at the beginning of the
 * list every time would undo the point of it being quick. */
void remember_place(app *a)
{
	char p[CFG_STR * 2];
	sysview *v = &a->view[a->sys_cursor];
	FILE *f;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = atomic_open(p, 0644);
	if (!f) return;
	fprintf(f, "%s\n%s\n", a->sys.systems[a->sys_cursor].tag,
	        v->list.count ? v->list.items[v->cursor].file : "");
	atomic_commit(f, p);
}

/* Whether a game was running when this process last stopped.
 *
 * `.last` says what you were LOOKING at, which is not the same question - it
 * is written when the shelf moves as well as when a game starts, and it says
 * nothing about whether that game was still up. This is written at launch and
 * cleared only on a clean return to the shelf, so a power-off mid-game and a
 * quit are distinguishable, which is the whole point.
 *
 * Beside `.last` and in the same shape, because it is the same two facts. */
static void playing_path(char *out, size_t n)
{
	snprintf(out, n, "%s/.playing", P_ROOT);
}

void playing_set(app *a)
{
	char p[CFG_STR * 2];
	sysview *v = &a->view[a->sys_cursor];
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	FILE *f;

	if (v->list.count == 0) return;
	playing_path(p, sizeof p);
	f = atomic_open(p, 0644);
	if (!f) return;
	/* The GAME's system, not the shelf's - launching from Favorites otherwise
	 * records a system that does not own the ROM, exactly as the launch path
	 * itself had to learn. */
	fprintf(f, "%s\n%s\n", a->sys.systems[o].tag, v->list.items[v->cursor].file);
	atomic_commit(f, p);
}

void playing_clear(void)
{
	char p[CFG_STR * 2];

	playing_path(p, sizeof p);
	remove(p);
}

/* Point the cursor at whatever was being played, if anything was, and say so.
 * Only when there is a state to come back to: without one, "resume" would
 * mean starting the game from its title screen, which is not what anyone
 * powering off mid-game is asking for. */
bool playing_restore(app *a)
{
	char p[CFG_STR * 2], tag[64] = { 0 }, file[LIB_PATH] = { 0 }, st[LIB_PATH * 2];
	FILE *f;
	int i, k;

	playing_path(p, sizeof p);
	f = fopen(p, "r");
	if (!f) return false;
	if (fgets(tag, sizeof tag, f)) tag[strcspn(tag, "\r\n")] = 0;
	if (fgets(file, sizeof file, f)) file[strcspn(file, "\r\n")] = 0;
	fclose(f);
	if (!tag[0] || !file[0]) { playing_clear(); return false; }

	for (i = 0; i < a->sys.count; i++) {
		if (strcmp(a->sys.systems[i].tag, tag) != 0) continue;
		for (k = 0; k < a->view[i].list.count; k++) {
			if (strcmp(a->view[i].list.items[k].file, file) != 0) continue;

			state_path(a, i, &a->view[i].list.items[k], st, sizeof st);
			if (access(st, R_OK) != 0) {
				/* The game is gone from under the state, or the autosave
				 * never landed. Clear the marker rather than trying every
				 * boot from here on. */
				playing_clear();
				return false;
			}
			a->sys_cursor = i;
			a->view[i].cursor = k;
			return true;
		}
	}
	playing_clear();                  /* the ROM or its system is no longer here */
	return false;
}

void restore_place(app *a)
{
	char p[CFG_STR * 2], tag[64] = { 0 }, file[LIB_PATH] = { 0 };
	FILE *f;
	snprintf(p, sizeof p, "%s/.last", P_ROOT);
	f = fopen(p, "r");
	if (!f) return;
	if (fgets(tag, sizeof tag, f)) tag[strcspn(tag, "\r\n")] = 0;
	if (fgets(file, sizeof file, f)) file[strcspn(file, "\r\n")] = 0;
	fclose(f);
	for (int i = 0; i < a->sys.count; i++) {
		if (strcmp(a->sys.systems[i].tag, tag) != 0) continue;
		a->sys_cursor = i;
		for (int k = 0; k < a->view[i].list.count; k++)
			if (strcmp(a->view[i].list.items[k].file, file) == 0) {
				a->view[i].cursor = k;
				break;
			}
		return;
	}
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Menu Music's rows. See src/bgm_menu.h. */
#include "bgm_menu.h"
#include "bgm.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

const char *bgm_menu_value(bool on, int ntracks)
{
	if (ntracks <= 0) return T(S_MUSIC_NO_TRACKS);
	return on ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE);
}

int bgm_menu_build(const bgm_ui *u, menu_row *rows, int max, bgm_bufs *b,
                   const char **heading)
{
	bool have = u->ntracks > 0;
	int n = 0;

	*heading = T(S_SYSMENU_MENU_MUSIC);
	memset(b->sound, 0, sizeof b->sound);
	if (max < BGM_ROWS + 2) return 0;

	bgm_volume_label(u->volume, b->vol, sizeof b->vol);
	if (u->track && u->track[0]) bgm_title(u->track, b->track, sizeof b->track);
	else snprintf(b->track, sizeof b->track, "%s", T(S_MUSIC_SHUFFLE));
	if (u->on && u->now && u->now[0] && u->now_name && u->now_name[0])
		snprintf(b->now, sizeof b->now, "%s", u->now_name);
	else if (u->on && u->now && u->now[0]) bgm_title(u->now, b->now, sizeof b->now);
	else snprintf(b->now, sizeof b->now, "%s", T(S_MUSIC_NOTHING));

	/* On and Off, and the track list, come round; Volume stops at its ends
	 * and is heard by whether it moved; A on Playing is the next track.
	 * Song Pop-up is On and Off, and A turns it like Music's */
	b->sound[n] = MENU_SND_RING;
	rows[n++] = (menu_row){ T(S_MUSIC_MUSIC), u->on ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE), true };
	b->sound[n] = MENU_SND_NO_A;
	rows[n++] = (menu_row){ T(S_MUSIC_VOLUME), b->vol, u->on };
	b->sound[n] = MENU_SND_RING | MENU_SND_NO_A;
	rows[n++] = (menu_row){ T(S_MUSIC_TRACK), b->track, u->on && have };
	b->sound[n] = MENU_SND_RING;
	rows[n++] = (menu_row){ T(S_MUSIC_POPUP), u->popup ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE),
	                        u->on && have };
	rows[n++] = (menu_row){ T(S_MUSIC_PLAYING), b->now, u->on && have };

	rows[n++] = MENU_RULE;
	if (have && u->builtin) {
		/* ZyperOS's own, so the note says whose, and where a card's own
		 * go - which then play in their place. Short enough to sit still
		 * in the panel: "Yours in Roms/BGM play instead" scrolled. */
		snprintf(b->count, sizeof b->count, TN(S_MUSIC_BUILTIN_COUNT, u->ntracks), u->ntracks);
		snprintf(b->put, sizeof b->put, T(S_MUSIC_PUT_YOURS), BGM_DIR);
		if (n < max) rows[n++] = MENU_NOTE(b->count);
		if (n < max) rows[n++] = MENU_NOTE(b->put);
	} else {
		if (have)
			snprintf(b->count, sizeof b->count, TN(S_MUSIC_COUNT_IN, u->ntracks),
			         u->ntracks, BGM_DIR);
		else
			snprintf(b->count, sizeof b->count, T(S_MUSIC_PUT_FILES), BGM_DIR);
		if (n < max) rows[n++] = MENU_NOTE(b->count);
	}
	return n;
}

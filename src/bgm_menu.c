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
	bool have = u->ntracks > 0, mine = u->own > 0;
	int n = 0, theirs = u->ntracks - u->own;

	*heading = T(S_SYSMENU_MENU_MUSIC);
	memset(b->sound, 0, sizeof b->sound);
	if (max < BGM_ROWS + 3) return 0;

	bgm_volume_label(u->volume, b->vol, sizeof b->vol);
	if (u->track && u->track[0]) bgm_title(u->track, b->track, sizeof b->track);
	else snprintf(b->track, sizeof b->track, "%s", T(S_MUSIC_SHUFFLE));
	if (u->on && u->now && u->now[0] && u->now_name && u->now_name[0])
		snprintf(b->now, sizeof b->now, "%s", u->now_name);
	else if (u->on && u->now && u->now[0]) bgm_title(u->now, b->now, sizeof b->now);
	else snprintf(b->now, sizeof b->now, "%s", T(S_MUSIC_NOTHING));

	/* On and Off, and the track list, come round; Volume stops at its ends
	 * and is heard by whether it moved; A on Playing is the next track.
	 * ZyperOS Tracks and Song Pop-up are On and Off, and A turns them like
	 * Music's */
#define ROW(id_, snd, ...) do { b->id[n] = (id_); b->sound[n] = (snd); \
                                rows[n++] = (menu_row){ __VA_ARGS__ }; } while (0)
	memset(b->id, BGM_ROWS, sizeof b->id);
	ROW(BGM_ROW_ON, MENU_SND_RING, T(S_MUSIC_MUSIC), u->on ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE), true);
	ROW(BGM_ROW_VOLUME, MENU_SND_NO_A, T(S_MUSIC_VOLUME), b->vol, u->on);
	if (mine)
		ROW(BGM_ROW_BUILTIN, MENU_SND_RING, T(S_MUSIC_BUILTIN),
		    u->with_builtin ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE), u->on);
	ROW(BGM_ROW_TRACK, MENU_SND_RING | MENU_SND_NO_A, T(S_MUSIC_TRACK), b->track, u->on && have);
	ROW(BGM_ROW_POPUP, MENU_SND_RING, T(S_MUSIC_POPUP),
	    u->popup ? T(S_MENU_ON_TITLE) : T(S_MENU_OFF_TITLE), u->on && have);
	ROW(BGM_ROW_NOW, 0, T(S_MUSIC_PLAYING), b->now, u->on && have);
#undef ROW

	rows[n++] = MENU_RULE;
	/* How many, and whose: "12 of yours, 10 ZyperOS", "12 tracks of
	 * yours"; or with none of the card's own, where they go. Short enough
	 * to sit still in the panel in every language. */
	if (mine && theirs > 0)
		snprintf(b->count, sizeof b->count, TN(S_MUSIC_MINE_AND_BUILTIN, u->own), u->own, theirs);
	else if (mine)
		snprintf(b->count, sizeof b->count, TN(S_MUSIC_MINE_COUNT, u->own), u->own);
	else
		snprintf(b->count, sizeof b->count, T(S_MUSIC_PUT_FILES), BGM_CARD_DIR);
	if (n < max) rows[n++] = MENU_NOTE(b->count);
	return n;
}

/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_AOUT_ROUTE_H
#define ZYPEROS_AOUT_ROUTE_H

/* Where the system's sound goes: the half of src/audioout.h that gathers -
 * the setting, the cable, the headset - and tells Diatom, Muse and the
 * interface sounds. Each is described in src/aout_route.c. */
#include "audioout.h"

#include <stdbool.h>

/* Audio Output, and Audio Channels: the settings, as loaded */
extern aout_policy   g_aout_policy;
extern aout_channels g_channels;
/* Inside on_game_tick: a game is running */
extern bool g_game_ticking;

aout_policy   aout_load(void);
void          aout_save(aout_policy p);
aout_channels channels_load(void);
void          channels_save(aout_channels c);

/* The connected Bluetooth sink, from launch.sh's bt_reconnect */
const char *aout_bt_sink(void);
aout_state  aout_now(void);
/* Where Diatom actually is */
aout_dest   aout_actual(const aout_state *s);

/* Route Diatom, Muse and the interface sounds; on a change, or `force` */
void aout_apply(bool force);
/* The interface sounds alone, every frame of a screen that does not route */
void ui_sounds_follow(void);
/* musec's hook, just before a PLAY or RESUME */
void aout_before_muse(void);
/* Muse's own screens, each frame */
void muse_screen_poll(void);
/* Just before a game is handed over, and while it runs */
void aout_before_launch(void);
void aout_retry_in_game(void);

#endif

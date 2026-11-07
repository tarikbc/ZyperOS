/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "bt_screen.h"

#include "aout_route.h"
#include "bt.h"
#include "bt_menu.h"
#include "design.h"
#include "menu.h"
#include "musec.h"
#include "panel.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Bluetooth: pair a headset, connect it, forget it.
 *
 * The three things this stack punishes you for getting wrong are all handled
 * in bt.c rather than here - the agent, judging by `info` instead of by the
 * return of `connect`, and where the bonds actually live. What is left here is
 * a list and four buttons.
 *
 * The list is refreshed on an INTERVAL, never per frame: every query is a fork
 * out of a 119 MB process, and this loop runs every frame. Same two seconds
 * the Wi-Fi row uses, for the same reason.
 */
#define BT_VISIBLE 7
#define BT_SCAN_S  20

/* After connecting `mac` from the Bluetooth screen: wait - five seconds at
 * most - until launch.sh has published it and the sound has reached it, so
 * the headset about to be bumped is not pulled out from under the song. Doing
 * that first sent every switch through the speaker: the old headset went,
 * Muse's write failed, it fell back, and only then was it moved - seen
 * 2026-09-26. Routing keeps running while it waits, which is what moves it. */
static void bt_await_route(const char *mac)
{
	char want[32];
	unsigned until = plat_now_ms() + 5000;
	int i;

	snprintf(want, sizeof want, "bt_%s", mac);
	for (i = 3; want[i]; i++) if (want[i] == ':') want[i] = '_';
	while ((int)(until - plat_now_ms()) > 0) {
		muse_poll();
		aout_apply(false);
		if (!strcasecmp(aout_bt_sink(), want) &&
		    (!musec_heard() || !strcasecmp(musec_sink_now(), want)))
			return;
		SDL_Delay(50);
	}
}

void bt_screen(app *a)
{
	bt_ui u = { 0 };
	menu_row rows[BT_VISIBLE + 4];
	unsigned next_refresh = 0, scan_until = 0, note_until = 0;
	int sel = 0;                  /* ROW space: 0 is the toggle, then devices */
	bool done = false;
	int lv = panel_open();
	float k;

	while (!done && !want_quit && a->running) {
		unsigned now = plat_now_ms();
		int nrows, i;

		if (next_refresh == 0 || now >= next_refresh) {
			u.state = bt_status();
			u.n = u.state == BT_READY ? bt_visible(u.dev, BT_MAX) : 0;
			for (i = 0; i < u.n; i++)
				bt_label(&u.dev[i], u.vals[i], sizeof u.vals[0]);
			next_refresh = now + 2000;
		}
		if (scan_until && now >= scan_until) scan_until = 0;
		/* A message says what just happened; it is not the state of anything,
		 * so it goes away on its own. "Forgotten" used to sit there until the
		 * screen was left and re-entered, which made it read as a condition
		 * rather than an event. */
		if (note_until && now >= note_until) { note_until = 0; u.note[0] = '\0'; }
		u.scanning = scan_until != 0;

		if (sel > u.n) sel = u.n;
		if (sel < 0) sel = 0;
		u.cursor = sel == 0 ? -1 : u.top + sel - 1;
		if (u.cursor >= u.n) u.cursor = u.n - 1;
		nrows = bt_menu_build(&u, rows, (int)(sizeof rows / sizeof rows[0]),
		                      BT_VISIBLE);

		plat_input_poll(&a->in);
		muse_poll();                        /* see menu_run_body */
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		if (in_repeat(&a->in, IN_DOWN)) {
			step_sound(a, IN_DOWN, sel, sel < u.n ? sel + 1 : sel);
			if (sel < u.n) sel++;
		}
		if (in_repeat(&a->in, IN_UP)) {
			step_sound(a, IN_UP, sel, sel > 0 ? sel - 1 : sel);
			if (sel > 0) sel--;
		}
		/* The window follows the cursor, so a long list scrolls a row at a
		 * time from either end. */
		if (u.cursor >= 0) {
			if (u.cursor < u.top) u.top = u.cursor;
			if (u.cursor >= u.top + BT_VISIBLE) u.top = u.cursor - BT_VISIBLE + 1;
		}

		if (a->in.pressed[IN_Y])
			uisound_play(u.state == BT_READY ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
		if (a->in.pressed[IN_Y] && u.state == BT_READY) {
			/* TWENTY seconds, not ten. Classic inquiry runs in cycles of
			 * about 10.24 s and a device answers probabilistically within
			 * one, so a single cycle misses devices that are plainly there:
			 * measured 2026-09-06 with an OpenRun Pro in pairing mode, ten
			 * seconds found nothing twice and twenty found it first try.
			 *
			 * It costs nothing to wait. The list refreshes every two seconds
			 * while the scan runs, so results appear as they arrive rather
			 * than at the end - a longer window only means it keeps
			 * filling. */
			bt_scan(BT_SCAN_S);
			scan_until = now + BT_SCAN_S * 1000;
			next_refresh = now + 1500;
			u.note[0] = '\0';
			note_until = 0;
		}

		if (a->in.pressed[IN_ACCEPT]) {
			char err[64];

			uisound_play(DS_SOUND_ACCEPT);
			if (u.cursor < 0) {
				bool want = u.state != BT_READY;

				/* The preference either way: launch.sh reads it at boot and
				 * starts the stack or not. */
				db_set_int(db_dev(), "bluetooth", want ? 1 : 0);
				db_write_boot_env();

				wait_panel(a, "Bluetooth",
				           want ? "Turning on..." : "Turning off...");
				/* On: the stack first when there is no adapter (Bluetooth
				 * was off at boot), so the switch works now rather than at
				 * the next boot. Never bt_power without one: bluetoothctl
				 * waits for a controller that is not coming. Off: the
				 * adapter, then the stack and the radio's power. */
				if (want && u.state == BT_NO_ADAPTER && !bt_up()) {
					snprintf(u.note, sizeof u.note, "Bluetooth did not come up");
				} else {
					/* The stack just started: the bonds are readable now
					 * (on the Brick they're on plastron's state partition,
					 * which bt_up mounts), so a PCM for each, as after a
					 * pair. The one written at boot had none to read */
					if (want && u.state == BT_NO_ADAPTER)
						bt_asoundrc(P_ROOT, P_USERDATA);
					bt_power(want);
					if (!want) bt_down();
					snprintf(u.note, sizeof u.note, "%s", want ? "On" : "Off");
				}
				note_until = now + 4000;
				next_refresh = 0;
			} else if (u.cursor < u.n) {
				bt_device *d = &u.dev[u.cursor];

				if (d->connected) {
					wait_panel(a, "Bluetooth", "Disconnecting...");
					if (bt_disconnect(d->mac)) d->connected = false;
					snprintf(u.note, sizeof u.note, "Disconnected"); note_until = now + 4000;
				} else {
					bool ok = true;

					if (!d->bonded) {
						wait_panel(a, "Bluetooth", "Pairing...");
						ok = bt_pair(d->mac, err, sizeof err);
						if (!ok) {
							snprintf(u.note, sizeof u.note, "%s", err);
							note_until = now + 4000;
						} else {
							/* A PCM for the new bond. The emulator and
							 * Muse already running see it at their next
							 * open - see bt.h. */
							bt_asoundrc(P_ROOT, P_USERDATA);
						}
					}
					if (ok) {
						wait_panel(a, "Bluetooth", "Connecting...");
						if (bt_connect(d->mac, err, sizeof err)) {
							/* Connecting one here is choosing it, so any other
							 * goes back to paired: "connected" on this screen then
							 * means "where the sound is", and switching back is one
							 * press rather than a disconnect and a connect. Eric's
							 * call, 2026-09-26. Only from here: a headset that
							 * connects by itself does not bump another, or two that
							 * both do could knock each other off forever. Neither
							 * headset reconnected by itself within a minute of the
							 * Brick disconnecting it, so paired stays paired. */
							int bumped;

							bt_await_route(d->mac);
							bumped = bt_disconnect_others(d->mac);

							d->connected = true;
							snprintf(u.note, sizeof u.note, "%s",
							         bumped ? "Connected. The other headset is paired now"
							                : "Connected");
							note_until = now + 4000;
						} else {
							snprintf(u.note, sizeof u.note, "%s", err); note_until = now + 4000;
						}
					}
				}
				next_refresh = 0;
			}
		}

		/* X forgets a paired headset, and only that. */
		if (a->in.pressed[IN_X])
			uisound_play(u.cursor >= 0 && u.cursor < u.n && u.dev[u.cursor].bonded
			             ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
		if (a->in.pressed[IN_X] && u.cursor >= 0 && u.cursor < u.n &&
		    u.dev[u.cursor].bonded) {
			wait_panel(a, "Bluetooth", "Forgetting...");
			if (bt_forget(u.dev[u.cursor].mac)) {
				bt_asoundrc(P_ROOT, P_USERDATA);
				snprintf(u.note, sizeof u.note, "Forgotten"); note_until = now + 4000;
			} else {
				snprintf(u.note, sizeof u.note, "It would not unpair"); note_until = now + 4000;
			}
			next_refresh = 0;
		}

		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw(a, "Bluetooth", rows, nrows, sel, menu_std_width(a),
		          MENU_ZYPEROS);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);

	/* On the way out, not while the list is up: `bluetoothctl devices` reads
	 * the same cache, so sweeping mid-screen would empty the list the player
	 * is looking at. */
	bt_sweep_cache(NULL);
}

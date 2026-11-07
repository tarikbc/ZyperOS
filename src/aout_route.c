/* SPDX-License-Identifier: GPL-3.0-only */
#include "aout_route.h"

#include "app.h"
#include "bgm.h"
#include "btvol.h"
#include "db.h"
#include "musec.h"
#include "platform.h"
#include "uisound.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ---- where the system's sound goes --------------------------------------- */

/* The policy is a setting; the other two facts are read from the device. The
 * RULE that combines them is src/audioout.c, where it can be checked without a
 * handheld - this half only gathers. */
aout_policy g_aout_policy;
static char g_aout_sent[128];      /* the device Diatom was last told to use */
static bool g_aout_ever;
static unsigned g_aout_gen;        /* which connection it was told on */
static unsigned g_aout_retry_at;   /* earliest next in-game retry of a fallback */
static int      g_aout_retries;    /* spent on the game running now */
static bool     g_aout_fallen;     /* a fallback already noticed, so not new */

aout_policy aout_load(void)
{
	char v[32];
	/* The default follows the cable; only an explicit "speaker" overrides. */
	db_get_str(db_dev(), "audioout", v, sizeof v, "auto");
	return !strcmp(v, "speaker") ? AOUT_SPEAKER : AOUT_AUTO;
}

void aout_save(aout_policy p)
{
	db_set_str(db_dev(), "audioout", p == AOUT_SPEAKER ? "speaker" : "auto");
}

/* Which ears hear what, for a player who hears with one. Chosen once; what
 * each program is told depends on where its own sound goes - see
 * aout_channels_for. */
aout_channels g_channels;

aout_channels channels_load(void)
{
	char v[16];

	db_get_str(db_dev(), "channels", v, sizeof v, "stereo");
	return aout_channels_parse(v);
}

void channels_save(aout_channels c)
{
	db_set_str(db_dev(), "channels", aout_channels_wire(c));
}

/* Diatom and Muse each by the device they were just given, because the two
 * can be in different places: with Muse on a headset, Diatom is on the codec,
 * and a side on the headset is still mono on the speaker. Both send only on a
 * change, so this is cheap every tick. */
static void channels_tell(const aout_state *s, const char *diatom_dev,
                          const char *muse_dev)
{
	plat_resident_channels(aout_channels_wire(
		aout_channels_for(g_channels, aout_dest_of(s, diatom_dev))));
	musec_channels(aout_channels_wire(
		aout_channels_for(g_channels, aout_dest_of(s, muse_dev))));
}

/* The connected sink, published by bt_reconnect in launch.sh.
 *
 * A file rather than asking Bluetooth ourselves: asking means forking
 * bluetoothctl out of a 119 MB process, which is what menu_wifi exists to
 * prevent. The shell loop already knows, so it writes and this reads. */
static char g_bt_link[32];    /* the published link's ACL handle, see bt_reconnect */

const char *aout_bt_sink(void)
{
	static char   sink[128];
	static unsigned last;
	static bool   primed;
	unsigned now = plat_now_ms();
	FILE *f;

	/* Half a second. Plugging a cable in should feel immediate; a stat that
	 * often is nothing, but there is no reason to do it 120 times a second. */
	if (primed && now - last < 500) return sink;
	primed = true;
	last = now;
	sink[0] = g_bt_link[0] = '\0';
	f = fopen("/tmp/zyperos_btsink", "r");
	if (f) {
		if (fgets(sink, sizeof sink, f)) sink[strcspn(sink, "\r\n")] = '\0';
		if (fgets(g_bt_link, sizeof g_bt_link, f))
			g_bt_link[strcspn(g_bt_link, "\r\n")] = '\0';
		fclose(f);
	}
	return sink;
}

aout_state aout_now(void)
{
	static unsigned usb_at;
	static bool     usb, usb_primed;
	unsigned now = plat_now_ms();
	aout_state s;

	/* A USB-C DAC as the Bluetooth sink is, every half second: the look is
	 * a path through /proc/asound, and this is asked on every pass of the
	 * shelf's loop, where it was most of aout_apply's 106 us (2026-11-07).
	 * The cable is an ioctl, and stays live. */
	if (!usb_primed || now - usb_at >= 500) {
		usb = plat_usb_audio_present();
		usb_at = now;
		usb_primed = true;
	}
	s.policy  = g_aout_policy;
	s.wired   = plat_headphones_present();
	s.usb     = usb;
	s.bt_sink = aout_bt_sink();
	return s;
}

/* Where Diatom actually IS, which is not always where it was told to go: a
 * sink that will not open, or one that died under it, makes the port fall back
 * and report the fallback. A non-empty device is the named sink; an empty one
 * is the codec, where the cable decides what you hear.
 *
 * Falls back to the launcher's own resolution until Diatom has said anything -
 * on the desktop build it never will. */
aout_dest aout_actual(const aout_state *s)
{
	char at[128];
	const char *muse = musec_sink_now();

	/* Muse holding the headset is where the sound is, whatever Diatom says:
	 * Diatom was moved off it to let Muse have it. */
	if (musec_heard() && muse[0] && strcmp(muse, "default"))
		return strcmp(muse, AOUT_USB_DEVICE) ? AOUT_BT : AOUT_USB;
	if (!plat_resident_audio(at, sizeof at)) return aout_resolve(s);
	if (at[0]) return strcmp(at, AOUT_USB_DEVICE) ? AOUT_BT : AOUT_USB;
	return s->wired ? AOUT_WIRED : AOUT_SPK;
}

/* SETAUDIO, after which Diatom's last report no longer counts until it has
 * answered this one. Reports are only read while a game runs, so at the shelf
 * the one held was from the last game: switching to Speaker there, or with
 * Muse on a headset, the Audio Output row went on saying "bluetooth" while the
 * sound came out of the speaker - #45, 2026-09-26. Until the answer, the row
 * shows what was asked; after it, what happened, fallback included. */
static bool aout_send(const char *dev)
{
	if (!plat_resident_line("SETAUDIO\tdevice=%s", dev)) return false;
	plat_resident_audio_asked();
	return true;
}

/* Send only on a change. SETAUDIO reopens an audio device, which is cheap but
 * not free, and doing it every tick would be a reopen per tick. */
static void aout_tell_diatom(const aout_state *s, const char *dev, bool force,
                             bool muse_has_it)
{
	unsigned gen = plat_resident_generation();

	/* Send when the ASKED-FOR device changes, or when the emulator is a new
	 * one. Never because Diatom ended up somewhere else.
	 *
	 * An earlier version compared against what Diatom REPORTED, so that a
	 * restarted emulator would be re-told. That is unrecoverable on a
	 * fallback: a headset switched off makes Diatom report the default while
	 * this side still wants the sink, the two can never agree, and the resend
	 * fires every tick. Measured 2026-09-05 - 413 route decisions and 335
	 * device reopens in one session, which left the codec parked in SETUP and
	 * the game silent on a speaker that was technically open.
	 *
	 * The restart case is covered by the generation instead, which changes
	 * exactly once per connection rather than continuously. */
	if (!force && g_aout_ever && gen == g_aout_gen && !strcmp(dev, g_aout_sent))
		return;
	g_aout_gen = gen;
	/* Only remember it as sent if it actually went. dsend does nothing when
	 * the socket is not open, and returns false saying so - which on the shelf
	 * before the first game is every time. Recording it as sent anyway meant
	 * the next call compared equal and never retried, so the route the log
	 * announced was one Diatom had never been told about. */
	if (!aout_send(dev)) return;

	snprintf(g_aout_sent, sizeof g_aout_sent, "%s", dev);
	g_aout_ever = true;
	/* Diatom's answer to this lands before a retry judges it. The previous
	 * device's report is still the one held until then. */
	g_aout_retry_at = plat_now_ms() + 5000;
	/* Logged because it only happens on a real change - a cable, a headset,
	 * or the row - and because "where is the sound going" is otherwise
	 * invisible after the fact. Diatom logs its own fallback, so the pair of
	 * lines says both what was asked for and what happened. */
	fprintf(stderr, "audio: %s -> %s%s\n",
	        aout_dest_name(aout_resolve(s)), dev[0] ? dev : "default",
	        muse_has_it ? " (Muse has the headset)" : "");
}

/* The headset's own volume, kept at the Brick's level while sound goes to one.
 *
 * The volume keys move the codec, which a Bluetooth route bypasses, so on a
 * headset they did nothing - and a headset with no buttons of its own, the
 * OpenFit or AirPods, had no volume anywhere. With btplayer registered, BlueZ
 * passes a volume on to the headset over AVRCP (tools/btplayer.c), so the
 * level is mirrored there: in a game the one Diatom last reported, since it
 * owns the keys and this side only applies its levels when the game ends;
 * otherwise this side's own. 0..20 onto AVRCP's 0..127, straight, because
 * AVRCP volume is a plain proportion and the headset applies its own curve.
 *
 * Sent on a change, and again for a new connection so a headset starts at
 * the Brick's level. One way only: a headset's own buttons move the headset
 * and not this level. A send that fails - no control yet, right after a
 * connect - waits two seconds before the next, because each is a mixer open. */
bool g_game_ticking;     /* inside on_game_tick: a game is running */

static void bt_volume_follow(const char *out, bool fresh)
{
	static int      sent = -1;
	static char     sent_to[64];     /* the headset it went to */
	static unsigned retry_at, looked, sent_at;
	static long     seen;            /* the stamp of the last report read */
	static bool     primed;
	int count = 0, level = plat_resident_volume(&count), v;
	unsigned now = plat_now_ms();
	struct stat st;

	if (!out[0]) { sent = -1; sent_to[0] = '\0'; return; }

	/* The other way first: the headset's OWN buttons, which btplayer passes on
	 * as `<PCM name> <0..127> <stamp>` (tools/btplayer.c). Adopted as the Brick's
	 * level - through Diatom in a game, which owns the level while it runs -
	 * and recorded as sent, so it is not echoed back to the headset. A new
	 * stamp is a new report - not a new inode, which tmpfs takes from a pool
	 * and can hand out again, and presses went missing until the next one.
	 * The report found at start is only noted, or a value left from before a
	 * restart would move the level. A tenth of a second between looks.
	 *
	 * NOT within a second of this side sending that headset a volume: it
	 * reports what it was sent, too - the OpenRun answered every set with its
	 * own announcement, measured 2026-09-26 - and sometimes not quite the same
	 * number (sent 32, back came 28), so adopting the echo stepped the level
	 * back and forth. A real press that close to a key press costs one step. */
	if (now - looked >= 100) {
		FILE *f = stat("/tmp/zyperos_btvol", &st) == 0 ? fopen("/tmp/zyperos_btvol", "r") : NULL;
		char who[64];
		int  hv, want;
		long stamp;

		looked = now;
		if (f && fscanf(f, "%63s %d %ld", who, &hv, &stamp) == 3 && stamp != seen) {
			bool fresh_report = primed;

			seen = stamp;
			primed = true;
			if (fresh_report && !strcmp(who, out) && hv >= 0 && hv <= 127) {
				bool echo = !strcmp(who, sent_to) && now - sent_at < 1000;
				int  have = g_game_ticking && level >= 0 ? level : plat_volume_get();

				want = (hv * PLAT_VOL_MAX + 63) / 127;
				if (!echo && want != have) {
					if (g_game_ticking)
						plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d",
						                   want, PLAT_VOL_MAX + 1);
					else
						plat_volume_nudge(want - have);
					sent = (want * 127 + PLAT_VOL_MAX / 2) / PLAT_VOL_MAX;
					snprintf(sent_to, sizeof sent_to, "%s", out);
					fprintf(stderr, "audio: %s's own buttons: %d/127, level %d of %d\n",
					        out, hv, want, PLAT_VOL_MAX);
				}
			}
		}
		if (f) fclose(f);
		if (!primed) primed = true;               /* no file yet is a start too */
	}
	if (level < 0) { level = plat_volume_get(); count = PLAT_VOL_MAX + 1; }
	if (level < 0 || count < 2) return;
	v = (level * 127 + (count - 1) / 2) / (count - 1);
	/* And again for a different headset at the same level: with two connected
	 * the sound can move between them, and a link handle is no help there - it
	 * is reused. */
	if (!fresh && v == sent && !strcmp(out, sent_to)) return;
	if ((int)(now - retry_at) < 0) return;
	if (!btvol_set(out, v)) { retry_at = now + 2000; return; }
	sent = v;
	sent_at = now;
	snprintf(sent_to, sizeof sent_to, "%s", out);
	/* With its cost, because it is a mixer open on the loop that draws. */
	fprintf(stderr, "audio: %s volume %d/127 (level %d of %d) in %u ms\n",
	        out, v, level, count - 1, plat_now_ms() - now);
}

/* Where the system's sound goes, for both players.
 *
 * On the speaker and the jack both simply open the default, which is dmix.
 * A Bluetooth headset cannot be shared - bluealsa 3.1 gives an A2DP PCM to one
 * client at a time, measured 2026-09-25 - so it is HANDED between them, and it
 * goes to whoever may be heard: Muse while it plays, which is exactly when
 * the game has already been quieted, and Diatom otherwise.
 *
 * Release before acquire, or the second open fails and falls back. Towards
 * Muse, Diatom is told first and Muse's SINK retries for a second while Diatom
 * lets go, because at the shelf nothing here reads Diatom's answers. Back
 * towards Diatom, Muse is told first and Diatom is only told once Muse has
 * answered, because Diatom does not retry. */
/* The interface sounds go where the system's sound goes, on the sides
 * Audio Channels says, and nowhere on a headset (src/uisound.h). Said by
 * aout_apply, and every frame by the screens that do not call it, with
 * ui_sounds_follow: a headset that connects while one is up takes the ticks
 * off the speaker there and then. */
static void ui_sounds_route(const aout_state *s)
{
	bool l, r;

	uisound_sides(g_channels, aout_resolve(s), &l, &r);
	uisound_route(uisound_device(s), l, r);
	bgm_route(uisound_device(s), l, r);          /* menu music, the same way */
}

void ui_sounds_follow(void)
{
	aout_state s = aout_now();

	ui_sounds_route(&s);
}

void aout_apply(bool force)
{
	static char  link_seen[32];
	aout_state  s    = aout_now();
	const char *out  = aout_device(&s);
	bool        muse = out[0] && musec_heard();
	/* A new connection under the same name - the headset dropped and came
	 * back between two looks at it. Nothing else here can see that, and
	 * whoever fell back to the speaker while it was gone would stay there:
	 * seen 2026-09-25, a song in Muse. So the holder is told again, once.
	 * Harmless when it never left: Diatom and Muse both ignore a device they
	 * are already on. */
	bool        fresh = out[0] && g_bt_link[0] && strcmp(g_bt_link, link_seen);

	ui_sounds_route(&s);

	/* A USB-C DAC is mixed (the system's "usb" device), so it is not handed
	 * over: Diatom and Muse both have it at once, as both have the codec, and
	 * the headset's volume has nothing to follow. */
	if (aout_resolve(&s) == AOUT_USB) {
		link_seen[0] = '\0';
		aout_tell_diatom(&s, out, force, false);
		musec_sink(out);
		channels_tell(&s, out, out);
		bt_volume_follow("", false);
		return;
	}
	if (!muse) {
		musec_sink("");
		if (out[0] && !musec_sink_settled()) return;    /* Muse is still letting go */
	}
	if (fresh) {
		fprintf(stderr, "audio: %s is a new connection (link %s)\n", out, g_bt_link);
		if (muse) musec_sink_again();
	}
	snprintf(link_seen, sizeof link_seen, "%s", out[0] ? g_bt_link : "");
	aout_tell_diatom(&s, muse ? "" : out, force || (fresh && !muse), muse);
	if (muse) musec_sink(out);
	channels_tell(&s, muse ? "" : out, muse ? out : "");
	bt_volume_follow(out, fresh);
}

/* The hook musec calls just before a PLAY or RESUME, so the track starts on
 * the headset rather than reaching it a tick later. */
void aout_before_muse(void)
{
	aout_apply(false);
}

/* Muse's own screens, each frame. They route only while music is heard: a
 * headset that connects mid-song takes the song there and then, where before
 * it stayed on the speaker until something new was played - seen 2026-09-25,
 * resume and R1 both went to the speaker right after a pairing. Paused, they
 * still leave the headset where it is; nothing else can be heard on these
 * screens, so handing it back on every pause would only be churn. */
void muse_screen_poll(void)
{
	muse_poll();
	if (musec_heard()) aout_apply(false);
	else               ui_sounds_follow();
}

/* A sink that failed once is otherwise never tried again. aout_apply sends on
 * a change of the ASKED-FOR device, and a headset that would not open, then
 * became usable under the same name, is no change at all: 2026-09-24 left
 * Diatom on the speaker through a game with the headset connected and working.
 *
 * Retried where it cannot be seen, and rarely where it can. A failed SETAUDIO
 * is not free: Diatom closes, fails the sink and reopens the default on its
 * main thread. Measured 2026-09-24 in Contra, 17 switches, 15 of them failed
 * fakes: 56.00 fps against 59.95, ~351 frames short, 18 resyncs against 3.
 * About 20 frames - a third of a second of frozen game - per failed try, and
 * that is the lower bound, because a fake name fails before bluealsa is ever
 * asked. A retry a minute would be a stutter a minute. */

/* Whether Diatom is on the default device although it was last told to use a
 * sink. The default and not merely "somewhere else", because the default is
 * the only place a fallback goes, and a report naming another sink is one that
 * has not caught up with the last SETAUDIO yet. Testing for any mismatch fired
 * a retry in the same tick as the send, before Diatom had answered - measured
 * on the device 2026-09-24, and it spent one of the two stalls on nothing.
 *
 * Only true to life while a game runs: at the shelf nothing reads Diatom's
 * reports - they are dropped at the next game start - so there it describes
 * the last game. */
static bool aout_fell_back(void)
{
	char at[128];

	if (!g_aout_ever || !g_aout_sent[0]) return false;
	if (!plat_resident_audio(at, sizeof at)) return false;
	return at[0] == '\0';
}

/* Just before a game is handed over. Unconditional, because the shelf cannot
 * know whether it is needed and it costs nothing when it is not: Diatom
 * returns early on the device it is already using, without reopening. When
 * the sink is still broken, the stall is spent inside the launch. */
void aout_before_launch(void)
{
	g_aout_retries = 0;
	g_aout_fallen = false;
	g_aout_retry_at = plat_now_ms() + 5000;
	if (!g_aout_ever || !g_aout_sent[0]) return;
	if (aout_fell_back())
		fprintf(stderr, "audio: trying %s again before the launch\n", g_aout_sent);
	aout_send(g_aout_sent);
}

/* Twice per game at most: for a headset whose A2DP stream was not up yet when
 * the game asked for it, or one that dropped out and came back under the same
 * name. Spaced so that Diatom's answer to the first has arrived before the
 * second is judged. */
void aout_retry_in_game(void)
{
	bool fell = aout_fell_back();

	/* Never in the moment a fallback is noticed. A sink that has just died is
	 * certainly still dead: a headset disconnected mid-game on 2026-09-24 had
	 * its first retry fire within the same second and fail, and only the
	 * second, after bt_reconnect brought it back 18s later, landed. */
	if (fell && !g_aout_fallen) {
		unsigned soonest = plat_now_ms() + 5000;

		if ((int)(soonest - g_aout_retry_at) > 0) g_aout_retry_at = soonest;
	}
	g_aout_fallen = fell;

	if (!fell || g_aout_retries >= 2) return;
	if ((int)(plat_now_ms() - g_aout_retry_at) < 0) return;
	if (!aout_send(g_aout_sent)) return;
	g_aout_retries++;
	g_aout_retry_at = plat_now_ms() + 25000;
	fprintf(stderr, "audio: %s fell back; trying again (%d of 2)\n",
	        g_aout_sent, g_aout_retries);
}

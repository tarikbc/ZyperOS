/* SPDX-License-Identifier: GPL-3.0-only */
/* Menu music: tracks from the card played behind the shelf and the menus, and
 * never over a game, a port or Muse.
 *
 * The card's own come from Menu Music, a folder of their own at the top of
 * the card beside Music and Videos, filled as they are: on the card in a
 * computer, in Flash Drive, or on File Transfer's page (2026-11-13: "a
 * specific BGM folder for those custom songs in flash drive mode/sd card/web
 * portal"; named Menu Music, my pick, as MENU calls it). It is the only one:
 * ROGUE OS's Roms/BGM, which played until 1.0.5, is an ordinary folder now
 * ("if we are getting rid of roms/bgm, then only menu music"). ZyperOS ships
 * a few free 8-bit tracks of its own (res/bgm/, credited in
 * THIRD-PARTY-LICENSES.md), played with the card's own while ZyperOS Tracks
 * is on, and whatever it says while the card has none (bgm_scan_all).
 *
 * Three halves, the split src/uisound.h uses. src/bgm.c is the rule and the
 * arithmetic - which files are tracks, the settings, the shuffle, the gain -
 * with no SDL, no ALSA and no threads, so tools/bgm-check.c holds it on a
 * build machine. src/bgm_dec.c decodes a file, MP3 or Ogg Vorbis, with the
 * two vendored decoders in src/third_party/. src/bgm_out.c is the player: a
 * thread of its own that decodes, fades and writes, so nothing the main
 * thread does waits on a file or on the device.
 *
 * MENU > Menu Music is src/bgm_menu.c's rows and main.c's screen. */
#ifndef ZYPEROS_BGM_H
#define ZYPEROS_BGM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bgm_tags.h"

/* The card's menu music folder, at its top (P_CARD) */
#define BGM_CARD_DIR "Menu Music"
/* ...and the one under ZyperOS's own files (P_ROOT) that holds the tracks it
 * ships */
#define BGM_BUILTIN_DIR "res/bgm"
/* How many folders deep in its folder a track is found: two, as in
 * Menu Music/Album/Disc 1/01 Song.mp3 */
#define BGM_DEPTH 2

/* ---- the settings -----------------------------------------------------------
 *
 * Four keys in the device database, beside "uisounds": "bgm" on or off,
 * "bgm.volume" 1 to 10 (tenths of the track's own level, under the system's
 * volume), "bgm.track", "" for shuffle or a track's name in the list
 * (bgm_list), played over and over, and "bgm.builtin", ZyperOS Tracks, on or
 * off (bgm_on_parse: on to start). */
#define BGM_VOLUME_MIN     1
#define BGM_VOLUME_MAX     10
#define BGM_VOLUME_DEFAULT 5

bool        bgm_on_parse(const char *v);          /* anything but "off" is on */
const char *bgm_on_id(bool on);                   /* "on" / "off" */
int         bgm_volume_parse(const char *v);      /* clamped; junk is the default */
/* "50%" for 5 */
void        bgm_volume_label(int volume, char *out, size_t n);
/* volume stepped by dir, stopping at either end */
int         bgm_volume_step(int volume, int dir);

/* ---- the folder ---------------------------------------------------------------
 *
 * A track is a file whose name ends in .mp3 or .ogg, any case, and does not
 * start with a dot - macOS leaves ._ files beside everything it copies. */
bool bgm_is_track(const char *name);

/* Where a track in the list comes from */
enum { BGM_FROM_FOLDER, BGM_FROM_BUILTIN };

/* The tracks that play. A folder's are sorted by name (strcasecmp), so a
 * track's place in the list and in the Track row does not depend on the
 * order readdir gives.
 *
 * `names` is what the Track setting keeps for each: its path in its folder,
 * which for a file straight in it is its name, as the setting always was
 * ("FC001.mp3"), and for one in a folder of its own the folder's too
 * ("Pixel Quest/02 Green Fields.mp3"). `paths` is where each one is, `from`
 * which of the two folders it came from, and `own` how many are the card's
 * own: Menu Music's. */
typedef struct {
	char         **names;
	char         **paths;
	unsigned char *from;
	int            n, own;
} bgm_list;

/* Fills `l` from `dir` and its folders, BGM_DEPTH deep; a missing or empty
 * folder is an empty list, not a failure. Frees what `l` held first. */
void bgm_scan(const char *dir, bgm_list *l);
void bgm_free(bgm_list *l);
/* Everything that plays (2026-11-13: "being able to disable the standard
 * tracks"): the card's own, Menu Music's, and ZyperOS's after them while
 * `with_builtin` (MENU > Menu Music > ZyperOS Tracks) is on. With none of
 * the card's own, ZyperOS's play whatever it says, so there is always music
 * to turn on. One of ZyperOS's that the card has a copy of by name plays
 * once, as the card's. */
typedef struct {
	const char *dir;       /* Menu Music, at the top of the card */
	const char *builtin;   /* ZyperOS's, NULL for none */
	bool        with_builtin;
} bgm_sources;

void bgm_scan_all(const bgm_sources *s, bgm_list *l);

/* The index of `name` in the list, or -1 */
int  bgm_find(const bgm_list *l, const char *name);

/* What the Track row shows for a file: the name without its folders and its
 * extension */
void bgm_title(const char *file, char *out, size_t n);

/* The Track row's positions: -1 is Shuffle, then each track. Steps either
 * way and comes round, Shuffle between the last track and the first. */
int bgm_track_step(int at, int n, int dir);

/* ---- the shuffle ----------------------------------------------------------------
 *
 * A deck, as ROGUE's EmulationStation did not have: it picked at random each
 * time, so a short folder played one track twice in a row and a long one
 * left tracks unheard for hours. Every track plays once before any plays
 * again, and a new deck never starts with the track the last one ended on. */
typedef struct {
	int     *order;
	int      n, at;
	uint32_t seed;
} bgm_deck;

void bgm_deck_init(bgm_deck *d, int n, uint32_t seed);
void bgm_deck_free(bgm_deck *d);
/* The next track's index, dealing a new deck when this one is played out.
 * -1 for an empty folder. */
int  bgm_deck_next(bgm_deck *d);

/* ---- going back ------------------------------------------------------------------
 *
 * L2 on the shelf is the track before, R2 the next (2026-11-06: "have
 * clicking r2 advance and l2 prev the song"). The deck knows what comes, not
 * what went, so the player keeps what was heard: the last BGM_HIST tracks
 * and which of them plays. Back walks along them; next after a back walks
 * forward again to the track it came from before the deck deals anything
 * new, the way a playlist does. Indices into the folder's list, so a read of
 * the folder empties it. */
#define BGM_HIST 32

typedef struct {
	int idx[BGM_HIST];
	int n;      /* kept */
	int at;     /* the one playing, n - 1 unless gone back */
} bgm_hist;

void bgm_hist_clear(bgm_hist *h);
/* A track begun that the history did not choose: anything ahead of the one
 * playing goes, and it follows it, the oldest let go when full */
void bgm_hist_push(bgm_hist *h, int track);
/* The track before the one playing, moved onto; -1 when there is none */
int  bgm_hist_back(bgm_hist *h);
/* The track after the one playing when gone back, moved onto; -1 at the
 * newest, where the deck deals */
int  bgm_hist_fwd(bgm_hist *h);
/* The one playing, -1 for none */
int  bgm_hist_now(const bgm_hist *h);

/* ---- the Track setting's neighbors ------------------------------------------------
 *
 * L2/R2 on one track, the Track row set to a file: the file before or after
 * it in the folder, coming round, and played over and over in its place -
 * the setting moves, as the Track row would move it, without Shuffle between
 * the last and the first. `at` is the setting's index; -1 when the folder is
 * empty. */
int bgm_file_step(int at, int n, int dir);

/* ---- the level --------------------------------------------------------------------
 *
 * A gain in 1/65536ths for the volume and where a fade is, `fade` running 0
 * (silent) to `fade_len` (full). Linear in the fade, and in the volume's
 * square, so 5 of 10 is a quarter of the power - half as loud to the ear,
 * which is what a step called 50% is taken to mean. */
int  bgm_gain(int volume, int fade, int fade_len);
/* Interleaved stereo frames scaled by a gain moving from g0 to g1 across them,
 * so a fade or a volume change is a ramp and not a click. */
void bgm_apply(int16_t *pcm, int frames, int g0, int g1);
/* Both channels mixed onto one side, the other silent */
void bgm_one_side(int16_t *pcm, int frames, bool left);
/* Both channels mixed onto both: mono, the average, which cannot clip */
void bgm_mono(int16_t *pcm, int frames);

/* ---- the rate ---------------------------------------------------------------------
 *
 * Every track to the mix's 48 kHz here, by linear interpolation, rather than by
 * ALSA's plug in front of dmix: measured on the Pixel 2 (2026-11-05), writing
 * 44.1 kHz through the plug cost 7.4% of a core against 5.9% for 48 kHz
 * written as it is, and an MP3's decoding is 0.9% - the plug was the larger
 * part of the music's cost. The same converter ALSA's plug uses, so nothing
 * is lost to the ear; and the output stays open from one track to the next
 * whatever their rates. Stereo frames in, stereo frames out. */
#define BGM_OUT_RATE 48000

typedef struct {
	uint32_t step;      /* input frames per output frame, 16.16 */
	uint32_t pos;       /* where the next output frame is, 16.16, counted from
	                     * the frame before this input (`last`) */
	int16_t  last[2];   /* the previous input's last frame */
	bool     primed;
} bgm_rs;

void bgm_rs_init(bgm_rs *r, int in_rate, int out_rate);
/* The output frames `in`'s n frames make, at most out_max (n * out_rate /
 * in_rate + 2 always fits). Returns how many. */
int  bgm_rs_run(bgm_rs *r, const int16_t *in, int n, int16_t *out, int out_max);

/* ---- the player (src/bgm_out.c) ----------------------------------------------- */

/* Why it is quiet. Any of these held is silence; the music comes back where it
 * stopped once all are let go. */
enum {
	BGM_HOLD_START = 1 << 0,  /* until the shelf's first pass: a game resumed
	                           * at boot goes on without a bar of music */
	BGM_HOLD_GAME  = 1 << 1,  /* a game, from the launch until the shelf */
	BGM_HOLD_MUSE  = 1 << 2,  /* Muse playing, or asked to */
	BGM_HOLD_VIDEO = 1 << 3,  /* the video player */
	BGM_HOLD_SNAP  = 1 << 4,  /* a video snap on the shelf, with its sound on */
};

/* The two folders (bgm_sources): Menu Music and ZyperOS's own. Starts held
 * by BGM_HOLD_START. */
void bgm_init(const char *dir, const char *builtin);
/* Lets the output go without waiting long; for the exit, a port's too. */
void bgm_quit(void);

void bgm_set_on(bool on);
void bgm_set_volume(int volume);
/* ZyperOS Tracks: ZyperOS's tracks with the card's own, or the card's alone */
void bgm_set_builtin(bool with);
bool bgm_builtin(void);
/* "" for shuffle, or a file in the folder */
void bgm_set_track(const char *file);
bool bgm_on(void);
int  bgm_volume(void);
/* The track setting: "" for shuffle */
const char *bgm_track(void);

/* Holds or lets go of one of the reasons above. Cheap when nothing changes,
 * so a loop may say it every pass. */
void bgm_hold(unsigned why, bool held);

/* Where to play: an ALSA device name, NULL for nowhere (a Bluetooth headset,
 * which is one player's at a time - the rule src/uisound.c follows), and
 * which sides carry it (uisound_sides): one side alone is both channels mixed
 * onto it, for Audio Channels' mono on one side. `mono` with both sides is
 * the two mixed onto both: Audio Channels' mono, and Auto's on one speaker. */
void bgm_route(const char *device, bool left, bool right, bool mono);

/* The next track now: in shuffle, the next of the deck; on one track, from
 * its start. */
void bgm_skip(void);

/* L2 and R2 on the shelf, `dir` -1 or 1. In shuffle, the track before (from
 * what was heard, bgm_hist; the one playing from its start when there is
 * none before it) or the next (bgm_skip). On one track, the Track setting
 * moves to the file before or after it (bgm_file_step) and that one plays:
 * then true, with the new setting in `track` for the caller to keep in the
 * database. Presses closer together than a track takes to open are all
 * counted: three R2s are three tracks on. */
bool bgm_step(int dir, char *track, size_t n);

/* Whether the music is to be heard now: on, a track in the folder, a place to
 * play it and nothing holding it. What L2/R2 ask before they act, and the
 * pop-up before it says a name. */
bool bgm_audible(void);

/* A track begun, for Zyper's pop-up (DESIGN.md 11.22): `seq` moves each time
 * the player opens one that is news - the first, the next, one chosen, one
 * skipped to - and not when one track starts again by itself at its end, nor
 * when a game or Muse let go of one that was paused. `at` is when it opened
 * (SDL_GetTicks), so a change the shelf did not see happen, under a menu, is
 * not said late. */
typedef struct {
	unsigned seq;
	unsigned at;
	char     file[512];
	bgm_tags tags;
} bgm_song;

void bgm_song_now(bgm_song *s);

/* The folder read again - a track copied on over Wi-Fi or USB. */
void bgm_rescan(void);

/* How many tracks the player last read in the folder; 0 before it has. */
int  bgm_count(void);

/* The file playing or paused, "" for none, into out. */
void bgm_now(char *out, size_t n);

#endif

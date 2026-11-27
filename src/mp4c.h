/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MP4C_H
#define ZYPEROS_MP4C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mp4hand.h"
#include "mp4queue.h"

/* The launcher's side of MP4: the connection to its daemon, mp4d, and the
 * play queue.
 *
 * The daemon (src/mp4d/) knows one file at a time. What comes next is policy,
 * and policy is the launcher's - so this holds the queue, and when the daemon
 * says END it sends the next PLAY itself.
 *
 * Polled, never waited on: mp4c_poll reads whatever has arrived and returns.
 * It is called from the shelf's main loop, from MP4's own screen, and from
 * the 10 Hz tick the launcher gets while a game runs - which is what lets an
 * album carry on through a game, the way EROS's MP4 did. On the speaker and
 * the jack the mixing needs nothing from anybody: both processes open
 * `default`, which is dmix. A Bluetooth headset is different - see mp4c_sink. */

typedef enum { MU_OFF, MU_STOPPED, MU_PLAYING, MU_PAUSED } mu_state;

typedef struct {
	mu_state state;
	char     title[128], artist[128], album[128];
	double   at, len;
	double   speed;                 /* 1.0 but on a book set otherwise */
	/* Where in the queue, and how long it is. The PLAY order's place, so
	 * shuffled it counts the order being heard rather than album order. */
	int      index, count;
	/* The playing file's picture, as META said: its size, 0x0 for sound
	 * alone. The pictures themselves come through src/mp4video.h. */
	int      vw, vh;
} mu_now;

/* Where the daemon lives, and the folder paths are relative to: the card,
 * since tracks come from Music and books from Audiobooks. Starting it is lazy:
 * the first call that wants it spawns it if it is not answering. */
void mp4c_init(const char *mp4_bin, const char *root);

void mp4c_poll(void);

/* Queue `n` tracks - paths relative to the root - and play from `start`,
 * `at` seconds into it. The launcher's copies; the caller's array may go away.
 *
 * A `book` queue plays in order whatever the play mode is: a book shuffled or
 * on repeat is not a way anyone listens to one. The mode is left as it was
 * set, for the music after it. */
void mp4c_play(const char *const *paths, int n, int start, double at, bool book,
                double speed, const char *artist, const char *album);

/* The next queue (mp4c_play, mp4c_play_videos) opens held: its file at its
 * place, paused, with no sound at all - PLAY's paused=1 - and Now Playing
 * shows it paused at once. MP4 back after a power-off (src/mp4resume.h). */
void mp4c_hold_next(void);

/* Where a queue whose tracks are from many albums - Shuffle All - finds each
 * track's artist and album: mp4c_play with NULL for both asks this for every
 * track, where one album's queue names its own once. The tags still win where
 * a file has them. */
void mp4c_names_by(void (*fn)(const char *track, char *artist, size_t an,
                               char *album, size_t aln));

/* The queue handed to the next process while a program of its own plays
 * over MP4 (src/mp4hand.h). mp4c_hand_out copies the queue, where it is and
 * what this process last heard of it into `h` (mp4hand_free it after); false
 * when there is no queue. mp4c_hand_in makes `h` this process's queue with
 * no PLAY - the daemon plays it already - and drops any connection, so the
 * daemon's next word is a whole announcement: the queue is put on the
 * track the daemon says it plays, and moved on to the next if the one `h`
 * said was playing has run out with nobody listening. An `h` with no tracks
 * leaves no queue. */
bool mp4c_hand_out(mp4hand *h);
void mp4c_hand_in(const mp4hand *h);

/* Whether the file playing has a picture: MP4's video. */
bool mp4c_video(void);

/* Whether the player's video is on screen. The daemon decodes pictures only
 * while it is, so a video left playing behind another screen, or under a
 * dark lock, costs what its sound does and no more. Sent when it changes,
 * and again to a daemon that has just started. */
void mp4c_video_show(bool on);

/* Bars on screen (MP4's Player Looks, src/mp4look.h): every frame that
 * draws them keeps the daemon measuring the sound for MP4C_BANDS_LEASE
 * more ms, 30 times a second (BANDS, src/mp4d/bands.h). A lease and not a
 * switch, so nothing has to remember to turn it off: a look left behind
 * another screen, under the lock, paused and still, or on Cover draws no
 * bars, and a moment later the daemon stops measuring. Sent when it
 * changes, and again to a daemon that has just started, as
 * mp4c_video_show is. */
#define MP4C_BANDS 32
#define MP4C_BANDS_LEASE 300
void mp4c_bands_keep(void);
/* The last levels the daemon sent, 0-255 from the lowest band up, and how
 * long ago in `age_ms`; false when none has come since bars were asked for */
bool mp4c_bands(uint8_t out[MP4C_BANDS], unsigned *age_ms);

/* A game's video snap on the shelf (src/mp4d/snap.h): `path` played from its
 * start and looped, its pictures in their own frames (src/snapview.h); NULL
 * stops it. `vol` is its sound, 0 to 100, 0 muted. `hold` asks for its
 * first picture only, held with no clock and no sound, and the same path
 * with `hold` false then starts it from there (src/mp4d/snap.h). Sent only
 * when one of the three changes, so a loop may say it every pass. Nothing
 * at all without a daemon: the shelf keeps its cover. */
void mp4c_snap(const char *path, int vol, bool hold);

/* A queue of videos - a film, or a series' episodes: in order whatever the
 * mode is, as a book is (a film on repeat is not how anyone watches one), and
 * otherwise like music - no speed, no chapters by L1 and R1. */
void mp4c_play_videos(const char *const *paths, int n, int start, double at,
                       const char *artist, const char *album);
bool mp4c_is_videos(void);

/* Whether the queue is a book. */
bool mp4c_is_book(void);

/* A book's speed, 0.5 to 2.0 with the pitch kept - the daemon's SPEED. Set on
 * the playing queue at once and carried by every PLAY after it: the daemon
 * treats a PLAY with no speed as 1x, so a speed the launcher did not repeat
 * would be lost at the next file. */
void   mp4c_set_speed(double x);
double mp4c_speed(void);

/* The chapters of the file playing, as the daemon read them from it (an
 * M4B's; a folder of MP3s has none, its files are its parts). A book with more
 * than one moves by chapter on L1 and R1 - see mp4c_next. */
int         mp4c_chapters(void);
const char *mp4c_chapter_title(int i);
/* The chapter the playing position is in, or -1 with none. */
int         mp4c_chapter_now(void);

/* True once each time a queue plays out to its end - the last track ended by
 * itself, not stopped or replaced. A book that does has been finished. */
bool mp4c_take_ran_out(void);

void mp4c_toggle(void);       /* pause or resume */
/* The next and previous track - or, in a book whose file has chapters, the
 * next chapter and the start of this one (or the one before, within three
 * seconds of its start), falling back to the files at either end. */
void mp4c_next(void);
void mp4c_prev(void);
void mp4c_seek_by(double delta);
void mp4c_stop(void);

const mu_now *mp4c_now(void);

/* Where the song is this moment, between the daemon's reports (POS, once a
 * second): carried on from the last at the speed it plays. For what moves
 * with the song every frame - a Player Look's tape and tonearm - where the
 * clock's whole seconds would step. */
double mp4c_at_live(void);

/* The queued track, relative to the music root, or "" - so a list can mark
 * the row that is playing without comparing titles, which tags can change. */
const char *mp4c_path(void);

/* Playing right now, for Auto Off: music is somebody using the device with
 * nobody touching it, the same as the charger. */
bool mp4c_playing(void);

/* Playing, or asked to and not yet answered. What decides who holds a
 * Bluetooth headset, which has to be true from the moment a PLAY is sent:
 * mp4c_playing only turns true when the daemon says so, and in between the
 * route would hand the headset back. */
bool mp4c_heard(void);

/* The output. bluealsa here gives a headset to one process at a time, so it
 * is handed between Diatom and MP4 rather than shared: whoever may be heard
 * holds it (Diatom already quiets a game while music plays). `device` is an
 * ALSA name, or "" for the default. Sent only when it changes; the daemon
 * tries a headset for up to a second, while the other side lets go, and falls
 * back to the default if it never does. */
void mp4c_sink(const char *device);
/* Which ears hear what: "stereo", "mono", "left" or "right", already decided
 * for where MP4's sound goes (src/audioout.h). Sent only when it changes,
 * and again to a daemon that has just said READY, since it starts in stereo. */
void mp4c_channels(const char *mode);
/* The equalizer's bands for where MP4 plays (src/eq.h), dB each, and
 * whether its page is open (`live`: MP4 keeps only about 60 ms queued, so a
 * step is heard in under a tenth of a second). Sent on a change only. */
void mp4c_eq(const int8_t db[5], bool live);

/* The volume boost's gain for MP4's sound, dB (src/levels.c): MP4 applies
 * it after the equalizer, with its limiter (src/eqdsp.h). Sent on a change,
 * and again to an MP4 started anew. */
void mp4c_gain(int db);
/* Make the next mp4c_sink send even if the device has not changed: for a
 * headset that dropped and came back under the same name, which MP4 may
 * have fallen back from while it was gone. The daemon ignores a SINK for the
 * device it is already on, so this costs nothing when it never left. */
void mp4c_sink_again(void);
/* Whether the last mp4c_sink has been answered, or given up on - so the
 * launcher can hand the headset to Diatom only once MP4 has let go. */
bool mp4c_sink_settled(void);
/* What the daemon last said it is on, or "" before it has said, or while a
 * SINK is waiting for its answer. */
const char *mp4c_sink_now(void);
/* Called just before a PLAY or RESUME is sent, so the launcher can move the
 * headset to MP4 first. */
void mp4c_on_before_heard(void (*fn)(void));

/* The queue's track `i` in PLAY order - shuffled, the i-th to be heard -
 * relative to the music root, or NULL past either end. */
const char *mp4c_track(int i);

/* What plays when this track finishes, or NULL when that is nothing or not yet
 * decided - see muq_upcoming. For "Next:", which has to follow the mode. */
const char *mp4c_upcoming(void);

/* The play mode, src/mp4queue.h. Applied to the queue playing now without
 * changing its track, and kept for every queue after it; saving it is the
 * caller's. */
void     mp4c_set_mode(muq_mode m);
muq_mode mp4c_mode(void);
/* Videos' own mode, MP4's MENU > Repeat (2026-11-19): in order (Off), repeat
 * all or repeat one, never shuffle. Applied to a queue of videos playing now
 * and kept for every one after it; music's mode is left as it is. Saving it
 * is the caller's. */
void     mp4c_set_video_mode(muq_mode m);
muq_mode mp4c_video_mode(void);

/* Ask for the picture `track` (relative to the music root) carries, written to
 * `base` plus the extension its format has. The daemon answers in a few
 * milliseconds and the answer is collected with mp4c_cover_take.
 *
 * False when it could not be asked - the daemon is not up yet, and asking has
 * just started it - and the caller asks again later. An answer can also be
 * lost with the daemon, so a caller that asked and heard nothing should ask
 * again after a while rather than wait for good. */
bool mp4c_cover_ask(const char *track, const char *base);

/* One answer: the `base` asked about, and the file written there - "" when the
 * track carries no picture or it could not be written. False when none is
 * waiting. */
bool mp4c_cover_take(char *base, size_t bn, char *file, size_t fn);

/* Ask who the tracks in `dir` (relative to the music root) are by, from their
 * tags: the album artist, or the artist every track agrees on, or "" (see
 * src/mp4d/tags.h). False when it could not be asked, as mp4c_cover_ask. The
 * answer is collected with mp4c_artist_take, `dir` as it was asked. */
bool mp4c_artist_ask(const char *dir);
bool mp4c_artist_take(char *dir, size_t dn, char *name, size_t nn);

/* Ask how long the tracks in `dir` (relative to the music root) play, for
 * the tape's number (src/mp4d/tags.h); the answer, in seconds, collected
 * with mp4c_length_take, `dir` as it was asked */
bool mp4c_length_ask(const char *dir);
bool mp4c_length_take(char *dir, size_t dn, double *s);

#endif

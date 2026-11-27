/* SPDX-License-Identifier: GPL-3.0-only */
/* See mp4c.h. The protocol is src/mp4d/mp4d.c's. */
#include "mp4c.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "mp4lib.h"
#include "mp4queue.h"
#include "platform.h"

#define MP4_SOCK "/tmp/mp4d.sock"

/* The daemon's socket: its own, or $ZYPEROS_MP4_SOCKET's for a check on a
 * computer, which talks to a stand-in daemon there (as
 * ZYPEROS_DIATOM_SOCKET does Diatom's) */
static const char *sock_path(void)
{
	const char *v = getenv("ZYPEROS_MP4_SOCKET");

	return v && *v ? v : MP4_SOCK;
}

static char    g_bin[256], g_root[256];
static int     g_fd = -1;
static char    g_in[4096];
static size_t  g_have;
static unsigned g_spawned_ms;
static int     g_spawned;

/* Covers the daemon has answered about and nobody has taken yet. A handful:
 * they are asked for one screen at a time, and one dropped here is asked for
 * again by a caller whose answer never came - see mp4c_cover_ask. */
#define COVER_RING 8
static struct { char base[LIB_PATH * 2], file[LIB_PATH * 2 + 8]; } g_cov[COVER_RING];
static int     g_cov_head, g_cov_n;

/* Who folders are by, answered and not yet taken, the same way. */
#define ARTIST_RING 8
static struct { char dir[LIB_PATH * 2], name[256]; } g_art[ARTIST_RING];
#define LENGTH_RING 4
static struct { char dir[LIB_PATH * 2]; double s; } g_len[LENGTH_RING];
static int g_len_head, g_len_n;
static int     g_art_head, g_art_n;

static int     g_show = -1;         /* VIDEO show= last sent, -1 none yet */
static bool    g_show_want;         /* what it should be */
static int     g_bands_sent = -1;   /* BANDS on= last sent, -1 none yet */
static bool    g_bands_want;        /* what it should be */
static unsigned g_bands_until;      /* ...until then, if no frame keeps it */
static uint8_t g_bands[MP4C_BANDS];  /* the last levels, and when they came */
static unsigned g_bands_ms;
static bool    g_names_each;        /* the queue names each track's own; see mp4c_names_by */
static void  (*g_names_fn)(const char *, char *, size_t, char *, size_t);
static char  **g_q;                 /* the queue, relative paths, ours */
static int     g_qn;
static muq     g_order;             /* which of them plays when: the mode */
static muq_mode g_mode = MUQ_IN_ORDER;
static muq_mode g_video_mode = MUQ_IN_ORDER;   /* videos' own: no shuffle */
static int     g_pending;           /* a PLAY waiting for the connection */
static bool    g_advanced;          /* the next track is asked for; see event() */
static int     g_skips;             /* consecutive tracks that would not open */
static char    g_artist[128], g_album[128];
static mu_now  g_now;
static bool    g_book;              /* the queue is a book: in order, always */
static bool    g_videos;            /* or videos: in order too */
static bool    g_videos_call;       /* inside mp4c_play_videos */
static double  g_start_at;          /* where the first PLAY of a queue starts */
static bool    g_start_held;        /* and that it opens held, paused (mp4c_hold_next) */
static bool    g_held_sent;         /* a held PLAY sent and not yet answered */
static bool    g_ran_out;           /* see mp4c_take_ran_out */
static double  g_speed = 1.0;       /* see mp4c_set_speed */
/* A queue handed over (mp4c_hand_in) and waiting for the daemon's word on
 * it: what the hand said, playing or paused, or 0 */
static int     g_adopt;
/* The playing file's chapters, from the daemon's CHAPTER lines. */
#define MAX_CHAPTERS 256
static double  g_ch_at[MAX_CHAPTERS];
static char    g_ch_title[MAX_CHAPTERS][96];
static int     g_nch;
/* A seek on its way: where to, and when it was asked for. See seek_to. */
static bool    g_seeking;
static double  g_seek_to;
static unsigned g_seek_ms;
/* When g_now.at was last said, for mp4c_at_live */
static unsigned g_at_ms;

/* The output, which MP4 holds for its whole life and has to be HANDED a
 * headset: see mp4c_sink in mp4c.h. */
static char     g_sink_sent[128];   /* what it was last asked for, "" unknown */
static char     g_sink_said[128];   /* what it last reported, "" not yet */
static unsigned g_sink_ms;          /* when the ask went */
static bool     g_sink_waiting;     /* asked, and no answer yet */
static char     g_chan_sent[8];     /* the channels last asked for, "" none yet */
/* The equalizer last asked for, as EQ carries it ("5,2,0,0,1 live=1"); ""
 * none yet */
static char     g_eq_sent[40];
static int      g_gain_sent = -1;   /* the boost's gain last sent, dB */
static bool     g_asked;            /* PLAY or RESUME sent, not yet answered */
static void   (*g_before_heard)(void);

static void seek_to(double t);

void mp4c_init(const char *mp4_bin, const char *root)
{
	snprintf(g_bin, sizeof g_bin, "%s", mp4_bin);
	snprintf(g_root, sizeof g_root, "%s", root);
}

/* What was last asked of the daemon's snap, so a frame loop's every pass
 * sends nothing; forgotten with the connection (drop), since a new daemon
 * has no snap playing. */
static char g_snap_sent[1024], g_snap_want[1024];
static int  g_snap_vol, g_snap_want_vol;
static bool g_snap_hold, g_snap_want_hold;

/* `why` is logged when a live connection ends: it is when "MP4 vanished"
 * happened, which on 2026-09-25 could only be inferred afterwards - #42. */
static void drop(const char *why, int err)
{
	if (g_fd >= 0) {
		fprintf(stderr, "mp4: connection lost: %s%s%s (state %s, sink %s)\n", why,
		        err ? ", " : "", err ? strerror(err) : "",
		        g_now.state == MU_PLAYING ? "playing" : g_now.state == MU_PAUSED ? "paused" : "stopped",
		        g_sink_said[0] ? g_sink_said : "?");
		close(g_fd);
	}
	g_fd = -1;
	g_have = 0;
	if (g_now.state != MU_OFF) g_now.state = MU_OFF;
	/* A new connection may be a new daemon, so its output is not known
	 * until it says; its READY announcement does. */
	g_sink_sent[0] = g_sink_said[0] = '\0';
	g_sink_waiting = g_asked = false;
	g_snap_sent[0] = '\0';
	/* And it starts in stereo, so whatever was asked of the old one has to
	 * be asked again. */
	g_chan_sent[0] = '\0';
	/* And flat, so the equalizer is asked again too */
	g_eq_sent[0] = '\0';
	/* And with no boost */
	g_gain_sent = -1;
	/* And with nobody watching, or looking at bars. */
	g_show = -1;
	g_bands_sent = -1;
	g_bands_ms = 0;
}

static void sendf(const char *fmt, ...);

static void show_send(void)
{
	if (g_fd >= 0 && g_bands_sent != (int)g_bands_want) {
		g_bands_sent = g_bands_want;
		sendf("BANDS\ton=%d", g_bands_sent);
	}
	if (g_fd < 0 || g_show == (int)g_show_want) return;
	g_show = g_show_want;
	sendf("VIDEO\tshow=%d", g_show);
}

static void sendf(const char *fmt, ...)
{
	char line[1400];
	va_list ap;
	int n;

	if (g_fd < 0) return;
	va_start(ap, fmt);
	n = vsnprintf(line, sizeof line - 1, fmt, ap);
	va_end(ap);
	/* Not sent at all rather than sent cut short: a PLAY cut short plays a
	 * file nobody asked for, and a COVER cut short writes a picture under a
	 * name nobody will look for. Nothing the launcher builds is this long -
	 * two paths of LIB_PATH each - so this is a guard, not a case. */
	if (n < 0 || n > (int)sizeof line - 2) {
		fprintf(stderr, "mp4: a line too long to send\n");
		return;
	}
	line[n++] = '\n';
	if (send(g_fd, line, (size_t)n, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 &&
	    errno != EAGAIN)
		drop("send failed", errno);      /* it went away; the next poll starts it */
}

/* Connected, or on the way. The daemon is started at most once every two
 * seconds, so an MP4 that cannot start does not become a fork a frame. */
static int connected(void)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int fd;

	if (g_fd >= 0) return 1;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path());
	/* Non-blocking by fcntl rather than SOCK_NONBLOCK, which is Linux's: the
	 * host build compiles this too, and a Unix-socket connect either answers
	 * at once or fails at once either way. */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd >= 0) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	if (fd >= 0 && connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) {
		g_fd = fd;
		g_have = 0;
		if (g_now.state == MU_OFF) g_now.state = MU_STOPPED;
		return 1;
	}
	if (fd >= 0) close(fd);
	if (!g_bin[0]) return 0;
	if (!g_spawned || plat_now_ms() - g_spawned_ms > 2000) {
		char *argv[] = { g_bin, NULL };

		plat_spawn_detached(argv, NULL, NULL);
		g_spawned = 1;
		g_spawned_ms = plat_now_ms();
		fprintf(stderr, "mp4: started %s\n", g_bin);
	}
	return 0;
}

/* What Now Playing shows of track `q`, the queue's current, before the
 * daemon has said a word of it: its file's name until META, the queue's
 * names, where it starts */
static void now_for(const char *q)
{
	ml_track_name(strrchr(q, '/') ? strrchr(q, '/') + 1 : q,
	              g_now.title, sizeof g_now.title);
	if (g_names_each && g_names_fn) {
		g_names_fn(q, g_now.artist, sizeof g_now.artist, g_now.album, sizeof g_now.album);
	} else {
		snprintf(g_now.artist, sizeof g_now.artist, "%s", g_artist);
		snprintf(g_now.album, sizeof g_now.album, "%s", g_album);
	}
	g_now.vw = g_now.vh = 0;            /* until META says it has a picture */
	g_now.at = g_start_at;
	g_now.speed = g_speed;
	g_now.len = 0;
	g_now.index = g_order.pos;
	g_now.count = g_qn;
}

static void play_current(void)
{
	int t = muq_current(&g_order);
	const char *q;

	if (t < 0 || t >= g_qn) return;
	q = g_q[t];
	/* Held until the daemon has said which output it is on - its READY
	 * announcement ends with SINK - or the route below has nothing to go by
	 * and the first track of a fresh daemon starts on the speaker. */
	if (!connected() || !g_sink_said[0]) { g_pending = 1; return; }
	g_pending = 0;
	/* Routed BEFORE the PLAY goes, so the track starts where it is heard
	 * rather than on the speaker for however long the route takes. The
	 * daemon runs its commands in order, so a SINK sent now lands first. */
	g_asked = true;
	if (g_before_heard) g_before_heard();
	/* Only the queue's first PLAY starts part way: every track after it is
	 * one the queue moved on to, from its beginning. */
	sendf("PLAY\tpath=%s/%s\tat=%.1f\tspeed=%.2f%s", g_root, q, g_start_at, g_speed,
	      g_start_held ? "\tpaused=1" : "");
	g_nch = 0;                     /* the new file says its own */
	/* Until META says otherwise, the name the file has. */
	now_for(q);
	if (g_start_held) g_now.state = MU_PAUSED;
	g_held_sent = g_start_held;
	g_start_at = 0;
	g_start_held = false;
	g_seeking = false;
}

/* The next track the queue's mode names, or the end. `failed` is a track that
 * would not play rather than one that finished - see muq_failed. */
static void advance(bool failed)
{
	if ((failed ? muq_failed(&g_order) : muq_ended(&g_order)) >= 0) {
		play_current();
		g_advanced = true;
	} else {
		g_now.state = MU_STOPPED;
		if (!failed) g_ran_out = true;
	}
}

/* `key=` in a tab-separated line, or "". */
static void field(const char *line, const char *key, char *out, size_t n)
{
	size_t kl = strlen(key);
	const char *p = line;

	out[0] = '\0';
	while ((p = strchr(p, '\t'))) {
		p++;
		if (!strncmp(p, key, kl) && p[kl] == '=') {
			size_t len = strcspn(p + kl + 1, "\t");

			if (len >= n) len = n - 1;
			memcpy(out, p + kl + 1, len);
			out[len] = '\0';
			return;
		}
	}
}

static bool adopt_state(const char *line, const char *state);

static void event(const char *line)
{
	char v[256];

	if (!strncmp(line, "READY", 5)) {
		show_send();
		if (g_pending) play_current();
	} else if (!strncmp(line, "BANDS", 5)) {
		/* 32 levels, two hex digits each: anything else is not taken */
		const char *v = strstr(line, "\tv=");
		uint8_t lv[MP4C_BANDS];
		int i;

		if (!v || strlen(v + 3) < MP4C_BANDS * 2) return;
		for (i = 0; i < MP4C_BANDS; i++) {
			char h[3] = { v[3 + i * 2], v[4 + i * 2], 0 };
			char *end;

			lv[i] = (uint8_t)strtoul(h, &end, 16);
			if (*end) return;
		}
		memcpy(g_bands, lv, sizeof g_bands);
		g_bands_ms = plat_now_ms() | 1;
	} else if (!strncmp(line, "STATE", 5)) {
		field(line, "state", v, sizeof v);
		if (g_adopt && adopt_state(line, v)) return;
		/* The track that ended says "stopped" AFTER its END, and by then the
		 * next one has already been asked for. Taken at its word, the queue
		 * reads as stopped for the moment between two tracks - and a game
		 * follows mp4c_playing to decide whether it is heard, so its sound
		 * came up for a tenth of a second at every track change. */
		if (g_advanced && !strcmp(v, "stopped")) { g_advanced = false; return; }
		g_advanced = false;
		g_asked = false;
		g_held_sent = false;           /* answered: it opened */
		g_now.state = !strcmp(v, "playing") ? MU_PLAYING
		            : !strcmp(v, "paused")  ? MU_PAUSED : MU_STOPPED;
		if (g_now.state == MU_PLAYING) g_skips = 0;
	} else if (!strncmp(line, "CHAPTER", 7)) {
		int i;

		field(line, "i", v, sizeof v);
		i = atoi(v);
		if (i >= 0 && i < MAX_CHAPTERS) {
			field(line, "at", v, sizeof v);
			g_ch_at[i] = atof(v);
			field(line, "title", g_ch_title[i], sizeof g_ch_title[i]);
			if (i + 1 > g_nch) g_nch = i + 1;
		}
	} else if (!strncmp(line, "META", 4)) {
		/* The CHAPTER lines follow, and are this file's alone. */
		g_nch = 0;
		/* The file's own tags win where it has them; the folder names stand
		 * in where it does not, which on this card is the artist almost
		 * every time - the rips carry an album tag and no artist one. */
		field(line, "title", v, sizeof v);
		if (v[0]) snprintf(g_now.title, sizeof g_now.title, "%s", v);
		field(line, "artist", v, sizeof v);
		if (v[0]) snprintf(g_now.artist, sizeof g_now.artist, "%s", v);
		field(line, "album", v, sizeof v);
		if (v[0]) snprintf(g_now.album, sizeof g_now.album, "%s", v);
		field(line, "len", v, sizeof v);
		g_now.len = atof(v);
		/* "640x360", or "0x0" - and nothing from a daemon older than video. */
		field(line, "video", v, sizeof v);
		if (sscanf(v, "%dx%d", &g_now.vw, &g_now.vh) != 2 || g_now.vw <= 0 || g_now.vh <= 0)
			g_now.vw = g_now.vh = 0;
	} else if (!strncmp(line, "POS", 3)) {
		double at;

		field(line, "len", v, sizeof v); if (atof(v) > 0) g_now.len = atof(v);
		field(line, "at", v, sizeof v);  at = atof(v);
		/* While a seek is on its way, a position that is not the seek's is
		 * from before it: the once-a-second report sent as the SEEK went
		 * out, or an earlier seek of a held d-pad landing. Taken at its
		 * word it pulled the clock back and then forward again - measured
		 * 2026-09-28, a 635.8 between seeks to 630 and 640, and an M4B,
		 * whose seeks take 700 ms, showing 1010 after 1050 was asked for.
		 * And the next press counts from the shown position, so a held
		 * d-pad also lost ground. Three seconds at most, for a seek that
		 * never lands; speed goes up to 2x, so that is the slack. */
		if (g_seeking) {
			double since = (plat_now_ms() - g_seek_ms) / 1000.0;

			if (at >= g_seek_to - 0.5 && at <= g_seek_to + since * 2 + 1.5)
				g_seeking = false;                          /* it landed */
			else if (since < 3.0)
				return;
			else
				g_seeking = false;
		}
		g_now.at = at;
		g_at_ms = plat_now_ms();
	} else if (!strncmp(line, "END", 3)) {
		advance(false);
	} else if (!strncmp(line, "SINK", 4)) {
		field(line, "device", g_sink_said, sizeof g_sink_said);
		g_sink_waiting = false;
		/* Taken as the starting point only when nothing has been asked of
		 * this daemon yet. After that, what was ASKED stands: re-asking
		 * because MP4 fell back would retry a dead headset every tick,
		 * which is the loop aout_apply's comment in main.c describes. */
		if (!g_sink_sent[0])
			snprintf(g_sink_sent, sizeof g_sink_sent, "%s", g_sink_said);
		if (g_pending) play_current();
	} else if (!strncmp(line, "COVER", 5)) {
		int k = (g_cov_head + g_cov_n) % COVER_RING;

		if (g_cov_n == COVER_RING) {           /* the oldest makes room */
			g_cov_head = (g_cov_head + 1) % COVER_RING;
			g_cov_n--;
			k = (g_cov_head + g_cov_n) % COVER_RING;
		}
		field(line, "base", g_cov[k].base, sizeof g_cov[k].base);
		field(line, "file", g_cov[k].file, sizeof g_cov[k].file);
		g_cov_n++;
	} else if (!strncmp(line, "ARTIST", 6)) {
		int k = (g_art_head + g_art_n) % ARTIST_RING;

		if (g_art_n == ARTIST_RING) {          /* the oldest makes room */
			g_art_head = (g_art_head + 1) % ARTIST_RING;
			g_art_n--;
			k = (g_art_head + g_art_n) % ARTIST_RING;
		}
		field(line, "dir", g_art[k].dir, sizeof g_art[k].dir);
		field(line, "name", g_art[k].name, sizeof g_art[k].name);
		g_art_n++;
	} else if (!strncmp(line, "LENGTH", 6)) {
		int k = (g_len_head + g_len_n) % LENGTH_RING;

		if (g_len_n == LENGTH_RING) {          /* the oldest makes room */
			g_len_head = (g_len_head + 1) % LENGTH_RING;
			g_len_n--;
			k = (g_len_head + g_len_n) % LENGTH_RING;
		}
		field(line, "dir", g_len[k].dir, sizeof g_len[k].dir);
		field(line, "s", v, sizeof v);
		g_len[k].s = atof(v);
		g_len_n++;
	} else if (!strncmp(line, "ERROR", 5)) {
		field(line, "why", v, sizeof v);
		fprintf(stderr, "mp4: %s\n", v);
		/* A track that will not open is skipped rather than ending the
		 * album - but not forever: a whole album of files that fail is a
		 * folder problem, and walking it at a frame a track says nothing.
		 * Not one opened held (mp4c_hold_next): the next track's PLAY
		 * would play, and nobody pressed anything; it stops there. */
		if (g_held_sent) { g_held_sent = false; g_now.state = MU_STOPPED; }
		else if (++g_skips < 8) advance(true);
		else g_now.state = MU_STOPPED;
	}
}

/* The daemon's first word on a queue handed over (mp4c_hand_in): which of
 * its tracks plays now. The file the daemon names, found in the queue, is
 * where it is, wherever the hand left it: a follower that moved on and
 * ended before it could write the file, a menu that started another song.
 * Stopped on the track the hand said was playing, it ran out while nobody
 * listened (between two processes, or under the in-game menu), and the
 * queue goes on to the next, as an END would have. True when that was done
 * and the STATE is used up. */
static bool adopt_state(const char *line, const char *state)
{
	char path[1100];
	size_t root = strlen(g_root);
	int said = g_adopt, i, at = -1;

	g_adopt = 0;
	field(line, "path", path, sizeof path);
	if (!strncmp(path, g_root, root) && path[root] == '/')
		for (i = 0; i < g_qn && at < 0; i++)
			if (!strcmp(path + root + 1, g_q[g_order.order[i]])) at = i;
	if (at >= 0 && at != g_order.pos) {
		g_order.pos = at;
		g_now.index = at;
	}
	if (!strcmp(state, "stopped") && said == MU_PLAYING && at >= 0) {
		fprintf(stderr, "mp4: the track handed over ran out meanwhile; the next\n");
		advance(false);
		return true;
	}
	/* Playing a file the queue does not have: the queue is not the one
	 * playing any more, and an END would go on into the wrong album */
	if (at < 0 && strcmp(state, "stopped") && g_qn > 0) {
		fprintf(stderr, "mp4: the daemon plays a file the queue handed over has not; no queue\n");
		for (i = 0; i < g_qn; i++) free(g_q[i]);
		free(g_q);
		g_q = NULL;
		g_qn = 0;
		muq_free(&g_order);
	}
	return false;
}

/* A headset's own play, pause, next and previous, which btplayer reads off its
 * AVRCP keyboard and writes as `<action> <stamp>` (tools/btplayer.c). Here and
 * not in any one screen, because mp4c_poll runs wherever MP4 matters - the
 * shelf, the menus, a game's tick, MP4's own screens. Only ever MP4, even in
 * a game: pausing the music is what brings the game's sound back, and a
 * headset's button never reaches into the game itself - Eric's call,
 * 2026-09-26. A new stamp is a new press; the one found at start is only
 * noted. Read every tenth of a second, before anything that could return
 * early, so a press made while MP4 was not running is used up, not replayed
 * later. */
static void headset_keys(void)
{
	static unsigned looked;
	static long     seen;
	static bool     primed;
	unsigned now = plat_now_ms();
	char     act[16];
	long     stamp;
	FILE    *f;

	if (primed && now - looked < 100) return;
	looked = now;
	f = fopen("/tmp/zyperos_btkey", "r");
	if (f && fscanf(f, "%15s %ld", act, &stamp) == 2 && stamp != seen) {
		bool fresh = primed;

		seen = stamp;
		if (fresh) {
			fprintf(stderr, "mp4: headset %s\n", act);
			if (!strcmp(act, "toggle"))     mp4c_toggle();
			else if (!strcmp(act, "pause")) { if (g_now.state == MU_PLAYING) sendf("PAUSE"); }
			else if (!strcmp(act, "next"))  mp4c_next();
			else if (!strcmp(act, "prev"))  mp4c_prev();
		}
	}
	if (f) fclose(f);
	primed = true;
}

static void snap_send(void);
static void bands_lapse(void);

void mp4c_poll(void)
{
	ssize_t got;

	headset_keys();
	bands_lapse();

	/* Reconnect whenever there is a queue to look after, not only when a
	 * play is waiting: a daemon that restarts mid-album would otherwise
	 * finish the track it was on and stop, because its END went to a socket
	 * nobody was reading. */
	if (g_fd < 0) {
		if ((!g_pending && g_qn == 0 && !g_snap_want[0]) || !connected()) return;
	}
	snap_send();
	for (;;) {
		got = recv(g_fd, g_in + g_have, sizeof g_in - g_have - 1, MSG_DONTWAIT);
		if (got == 0) { drop("closed by MP4", 0); return; }
		if (got < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) drop("receive failed", errno);
			break;
		}
		g_have += (size_t)got;
		g_in[g_have] = '\0';
		for (;;) {
			char *nl = memchr(g_in, '\n', g_have);
			size_t used;

			if (!nl) break;
			*nl = '\0';
			event(g_in);
			if (g_fd < 0) return;              /* the event dropped us */
			used = (size_t)(nl - g_in) + 1;
			memmove(g_in, nl + 1, g_have - used);
			g_have -= used;
		}
		if (g_have >= sizeof g_in - 1) g_have = 0;
	}
}

void mp4c_play(const char *const *paths, int n, int start, double at, bool book,
                double speed, const char *artist, const char *album)
{
	int i, k = 0;

	for (i = 0; i < g_qn; i++) free(g_q[i]);
	free(g_q);
	g_q = calloc((size_t)(n > 0 ? n : 1), sizeof *g_q);
	g_qn = 0;
	if (!g_q) return;
	for (i = 0; i < n; i++) {
		/* A tab or a newline is structure on the wire; a name holding one
		 * would arrive as a different path. Nobody names a song that way,
		 * and the one that did is left out rather than misplayed. */
		if (strpbrk(paths[i], "\t\n")) { if (i < start) start--; continue; }
		g_q[k] = strdup(paths[i]);
		if (g_q[k]) k++;
	}
	g_qn = k;
	muq_free(&g_order);
	g_book = book;
	/* mp4c_play_videos sets it for its own call and nothing else does. */
	if (!g_videos_call) g_videos = false;
	g_speed = speed >= 0.5 && speed <= 2.0 ? speed : 1.0;
	muq_init(&g_order, k, start >= 0 && start < k ? start : 0,
	         book ? MUQ_IN_ORDER : g_videos ? g_video_mode : g_mode, plat_now_ms() * 2654435761u + 1);
	g_skips = 0;
	g_start_at = at > 0 ? at : 0;
	g_ran_out = false;
	g_names_each = !artist && !album;
	snprintf(g_artist, sizeof g_artist, "%s", artist ? artist : "");
	snprintf(g_album, sizeof g_album, "%s", album ? album : "");
	/* Held: Now Playing shows it paused at its place at once, before a
	 * daemon that is still starting has said so */
	if (g_start_held) {
		int t = muq_current(&g_order);

		if (t >= 0 && t < g_qn) {
			now_for(g_q[t]);
			g_now.state = MU_PAUSED;
		}
	}
	play_current();
}

void mp4c_hold_next(void) { g_start_held = true; }

bool mp4c_hand_out(mp4hand *h)
{
	int i;

	memset(h, 0, sizeof *h);
	if (g_qn <= 0 || !g_order.order || g_order.n != g_qn) return false;
	h->order = calloc((size_t)g_qn, sizeof *h->order);
	h->track = calloc((size_t)g_qn, sizeof *h->track);
	if (!h->order || !h->track) { mp4hand_free(h); return false; }
	h->n = g_qn;
	for (i = 0; i < g_qn; i++) {
		h->order[i] = g_order.order[i];
		if (!(h->track[i] = strdup(g_q[i]))) { mp4hand_free(h); return false; }
	}
	h->pos = g_order.pos >= 0 && g_order.pos < g_qn ? g_order.pos : 0;
	h->state = g_now.state == MU_PLAYING || g_asked ? MH_PLAYING
	         : g_now.state == MU_PAUSED ? MH_PAUSED : MH_STOPPED;
	h->at = g_now.state == MU_PLAYING ? mp4c_at_live() : g_now.at;
	if (!(h->at >= 0)) h->at = 0;
	h->mode = g_order.mode;
	h->seed = g_order.seed;
	h->book = g_book;
	h->videos = g_videos;
	h->names_each = g_names_each;
	h->speed = g_speed;
	snprintf(h->root, sizeof h->root, "%s", g_root);
	snprintf(h->artist, sizeof h->artist, "%s", g_artist);
	snprintf(h->album, sizeof h->album, "%s", g_album);
	return true;
}

void mp4c_hand_in(const mp4hand *h)
{
	int i, k = 0;

	for (i = 0; i < g_qn; i++) free(g_q[i]);
	free(g_q);
	g_q = NULL;
	g_qn = 0;
	muq_free(&g_order);
	g_pending = 0;
	g_adopt = 0;
	g_advanced = g_start_held = g_held_sent = g_ran_out = false;
	g_skips = 0;
	g_start_at = 0;
	g_nch = 0;
	/* A whole announcement from the daemon, which only a new connection
	 * gets: mp4c_poll connects again, as there is a queue to look after */
	drop("a queue handed over", 0);
	if (!h || h->n <= 0 || strcmp(h->root, g_root)) {
		g_now.state = MU_OFF;
		return;
	}
	g_q = calloc((size_t)h->n, sizeof *g_q);
	if (g_q && muq_init(&g_order, h->n, 0, (muq_mode)h->mode, h->seed))
		for (k = 0; k < h->n && (g_q[k] = strdup(h->track[k])); k++) ;
	if (k < h->n) {
		for (i = 0; i < k; i++) free(g_q[i]);
		free(g_q);
		g_q = NULL;
		muq_free(&g_order);
		return;
	}
	g_qn = h->n;
	memcpy(g_order.order, h->order, (size_t)h->n * sizeof *h->order);
	g_order.pos = h->pos;
	g_order.seed = h->seed;
	g_book = h->book;
	g_videos = h->videos;
	g_speed = h->speed >= 0.5 && h->speed <= 2.0 ? h->speed : 1.0;
	g_names_each = h->names_each;
	snprintf(g_artist, sizeof g_artist, "%s", h->artist);
	snprintf(g_album, sizeof g_album, "%s", h->album);
	g_start_at = h->at;
	now_for(g_q[muq_current(&g_order)]);
	g_start_at = 0;
	g_now.state = h->state == MH_PLAYING ? MU_PLAYING : h->state == MH_PAUSED ? MU_PAUSED : MU_STOPPED;
	g_at_ms = plat_now_ms();
	if (h->state != MH_STOPPED) g_adopt = g_now.state;
}

void mp4c_play_videos(const char *const *paths, int n, int start, double at,
                       const char *artist, const char *album)
{
	g_videos = g_videos_call = true;
	mp4c_play(paths, n, start, at, false, 1.0, artist, album);
	g_videos_call = false;
}

bool mp4c_is_videos(void) { return g_videos; }

void mp4c_names_by(void (*fn)(const char *track, char *artist, size_t an,
                               char *album, size_t aln))
{
	g_names_fn = fn;
}

bool mp4c_video(void) { return g_now.vw > 0 && g_now.state != MU_OFF; }

void mp4c_video_show(bool on)
{
	g_show_want = on;
	show_send();
}

void mp4c_bands_keep(void)
{
	g_bands_until = plat_now_ms() + MP4C_BANDS_LEASE;
	if (!g_bands_want) {
		g_bands_want = true;
		g_bands_ms = 0;
		show_send();
	}
}

/* The lease run out: no frame of bars for a while, so nobody is looking */
static void bands_lapse(void)
{
	if (!g_bands_want || (int)(plat_now_ms() - g_bands_until) < 0) return;
	g_bands_want = false;
	g_bands_ms = 0;
	show_send();
}

bool mp4c_bands(uint8_t out[MP4C_BANDS], unsigned *age_ms)
{
	if (!g_bands_ms || !g_bands_want) return false;
	memcpy(out, g_bands, sizeof g_bands);
	if (age_ms) *age_ms = plat_now_ms() - g_bands_ms;
	return true;
}

/* What is wanted, sent once there is a daemon to send it to: the first snap
 * asked of a launcher whose MP4 has not started yet starts it, and goes out
 * when it answers (mp4c_poll). */
static void snap_send(void)
{
	if (g_fd < 0) return;
	if (!strcmp(g_snap_sent, g_snap_want) && g_snap_vol == g_snap_want_vol &&
	    g_snap_hold == g_snap_want_hold)
		return;
	snprintf(g_snap_sent, sizeof g_snap_sent, "%s", g_snap_want);
	g_snap_vol = g_snap_want_vol;
	g_snap_hold = g_snap_want_hold;
	sendf("SNAP\tpath=%s\tvol=%d\thold=%d", g_snap_sent, g_snap_vol, g_snap_hold ? 1 : 0);
}

void mp4c_snap(const char *path, int vol, bool hold)
{
	/* A tab or a line break would end the line's field or the line */
	if (!path || strpbrk(path, "\t\n")) path = "";
	snprintf(g_snap_want, sizeof g_snap_want, "%s", path);
	g_snap_want_vol = vol;
	g_snap_want_hold = *path && hold;
	/* A stop with no daemon needs no daemon */
	if (g_fd < 0 && (!*path || !connected())) return;
	snap_send();
}

void mp4c_toggle(void)
{
	if (g_now.state == MU_PLAYING) sendf("PAUSE");
	else if (g_now.state == MU_PAUSED) {
		g_asked = true;
		if (g_before_heard) g_before_heard();   /* see play_current */
		sendf("RESUME");
	}
	else if (g_qn > 0) play_current();
}

void mp4c_on_before_heard(void (*fn)(void)) { g_before_heard = fn; }

bool mp4c_heard(void) { return g_now.state == MU_PLAYING || g_asked; }

void mp4c_sink(const char *device)
{
	const char *dev = device && device[0] ? device : "default";

	if (g_fd < 0 || !g_sink_said[0]) return;   /* not connected, or not heard from */
	if (!strcmp(dev, g_sink_sent)) return;
	snprintf(g_sink_sent, sizeof g_sink_sent, "%s", dev);
	g_sink_ms = plat_now_ms();
	g_sink_waiting = true;
	sendf("SINK\tdevice=%s", dev);
	fprintf(stderr, "mp4: sink -> %s\n", dev);
}

void mp4c_channels(const char *mode)
{
	const char *m = mode && mode[0] ? mode : "stereo";

	if (g_fd < 0 || !g_sink_said[0]) return;   /* not connected, or not heard from */
	if (!strcmp(m, g_chan_sent)) return;
	snprintf(g_chan_sent, sizeof g_chan_sent, "%s", m);
	sendf("CHANNELS\tmode=%s", m);
	fprintf(stderr, "mp4: channels -> %s\n", m);
}

void mp4c_eq(const int8_t db[5], bool live)
{
	char bands[32], said[40];

	if (g_fd < 0 || !g_sink_said[0]) return;   /* not connected, or not heard from */
	snprintf(bands, sizeof bands, "%d,%d,%d,%d,%d", db[0], db[1], db[2], db[3], db[4]);
	snprintf(said, sizeof said, "%s live=%d", bands, live ? 1 : 0);
	if (!strcmp(said, g_eq_sent)) return;
	snprintf(g_eq_sent, sizeof g_eq_sent, "%s", said);
	sendf("EQ\tbands=%s\tlive=%d", bands, live ? 1 : 0);
}

void mp4c_gain(int db)
{
	if (g_fd < 0 || !g_sink_said[0]) return;   /* not connected, or not heard from */
	if (db == g_gain_sent) return;
	g_gain_sent = db;
	sendf("GAIN\tdb=%d", db);
}

void mp4c_sink_again(void)
{
	/* Forget what was asked, so the next mp4c_sink sends even an unchanged
	 * device. Only while connected and heard from: an empty ask otherwise
	 * means "not known yet", which the SINK answer fills in. */
	if (g_fd >= 0 && g_sink_said[0]) g_sink_sent[0] = '\0';
}

bool mp4c_sink_settled(void)
{
	/* The daemon answers a SINK after its retries, which are a second at
	 * most; a little over that, and it is taken as done either way. */
	if (g_fd < 0 || !g_sink_waiting) return true;
	return plat_now_ms() - g_sink_ms > 1200;
}

const char *mp4c_sink_now(void)
{
	/* Not while a SINK is unanswered: until then it is the device before -
	 * the same stale-report trouble as Diatom's, see aout_send in main.c. */
	return g_fd >= 0 && g_sink_said[0] && !g_sink_waiting ? g_sink_said : "";
}

void mp4c_next(void)
{
	if (g_book && g_nch > 1) {
		int c = mp4c_chapter_now();

		if (c + 1 < g_nch) { seek_to(g_ch_at[c + 1]); return; }
	}
	if (muq_next(&g_order) >= 0) play_current();
}

/* Seek, and hold the shown position there until the daemon says it landed:
 * see the POS case in event(). */
static void seek_to(double t)
{
	g_now.at = t;                  /* shown at once, confirmed by a POS */
	g_at_ms = plat_now_ms();
	g_seeking = true;
	g_seek_to = t;
	g_seek_ms = plat_now_ms();
	sendf("SEEK\tat=%.1f", t);
}

void mp4c_prev(void)
{
	/* The way every player does it: three seconds in, "back" means the start
	 * of this track; before that it means the one before - and where there is
	 * none before, the start of this one again. */
	if (g_book && g_nch > 1) {
		int c = mp4c_chapter_now();
		double into = c >= 0 ? g_now.at - g_ch_at[c] : 0;

		if (c >= 0 && into > 3.0) { seek_to(g_ch_at[c]); return; }
		if (c > 0)                { seek_to(g_ch_at[c - 1]); return; }
	}
	if (g_now.at > 3.0 || muq_prev(&g_order) < 0) seek_to(0);
	else play_current();
}

void mp4c_seek_by(double delta)
{
	double t = g_now.at + delta;

	if (g_now.state != MU_PLAYING && g_now.state != MU_PAUSED) return;
	if (t < 0) t = 0;
	if (g_now.len > 0 && t > g_now.len - 1) t = g_now.len - 1;
	seek_to(t);
}

void mp4c_stop(void)
{
	sendf("STOP");
}

bool mp4c_cover_ask(const char *track, const char *base)
{
	if (strpbrk(track, "\t\n") || strpbrk(base, "\t\n")) return false;
	if (!connected()) return false;
	sendf("COVER\tpath=%s/%s\tbase=%s", g_root, track, base);
	return g_fd >= 0;
}

bool mp4c_cover_take(char *base, size_t bn, char *file, size_t fn)
{
	if (g_cov_n == 0) return false;
	snprintf(base, bn, "%s", g_cov[g_cov_head].base);
	snprintf(file, fn, "%s", g_cov[g_cov_head].file);
	g_cov_head = (g_cov_head + 1) % COVER_RING;
	g_cov_n--;
	return true;
}

bool mp4c_artist_ask(const char *dir)
{
	if (strpbrk(dir, "\t\n")) return false;
	if (!connected()) return false;
	sendf("ARTIST\tdir=%s/%s", g_root, dir);
	return g_fd >= 0;
}

bool mp4c_artist_take(char *dir, size_t dn, char *name, size_t nn)
{
	size_t root = strlen(g_root);
	const char *d;

	if (g_art_n == 0) return false;
	d = g_art[g_art_head].dir;
	/* Handed back as it was asked, under the music root. */
	if (!strncmp(d, g_root, root) && d[root] == '/') d += root + 1;
	snprintf(dir, dn, "%s", d);
	snprintf(name, nn, "%s", g_art[g_art_head].name);
	g_art_head = (g_art_head + 1) % ARTIST_RING;
	g_art_n--;
	return true;
}

bool mp4c_length_ask(const char *dir)
{
	if (strpbrk(dir, "\t\n")) return false;
	if (!connected()) return false;
	sendf("LENGTH\tdir=%s/%s", g_root, dir);
	return g_fd >= 0;
}

bool mp4c_length_take(char *dir, size_t dn, double *s)
{
	size_t root = strlen(g_root);
	const char *d;

	if (g_len_n == 0) return false;
	d = g_len[g_len_head].dir;
	if (!strncmp(d, g_root, root) && d[root] == '/') d += root + 1;
	snprintf(dir, dn, "%s", d);
	*s = g_len[g_len_head].s;
	g_len_head = (g_len_head + 1) % LENGTH_RING;
	g_len_n--;
	return true;
}

const char *mp4c_track(int i)
{
	return i >= 0 && i < g_qn ? g_q[g_order.order[i]] : NULL;
}

const char *mp4c_upcoming(void)
{
	int t = muq_upcoming(&g_order);

	return t >= 0 && t < g_qn ? g_q[t] : NULL;
}

void mp4c_set_mode(muq_mode m)
{
	if ((int)m < 0 || m >= MUQ_MODES) m = MUQ_IN_ORDER;
	g_mode = m;
	if (g_book || g_videos) return;      /* see mp4c_play */
	muq_set_mode(&g_order, m);
	g_now.index = g_order.pos;
}

bool mp4c_is_book(void) { return g_book; }

void mp4c_set_speed(double x)
{
	if (x < 0.5 || x > 2.0) return;
	g_speed = x;
	g_now.speed = x;
	if (g_now.state == MU_PLAYING || g_now.state == MU_PAUSED) sendf("SPEED\tx=%.2f", x);
}

double mp4c_speed(void) { return g_speed; }

int mp4c_chapters(void) { return g_nch; }

const char *mp4c_chapter_title(int i) { return i >= 0 && i < g_nch ? g_ch_title[i] : ""; }

int mp4c_chapter_now(void)
{
	int i, c = -1;

	/* Half a second of slack: a seek to a chapter's start is shown at once
	 * and confirmed a moment later, and should read as that chapter. */
	for (i = 0; i < g_nch; i++)
		if (g_ch_at[i] <= g_now.at + 0.5) c = i;
	return c;
}

bool mp4c_take_ran_out(void)
{
	bool r = g_ran_out;

	g_ran_out = false;
	return r;
}

muq_mode mp4c_mode(void) { return g_mode; }

void mp4c_set_video_mode(muq_mode m)
{
	if (m != MUQ_REPEAT_ALL && m != MUQ_REPEAT_ONE) m = MUQ_IN_ORDER;
	g_video_mode = m;
	if (!g_videos) return;
	muq_set_mode(&g_order, m);
	g_now.index = g_order.pos;
}

muq_mode mp4c_video_mode(void) { return g_video_mode; }

const mu_now *mp4c_now(void) { return &g_now; }

double mp4c_at_live(void)
{
	double at = g_now.at, since;

	if (g_now.state != MU_PLAYING || !g_at_ms) return at;
	/* On from the last report at the speed it plays, but never more than
	 * a report and a half past it: a daemon that has stopped saying is not
	 * run away from */
	since = (double)(plat_now_ms() - g_at_ms) / 1000.0 * (g_book ? g_speed : 1.0);
	if (since > 1.5) since = 1.5;
	at += since;
	return g_now.len > 0 && at > g_now.len ? g_now.len : at;
}

const char *mp4c_path(void)
{
	int t = muq_current(&g_order);

	if (g_now.state == MU_OFF || g_now.state == MU_STOPPED) return "";
	return t >= 0 && t < g_qn ? g_q[t] : "";
}

bool mp4c_playing(void) { return g_now.state == MU_PLAYING; }

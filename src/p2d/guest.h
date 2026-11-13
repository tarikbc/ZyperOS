/* SPDX-License-Identifier: GPL-3.0-only */
/* One browser on p2d's WebSocket: its socket, what it sent that is not a
 * whole message yet, and what is on its way to it.
 *
 * What a guest may send, each a binary message, little-endian:
 *
 *   'A' + u32   a frame it has drawn (an ack), by the frame's number
 *   'P' + u16   the buttons it holds now, RetroPad bits
 *
 * Anything else, and anything ws_parse refuses, closes it: the play page is
 * the only client and is written beside this file, so a message it would
 * never send is a page that is not ours. A ping gets its pong and a close its
 * close here, without the caller.
 *
 * What goes to it: status as text (main.c's "S" and a line of JSON), and
 * frames as binary (codec.h). Two frames at most wait for an ack, so a slow
 * link gets fewer frames rather than a queue of old ones, and Diatom never
 * waits whatever the guest does. A frame is encoded only when the socket can
 * take it now (guest_ready), so the one waiting is the share's newest and a
 * newer one replaces it; once encoded it is a link in the codec's chain of
 * deltas and goes. A frame that has started out finishes before anything
 * else, as a WebSocket's frames cannot interleave; until then a status or a
 * close goes first, and a close drops a frame that has not started.
 *
 * A guest with a frame waiting for its ack that has sent nothing for
 * GUEST_QUIET_MS is gone: a phone that walked out of range, or a tab closed
 * while it was, sends no FIN, and TCP would take many minutes to say so,
 * all of them with player 2 held and the same friend's reload told it is
 * taken. A page that is there acks every frame it is sent.
 *
 * The socket is non-blocking and every call does a bounded amount, as in
 * src/httpd.h: p2d's loop is one thread. */
#ifndef P2D_GUEST_H
#define P2D_GUEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GUEST_HEAD     10    /* the longest frame head, ws_frame_head's */
#define GUEST_FLIGHT   2     /* frames sent and not acked, at most */
#define GUEST_QUIET_MS 5000  /* a frame unacked and not a byte from it */

typedef struct {
	int      fd;                    /* -1: the slot is free */
	uint8_t  in[256];               /* read, not a whole message yet */
	size_t   in_used;
	uint8_t  ctl[512];              /* status, pongs and the close, in order */
	size_t   ctl_len, ctl_sent;
	uint8_t *out;                   /* GUEST_HEAD bytes, then a frame message */
	size_t   out_cap;               /* the message's most bytes */
	size_t   out_start;             /* where the frame's head begins in out */
	size_t   out_at, out_end;       /* still to go; out_end 0 for no frame */
	uint32_t last_frame;            /* the newest frame put to it */
	uint32_t flight[GUEST_FLIGHT];  /* sent and not acked */
	int      nflight;
	bool     closing;               /* our close is queued: nothing else goes */
	bool     heard;                 /* its close came, or it can no longer be read */
	bool     shut;                  /* our close is out, since shut_ms */
	long     shut_ms;
	long     in_ms;                 /* when it last sent a byte, or came */
	bool     dead;                  /* drop it: the socket failed or it flooded */
} guest;

typedef struct {
	int  pad;                       /* the last 'P' read, -1 when none came */
	bool moved;                     /* a byte went either way */
} guest_news;

/* The slot's frame buffer, once at start, for messages of up to `cap` bytes */
bool guest_alloc(guest *g, size_t cap);

/* A guest on `fd`, the socket httpd handed over after the 101, at `now_ms` */
void guest_open(guest *g, int fd, long now_ms);

/* The socket closed, the slot free */
void guest_drop(guest *g);

/* A text message, queued; false when there is no room or it is closing */
bool guest_text(guest *g, const char *s);

/* A close with `code` (RFC 6455, 7.4), queued; once it is out and the
 * guest's own close has come (or half a second has gone), guest_poll says the
 * guest is over */
void guest_close(guest *g, unsigned code);

/* A socket after its 101 that no slot can take (every one busy): `text` and
 * a close with `code` written straight to it, and the socket closed. Fresh
 * from its 101, its buffer has room for a few bytes; what it sent already is
 * read and thrown away, and our side shut first, so it ends with a FIN, not
 * a reset that could reach the browser before the text and lose it. */
void guest_refuse(int fd, const char *text, unsigned code);

/* Whether a frame may be put now: open, nothing of its own going out, fewer
 * than GUEST_FLIGHT unacked, and the socket able to take more */
bool guest_ready(guest *g);

/* Where the next frame's message goes and its room; then guest_frame_put
 * with its length sends it on its way */
uint8_t *guest_frame_room(guest *g, size_t *cap);
void guest_frame_put(guest *g, size_t len, uint32_t frame);

/* Moves the guest along: writes what is queued, reads what came, answers
 * pings and closes, counts acks. The newest pad in `news`. False when the
 * guest is over (it hung up, failed, went quiet, or its close is done):
 * guest_drop it. */
bool guest_poll(guest *g, long now_ms, guest_news *news);

#endif

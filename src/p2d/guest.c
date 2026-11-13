/* SPDX-License-Identifier: GPL-3.0-only */
/* One guest's socket. See guest.h. */
#include "guest.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "ws.h"

#define OP_TEXT   1
#define OP_BINARY 2
#define OP_CLOSE  8
#define OP_PING   9
#define OP_PONG   10

/* Bytes read in one poll. Acks and pads are a few bytes each, sixty-odd a
 * second, so this is room for a burst after a stall, not a rate. */
#define READ_MAX 4096

/* How long our close waits for the guest's. A browser answers in a round
 * trip; one that does not is gone, and the socket goes too. */
#define CLOSE_WAIT_MS 500

bool guest_alloc(guest *g, size_t cap)
{
	memset(g, 0, sizeof *g);
	g->fd = -1;
	g->out = malloc(GUEST_HEAD + cap);
	g->out_cap = cap;
	return g->out != NULL;
}

void guest_open(guest *g, int fd, long now_ms)
{
	uint8_t *out = g->out;
	size_t cap = g->out_cap;
	int fl = fcntl(fd, F_GETFL, 0);

	memset(g, 0, sizeof *g);
	g->out = out;
	g->out_cap = cap;
	g->fd = fd;
	g->in_ms = now_ms;
	/* httpd's sockets are non-blocking already; this file does not lean on
	 * that being so */
	if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

void guest_drop(guest *g)
{
	if (g->fd >= 0) close(g->fd);
	g->fd = -1;
}

/* A whole control or text frame onto ctl, behind what is there */
static bool ctl_put(guest *g, int opcode, const void *data, size_t len)
{
	uint8_t head[GUEST_HEAD];
	size_t hl;

	if (g->closing) return false;
	hl = ws_frame_head(head, opcode, len);
	if (g->ctl_len + hl + len > sizeof g->ctl) return false;
	memcpy(g->ctl + g->ctl_len, head, hl);
	memcpy(g->ctl + g->ctl_len + hl, data, len);
	g->ctl_len += hl + len;
	return true;
}

bool guest_text(guest *g, const char *s)
{
	return ctl_put(g, OP_TEXT, s, strlen(s));
}

void guest_close(guest *g, unsigned code)
{
	uint8_t c[2] = { (uint8_t)(code >> 8), (uint8_t)code };

	if (g->closing) return;
	/* No room for the close is a guest that sent pings faster than it read
	 * the pongs: it goes without one */
	if (!ctl_put(g, OP_CLOSE, c, 2)) g->dead = true;
	g->closing = true;
	if (g->out_end && g->out_at == g->out_start) g->out_end = g->out_at = g->out_start = 0;
}

bool guest_ready(guest *g)
{
	struct pollfd p;

	if (g->fd < 0 || g->closing || g->dead || g->out_end || g->ctl_len ||
	    g->nflight >= GUEST_FLIGHT)
		return false;
	/* Writable now, so the frame encoded next starts out at once rather
	 * than waiting, stale, behind a full socket */
	p.fd = g->fd;
	p.events = POLLOUT;
	p.revents = 0;
	return poll(&p, 1, 0) == 1 && (p.revents & POLLOUT);
}

uint8_t *guest_frame_room(guest *g, size_t *cap)
{
	*cap = g->out_cap;
	return g->out + GUEST_HEAD;
}

/* What is queued, until the socket takes no more. A frame that has started
 * goes first; then ctl; then a frame that has not. False on a failed socket. */
static bool flush(guest *g, bool *moved)
{
	for (;;) {
		bool frame;
		const uint8_t *src;
		size_t len;
		ssize_t put;

		if (g->out_end && g->out_at > g->out_start) frame = true;
		else if (g->ctl_sent < g->ctl_len) frame = false;
		else if (g->out_end) frame = true;
		else return true;
		src = frame ? g->out + g->out_at : g->ctl + g->ctl_sent;
		len = frame ? g->out_end - g->out_at : g->ctl_len - g->ctl_sent;
		put = send(g->fd, src, len, 0);
		if (put < 0) {
			if (errno == EINTR) continue;
			return errno == EAGAIN || errno == EWOULDBLOCK;
		}
		*moved = true;
		if (frame) {
			g->out_at += (size_t)put;
			if (g->out_at == g->out_end) g->out_end = g->out_at = g->out_start = 0;
		} else {
			g->ctl_sent += (size_t)put;
			if (g->ctl_sent == g->ctl_len) g->ctl_sent = g->ctl_len = 0;
		}
	}
}

void guest_frame_put(guest *g, size_t len, uint32_t frame)
{
	uint8_t head[GUEST_HEAD];
	size_t hl = ws_frame_head(head, OP_BINARY, len);
	bool moved = false;

	/* The head right before the message, so the two go as one buffer */
	g->out_start = GUEST_HEAD - hl;
	memcpy(g->out + g->out_start, head, hl);
	g->out_at = g->out_start;
	g->out_end = GUEST_HEAD + len;
	g->last_frame = frame;
	if (g->nflight < GUEST_FLIGHT) g->flight[g->nflight++] = frame;
	if (!flush(g, &moved)) g->dead = true;
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Frames up to and including `frame` are drawn. By number, not by count, so
 * an ack the guest repeats or skips cannot open the window wider than two. */
static void acked(guest *g, uint32_t frame)
{
	int k = 0;

	for (int i = 0; i < g->nflight; i++)
		if ((int32_t)(g->flight[i] - frame) > 0) g->flight[k++] = g->flight[i];
	g->nflight = k;
}

/* One message; false to stop reading (the guest is closing or dead) */
static bool message(guest *g, const ws_msg *m, guest_news *news)
{
	switch (m->opcode) {
	case OP_CLOSE:
		/* Its close: ours back, and done once that is out */
		g->heard = true;
		guest_close(g, 1000);
		return false;
	case OP_PING:
		if (!g->closing && !ctl_put(g, OP_PONG, m->data, m->len)) g->dead = true;
		return !g->dead;
	case OP_PONG:
		return true;               /* nothing here pings, so nothing to match */
	}
	if (g->closing) return true;   /* on its way before it heard our close */
	if (m->opcode == OP_BINARY && m->len == 5 && m->data[0] == 'A') {
		acked(g, le32(m->data + 1));
		return true;
	}
	if (m->opcode == OP_BINARY && m->len == 3 && m->data[0] == 'P') {
		news->pad = m->data[1] | m->data[2] << 8;
		return true;
	}
	guest_close(g, 1003);          /* "cannot accept that kind of data" */
	return !g->dead;
}

/* What came, at most READ_MAX bytes, each whole message acted on. False when
 * the guest hung up or the socket failed. */
static bool take(guest *g, long now_ms, guest_news *news)
{
	size_t budget = READ_MAX;

	while (budget > 0 && !g->dead) {
		size_t want = sizeof g->in - g->in_used, at = 0;
		ssize_t got;

		if (want > budget) want = budget;
		got = recv(g->fd, g->in + g->in_used, want, 0);
		if (got == 0) return false;
		if (got < 0) {
			if (errno == EINTR) continue;
			return errno == EAGAIN || errno == EWOULDBLOCK;
		}
		news->moved = true;
		g->in_ms = now_ms;
		budget -= (size_t)got;
		if (g->heard) continue;    /* nothing it says now is read */
		g->in_used += (size_t)got;

		/* in holds more than the longest frame ws_parse takes (6 + 125
		 * bytes), so a full one always has a whole message, or an error,
		 * at its front, and there is always room to read */
		for (;;) {
			ws_msg m;
			int used = ws_parse(g->in + at, g->in_used - at, &m);

			if (used == 0) break;
			if (used < 0) {
				/* The stream cannot be followed past this: our close,
				 * and nothing more read (RFC 6455, 7.1.7) */
				guest_close(g, 1002);
				g->heard = true;
				at = g->in_used;
				break;
			}
			at += (size_t)used;
			if (!message(g, &m, news)) {
				if (g->heard) at = g->in_used;
				break;
			}
		}
		memmove(g->in, g->in + at, g->in_used - at);
		g->in_used -= at;
	}
	return true;
}

bool guest_poll(guest *g, long now_ms, guest_news *news)
{
	news->pad = -1;
	news->moved = false;
	if (g->fd < 0 || g->dead) return false;
	if (!flush(g, &news->moved)) return false;
	if (!take(g, now_ms, news) || g->dead) return false;
	if (!flush(g, &news->moved) || g->dead) return false;   /* a pong or a close */
	/* Gone without a word (guest.h): no close to wait for, nobody to hear it */
	if (g->nflight > 0 && now_ms - g->in_ms >= GUEST_QUIET_MS) return false;
	if (g->closing && !g->out_end && !g->ctl_len) {
		if (g->heard) return false;
		if (!g->shut) {
			g->shut = true;
			g->shut_ms = now_ms;
		} else if (now_ms - g->shut_ms >= CLOSE_WAIT_MS) {
			return false;
		}
	}
	return true;
}

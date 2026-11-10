/* SPDX-License-Identifier: GPL-3.0-only */
/* File Transfer's "1 connected": browsers, not sockets.
 *
 * A browser keeps several connections open to one page (keep-alive, a
 * stylesheet, a script, the font, an upload each), so counting the server's
 * connections said "5 connected" with one tab open (2026-11-10). A browser is
 * told apart by where it is (the address it connects from) and, once signed
 * in, by its session cookie: two browsers on one computer have two sessions,
 * two tabs of one browser share one. One counts while it has a connection
 * open or asked something in the last CLIENTS_WINDOW_MS; a browser's
 * connection that has not asked anything yet, or asked without a session,
 * is that address's signed-in browser where there is one, else one of its
 * own.
 *
 * Pure: hare.c tells it each request and the open connections' addresses;
 * tools/clients-check.c holds it to the cases. */
#ifndef CLIENTS_H
#define CLIENTS_H

#include <stdbool.h>

#define CLIENTS_MAX        32
#define CLIENTS_WINDOW_MS  10000u
#define CLIENTS_ADDR       48
#define CLIENTS_KEY        16

typedef struct {
	char     addr[CLIENTS_ADDR];
	char     key[CLIENTS_KEY];      /* the session's first characters, "" none */
	unsigned last;
	bool     used;
} client_seen;

typedef struct {
	client_seen c[CLIENTS_MAX];
} clients;

void clients_clear(clients *t);

/* A request from `addr`, with session `key` ("" or NULL for none) */
void clients_note(clients *t, const char *addr, const char *key, unsigned now_ms);

/* How many browsers: `open` the addresses of the connections open now */
int clients_count(const clients *t, const char *const *open, int nopen, unsigned now_ms);

#endif

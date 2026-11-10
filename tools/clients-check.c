/* SPDX-License-Identifier: GPL-3.0-only */
/* File Transfer's browser count (src/clients.h): one browser with five
 * keep-alive connections is one; two tabs of one browser are one; two
 * browsers on one computer, each signed in, are two; a phone and a computer
 * are two; a browser on the PIN page counts, and stops being counted once it
 * signs in (it is the same browser); and a browser that closed is gone once
 * its connections are and the window has passed. */
#include <stdio.h>

#include "../src/clients.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

int main(void)
{
	clients t;
	const char *five[] = { "10.42.0.2", "10.42.0.2", "10.42.0.2", "10.42.0.2", "10.42.0.2" };
	const char *two[] = { "192.168.1.20", "192.168.1.31" };
	const char *none[] = { NULL };
	int n;

	/* The Mac of 2026-11-10: one Safari tab, five sockets, signed in */
	clients_clear(&t);
	clients_note(&t, "10.42.0.2", "", 1000);                  /* the PIN page */
	clients_note(&t, "10.42.0.2", "a1b2c3d4e5f6", 2000);      /* signed in */
	clients_note(&t, "10.42.0.2", "a1b2c3d4e5f6", 2100);
	n = clients_count(&t, five, 5, 2200);
	CHECK(n == 1, "one browser, five sockets: %d", n);
	/* Idle for a minute, its keep-alive sockets still open: still one */
	n = clients_count(&t, five, 5, 62000);
	CHECK(n == 1, "an idle browser with sockets open: %d", n);
	/* The tab closed: its sockets go, and after the window, nobody */
	n = clients_count(&t, none, 0, 62000 + CLIENTS_WINDOW_MS);
	CHECK(n == 0, "a closed browser still counted: %d", n);

	/* A second tab of the same browser shares its cookie: one */
	clients_clear(&t);
	clients_note(&t, "10.42.0.2", "a1b2c3d4e5f6", 1000);
	clients_note(&t, "10.42.0.2", "a1b2c3d4e5f6", 1500);
	CHECK(clients_count(&t, five, 2, 1600) == 1, "two tabs of one browser");
	/* Firefox beside Safari on the same Mac, signed in on its own: two */
	clients_note(&t, "10.42.0.2", "zz99yy88xx77", 1700);
	CHECK(clients_count(&t, five, 3, 1800) == 2, "two browsers on one computer");

	/* A phone and a computer on Wi-Fi */
	clients_clear(&t);
	clients_note(&t, "192.168.1.20", "k1", 1000);
	clients_note(&t, "192.168.1.31", "", 1000);               /* on the PIN page */
	CHECK(clients_count(&t, two, 2, 1100) == 2, "a phone and a computer");
	/* The phone signs in: still two */
	clients_note(&t, "192.168.1.31", "k2", 3000);
	CHECK(clients_count(&t, two, 2, 3100) == 2, "the phone signed in counted twice");

	/* A connection that has asked nothing yet: one browser on its way */
	clients_clear(&t);
	CHECK(clients_count(&t, five, 3, 1000) == 1, "three fresh sockets from one address");
	CHECK(clients_count(&t, two, 2, 1000) == 2, "fresh sockets from two addresses");

	/* A full table makes room by its oldest */
	{
		char addr[32];
		int i;

		clients_clear(&t);
		for (i = 0; i < CLIENTS_MAX + 4; i++) {
			snprintf(addr, sizeof addr, "10.0.0.%d", i);
			clients_note(&t, addr, "", (unsigned)(1000 + i));
		}
		n = clients_count(&t, none, 0, 1000 + CLIENTS_MAX + 4);
		CHECK(n == CLIENTS_MAX, "a full table counted %d", n);
	}

	if (fails) {
		fprintf(stderr, "clients-check: %d failed\n", fails);
		return 1;
	}
	printf("clients-check: browsers, not sockets: one tab of five sockets is one, two browsers two\n");
	return 0;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* SSH over USB File Transfer's cable, with the screen's PIN.
 *
 * While USB File Transfer is open on the GKD Pixel 2, the launcher runs
 * plastron's dropbear on the cable's address alone (10.42.0.1:22, never
 * Wi-Fi), and stops it when the screen closes, which ends open sessions too.
 * A release image starts no SSH at boot and carries no keys (plastron's
 * post-build takes dropbear's start script and root's .ssh out); this is the
 * only way in, and only from a computer on the cable while somebody has the
 * screen open.
 *
 *   ssh root@10.42.0.1      the password is the PIN on the screen
 *   ssh-copy-id root@...    saves a key on the card; later logins skip the PIN
 *
 * dropbear checks passwords against /etc/shadow, which on plastron's
 * read-only root says root has none. So dropbear runs in a mount namespace
 * of its own, where a file in /tmp holding the PIN's hash is bound over
 * /etc/shadow and a home in /tmp over /root, its .ssh a link to the card's
 * .userdata/<device>/ssh (keys and the host key, which stays the same so the
 * computer knows the device next time). Nothing outside that namespace sees
 * either: the system's own password stays empty, and a development image's
 * dropbear, if it runs, is not touched.
 *
 * Five wrong PINs turn SSH off until the screen opens again, as the page
 * waits after five: root's password in the namespace's shadow becomes "*" at
 * once, then everything in the namespace is stopped. Several connections can
 * each have a guess in flight when the fifth lands, so a computer on the
 * cable gets a few dozen guesses at most of 10,000 per opening of the screen.
 * Wrong PINs on the page count toward the same five. The lockout ends a
 * signed-in session too: a count that ignored guesses made while somebody is
 * in would let anyone wait for the owner and then guess freely.
 * A development image already has dropbear on port 22 for every interface
 * (with the developer's key); there this says so and starts nothing.
 *
 * Never with a radio: dropbear binds the cable's address, but Linux answers
 * for any of its addresses on any interface, and the kernel has no netfilter
 * to say otherwise. So SSH does not start, or stops, while any 802.11
 * interface exists - which on the Pixel 2 it cannot while the one USB port is
 * a computer's cable. A launcher that dies with sessions open leaves their
 * namespace's name in /tmp, and the next one ends them as it starts.
 *
 * Linux only: elsewhere every call does nothing and the status is "off". */
#ifndef SSHLINK_H
#define SSHLINK_H

#include <stdbool.h>
#include <stddef.h>

#define SSHLINK_MAX_TRIES 5

/* Start SSH on `addr` (the cable's address), the password `pin`, keys and
 * the host key under `keydir` (made if missing). False when it could not
 * start; sshlink_status says why in the screen's words. */
bool sshlink_start(const char *addr, const char *pin, const char *keydir);

/* What dropbear has said since the last call: sign-ins, sign-outs, wrong
 * PINs. Bounded; call it from the screen's loop. */
void sshlink_poll(void);

/* Stop dropbear and everything it started, and take the PIN's file away.
 * Also called once as the launcher starts, for what a launcher before it
 * left behind. */
void sshlink_stop(void);

/* Somebody signed in right now, which holds Auto Off off as a connected
 * browser does. */
int sshlink_sessions(void);

/* Listening, so the page may show the command. */
bool sshlink_on(void);

/* The wrong PINs the page has had since the screen opened (hare_wrong_pins).
 * They count toward the same five: the PIN is one secret, and a page that
 * waits 30 seconds after five and then takes five more would otherwise let a
 * computer on the cable find it in hours and then sign in as root. */
void sshlink_note_web(int wrong);

/* The screen's value for its SSH row, lowercase as status words are
 * (DESIGN.md 4): "waiting", "1 signed in", "locked", "dev image",
 * "not with Wi-Fi", "off". */
const char *sshlink_status(void);

/* ---- the parts a host check can reach (tools/sshlink-check.c) ---------- */

typedef struct {
	int  sessions;      /* signed in now */
	int  bad;           /* wrong passwords since the start */
	bool failed;        /* dropbear could not listen */
} sshlink_count;

/* One line of what dropbear logs (-E), counted into `c`. */
void sshlink_feed(sshlink_count *c, const char *line);

/* /etc/shadow's text with root's password field set to `hash`, into `out`.
 * False if it does not fit, or there is no root line to set. */
bool sshlink_shadow(const char *shadow, const char *hash, char *out, size_t n);

#endif

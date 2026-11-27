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
 * to say otherwise. So SSH with the password does not start, or stops, while
 * any 802.11 interface exists - which on the Pixel 2 it cannot while the one
 * USB port is a computer's cable. A launcher that dies with sessions open
 * leaves their namespace's name in /tmp, and the next one ends them as it
 * starts.
 *
 * SSH over Wi-Fi (Developer Mode's choice, off as it ships) is the other way
 * to run it: on every address, Wi-Fi's and the cable's alike, and with keys
 * alone. dropbear takes no password at all there (-s), and root's password
 * in its namespace is "*" besides, so nothing a network can send signs in
 * without a key the card's owner put on the card. That is what makes it safe
 * with a radio where the password is not: which interface a packet came in
 * on stops mattering when no interface takes a password. sshlink_want says
 * which of the two a moment wants.
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

/* The cable's SSH running already on `addr` with a password (Developer
 * Mode's, sshlink_start): its password becomes `pin` in place, in the file
 * its namespace reads, and dropbear goes on with its sessions, rather than
 * a stop and a start (which ended them and held the screen while it
 * waited). False, with nothing changed, for anything else: then start. */
bool sshlink_repin(const char *addr, const char *pin);

/* Start SSH on the cable's `addr` alone, keys only (over a port). False as
 * sshlink_start_net is, with no key. */
bool sshlink_start_cable_keys(const char *addr, const char *keydir);

/* `ifname`'s IPv4 address into `addr` while the interface is up: the cable's
 * usb0 with 10.42.0.1, as over a port, where portkeys has no USB code of its
 * own. False when it is not there, down, or has none. */
bool sshlink_if_addr(const char *ifname, char *addr, size_t n);

/* dropbear's log, which sshlink_poll reads; -1 while nothing runs. A loop
 * that sleeps in poll can wake on it. */
int sshlink_log_fd(void);

/* Start SSH on every address (port 22 on Wi-Fi's and the cable's), keys
 * only: no password is made or taken. False when it could not start, and
 * when `keydir`/authorized_keys has no key, since then nobody could sign in
 * (sshlink_status: "needs a key"). */
bool sshlink_start_net(const char *keydir);

/* An 802.11 interface on the device, and its name and IPv4 address into
 * `ifname` and `addr` (empty while it has none: joining, or the radio is
 * there and off). False when there is no radio. A walk of /sys and
 * getifaddrs, no fork, so a screen's loop can ask a few times a second. */
bool sshlink_radio(char *ifname, size_t in, char *addr, size_t an);

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

/* SSH over Wi-Fi's last start found no key to take (sshlink_start_net) */
bool sshlink_needs_key(void);

/* The last start found port 22 taken: a development image's dropbear, which
 * runs for every interface from boot and is left as it is */
bool sshlink_dev_image(void);

/* The wrong PINs the page has had since the screen opened (hare_wrong_pins).
 * They count toward the same five: the PIN is one secret, and a page that
 * waits 30 seconds after five and then takes five more would otherwise let a
 * computer on the cable find it in hours and then sign in as root. */
void sshlink_note_web(int wrong);

/* The screen's value for its SSH row, begun with a capital as every row's
 * value is (DESIGN.md 4): "Waiting", "1 signed in", "Locked", "Dev image",
 * "Not with Wi-Fi", "Needs a key", "Off". */
const char *sshlink_status(void);

/* ---- the parts a host check can reach (tools/sshlink-check.c) ---------- */

/* Where SSH listens, and how one signs in there */
typedef enum {
	SSHLINK_OFF,        /* nothing listens */
	SSHLINK_CABLE,      /* the cable's address alone: the password or a key */
	SSHLINK_NET,        /* every address, Wi-Fi's too: a key only */
	SSHLINK_CABLE_KEYS, /* the cable's address alone, a key only: over a port,
	                     * where nobody can read a password off the screen */
} sshlink_mode;

/* What Developer Mode's SSH is listening as now: SSHLINK_OFF unless it is
 * on. File Transfer's SSH over the cable reads SSHLINK_CABLE too. */
sshlink_mode sshlink_running(void);

/* What decides it, each a fact of the moment */
typedef struct {
	bool cable;         /* a computer on the cable, which has its address */
	bool radio;         /* an 802.11 interface is there (sshlink_radio) */
	bool over_wifi;     /* the player turned SSH over Wi-Fi on */
	bool cable_spent;   /* the cable's SSH already ran for this plug: a
	                     * lockout, or a radio that ended it, holds until
	                     * the cable comes out */
	bool link;          /* the Game Boy's Link Cable holds the radio for its
	                     * own network (src/link.h's link_holds_radio): no
	                     * SSH over Wi-Fi there, as if there were no radio.
	                     * Its code is four digits, so that network is no
	                     * place for a login */
} sshlink_facts;

/* Developer Mode's SSH, for these facts:
 *   a radio, SSH over Wi-Fi on    SSHLINK_NET, with the cable in or not:
 *                                 keys only on every address
 *   a radio, SSH over Wi-Fi off   SSHLINK_OFF ("not with Wi-Fi"): the
 *                                 password must never meet a network
 *   no radio, a computer          SSHLINK_CABLE, once per plug
 *   neither                       SSHLINK_OFF
 * The password is never offered while a radio is there, whatever the
 * choice says. */
sshlink_mode sshlink_want(const sshlink_facts *f);

/* What Developer Mode's side keeps between looks: the mode it started and
 * believes runs, and whether the cable's SSH ended with the cable still in
 * (a lockout, dropbear gone, a radio that came), which holds it off until
 * the cable comes out. */
typedef struct {
	sshlink_mode mode;
	bool         spent;
} sshlink_plan;

/* One look (dev_ssh_tick, five a second), with `running` what sshlink says
 * runs now (sshlink_running). Returns the mode that should run after it:
 * p->mode to leave things as they are, another to end the one running and
 * start that one (none for SSHLINK_OFF). Keeps p->spent; p->mode is the
 * caller's to set once a start works, and SSHLINK_OFF once it ends one.
 * A cable SSH that runs is never ended by its own plug. */
sshlink_mode sshlink_step(sshlink_plan *p, const sshlink_facts *f, sshlink_mode running);

/* ---- over a port (tools/pkssh.c) ---------------------------------------
 *
 * A port exits the launcher, and Developer SSH, its child, went with it:
 * every port cut the sessions off, and a player debugging a port needed SSH
 * most then. So portkeys, which runs for the whole port, takes SSH over
 * while one runs, keys only on every link, and gives it back as the port
 * ends; the in-game menu over a port (zyperos.elf --port-menu) touches none
 * of it. Each hand-over is one stop and one start (a dropbear cannot change
 * parents), and the namespace a process before it left (RUN_NS) is ended as
 * the next one starts, so nothing of one owner outlives it. */

/* Who runs Developer SSH: the launcher (the shelf, a game, File Transfer),
 * portkeys while a port runs, and nobody in the in-game menu over a port,
 * which is a second zyperos.elf for a moment and must not end portkeys'
 * sessions as it opens or closes */
typedef enum { SSHLINK_BY_LAUNCHER, SSHLINK_BY_PORTKEYS, SSHLINK_BY_NONE } sshlink_owner;
sshlink_owner sshlink_owner_of(bool port_running, bool port_menu);

/* Over a port, for these facts and whether Developer Mode is on:
 *   Developer Mode off            SSHLINK_OFF
 *   a radio, SSH over Wi-Fi on    SSHLINK_NET (keys only, every address)
 *   a radio, SSH over Wi-Fi off   SSHLINK_OFF (not with Wi-Fi, as ever)
 *   the cable's link up           SSHLINK_CABLE_KEYS (keys only, its address)
 *   neither                       SSHLINK_OFF
 * Never the password: it lives only on the Developer page, which no port
 * shows, and with keys alone there is nothing to guess and nothing to lock. */
sshlink_mode sshlink_want_port(const sshlink_facts *f, bool devmode);

/* ---- a development image over Wi-Fi ------------------------------------
 *
 * A development image (plastron's, any build but a release) starts its own
 * dropbear at boot on port 22 of every address, with the key the image was
 * built with (plastron's local/authorized_keys) and no password (root has
 * none, and dropbear turns an empty one down): SSH over Wi-Fi, keys only,
 * from the first boot, with nothing of Developer Mode's in it. What it
 * lacked was the name: zyperos.local on the Wi-Fi address, as Developer
 * Mode's SSH over Wi-Fi answers it on a release image (src/mdns.h), so a
 * computer on the same network reaches a test build without the cable.
 * Whether to answer it now: on a development image, with an address on the
 * radio, and not while File Transfer has the name. devmode_screen.c follows
 * it every two seconds. */
bool sshlink_dev_image_name(bool dev_image, bool xfer_on, const char *wifi_addr);

/* What dropbear is started with for `mode` (CABLE or NET) into `argv`, at
 * most SSHLINK_ARGV entries with its NULL: `listen` is the cable's
 * "addr:22" for CABLE, unused for NET, which takes port 22 on every
 * address; `key` the host key. NET always carries -s (no password logins).
 * Returns how many entries before the NULL, 0 for SSHLINK_OFF. */
#define SSHLINK_ARGV 16
int sshlink_argv(sshlink_mode mode, const char *listen, const char *key, const char **argv);

/* The keys SSH takes: `keydir`/authorized_keys's (sshlink_key_lines), the
 * Developer folder's and ssh-copy-id's alike. 0 with no file. */
int sshlink_keys(const char *keydir);

/* The keys in an authorized_keys text: lines that are not blank and not a
 * comment. dropbear has the last word on each; this is only whether there
 * is anything for it to read. */
int sshlink_key_lines(const char *text);

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

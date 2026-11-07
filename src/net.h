/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_NET_H
#define ZYPEROS_NET_H

#include <stdbool.h>
#include <stddef.h>

/* The launcher's HTTP client.
 *
 * It was ranet.c, and RetroAchievements was the only thing that talked. Box
 * art is fetched from libretro's thumbnail collection now and needs the same
 * curl, the same certificate store and the same fork-and-execv, so the file
 * that owns all three stopped being about one service. Nothing about the
 * transport was ever specific to RetroAchievements.
 *
 * Through the device's own curl, by fork and execv, the same way wifi.c drives
 * wpa_cli. The Brick ships curl 7.54.1 against OpenSSL 1.1.0i, so HTTPS is
 * already here; what it does not ship is anything to trust, which is why
 * res/ssl/cacert.pem travels with the launcher (see its README for the
 * measurement).
 *
 * THE REQUEST GOES IN A FILE, not on the command line. curl's -K reads options
 * from one, and an account token in argv is readable by anything that can list
 * processes. The file is written 0600 and unlinked straight after.
 *
 * Two halves. The blocking calls wait for at most `timeout_s`, and none is
 * made from a frame loop or the game tick: sign-in waits behind a panel, and
 * unlocks go out on a worker thread. Everything a frame loop or the game tick
 * needs - box art, a set fetched behind a first play, the account's unlocks -
 * uses the async half further down, which starts a request or polls it and
 * never waits.
 */

/* Where the certificate store is. Set once at startup; without it every
 * handshake fails verification, because the device has no trust store of its
 * own (res/ssl/README.md). Passed in rather than read from platform.h so this
 * file does not drag SDL into a check that only wants to talk to a socket. */
void net_set_ca_path(const char *path);

typedef struct { const char *k, *v; } net_field;

/* Small replies - an unlock sent. Returns the body length, or -1: no curl,
 * no network, HTTP error, or a timeout. Which failure it was is logged, not
 * returned; net_post_answer below returns it, for sign-in. */
long net_post_buf(const net_field *f, int n, char *out, size_t outn, int timeout_s);

/* The same, as a GET to any URL, with the fields urlencoded onto the query
 * string. For a service that authenticates every call with parameters rather
 * than with a token in a body - ScreenScraper does, and the parameters include
 * an account password, which is why this goes through the same 0600 config
 * file and never through argv. */
long net_get_buf(const char *url, const net_field *f, int n, char *out,
                 size_t outn, int timeout_s);

/* A plain GET with one request header, for a service that takes its key in
 * one - SteamGridDB's "Authorization: Bearer <key>". The header is a
 * credential and is treated as one: written into the 0600 config file, never
 * into argv or the URL, and a request carrying one follows no redirect, so
 * the key reaches the host named and no other. Blocking: for a worker
 * thread. */
long net_get_hdr_buf(const char *url, const char *header, char *out, size_t outn,
                     int timeout_s);

/* net_post_buf and net_get_hdr_buf, saying who answered.
 *
 * curl runs with `fail`, so to the calls above every HTTP error is a failed
 * request with no body, the same as no network at all: a sign-in with the
 * wrong password said "could not reach retroachievements.org", and a wrong
 * SteamGridDB key was kept as unchecked. These put in `*http` the status the
 * server answered with, whole - 200, a 401 or a 503 refusing, a 302 not
 * followed - or 0 when nothing answered whole: no curl, no network, no
 * name, no route, the timeout, a reply cut short. The body is handed over,
 * and its length returned, only for a 2xx that came whole and fits `out`;
 * -1 otherwise, whatever `*http` says, so half of a reply (half a token) is
 * never read as one. The same file and the same rules for what it carries;
 * a refusal costs no more time than an answer. */
long net_post_answer(const net_field *f, int n, char *out, size_t outn,
                     int timeout_s, int *http);
long net_get_hdr_answer(const char *url, const char *header, char *out,
                        size_t outn, int timeout_s, int *http);

/* Many small files in one curl - one process, one connection, one TLS
 * handshake - for a worker thread: blocks for at most `timeout_s` in all.
 * Each lands at paths[i] only when what came back starts as a PNG does, so a
 * 404's page or a cut transfer never stands where a later run would take it
 * for a badge. Returns how many landed. Badges are 64 pixels and a set has
 * up to two hundred: a curl each was a second per five of them on the
 * device, most of it the handshake every one repeated. */
int net_get_pngs(const char *const *urls, const char *const *paths, int n,
                 int timeout_s);

/* The same request, started and left to run. Nothing waits for it.
 *
 * This exists because a launch was 15ms warm and a request to RetroAchievements
 * is 310-460ms measured on the device, 150ms of which is the TLS handshake
 * alone - so anything on the launch path that waits for the network has
 * already lost. One in flight at a time, which is all this needs.
 *
 * net_async_poll: 1 finished and the file is there, 0 still running, -1 nothing
 * started or it failed. Reaping is the caller's job via poll; an unreaped
 * child is a zombie until then. */
bool net_post_async(const net_field *f, int n, const char *path, int timeout_s);
int  net_async_poll(void);

/* A GET, for things that are not RetroAchievements - a catalog, a checksum
 * list, a cover - started and left running, reaped through net_async_poll.
 * Same certificate store, same timeout, same fork-and-execv.
 *
 * For anything driven from a screen's frame loop. A blocking fetch there is
 * not merely slow: that loop is where the power button is read, so a request
 * sitting on its timeout is a handheld that has stopped answering its own
 * power button for a minute. One in flight at a time, like net_post_async.
 *
 * The file appears at `path` only when the poll says 1 - it is written
 * through a temporary, so a run that is canceled or dies partway cannot
 * leave half a file where a later run would find it and skip the download. */
bool net_get_async(const char *url, const char *path, int timeout_s);

/* The same, with one request header (see net_get_hdr_buf for what it may
 * carry and where it goes). NULL is net_get_async. */
bool net_get_async_hdr(const char *url, const char *path, int timeout_s,
                       const char *header);

/* What the server answered with, once net_async_poll has returned non-zero:
 * an HTTP status, or 0 when the request never reached a server at all.
 *
 * Needed because curl runs with `fail` and so reports every refusal the same
 * way - no body, one exit code. ScreenScraper says "we do not know this game",
 * "too many at once" and "today's quota is spent" all as 4xx, and a bulk run
 * has to do three different things about them: carry on, wait a beat, and stop
 * for the day. Guessing between those either abandons a library that could
 * have been scraped or keeps asking a server that has already said no. */
int net_async_http(void);

/* How long the request took, start to reap, and curl's exit status (-1 when it
 * did not exit on its own), for the same finished request. For the log: a
 * scrape that is slow has to say which request was, and "no answer" at the
 * timeout (curl's 28) is a different problem from a quick refusal. */
int net_async_ms(void);
int net_async_exit(void);

/* Give up on whatever is in flight: kill it, reap it, and free the slot.
 *
 * For a caller that stops caring - a screen the player closed mid-fetch. Not
 * optional politeness: there is ONE slot, and a request abandoned without
 * this holds it forever. Everything async then fails, permanently, and
 * silently. */
void net_async_abort(void);

/* Percent-encode for a query string. Unreserved characters pass; everything
 * else becomes %XX. Truncates rather than overflowing. Implemented in
 * src/urlenc.c rather than net.c, for the reason written there.
 *
 * Here because two scrapers now build query strings and one copy of this is
 * enough - and because the thing most likely to need it is a password, where
 * an unencoded `&` would end the parameter and hand the rest of the password
 * to the server as a field of its own. */
void net_urlencode(const char *in, char *out, size_t outn);

/* Whether there is any point trying: curl present and an address on a
 * non-loopback interface. Cheap, and it turns "achievements did not appear"
 * into something the menu can explain. */
bool net_online(void);

#endif

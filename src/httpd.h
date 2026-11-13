/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HTTPD_H
#define ZYPEROS_HTTPD_H

#include <stdbool.h>
#include <stddef.h>

/* A small HTTP/1.1 server that never blocks.
 *
 * It cannot block, and that is the whole shape of this file. The launcher is
 * one thread; the loop that would call httpd_poll is the same loop that reads
 * the power button, and the rule the rest of ZyperOS already lives by is that
 * nothing inside a frame is allowed to wait. A read() on a socket with no data
 * would stop the device answering its own power button for as long as a phone
 * on the far side of the house took to send the next packet.
 *
 * So: every socket is O_NONBLOCK, every connection is a state machine, and
 * httpd_poll does a bounded amount of work and returns. A 900 MB ROM arrives
 * across several thousand frames and never exists in memory - it is read in
 * chunks and written to the card in the same chunks.
 *
 * It is not a general web server and should not grow into one. No TLS: this is
 * a LAN service the user opens deliberately and closes when done, and a
 * certificate for 192.168.1.x is a thing nobody can make work. No CGI, no
 * directory indexes, no ranges, no chunked request bodies - the only client is
 * the page in res/web, which this file's author also writes.
 */

/* One request, from the line and headers through to the reply. The handler
 * fills in the second half. */
typedef struct httpd_req httpd_req;

/* What a handler does with a request body:
 *
 *   HTTPD_BODY_NONE   there is none, or it is not wanted - discard it
 *   HTTPD_BODY_MEM    small and wanted whole (a PIN, a form field)
 *   HTTPD_BODY_FILE   large - stream it to sink_path as it arrives
 */
typedef enum {
	HTTPD_BODY_NONE,
	HTTPD_BODY_MEM,
	HTTPD_BODY_FILE
} httpd_body_mode;

/* Anything larger than this in HTTPD_BODY_MEM is refused with 413. A PIN is
 * four bytes; nothing that belongs in memory is near this. */
#define HTTPD_MEM_MAX 4096

/* Called when the request line and headers are complete, and again when the
 * body is. The first call decides what happens to the body; the second sets
 * the response. A request with no body gets one call with both jobs.
 *
 * `done` is false on the first call and true on the second. */
typedef void (*httpd_handler)(httpd_req *r, bool done, void *ctx);

/* Called once when a request is over, for every request the handler was called
 * for. `whole` is true when its reply went out in full, false when the
 * connection went first - a hang-up, a dropped network, or twenty seconds of
 * silence. The request can still be read here and is released straight after.
 *
 * The handler hears when a request arrives and when its body has, and without
 * this never heard when one ended. So the transfer screen, which says what is
 * happening now, went on saying "receiving" for an upload that died partway,
 * and "sending" for every download long after it had finished.
 *
 * Not called for connections httpd_stop closes: whoever stops the server
 * already knows. */
typedef void (*httpd_end)(httpd_req *r, bool whole, void *ctx);

/* A number the handler keeps on a request for itself, zero until set. Nothing
 * in httpd reads it. It is how an httpd_end tells the requests it has
 * something to say about from the ones it does not. */
void httpd_set_tag(httpd_req *r, int tag);
int  httpd_tag(const httpd_req *r);

/* Reading the request. */
const char *httpd_method(const httpd_req *r);   /* "GET", "PUT", ... */
const char *httpd_path(const httpd_req *r);     /* raw, no query, undecoded */
const char *httpd_query(const httpd_req *r, const char *key, char *out,
                        size_t outn);           /* "" when absent */
const char *httpd_header(const httpd_req *r, const char *name);
long        httpd_content_len(const httpd_req *r);
const char *httpd_body(const httpd_req *r, size_t *len);  /* HTTPD_BODY_MEM */

/* Deciding what to do with the body. Call at most once, on the first call. */
void httpd_want_body(httpd_req *r, httpd_body_mode mode, const char *sink_path);

/* Setting the response. Exactly one of these, on the last call.
 *
 * httpd_reply copies `body`, so the caller keeps ownership of whatever it
 * passed. httpd_reply_file streams from the card and never reads the file into
 * memory, which is what makes a 900 MB download cost nothing. It is false when
 * the file could not be opened, in which case it has replied with an error
 * instead. `extra_headers` may be NULL; when given it is inserted verbatim and
 * must carry its own CRLF terminators. */
void httpd_reply(httpd_req *r, int status, const char *content_type,
                 const void *body, size_t len, const char *extra_headers);
bool httpd_reply_file(httpd_req *r, const char *path, const char *content_type,
                      const char *extra_headers);

/* One of the page's own files: answered 304 with no body when the browser
 * already has it (its ETag, the file's size, time and path), and gzipped
 * when the browser takes gzip and it is text. `no-cache`, so a browser asks
 * every time and a changed file is never served stale. False, as
 * httpd_reply_file, when it replied with an error. */
bool httpd_reply_static(httpd_req *r, const char *path, const char *content_type);
void httpd_reply_status(httpd_req *r, int status, const char *text);
/* Headers every reply carries from now on, each with its CRLF, kept by
 * pointer (a string that outlives the server); NULL for none, as at the
 * start. p2d's: its page is never to be drawn inside another site's. */
void httpd_always(const char *headers);

/* The connection, after a 101 reply, for whoever takes it from here (p2d's
 * WebSocket). Called once the 101 and its headers are all out, with the
 * socket still non-blocking; from then on it is the callee's to read, write
 * and close. httpd forgets it: no close, no timeout, no more reads, and
 * httpd_stop leaves it alone. */
typedef void (*httpd_taken)(int fd, void *ctx);

/* "HTTP/1.1 101 Switching Protocols", `extra_headers` (each with its CRLF),
 * and the hand-over to `fn`. A reply like the others, on the first call.
 *
 * Only for a request with nothing behind it: the bytes httpd has read past
 * the headers are its own, so they would be lost to `fn`, which reads the
 * socket from where it is. A WebSocket client sends nothing until it has the
 * 101 (RFC 6455, 4.1), so a request with a body, or with bytes already after
 * it, is answered 400 instead. */
void httpd_reply_upgrade(httpd_req *r, const char *extra_headers,
                         httpd_taken fn, void *ctx);

/* Bind and start listening. Tries `port`, then the fallbacks in order, so
 * port 80 can be preferred without a busy one being fatal - the address the
 * user is shown comes from httpd_port(). False if none of them bind. */
bool httpd_start(const int *ports, int nports);
/* The same on one address only (dotted IPv4; NULL is every one, as
 * httpd_start): p2d's, the handheld's address on the link Player 2 is on, so
 * a client on a network the handheld's Wi-Fi is still on cannot reach it at
 * the handheld's address there. False for an address that is none. */
bool httpd_start_on(const char *ip, const int *ports, int nports);
void httpd_stop(void);
bool httpd_running(void);
int  httpd_port(void);
/* The listening socket, -1 while stopped: for a caller with no connection
 * open to wait in poll() on, until a browser comes */
int  httpd_fd(void);

/* A bounded slice of work: accept what is waiting, move every live connection
 * along, drop the timed-out. Returns the number of connections that did
 * something, which is zero on a quiet frame - the caller uses that to tell
 * "a transfer is running" from "nothing is happening", because Auto Off must
 * not power the device off in the middle of an upload. `end` may be NULL. */
int httpd_poll(httpd_handler fn, httpd_end end, void *ctx);

/* Bytes moved since the last call, in and out. For the screen to show that
 * something is happening, and for the caller to keep Auto Off honest. */
void httpd_traffic(unsigned long *in, unsigned long *out);

/* How many connections are open. */
int  httpd_conn_count(void);
/* The address a request came from, as text ("10.42.0.2"), and those of the
 * connections open now (at most `max`): File Transfer counts browsers by
 * them, not connections (src/clients.h) */
const char *httpd_peer(const httpd_req *r);
int  httpd_peers(const char **out, int max);

/* The bodies arriving into files now (HTTPD_BODY_FILE: uploads), summed:
 * bytes in so far and bytes they will be. False when none is arriving. For
 * the ring round File Transfer's mark in the corner. */
bool httpd_body_progress(long long *got, long long *total);

#endif

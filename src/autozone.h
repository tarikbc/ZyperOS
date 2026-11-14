/* SPDX-License-Identifier: GPL-3.0-only */
/* Date & Time's Auto: the time zone of where the handheld is, asked of an
 * internet service each time it gets internet (Wi-Fi joined, or a phone's
 * USB tethering: the launcher's net_internet).
 *
 * The service is ipwho.is: no account and no key, HTTPS with the card's
 * certificate store, and a reply with the zone database's name for where
 * the address is and its UTC offset now:
 *
 *     {"success":true,"timezone":{"id":"America/Sao_Paulo","abbr":"-03",
 *      "is_dst":false,"offset":-10800,"utc":"-03:00"}}
 *
 * Free for 1,000 requests a day from each address, and Auto asks once a
 * connection and at most every AUTOZONE_EVERY_S. What it sees is the
 * handheld's public address, which it answers from: the row's note says so.
 *
 * The launcher's zone is a zone database name in TZ (plat_clock_zone), and
 * the devices' systems carry the whole database (plastron's
 * BR2_TARGET_TZ_INFO), so the name the service gives is set as it is, with
 * its own daylight saving rules, half-hour zones and all. Only a reply with
 * no name this system has falls back to its offset, which is the zone for
 * now and not its rules: Etc/GMT+3 for -03:00, a city for an offset off the
 * hour. A daylight saving change then comes with the next connection.
 *
 * The clock itself is set as before: plastron's udhcpc hook syncs it with
 * NTP on every lease, Wi-Fi's and the phone's.
 *
 * The rules, with no SDL and no device, so tools/autozone-check.c holds them:
 * reading the reply, the zone it comes to and when to ask. The launcher asks
 * on a thread of its own and sets what comes back as the row sets a zone
 * (src/main.c, tick_auto_zone). */
#ifndef ZYPEROS_AUTOZONE_H
#define ZYPEROS_AUTOZONE_H

#include <stdbool.h>
#include <stddef.h>

/* Who is asked, for the log, and the request: only the fields read */
#define AUTOZONE_SERVICE   "ipwho.is"
#define AUTOZONE_URL       "https://ipwho.is/?fields=success,message,timezone"
#define AUTOZONE_TIMEOUT_S 10

/* The library database's keys, beside "timezone": Auto on ("1") or off
 * ("0", every card's default, src/db.c), and the zone Auto set last, so the
 * row can say it found the zone in use. The zone in use stays "timezone",
 * which Auto writes as the row does: the launcher's start, a game's clock
 * and the bar over a port read it as before. */
#define AUTOZONE_KEY       "timezone.auto"
#define AUTOZONE_FOUND_KEY "timezone.auto_zone"

typedef struct {
	char id[64];        /* the zone database's name, "" when none came */
	int  offset_s;      /* seconds east of UTC, now */
	bool has_offset;
} autozone_answer;

/* The service's reply, `len` bytes of it, into `out`: true when it says
 * success and gives a zone name clock_zone_name_ok takes or an offset a zone
 * can have (-12:00 to +14:00, in quarter hours). A name that is not one is
 * left out and its offset kept; anything else (a refusal, a page that is not
 * JSON, a reply cut short) is false, and the zone there is stays. */
bool autozone_parse(const char *body, size_t len, autozone_answer *out);

/* The zone an answer comes to, into `out`: its name when `have` says this
 * system has the zone (clock_zone_have), else autozone_offset_zone's for its
 * offset; false for neither. */
bool autozone_pick(const autozone_answer *a, bool (*have)(const char *id),
                   char *out, size_t n);

/* A zone for an offset alone, the zone for now: UTC for 0, Etc/GMT-N for +N
 * hours (the zone database's sign, the other way round), and for the offsets
 * off the hour the city that has it (Asia/Kolkata for +05:30). False for an
 * offset no zone has. */
bool autozone_offset_zone(int offset_s, char *out, size_t n);

/* "+05:30", "-03:00", "+00:00": an offset as the log says it */
void autozone_offset_words(int offset_s, char *out, size_t n);

/* When to ask. Each look (every two seconds, from the screens' loops) says
 * whether Auto is on, whether there is a way out to the internet and whether
 * the clock is set (2026 or later: HTTPS cannot check a certificate against
 * a Pixel 2 still in 2018, and its clock syncs moments after the lease).
 *
 * A new connection, or Auto just chosen, asks AUTOZONE_SETTLE_MS on, as the
 * network settles after its lease; an ask that comes to nothing is tried
 * again 30 s, 2 min and 10 min on, AUTOZONE_TRIES in all, and then not until
 * the next connection. An answer ends the connection's asking, and a new
 * connection within AUTOZONE_EVERY_S of it does not ask (a Wi-Fi that drops
 * and comes back is not a new place). Choosing Auto again asks again. */
#define AUTOZONE_SETTLE_MS 5000u
#define AUTOZONE_TRIES     4
#define AUTOZONE_EVERY_S   (3 * 3600)

typedef struct {
	bool      looked;      /* Auto on and a way out at the last look */
	bool      asking;      /* an ask is out */
	int       tries;       /* this connection's; AUTOZONE_TRIES once done */
	unsigned  next_ms;     /* when the next try may go */
	long long answered_s;  /* the clock at the last answer, 0 for none */
} autozone_clock;

/* True when an ask should go now (it is then out until autozone_result).
 * `now_ms` is a clock that only goes on (plat_now_ms), `now_s` the time. */
bool autozone_due(autozone_clock *c, bool on, bool online, bool clock_set,
                  unsigned now_ms, long long now_s);

/* What the ask came to: `ok`, a zone set from it */
void autozone_result(autozone_clock *c, bool ok, unsigned now_ms, long long now_s);

#endif

/* SPDX-License-Identifier: GPL-3.0-only */
/* Date & Time's Auto: the reply, the zone, and when to ask. See autozone.h. */
#include "autozone.h"

#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "rajson.h"

/* A whole number and nothing else, as JSON writes one: an optional minus and
 * up to six digits (an offset is five at most). A fraction, an exponent, a
 * string or a word is no offset. */
static bool whole_number(jsv v, long *out)
{
	const char *p = v.b, *e = v.e;
	long n = 0;
	int digits = 0;
	bool neg = false;

	while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
	if (p < e && *p == '-') { neg = true; p++; }
	for (; p < e; p++) {
		if (*p < '0' || *p > '9' || ++digits > 6) return false;
		n = n * 10 + (*p - '0');
	}
	if (!digits) return false;
	*out = neg ? -n : n;
	return true;
}

/* A string with its closing quote: one cut short is not read as a name */
static bool whole_string(jsv v, char *out, size_t n)
{
	const char *e = v.e;

	while (e > v.b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
	if (e - v.b < 2 || e[-1] != '"') return false;
	return js_str(v, out, n);
}

bool autozone_parse(const char *body, size_t len, autozone_answer *out)
{
	jsv root, v, tz;
	char id[128];
	long s;

	memset(out, 0, sizeof *out);
	if (!body || !len) return false;
	root = js_root(body, len);
	if (!js_member(root, "success", &v) || !js_is_true(v)) return false;
	if (!js_member(root, "timezone", &tz)) return false;
	/* A name, only one the zone database could have: it goes into TZ */
	if (js_member(tz, "id", &v) && whole_string(v, id, sizeof id) &&
	    strlen(id) < sizeof out->id && clock_zone_name_ok(id))
		snprintf(out->id, sizeof out->id, "%s", id);
	/* Seconds east of UTC, now, daylight saving in it. Every zone there is
	 * is from -12:00 to +14:00, in quarter hours (Kathmandu's +05:45). */
	if (js_member(tz, "offset", &v) && whole_number(v, &s) &&
	    s >= -12 * 3600 && s <= 14 * 3600 && s % 900 == 0) {
		out->offset_s = (int)s;
		out->has_offset = true;
	}
	return out->id[0] || out->has_offset;
}

/* The offsets off the hour, now, and the zone of each: the one most live in,
 * or the only one. Its own rules from then on, which for a zone with
 * daylight saving are right until its next change, as an Etc zone's are. */
static const struct { int s; const char *id; } OFF_HOUR[] = {
	{ -9 * 3600 - 1800, "Pacific/Marquesas" },    /* -09:30 */
	{ -3 * 3600 - 1800, "America/St_Johns" },     /* -03:30, its winter */
	{ -2 * 3600 - 1800, "America/St_Johns" },     /* -02:30, its summer */
	{  3 * 3600 + 1800, "Asia/Tehran" },          /* +03:30 */
	{  4 * 3600 + 1800, "Asia/Kabul" },           /* +04:30 */
	{  5 * 3600 + 1800, "Asia/Kolkata" },         /* +05:30 */
	{  5 * 3600 + 2700, "Asia/Kathmandu" },       /* +05:45 */
	{  6 * 3600 + 1800, "Asia/Yangon" },          /* +06:30 */
	{  8 * 3600 + 2700, "Australia/Eucla" },      /* +08:45 */
	{  9 * 3600 + 1800, "Australia/Darwin" },     /* +09:30 */
	{ 10 * 3600 + 1800, "Australia/Adelaide" },   /* +10:30, its summer */
	{ 12 * 3600 + 2700, "Pacific/Chatham" },      /* +12:45 */
	{ 13 * 3600 + 2700, "Pacific/Chatham" },      /* +13:45, its summer */
};

bool autozone_offset_zone(int offset_s, char *out, size_t n)
{
	size_t i;

	if (!n) return false;
	out[0] = '\0';
	if (offset_s < -12 * 3600 || offset_s > 14 * 3600) return false;
	if (offset_s == 0) { snprintf(out, n, "UTC"); return true; }
	if (offset_s % 3600 == 0) {
		/* Etc/GMT-3 is UTC+3: POSIX's sign, which the database keeps */
		snprintf(out, n, "Etc/GMT%c%d", offset_s > 0 ? '-' : '+',
		         (offset_s > 0 ? offset_s : -offset_s) / 3600);
		return true;
	}
	for (i = 0; i < sizeof OFF_HOUR / sizeof OFF_HOUR[0]; i++)
		if (OFF_HOUR[i].s == offset_s) { snprintf(out, n, "%s", OFF_HOUR[i].id); return true; }
	return false;
}

bool autozone_pick(const autozone_answer *a, bool (*have)(const char *id),
                   char *out, size_t n)
{
	if (!a || !n) return false;
	out[0] = '\0';
	if (a->id[0] && have && have(a->id)) {
		snprintf(out, n, "%s", a->id);
		return true;
	}
	return a->has_offset && autozone_offset_zone(a->offset_s, out, n);
}

void autozone_offset_words(int offset_s, char *out, size_t n)
{
	int a = offset_s < 0 ? -offset_s : offset_s;

	snprintf(out, n, "%c%02d:%02d", offset_s < 0 ? '-' : '+', a / 3600, a % 3600 / 60);
}

/* ---- when to ask --------------------------------------------------------- */

bool autozone_due(autozone_clock *c, bool on, bool online, bool clock_set,
                  unsigned now_ms, long long now_s)
{
	if (!on || !online) {
		/* Auto off forgets its last answer, so choosing it again asks */
		if (!on) c->answered_s = 0;
		c->looked = false;
		return false;
	}
	if (!c->looked) {
		/* A new connection, or Auto just chosen */
		c->looked = true;
		c->tries = 0;
		c->next_ms = now_ms + AUTOZONE_SETTLE_MS;
		/* Answered a few hours ago: the same place, most likely. A clock
		 * set back since is no answer to go by. */
		if (c->answered_s && now_s >= c->answered_s && now_s - c->answered_s < AUTOZONE_EVERY_S)
			c->tries = AUTOZONE_TRIES;
	}
	if (c->asking || c->tries >= AUTOZONE_TRIES || !clock_set) return false;
	if ((int)(now_ms - c->next_ms) < 0) return false;
	c->asking = true;
	return true;
}

void autozone_result(autozone_clock *c, bool ok, unsigned now_ms, long long now_s)
{
	/* after the first, second and third tries */
	static const unsigned again_ms[AUTOZONE_TRIES - 1] = { 30000u, 120000u, 600000u };

	c->asking = false;
	if (ok) {
		c->answered_s = now_s > 0 ? now_s : 1;
		c->tries = AUTOZONE_TRIES;
		return;
	}
	if (c->tries < AUTOZONE_TRIES) c->tries++;
	if (c->tries < AUTOZONE_TRIES) c->next_ms = now_ms + again_ms[c->tries - 1];
}

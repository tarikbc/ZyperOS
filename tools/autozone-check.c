/* SPDX-License-Identifier: GPL-3.0-only */
/* Does Date & Time's Auto read the service's reply, and come to the right
 * zone, as src/autozone.h says?
 *
 *     make check-autozone
 *
 * The reply: in the shape ipwho.is answered in on 2026-11-13, the places
 * made up, with the fields Auto asks for and with all of them; a refusal; a
 * rate limit; a page that is not JSON; a reply cut short; a name that is a
 * path; offsets no zone has. The
 * zone: the name when the system has it, the offset's zone when it does not
 * (Etc/GMT-N, the sign the other way round, and the cities of the offsets off
 * the hour), and none for neither. The row's words for each. And when to ask:
 * once a connection, after the network settles, again on a failure and not
 * past four tries, not again within a few hours, never while the clock is
 * unset, and again when Auto is chosen again.
 *
 * No SDL, no device, no network: the replies are written here, and the zone
 * files are a list this file chooses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/autozone.h"
#include "../src/clock.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* The zone files this "system" has: the list's, and a few off it */
static bool have(const char *id)
{
	static const char *const more[] = { "Asia/Ho_Chi_Minh", "Asia/Kathmandu",
	                                    "America/Argentina/Buenos_Aires", NULL };
	int i;

	if (clock_zone_find(id) >= 0) return true;
	for (i = 0; more[i]; i++)
		if (!strcmp(more[i], id)) return true;
	return false;
}

static bool parse(const char *body, autozone_answer *a)
{
	return autozone_parse(body, strlen(body), a);
}

/* The zone a reply comes to, "" for none */
static const char *zone_of(const char *body)
{
	static char z[64];
	autozone_answer a;

	z[0] = '\0';
	if (!parse(body, &a) || !autozone_pick(&a, have, z, sizeof z)) z[0] = '\0';
	return z;
}

static const char *offset_zone(int s)
{
	static char z[64];

	if (!autozone_offset_zone(s, z, sizeof z)) z[0] = '\0';
	return z;
}

static const char *city(const char *id)
{
	static char c[64];

	clock_zone_city(id, c, sizeof c);
	return c;
}

int main(void)
{
	autozone_answer a;
	char w[16];

	printf("the reply:\n");
	CHECK(parse("{\n    \"success\": true,\n    \"timezone\": {\n        \"id\": \"Asia/Ho_Chi_Minh\",\n"
	            "        \"abbr\": \"+07\",\n        \"is_dst\": false,\n        \"offset\": 25200,\n"
	            "        \"utc\": \"+07:00\"\n    }\n}", &a) &&
	      !strcmp(a.id, "Asia/Ho_Chi_Minh") && a.has_offset && a.offset_s == 25200,
	      "ipwho.is's reply to the fields Auto asks for: the name and the offset");
	CHECK(parse("{\"ip\":\"203.0.113.9\",\"success\":true,\"type\":\"IPv4\",\"continent\":\"South America\","
	            "\"country\":\"Brazil\",\"city\":\"S\\u00e3o Paulo\",\"flag\":{\"img\":\"x.svg\",\"emoji\":\"\"},"
	            "\"connection\":{\"asn\":1,\"org\":\"A, B\"},\"timezone\":{\"id\":\"America/Sao_Paulo\","
	            "\"abbr\":\"-03\",\"is_dst\":false,\"offset\":-10800,\"utc\":\"-03:00\"},\"readme\":\"r\"}", &a) &&
	      !strcmp(a.id, "America/Sao_Paulo") && a.offset_s == -10800,
	      "and to all of them, with objects and escapes before the zone");
	CHECK(parse("{\"success\":true,\"timezone\":{\"offset\":20700,\"id\":\"Asia/Kathmandu\"}}", &a) &&
	      !strcmp(a.id, "Asia/Kathmandu") && a.offset_s == 20700, "the fields in any order");
	CHECK(!parse("{\"ip\":\"999.1.1.1\",\"success\":false,\"message\":\"Invalid IP address\"}", &a),
	      "a refusal is no answer");
	CHECK(!parse("{\"success\":false,\"message\":\"You've hit the monthly limit\",\"timezone\":"
	             "{\"id\":\"UTC\",\"offset\":0}}", &a),
	      "nor a rate limit, whatever else it carries");
	CHECK(!parse("{\"timezone\":{\"id\":\"Europe/Paris\",\"offset\":3600}}", &a),
	      "nor a reply that does not say success");
	CHECK(!parse("<html><head><title>429 Too Many Requests</title></head></html>", &a),
	      "nor a page that is not JSON");
	CHECK(!parse("", &a) && !autozone_parse(NULL, 10, &a), "nor nothing");
	CHECK(!parse("{\"success\":true}", &a), "nor success with no zone");
	CHECK(!parse("{\"success\":true,\"timezone\":\"America/Sao_Paulo\"}", &a),
	      "nor a zone that is not the object");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"id\":\"America/Sao_Pa", &a),
	      "nor a reply cut short in the name");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"id\":\"../../etc/passwd\"}}", &a),
	      "nor a name that is a path");
	CHECK(parse("{\"success\":true,\"timezone\":{\"id\":\":/etc/passwd\",\"offset\":3600}}", &a) &&
	      !a.id[0] && a.offset_s == 3600, "whose offset is kept, the name left out");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"id\":\"<-03>3\"}}", &a), "nor TZ's other forms");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"id\":\"America/Sao_Paulo/Too/Long/For/The/Room/"
	             "Of/Sixty/Four/Bytes/Here\"}}", &a), "nor a name past 63 bytes");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":-43201}}", &a), "an offset off the quarter hour is none");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":54000}}", &a), "nor one past +14:00");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":-46800}}", &a), "nor one before -12:00");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":\"-10800\"}}", &a), "nor one in a string");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":-10800.5}}", &a), "nor a fraction");
	CHECK(!parse("{\"success\":true,\"timezone\":{\"offset\":1e4}}", &a), "nor an exponent");
	CHECK(parse("{\"success\":true,\"timezone\":{\"offset\":50400}}", &a) && a.offset_s == 50400,
	      "+14:00, Kiribati's, is one");

	printf("the zone:\n");
	CHECK(!strcmp(zone_of("{\"success\":true,\"timezone\":{\"id\":\"America/Sao_Paulo\",\"offset\":-10800}}"),
	              "America/Sao_Paulo"), "a zone on the list is set by its name");
	CHECK(!strcmp(zone_of("{\"success\":true,\"timezone\":{\"id\":\"Asia/Ho_Chi_Minh\",\"offset\":25200}}"),
	              "Asia/Ho_Chi_Minh"), "and one off the list that the system has, as it is");
	CHECK(!strcmp(zone_of("{\"success\":true,\"timezone\":{\"id\":\"America/Argentina/Buenos_Aires\"}}"),
	              "America/Argentina/Buenos_Aires"), "with no offset too");
	CHECK(!strcmp(zone_of("{\"success\":true,\"timezone\":{\"id\":\"Mars/Olympus_Mons\",\"offset\":-10800}}"),
	              "Etc/GMT+3"), "a name the system lacks comes to its offset's zone: -03:00 is Etc/GMT+3");
	CHECK(!strcmp(zone_of("{\"success\":true,\"timezone\":{\"offset\":19800}}"), "Asia/Kolkata"),
	      "no name, +05:30, is Kolkata's");
	CHECK(!zone_of("{\"success\":true,\"timezone\":{\"id\":\"Mars/Olympus_Mons\"}}")[0],
	      "a name the system lacks and no offset is no zone: the one there is stays");
	CHECK(!zone_of("{\"success\":true,\"timezone\":{\"offset\":-35100}}")[0],
	      "nor an offset no zone has (-09:45)");
	CHECK(!strcmp(offset_zone(0), "UTC"), "0 is UTC");
	CHECK(!strcmp(offset_zone(9 * 3600), "Etc/GMT-9"), "+09:00 is Etc/GMT-9");
	CHECK(!strcmp(offset_zone(-12 * 3600), "Etc/GMT+12"), "-12:00 is Etc/GMT+12");
	CHECK(!strcmp(offset_zone(14 * 3600), "Etc/GMT-14"), "+14:00 is Etc/GMT-14");
	CHECK(!strcmp(offset_zone(5 * 3600 + 2700), "Asia/Kathmandu"), "+05:45 is Kathmandu's");
	CHECK(!strcmp(offset_zone(-3 * 3600 - 1800), "America/St_Johns"), "-03:30 is St. John's");
	CHECK(!strcmp(offset_zone(12 * 3600 + 2700), "Pacific/Chatham"), "+12:45 is Chatham's");
	CHECK(!offset_zone(15 * 3600)[0] && !offset_zone(-13 * 3600)[0], "past the ends, none");
	{
		/* Every zone the offsets come to is one the zone database has, and
		 * at that offset on some day: a misspelled one would read as UTC */
		static const int off[] = { -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
		                           1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
		static const int half[] = { -34200, -12600, -9000, 12600, 16200, 19800, 20700,
		                            23400, 31500, 34200, 37800, 45900, 49500 };
		char z[64];
		size_t i;

		for (i = 0; i < sizeof off / sizeof off[0] + sizeof half / sizeof half[0]; i++) {
			int s = i < sizeof off / sizeof off[0] ? off[i] * 3600 : half[i - sizeof off / sizeof off[0]];
			bool met = false;
			time_t day;

			CHECK(autozone_offset_zone(s, z, sizeof z) && clock_zone_have(z),
			      "the zone for %+d s, %s, is in this system's zone database", s, z);
			setenv("TZ", z, 1);
			tzset();
			for (day = 1767268800; day < 1767268800 + 366 * 86400; day += 86400) {
				struct tm tm;

				localtime_r(&day, &tm);
				if (tm.tm_gmtoff == s) { met = true; break; }
			}
			CHECK(met, "%s is at %+d s on some day of 2026", z, s);
		}
		unsetenv("TZ");
		tzset();
	}

	printf("the row's words:\n");
	CHECK(!strcmp(city("America/Sao_Paulo"), "Sao Paulo"), "the list's city");
	CHECK(!strcmp(city("Asia/Kolkata"), "India"), "as the list says it");
	CHECK(!strcmp(city("Asia/Ho_Chi_Minh"), "Ho Chi Minh"), "a name off it, its last part with spaces");
	CHECK(!strcmp(city("America/Argentina/Buenos_Aires"), "Buenos Aires"), "however deep");
	CHECK(!strcmp(city("Etc/GMT+3"), "UTC-3"), "Etc/GMT+3 is UTC-3");
	CHECK(!strcmp(city("Etc/GMT-14"), "UTC+14"), "and Etc/GMT-14 UTC+14");
	{
		char v[80];

		clock_zone_value("America/New_York", false, "", v, sizeof v);
		CHECK(!strcmp(v, "New York"), "Auto off: the zone's city");
		clock_zone_value("Europe/Rome", false, "", v, sizeof v);
		CHECK(!strcmp(v, "Europe/Rome"), "or an id not on the list as it is");
		clock_zone_value("America/New_York", true, "", v, sizeof v);
		CHECK(!strcmp(v, "Auto"), "Auto chosen, nothing found yet: \"%s\"", v);
		clock_zone_value("Asia/Ho_Chi_Minh", true, "Asia/Ho_Chi_Minh", v, sizeof v);
		CHECK(!strcmp(v, "Auto (Ho Chi Minh)"), "Auto with the zone it found: \"%s\"", v);
		clock_zone_value("America/New_York", true, "America/Sao_Paulo", v, sizeof v);
		CHECK(!strcmp(v, "Auto"), "and Auto alone when the zone in use is not the one it found");
	}
	autozone_offset_words(-10800, w, sizeof w);
	CHECK(!strcmp(w, "-03:00"), "the log's offset, \"%s\"", w);
	autozone_offset_words(20700, w, sizeof w);
	CHECK(!strcmp(w, "+05:45"), "and \"%s\"", w);
	autozone_offset_words(0, w, sizeof w);
	CHECK(!strcmp(w, "+00:00"), "and \"%s\"", w);

	printf("when to ask:\n");
	{
		autozone_clock c;
		const long long day = 1794500000;   /* 2026-11-13, a set clock */
		unsigned t = 100000;
		int i, asks;

		memset(&c, 0, sizeof c);
		CHECK(!autozone_due(&c, false, true, true, t, day), "Auto off never asks");
		CHECK(!autozone_due(&c, true, false, true, t, day), "nor Auto with no way out");
		CHECK(!autozone_due(&c, true, true, true, t, day), "a new connection waits for the network to settle");
		CHECK(!autozone_due(&c, true, true, true, t + AUTOZONE_SETTLE_MS - 1, day), "for its whole wait");
		CHECK(autozone_due(&c, true, true, true, t + AUTOZONE_SETTLE_MS, day), "and then asks");
		CHECK(!autozone_due(&c, true, true, true, t + AUTOZONE_SETTLE_MS + 2000, day),
		      "one ask out at a time");
		autozone_result(&c, true, t + 6000, day);
		CHECK(!autozone_due(&c, true, true, true, t + 3600000u, day + 3600),
		      "an answer ends the connection's asking");
		/* dropped and back an hour on: the same place */
		CHECK(!autozone_due(&c, true, false, true, t + 3700000u, day + 3700), "offline");
		for (i = 0, asks = 0; i < 100; i++)
			asks += autozone_due(&c, true, true, true, t + 3800000u + (unsigned)i * 2000u, day + 3800 + i * 2);
		CHECK(!asks, "a new connection within a few hours of the answer does not ask");
		CHECK(!autozone_due(&c, true, false, true, t + 4000000u, day + 4000), "offline again");
		t += 4000000u;
		CHECK(!autozone_due(&c, true, true, true, t, day + AUTOZONE_EVERY_S + 10), "");
		CHECK(autozone_due(&c, true, true, true, t + AUTOZONE_SETTLE_MS, day + AUTOZONE_EVERY_S + 15),
		      "one past them asks");
		/* it comes to nothing: again 30 s, 2 min and 10 min on, and no more */
		{
			const long long s = day + AUTOZONE_EVERY_S + 16;

			autozone_result(&c, false, t + 6000, s);
			CHECK(!autozone_due(&c, true, true, true, t + 6000 + 29999, s + 30), "a failure waits 30 s");
			CHECK(autozone_due(&c, true, true, true, t + 6000 + 30000, s + 30), "then tries again");
			autozone_result(&c, false, t + 40000, s + 34);
			CHECK(!autozone_due(&c, true, true, true, t + 40000 + 119999, s + 154), "then 2 min");
			CHECK(autozone_due(&c, true, true, true, t + 40000 + 120000, s + 154), "");
			autozone_result(&c, false, t + 170000, s + 164);
			CHECK(!autozone_due(&c, true, true, true, t + 170000 + 599999, s + 764), "then 10 min");
			CHECK(autozone_due(&c, true, true, true, t + 170000 + 600000, s + 764), "");
			autozone_result(&c, false, t + 780000, s + 774);
			for (i = 0, asks = 0; i < 2000; i++)
				asks += autozone_due(&c, true, true, true, t + 780000 + (unsigned)i * 5000u, s + 774 + i * 5);
			CHECK(!asks, "and after four tries, not on this connection");
			CHECK(!autozone_due(&c, true, false, true, t + 20000000u, s + 20000), "");
			CHECK(!autozone_due(&c, true, true, true, t + 20000001u, s + 20001), "");
			CHECK(autozone_due(&c, true, true, true, t + 20000001u + AUTOZONE_SETTLE_MS, s + 20006),
			      "the next connection asks again");
			autozone_result(&c, true, t + 20010000u, s + 20010);
		}

		/* Auto chosen again asks again, answered or not */
		CHECK(!autozone_due(&c, false, true, true, t + 20020000u, day + 10), "Auto off");
		CHECK(!autozone_due(&c, true, true, true, t + 20022000u, day + 12), "");
		CHECK(autozone_due(&c, true, true, true, t + 20022000u + AUTOZONE_SETTLE_MS, day + 17),
		      "Auto chosen again asks, a minute after an answer");
		autozone_result(&c, true, t + 20030000u, day + 20);

		/* an unset clock: HTTPS cannot check a certificate in 2018 */
		memset(&c, 0, sizeof c);
		t = 5000;
		CHECK(!autozone_due(&c, true, true, false, t, 1536000000), "");
		for (i = 0, asks = 0; i < 20; i++)
			asks += autozone_due(&c, true, true, false, t + (unsigned)i * 2000u, 1536000000 + i * 2);
		CHECK(!asks, "no ask while the clock is unset");
		CHECK(autozone_due(&c, true, true, true, t + 42000, day),
		      "and one once it is set, with no try spent");

		/* a clock set back past the answer is no answer to go by */
		memset(&c, 0, sizeof c);
		CHECK(!autozone_due(&c, true, true, true, 0, day), "");
		CHECK(autozone_due(&c, true, true, true, AUTOZONE_SETTLE_MS, day), "");
		autozone_result(&c, true, 6000, day);
		CHECK(!autozone_due(&c, true, false, true, 8000, day - 86400), "");
		CHECK(!autozone_due(&c, true, true, true, 10000, day - 86400), "");
		CHECK(autozone_due(&c, true, true, true, 10000 + AUTOZONE_SETTLE_MS, day - 86400),
		      "a clock set back since the answer asks again");

		/* across plat_now_ms's wrap */
		memset(&c, 0, sizeof c);
		CHECK(!autozone_due(&c, true, true, true, 0xFFFFF000u, day), "");
		CHECK(!autozone_due(&c, true, true, true, 0xFFFFFFF0u, day), "");
		CHECK(autozone_due(&c, true, true, true, 0xFFFFF000u + AUTOZONE_SETTLE_MS, day),
		      "the settle wait holds across the clock's wrap");
	}

	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}

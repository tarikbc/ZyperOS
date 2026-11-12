/* What an unlock's refusal means for the queue (src/rafetch.h, ra_refusal):
 * which refusals settle the row, which are the account's and stop the pass,
 * and which keep the one row owed while the rest are sent. A wrong answer here
 * is silent on the device - an unlock never sent, or every later one stuck
 * behind one RetroAchievements will never take - so it is held here.
 *
 * Links src/rafetch.c and what it needs; makes no request. */
#include "../src/rafetch.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

int main(void)
{
	printf("refusals:\n");
	ck(ra_refusal("User already has this achievement unlocked.", "") == RA_SUBMIT_SETTLED,
	   "already held settles the row");
	ck(ra_refusal("Unofficial achievements cannot be unlocked", "") == RA_SUBMIT_SETTLED,
	   "an achievement demoted to unofficial settles it: refused for good");
	ck(ra_refusal("Data not found for achievement 12345", "not_found") == RA_SUBMIT_SETTLED,
	   "and so does one that is not there any more");
	ck(ra_refusal("", "not_found") == RA_SUBMIT_SETTLED, "by its code alone too");
	ck(ra_refusal("Credentials invalid (0)", "invalid_credentials") == RA_SUBMIT_FAILED,
	   "bad credentials are the account's: stop, all still owed");
	ck(ra_refusal("Invalid token", "") == RA_SUBMIT_FAILED, "an expired token too");
	ck(ra_refusal("You must verify your email address", "") == RA_SUBMIT_FAILED,
	   "and an account not yet confirmed");
	ck(ra_refusal("Something nobody has seen before", "") == RA_SUBMIT_REFUSED,
	   "anything else keeps the row owed and lets the rest go");
	ck(ra_refusal(NULL, NULL) == RA_SUBMIT_REFUSED, "no reason given at all, the same");
	/* RAWeb's own words, app/Connect/Actions/AwardAchievementsAction.php and
	 * BaseApiAction.php: a demoted achievement and a deleted one are
	 * refused for good. An unlock that waited offline meets both more often
	 * than one sent at once, since the set had longer to change. */
	ck(ra_refusal("Unpromoted achievements cannot be unlocked.", "invalid_state") == RA_SUBMIT_SETTLED,
	   "an unpromoted achievement settles the row");
	ck(ra_refusal("Unknown achievement.", "not_found") == RA_SUBMIT_SETTLED,
	   "and so does one RetroAchievements does not know");

	printf("an unlock's age and signature:\n");
	/* As rcheevos's rc_api_init_award_achievement_request builds it and RAWeb
	 * checks it; the md5 sums are Python's hashlib over the same strings */
	{
		char v[33];

		ra_award_signature(12345, "tester", false, 0, v);
		ck(!strcmp(v, "061a6263c5338df66b909c98a9863fa8"), "no offset: md5(id user h)");
		ra_award_signature(12345, "tester", false, 3600, v);
		ck(!strcmp(v, "2e40455ac53c922faf780720a27f5e70"), "an offset: md5(id user h id offset)");
		ra_award_signature(12345, "tester", true, 0, v);
		ck(!strcmp(v, "15d3e193d7cd8a046d1652ef4336667d"), "hardcore signs h=1");
		ra_award_signature(12345, "tester", true, 1209600, v);
		ck(!strcmp(v, "38189a0843dc11c443bc52948cda9695"), "hardcore with the longest offset");
	}
	ck(ra_unlock_offset(0, 1800000000L) == 0, "an unlock of unknown age is sent with none");
	ck(ra_unlock_offset(1514764800L, 1514768400L) == 0,
	   "nor one from a clock never set (2018, the Pixel 2's)");
	ck(ra_unlock_offset(1800003600L, 1800000000L) == 0, "nor one from a clock that went back");
	ck(ra_unlock_offset(1800000000L, 1800003600L) == 3600, "an hour ago is 3600");
	ck(ra_unlock_offset(1800000000L, 1800000000L + 40L * 86400) == RA_OFFSET_MAX,
	   "40 days ago is the server's 14, sent and signed as 14");
	if (fails) { printf("rasubmit-check: %d FAILED\n", fails); return 1; }
	printf("rasubmit-check: ok\n");
	return 0;
}

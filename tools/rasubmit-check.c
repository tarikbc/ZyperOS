/* What an unlock's refusal means for the queue (src/rafetch.h, ra_refusal):
 * which refusals settle the row, which are the account's and stop the pass,
 * and which keep the one row owed while the rest are sent. A wrong answer here
 * is silent on the device - an unlock never sent, or every later one stuck
 * behind one RetroAchievements will never take - so it is held here.
 *
 * Links src/rafetch.c and what it needs; makes no request. */
#include "../src/rafetch.h"

#include <stdio.h>

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
	if (fails) { printf("rasubmit-check: %d FAILED\n", fails); return 1; }
	printf("rasubmit-check: ok\n");
	return 0;
}

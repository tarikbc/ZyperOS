/* SPDX-License-Identifier: GPL-3.0-only */
/* See phonelink.h. */
#include <stdio.h>
#include <string.h>

#include "phonelink.h"

bool phonelink_driver(const char *driver)
{
	static const char *const OURS[] = { "rndis_host", "cdc_ncm", "cdc_ether", "ipheth" };
	size_t i;

	if (!driver) return false;
	for (i = 0; i < sizeof OURS / sizeof OURS[0]; i++)
		if (!strcmp(driver, OURS[i])) return true;
	return false;
}

int phonelink_pick(const char *const *names, const char *const *drivers, int n)
{
	int i, first = -1;

	for (i = 0; i < n; i++) {
		if (!names[i] || !phonelink_driver(drivers[i])) continue;
		if (!strcmp(names[i], PHONELINK_IF)) return i;
		if (first < 0) first = i;
	}
	return first;
}

/* The length of s with its trailing white space (sysfs ends each file in a
 * newline) left out */
static size_t trimmed_len(const char *s)
{
	size_t n = strlen(s);

	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	return n;
}

/* s, trailing white space left out, is word */
static bool same_word(const char *s, const char *word)
{
	size_t n = trimmed_len(s);

	return n == strlen(word) && !strncmp(s, word, n);
}

bool phonelink_is_iphone(const char *vendor, const char *product)
{
	return vendor && same_word(vendor, "05ac") && product && trimmed_len(product) &&
	       !same_word(product, "Mac");
}

bool phonelink_new(char last[64], const char *ifname, const char *ip)
{
	char now[64];

	if (!ifname || !ifname[0] || !ip || !ip[0]) {
		last[0] = '\0';
		return false;
	}
	snprintf(now, sizeof now, "%s %s", ifname, ip);
	if (!strcmp(now, last)) return false;
	snprintf(last, 64, "%s", now);
	return true;
}

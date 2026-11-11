/* The CPU's minimum clock, for diatom_port_fast_clock in both Linux ports.
 *
 * Raising the minimum to the maximum holds the top clock under whatever
 * governor is running, without replacing it, and writing the hardware
 * minimum back hands the clock to the governor again. Nothing else on either
 * device sets the minimum, so the hardware's is the one to restore. Policy 0
 * covers every core on both: the Brick's A133 and the Pixel 2's PX30 clock
 * their four cores together.
 *
 * The last state written is kept, so the host can say it every turn and only
 * a change reaches the kernel; the first call always writes, which is how a
 * hold left by a Diatom that crashed is cleared.
 *
 * A device whose device tree has turbo-mode steps (the Pixel 2's 1416 and
 * 1512 MHz) has a third state, for fast forward alone: cpufreq boost turned
 * on, which adds those steps, and the clock held at the top of them
 * (diatom_cpufreq_hold). Turning boost off does not take the top clock back
 * down: the kernel only ever raises cpuinfo_max_freq
 * (cpufreq_frequency_table_cpuinfo in 7.1), so scaling_max_freq would stay
 * at 1512 MHz and ondemand go on using it at 1x. So leaving boost sets
 * scaling_max_freq to the highest step outside it, and the other holds read
 * that step rather than cpuinfo_max_freq. After diatom 7649d4e, by Eric
 * Reinsmidt, whose helpers these are. */
#ifndef DIATOM_PORT_CPUFREQ_H
#define DIATOM_PORT_CPUFREQ_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* A check points these at a folder of its own (test/cpufreq_test.c) */
#ifndef DIATOM_CPUFREQ
#define DIATOM_CPUFREQ "/sys/devices/system/cpu/cpu0/cpufreq/"
#endif
#ifndef DIATOM_CPUFREQ_BOOST
#define DIATOM_CPUFREQ_BOOST "/sys/devices/system/cpu/cpufreq/boost"
#endif

static inline bool diatom_cpufreq_write(const char *to, const char *v)
{
	FILE *f = fopen(to, "w");
	bool ok;

	if (!f) return false;
	ok = fputs(v, f) >= 0;
	return fclose(f) == 0 && ok;
}

static inline bool diatom_cpufreq_copy(const char *from, const char *to)
{
	char v[32];
	FILE *f = fopen(from, "r");
	bool ok;

	if (!f) return false;
	ok = fgets(v, sizeof v, f) != NULL;
	fclose(f);
	return ok && diatom_cpufreq_write(to, v);
}

/* The highest step outside boost, in kHz, as text: the largest in
 * scaling_available_frequencies, which leaves the boost steps out. */
static inline bool diatom_cpufreq_unboosted_top(char *v, size_t n)
{
	char list[256];
	unsigned long top = 0;
	char *p, *end;
	FILE *f = fopen(DIATOM_CPUFREQ "scaling_available_frequencies", "r");
	bool ok;

	if (!f) return false;
	ok = fgets(list, sizeof list, f) != NULL;
	fclose(f);
	if (!ok) return false;
	for (p = list;; p = end) {
		unsigned long khz = strtoul(p, &end, 10);

		if (end == p) break;
		if (khz > top) top = khz;
	}
	if (!top) return false;
	snprintf(v, n, "%lu", top);
	return true;
}

/* scaling_max_freq at the highest step outside boost: the ceiling with
 * nothing capping it, whether or not boost was ever on. */
static inline bool diatom_cpufreq_cap_unboosted(void)
{
	char v[32];

	return diatom_cpufreq_unboosted_top(v, sizeof v) &&
	       diatom_cpufreq_write(DIATOM_CPUFREQ "scaling_max_freq", v);
}

/* False when the write failed, which leaves the clock as it was. */
static inline bool diatom_cpufreq_hold_top(bool on)
{
	static int held = -1;

	if (held == (int)on) return true;
	held = on;
	return diatom_cpufreq_copy(on ? DIATOM_CPUFREQ "cpuinfo_max_freq"
	                              : DIATOM_CPUFREQ "cpuinfo_min_freq",
	                           DIATOM_CPUFREQ "scaling_min_freq");
}

/* For a device with boost steps: DIATOM_HOLD_NONE gives the clock back to
 * the governor, under the highest step outside boost; DIATOM_HOLD_TOP holds
 * that step; DIATOM_HOLD_BOOST turns boost on and holds the top of it. The
 * minimum always moves down before the maximum does, so it is never asked to
 * be above it. False when a write failed, which may leave the clock where it
 * was; with no boost to turn on, BOOST holds what top there is. */
enum { DIATOM_HOLD_NONE, DIATOM_HOLD_TOP, DIATOM_HOLD_BOOST };

static inline bool diatom_cpufreq_hold(int want)
{
	static int held = -1;
	char top[32];
	bool ok;

	if (held == want) return true;
	if (want == DIATOM_HOLD_BOOST) {
		held = want;
		ok = diatom_cpufreq_write(DIATOM_CPUFREQ_BOOST, "1");
		ok = diatom_cpufreq_copy(DIATOM_CPUFREQ "cpuinfo_max_freq",
		                         DIATOM_CPUFREQ "scaling_max_freq") && ok;
		return diatom_cpufreq_copy(DIATOM_CPUFREQ "cpuinfo_max_freq",
		                           DIATOM_CPUFREQ "scaling_min_freq") && ok;
	}
	/* Out of boost, or out of a hold a crashed Diatom may have left at it:
	 * the minimum down, boost off, the ceiling to the top outside it. A hold
	 * that was never boosted leaves the ceiling alone, which
	 * diatom_port_power may have lowered. */
	ok = diatom_cpufreq_copy(DIATOM_CPUFREQ "cpuinfo_min_freq",
	                         DIATOM_CPUFREQ "scaling_min_freq");
	if (held == DIATOM_HOLD_BOOST || held < 0) {
		diatom_cpufreq_write(DIATOM_CPUFREQ_BOOST, "0");
		ok = diatom_cpufreq_cap_unboosted() && ok;
	}
	held = want;
	if (want == DIATOM_HOLD_TOP)
		ok = diatom_cpufreq_unboosted_top(top, sizeof top) &&
		     diatom_cpufreq_write(DIATOM_CPUFREQ "scaling_min_freq", top) && ok;
	return ok;
}

#endif /* DIATOM_PORT_CPUFREQ_H */

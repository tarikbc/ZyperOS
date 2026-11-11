/* The CPU clock holds of port/port_cpufreq.h, against a folder standing in
 * for cpufreq's sysfs (make check-cpufreq).
 *
 * The Pixel 2's fast forward turns cpufreq boost on and holds the top of its
 * steps, 1512 MHz; a hold at 1x (a GPU core, a DS game) holds 1296, the top
 * outside boost; and leaving boost must take the ceiling back to 1296,
 * because the kernel leaves cpuinfo_max_freq at 1512 once boost has added
 * it, and ondemand would go on using 1512 at 1x. The folder holds what the
 * kernel shows after boost has once been on, cpuinfo_max_freq 1512, so a
 * hold that read it outside boost would be caught. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The header pastes these onto its file names, so they are literals:
 * relative, in a folder of this process's own that main moves into */
#define DIATOM_CPUFREQ "cpu0/"
#define DIATOM_CPUFREQ_BOOST "boost"
#include "../port/port_cpufreq.h"

static int fails;

static void put(const char *name, const char *v)
{
	char p[160];
	FILE *f;

	snprintf(p, sizeof p, "%s%s", DIATOM_CPUFREQ, name);
	if (!(f = fopen(p, "w"))) { perror(p); exit(1); }
	fputs(v, f);
	fclose(f);
}

static long get(const char *path)
{
	char v[64] = "";
	FILE *f = fopen(path, "r");

	if (!f) return -1;
	if (!fgets(v, sizeof v, f)) v[0] = '\0';
	fclose(f);
	return strtol(v, NULL, 10);
}

#define MIN()   get(DIATOM_CPUFREQ "scaling_min_freq")
#define MAX()   get(DIATOM_CPUFREQ "scaling_max_freq")
#define BOOST() get(DIATOM_CPUFREQ_BOOST)

static void expect(const char *what, long min, long max, long boost)
{
	bool ok = MIN() == min && MAX() == max && (boost < 0 || BOOST() == boost);

	printf("  %s %s: min %ld max %ld boost %ld\n", ok ? "ok  " : "FAIL", what, MIN(), MAX(), BOOST());
	if (!ok) {
		printf("       want min %ld max %ld boost %ld\n", min, max, boost);
		fails++;
	}
}

int main(void)
{
	char dir[] = "/tmp/diatom-cpufreq-XXXXXX";

	if (!mkdtemp(dir) || chdir(dir) != 0 || mkdir("cpu0", 0755) != 0) {
		perror("cpufreq-test: a folder of its own");
		return 1;
	}
	/* The Pixel 2 after a boost that a crashed Diatom left on, holding 1512 */
	put("cpuinfo_min_freq", "408000\n");
	put("cpuinfo_max_freq", "1512000\n");
	put("scaling_available_frequencies", "408000 600000 816000 1008000 1200000 1248000 1296000 \n");
	put("scaling_min_freq", "1512000\n");
	put("scaling_max_freq", "1512000\n");
	{
		FILE *f = fopen(DIATOM_CPUFREQ_BOOST, "w");

		fputs("1\n", f);
		fclose(f);
	}

	printf("the Pixel 2's CPU holds:\n");
	diatom_cpufreq_hold(DIATOM_HOLD_NONE);
	expect("the first call clears a crashed boost hold", 408000, 1296000, 0);
	diatom_cpufreq_hold(DIATOM_HOLD_TOP);
	expect("1x hold (GPU core, DS game) at the top outside boost", 1296000, 1296000, 0);
	diatom_cpufreq_hold(DIATOM_HOLD_BOOST);
	expect("fast forward: boost on, held at 1512", 1512000, 1512000, 1);
	diatom_cpufreq_hold(DIATOM_HOLD_BOOST);
	expect("said again, nothing changes", 1512000, 1512000, 1);
	diatom_cpufreq_hold(DIATOM_HOLD_TOP);
	expect("back to 1x with a hold: boost off, 1296 again", 1296000, 1296000, 0);
	diatom_cpufreq_hold(DIATOM_HOLD_BOOST);
	diatom_cpufreq_hold(DIATOM_HOLD_NONE);
	expect("fast forward let go: the governor's, under 1296", 408000, 1296000, 0);
	/* A power ceiling (diatom_port_power's MEDIUM) outlives a hold that was
	 * never boosted */
	put("scaling_max_freq", "1008000\n");
	diatom_cpufreq_hold(DIATOM_HOLD_TOP);
	diatom_cpufreq_hold(DIATOM_HOLD_NONE);
	expect("a 1x hold let go keeps a lower ceiling", 408000, 1008000, 0);
	put("scaling_max_freq", "1512000\n");
	diatom_cpufreq_cap_unboosted();
	expect("FULL power's ceiling is the top outside boost", 408000, 1296000, 0);

	{
		static const char *const files[] = {
			"cpu0/cpuinfo_min_freq", "cpu0/cpuinfo_max_freq",
			"cpu0/scaling_available_frequencies", "cpu0/scaling_min_freq",
			"cpu0/scaling_max_freq", "boost",
		};
		size_t i;

		for (i = 0; i < sizeof files / sizeof *files; i++) unlink(files[i]);
		rmdir("cpu0");
		if (chdir("/") == 0) rmdir(dir);
	}
	if (fails) printf("%d FAILED\n", fails);
	else printf("ok: fast forward at 1512, never at 1x\n");
	return fails != 0;
}

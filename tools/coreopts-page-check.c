/* SPDX-License-Identifier: GPL-3.0-only */
/* Core Settings helpers (src/coreopt_defs.h): OPTION line parse, whitelist
 * empty = show all, value cycle, and the library key shape. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/coreopt_defs.h"

static int fails, checks;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

/* A small stand-in set of defs (was the blueMSX seed; now handmade so the
 * helpers still have something to filter and cycle). `out` must be zeroed
 * or already cleared. */
static void sample_defs(codefs *out)
{
	codefs_clear(out);
	snprintf(out->opt[0].key, sizeof out->opt[0].key, "bluemsx_msxtype");
	snprintf(out->opt[0].desc, sizeof out->opt[0].desc, "Machine Type (Restart)");
	snprintf(out->opt[0].deflt, sizeof out->opt[0].deflt, "Auto");
	snprintf(out->opt[0].value, sizeof out->opt[0].value, "Auto");
	codef_set_values(&out->opt[0], "Auto|MSX|MSXturboR|MSX2");
	snprintf(out->opt[1].key, sizeof out->opt[1].key, "bluemsx_cartmapper");
	snprintf(out->opt[1].desc, sizeof out->opt[1].desc, "Cart Mapper Type (Restart)");
	snprintf(out->opt[1].deflt, sizeof out->opt[1].deflt, "Auto");
	snprintf(out->opt[1].value, sizeof out->opt[1].value, "Auto");
	codef_set_values(&out->opt[1], "Auto|Normal|konami4");
	snprintf(out->opt[2].key, sizeof out->opt[2].key, "bluemsx_additional_cart");
	snprintf(out->opt[2].desc, sizeof out->opt[2].desc,
	         "Additional MSX Sound Cartridge (Restart)");
	snprintf(out->opt[2].deflt, sizeof out->opt[2].deflt, "disabled");
	snprintf(out->opt[2].value, sizeof out->opt[2].value, "disabled");
	codef_set_values(&out->opt[2], "disabled|scc|sccexpanded|sccplus");
	out->n = 3;
}

static void parse_and_key(void)
{
	codef o;
	char key[80];

	printf("OPTION lines and db keys:\n");
	memset(&o, 0, sizeof o);
	ck(codef_parse_line(
	       "OPTION\tkey=bluemsx_additional_cart\tvalue=scc\tdefault=disabled\t"
	       "values=disabled|scc|sccplus\tdesc=Additional MSX Sound Cartridge (Restart)",
	       &o),
	   "a full OPTION line parses");
	ck(!strcmp(o.key, "bluemsx_additional_cart") && !strcmp(o.value, "scc") &&
	   !strcmp(o.deflt, "disabled") && o.nvals == 3 && !strcmp(o.vals[1], "scc") &&
	   strstr(o.desc, "Restart"),
	   "key, value, default, values and desc");
	codef_clear(&o);
	ck(!codef_parse_line("OPTIONS\tcount=2", &o), "OPTIONS count is not an OPTION");
	codef_db_key("MSX2", "bluemsx_additional_cart", key, sizeof key);
	ck(!strcmp(key, "coreopt.MSX2.bluemsx_additional_cart"),
	   "db key is coreopt.<TAG>.<option>");
}

static void seed_and_cycle(void)
{
	codefs s = {0};
	int i;

	printf("no baked-in seed; value cycle:\n");
	ck(!codefs_seed(&s, "bluemsx") && s.n == 0, "bluemsx has no seed");
	ck(!codefs_seed(&s, "gambatte"), "another core has no seed");
	sample_defs(&s);
	ck(codef_value_index(&s.opt[0], "MSXturboR") >= 0 &&
	   codef_value_index(&s.opt[2], "disabled") >= 0 &&
	   codef_value_index(&s.opt[2], "scc") >= 0 &&
	   codef_value_index(&s.opt[2], "sccplus") >= 0 &&
	   codef_value_index(&s.opt[1], "Auto") >= 0,
	   "MSXturboR, disabled, scc, sccplus and Auto are among the values");
	i = codef_value_index(&s.opt[2], "scc");
	ck(!strcmp(s.opt[2].vals[codef_value_step(&s.opt[2], i, 1)], "sccexpanded") ||
	   codef_value_step(&s.opt[2], i, 1) != i,
	   "stepping moves to another value");
	/* A value longer than CODEFS_VAL_MAX: truncated, and the next field
	 * is still the next field (not the cut tail). */
	{
		char longv[CODEFS_VAL_MAX + 40], pipe[CODEFS_PIPE_MAX];
		codef o;
		size_t k;

		for (k = 0; k < sizeof longv - 1; k++) longv[k] = 'a' + (char)(k % 26);
		longv[sizeof longv - 1] = '\0';
		snprintf(pipe, sizeof pipe, "%s|short", longv);
		memset(&o, 0, sizeof o);
		codef_set_values(&o, pipe);
		ck(o.nvals == 2 && o.vals[0] && strlen(o.vals[0]) == CODEFS_VAL_MAX - 1 &&
		   o.vals[1] && !strcmp(o.vals[1], "short"),
		   "a long value is cut; the next stays intact");
		codef_clear(&o);
	}
	codefs_clear(&s);
}

static void whitelist(void)
{
	char path[] = "/tmp/zyperos-coreopts-showXXXXXX";
	char allow[CODEFS_ALLOW_MAX][CODEFS_KEY_MAX];
	codefs all = {0};
	int fd, n, kept;
	FILE *f;

	printf("whitelist:\n");
	fd = mkstemp(path);
	ck(fd >= 0, "a temp show file");
	if (fd < 0) return;
	close(fd);
	f = fopen(path, "w");
	fprintf(f, "# comment\ncore=bluemsx\nkey=bluemsx_msxtype\n"
	           "key=bluemsx_cartmapper\nkey=bluemsx_additional_cart\n"
	           "core=gambatte\n");
	fclose(f);

	sample_defs(&all);
	if (all.n < CODEFS_MAX) {
		codef *o = &all.opt[all.n++];

		memset(o, 0, sizeof *o);
		snprintf(o->key, sizeof o->key, "bluemsx_other");
		codef_set_values(o, "a|b");
	}
	n = codefs_load_allow(path, "bluemsx", allow, CODEFS_ALLOW_MAX);
	ck(n == 3 && !strcmp(allow[0], "bluemsx_msxtype") &&
	   !strcmp(allow[1], "bluemsx_cartmapper") &&
	   !strcmp(allow[2], "bluemsx_additional_cart"),
	   "three keys under bluemsx");
	kept = all.n;
	codefs_filter(&all, allow, n);
	ck(all.n == 3 && all.n < kept && !strcmp(all.opt[0].key, "bluemsx_msxtype") &&
	   !strcmp(all.opt[2].key, "bluemsx_additional_cart"),
	   "filter keeps only the listed keys");

	n = codefs_load_allow(path, "gambatte", allow, CODEFS_ALLOW_MAX);
	ck(n == 0, "a core with no key= lines allows all");
	kept = all.n;
	codefs_filter(&all, allow, n);
	ck(all.n == kept, "empty allow list passes every definition through");

	n = codefs_load_allow("/no/such/coreopts-show.cfg", "bluemsx", allow, CODEFS_ALLOW_MAX);
	ck(n == 0, "a missing file allows all");
	unlink(path);
	codefs_clear(&all);
}

static void cache_roundtrip(void)
{
	char path[] = "/tmp/zyperos-coreopt-defsXXXXXX";
	char out[256];
	codefs a = {0}, b = {0};
	int fd;

	printf("cache file:\n");
	fd = mkstemp(path);
	ck(fd >= 0, "a temp cache file");
	if (fd < 0) return;
	close(fd);
	sample_defs(&a);
	ck(codefs_save_file(&a, path) && codefs_load_file(&b, path) && b.n == a.n &&
	   !strcmp(b.opt[0].key, a.opt[0].key) &&
	   b.opt[1].nvals == a.opt[1].nvals &&
	   !strcmp(b.opt[1].vals[0], a.opt[1].vals[0]) &&
	   !strcmp(b.opt[1].vals[a.opt[1].nvals - 1], a.opt[1].vals[a.opt[1].nvals - 1]),
	   "save and load keep keys and values");
	/* A description with a tab and a newline must round-trip as one field. */
	codefs_clear(&a);
	snprintf(a.opt[0].key, sizeof a.opt[0].key, "opt_tabby");
	snprintf(a.opt[0].desc, sizeof a.opt[0].desc, "one\ttwo\nthree");
	snprintf(a.opt[0].deflt, sizeof a.opt[0].deflt, "a");
	snprintf(a.opt[0].value, sizeof a.opt[0].value, "a");
	codef_set_values(&a.opt[0], "a|b");
	a.n = 1;
	ck(codefs_save_file(&a, path) && codefs_load_file(&b, path) && b.n == 1 &&
	   !strcmp(b.opt[0].key, "opt_tabby") &&
	   !strcmp(b.opt[0].desc, "one\ttwo\nthree"),
	   "a desc with tab and newline survives the cache");
	unlink(path);
	codefs_clear(&a);
	codefs_clear(&b);

	codefs_cache_path("/tmp/ud", "fceumm", out, sizeof out);
	ck(!strcmp(out, "/tmp/ud/coreopt-defs/fceumm.txt"),
	   "a plain core name builds the cache path");
	codefs_cache_path("/tmp/ud", "run:Emulators/Flycast/Flycast.sh", out, sizeof out);
	ck(!out[0], "a core with / leaves the path empty");
	codefs_cache_path("/tmp/ud", "../etc", out, sizeof out);
	ck(!out[0], "a core with .. leaves the path empty");
	codefs_cache_path("/tmp/ud", ".hidden", out, sizeof out);
	ck(!out[0], "a core starting with . leaves the path empty");
}

int main(void)
{
	parse_and_key();
	seed_and_cycle();
	whitelist();
	cache_roundtrip();
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}

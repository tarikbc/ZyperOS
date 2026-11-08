/* SPDX-License-Identifier: GPL-3.0-only */
/* A Nintendo DS game's resume, by the language it was left in. See
 * src/dsresume.h. */
#include "dsresume.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "atomic.h"

/* The user settings' language field, 0 to 6, by its word: the words of the
 * core's dsperate_language (mk/patches/dsperate-lr-language.patch) */
static const char *const WORDS[7] = {
	"japanese", "english", "french", "german", "italian", "spanish", "chinese",
};

/* A DS firmware is 256 KB; DSperate's direct boot copies the user settings
 * only from one that size or more */
#define FW_MIN      0x40000
#define FW_USER_PTR 0x20     /* where the settings are, in 8-byte units */
#define USER_SIZE   0x100    /* each of the two copies */
#define USER_LANG   0x64     /* the language, in the low 3 bits */
#define USER_COUNT  0x70     /* the copy's update counter, 0 to 0x7F */

static const char *word_of(const char *w)
{
	int i;

	if (!w) return NULL;
	for (i = 0; i < 7; i++)
		if (!strcmp(w, WORDS[i])) return WORDS[i];
	return NULL;
}

const char *dsr_firmware_language(const char *firmware_path)
{
	unsigned char head[FW_USER_PTR + 2], u[2][USER_COUNT + 2];
	unsigned base;
	long size;
	int c0, c1, k;
	FILE *f;

	if (!firmware_path || !(f = fopen(firmware_path, "rb"))) return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < FW_MIN ||
	    fseek(f, 0, SEEK_SET) != 0 || fread(head, 1, sizeof head, f) != sizeof head) {
		fclose(f);
		return NULL;
	}
	base = (unsigned)(head[FW_USER_PTR] | head[FW_USER_PTR + 1] << 8) << 3;
	if (base == 0 || (long)base + 2 * USER_SIZE > size) {
		fclose(f);
		return NULL;
	}
	for (k = 0; k < 2; k++)
		if (fseek(f, (long)base + k * USER_SIZE, SEEK_SET) != 0 ||
		    fread(u[k], 1, sizeof u[k], f) != sizeof u[k]) {
			fclose(f);
			return NULL;
		}
	fclose(f);
	/* The copy DSperate's setup_direct_boot takes: the second only when its
	 * counter is one past the first's */
	c0 = (u[0][USER_COUNT] | u[0][USER_COUNT + 1] << 8) & 0x7F;
	c1 = (u[1][USER_COUNT] | u[1][USER_COUNT + 1] << 8) & 0x7F;
	k = c1 > c0 && ((c1 - c0) & 0x7F) == 1 ? 1 : 0;
	return (u[k][USER_LANG] & 7) < 7 ? WORDS[u[k][USER_LANG] & 7] : NULL;
}

/* The firmware's own language, as DSperate starts a game in it: the dump's,
 * or English, the language of the firmware DSperate makes without one */
static const char *firmware_own(const char *firmware_path)
{
	if (!firmware_path || access(firmware_path, F_OK) != 0) return "english";
	return dsr_firmware_language(firmware_path);
}

const char *dsr_effective(const char *option, const char *firmware_path)
{
	const char *w = word_of(option);

	return w ? w : firmware_own(firmware_path);
}

void dsr_lang_path(const char *state_path, char *out, size_t n)
{
	size_t len = state_path ? strlen(state_path) : 0;
	const size_t ext = sizeof ".state" - 1;

	if (!n) return;
	if (len >= ext && !strcmp(state_path + len - ext, ".state"))
		snprintf(out, n, "%.*s.lang", (int)(len - ext), state_path);
	else
		snprintf(out, n, "%s.lang", state_path ? state_path : "");
}

const char *dsr_recorded(const char *state_path, const char *firmware_path,
                         char *buf, size_t n)
{
	char path[4096], line[32];
	const char *w;
	FILE *f;

	if (!buf || !n) return NULL;
	buf[0] = '\0';
	dsr_lang_path(state_path, path, sizeof path);
	if ((f = fopen(path, "r"))) {
		size_t k;

		if (!fgets(line, sizeof line, f)) line[0] = '\0';
		fclose(f);
		k = strcspn(line, "\r\n \t");
		line[k] = '\0';
		/* A file that names no language says nothing: nothing to hold the
		 * state to, so it is resumed as before */
		if (!(w = word_of(line))) return NULL;
	} else if (!(w = firmware_own(firmware_path))) {
		return NULL;
	}
	snprintf(buf, n, "%s", w);
	return buf;
}

bool dsr_start_fresh(const char *recorded, const char *effective)
{
	return recorded && effective && strcmp(recorded, effective) != 0;
}

bool dsr_record(const char *state_path, const char *lang)
{
	char path[4096];
	FILE *f;

	if (!word_of(lang)) return false;
	dsr_lang_path(state_path, path, sizeof path);
	if (!(f = atomic_open(path, 0644))) return false;
	fprintf(f, "%s\n", lang);
	return atomic_commit(f, path);
}

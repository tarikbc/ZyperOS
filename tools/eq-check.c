/* SPDX-License-Identifier: GPL-3.0-only */
/* The equalizer's settings (src/eq.c): the presets and their curves, each
 * output's own, Custom's bands kept, a band's step making a preset Custom,
 * the words SETEQ and MP4's EQ carry, and the file a port's plugin reads:
 *
 *     make check-eq
 *
 * No SDL, no device. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../src/db.h"
#include "../src/eq.h"
#include "../src/i18n.h"

static int fails;

static void report(int ok, const char *name)
{
	printf("  %s %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) fails++;
}

static int same(const int8_t *a, const int8_t *b) { return !memcmp(a, b, EQ_BANDS); }

static void words(void)
{
	int8_t db[EQ_BANDS];
	const int8_t want[EQ_BANDS] = { 5, -2, 0, 3, -6 };
	char w[40], name[16];

	report(eq_out_of(AOUT_SPK) == EQ_OUT_SPEAKER && eq_out_of(AOUT_WIRED) == EQ_OUT_PHONES &&
	       eq_out_of(AOUT_BT) == EQ_OUT_PHONES && eq_out_of(AOUT_USB) == EQ_OUT_PHONES,
	       "the_speaker_or_headphones");
	report(eq_preset_parse("bass", EQ_OFF) == EQ_BASS && eq_preset_parse("loud", EQ_VOICE) == EQ_VOICE &&
	       eq_preset_parse(NULL, EQ_OFF) == EQ_OFF && !strcmp(eq_preset_id(EQ_TREBLE), "treble"),
	       "preset_words");
	report(!strcmp(eq_preset_name(EQ_OFF), "Off") && !strcmp(eq_preset_name(EQ_CUSTOM), "Custom") &&
	       !strcmp(eq_out_name(EQ_OUT_SPEAKER), "Speaker") && !strcmp(eq_out_name(EQ_OUT_PHONES), "Headphones"),
	       "names_in_english");
	eq_band_name(0, name, sizeof name);
	report(!strcmp(name, "60 Hz") && (eq_band_name(4, name, sizeof name), !strcmp(name, "12 kHz")), "band_names");
	eq_bands_wire(want, w, sizeof w);
	report(!strcmp(w, "5,-2,0,3,-6"), "bands_on_the_wire");
	report(eq_bands_parse(w, db) && same(db, want), "and_back");
	memset(db, 9, sizeof db);
	report(!eq_bands_parse("1,2,3,4", db) && !eq_bands_parse("1,2,3,4,7", db) &&
	       !eq_bands_parse("1,2,3,4,5,6", db) && !eq_bands_parse("a,2,3,4,5", db) && db[0] == 9,
	       "anything_else_is_refused_and_changes_nothing");
	report(EQ_CURVE[EQ_BASS][0] == 5 && EQ_CURVE[EQ_VOICE][2] == 3 && EQ_CURVE[EQ_TREBLE][4] == 5 &&
	       eq_flat(EQ_CURVE[EQ_OFF]), "the_labs_curves");
}

static void store(void)
{
	int8_t db[EQ_BANDS];
	const int8_t mine[EQ_BANDS] = { 2, 0, -1, 0, 3 };

	report(eq_preset_get(EQ_OUT_SPEAKER) == EQ_OFF && eq_preset_get(EQ_OUT_PHONES) == EQ_OFF,
	       "off_on_a_new_card");
	eq_bands(EQ_OUT_PHONES, db);
	report(eq_flat(db), "and_flat");
	eq_preset_set(EQ_OUT_PHONES, EQ_BASS);
	eq_bands(EQ_OUT_PHONES, db);
	report(same(db, EQ_CURVE[EQ_BASS]) && eq_preset_get(EQ_OUT_SPEAKER) == EQ_OFF,
	       "headphones_bass_the_speaker_still_off");
	eq_custom_set(EQ_OUT_SPEAKER, mine);
	eq_preset_set(EQ_OUT_SPEAKER, EQ_CUSTOM);
	eq_bands(EQ_OUT_SPEAKER, db);
	report(same(db, mine), "custom_is_its_own_bands");
	eq_preset_set(EQ_OUT_SPEAKER, EQ_TREBLE);
	eq_preset_set(EQ_OUT_SPEAKER, EQ_CUSTOM);
	eq_bands(EQ_OUT_SPEAKER, db);
	report(same(db, mine), "a_preset_tried_leaves_custom_as_it_was");
	/* A step on Bass: Bass's curve with the step becomes Custom's */
	report(eq_band_step(EQ_OUT_PHONES, 1, 1), "a_step_on_a_preset");
	eq_bands(EQ_OUT_PHONES, db);
	report(eq_preset_get(EQ_OUT_PHONES) == EQ_CUSTOM && db[0] == 5 && db[1] == 3 && db[4] == 1,
	       "makes_it_custom_with_the_step");
	eq_preset_set(EQ_OUT_PHONES, EQ_CUSTOM);
	db[0] = 6;
	eq_custom_set(EQ_OUT_PHONES, db);
	report(!eq_band_step(EQ_OUT_PHONES, 0, 1), "a_band_stops_at_plus_6");
	eq_preset_set(EQ_OUT_PHONES, EQ_OFF);
	report(!db_has(db_dev(), "eq.phones"), "off_keeps_no_key");
	memset(db, 0, sizeof db);
	eq_custom_set(EQ_OUT_PHONES, db);
	report(!db_has(db_dev(), "eq.phones.bands"), "flat_custom_keeps_no_key");
	db_set_str(db_dev(), "eq.speaker", "loud");
	report(eq_preset_get(EQ_OUT_SPEAKER) == EQ_OFF, "a_word_it_cannot_read_is_off");
}

static void share(void)
{
	char path[] = "/tmp/eq-check-share-XXXXXX";
	int fd = mkstemp(path);
	const int8_t bass[EQ_BANDS] = { 5, 2, 0, 0, 1 };
	int8_t got[EQ_BANDS];
	eq_share s;
	FILE *f;

	if (fd >= 0) close(fd);
	unlink(path);
	setenv("ZYPEROS_EQ_FILE", path, 1);
	report(eq_share_write(bass), "the_file_written");
	f = fopen(path, "rb");
	report(f && fread(&s, sizeof s, 1, f) == 1 && s.magic == EQ_SHARE_MAGIC && !(s.seq & 1) &&
	       eq_share_read(&s, got, NULL) && same(got, bass), "and_read_back_whole");
	if (f) fclose(f);
	/* The volume boost's gain beside the bands, which it leaves as they are */
	report(eq_share_gain(8), "the_boost_written");
	f = fopen(path, "rb");
	{
		int8_t gain = -1;

		report(f && fread(&s, sizeof s, 1, f) == 1 && eq_share_read(&s, got, &gain) && gain == 8 &&
		       same(got, bass), "and_read_back_with_the_bands");
	}
	if (f) fclose(f);
	/* And read back as MP4's follower beside your PICO-8 reads it, to pass
	 * on to MP4: what portkeys wrote, written by another process into the
	 * same file in place (its own map, eqdsp.h's eq_share_put) */
	report(eq_share_gain_get() == 8, "the_boost_read_back_for_mp4");
	{
		int mfd = open(path, O_RDWR);
		eq_share *m = mfd < 0 ? NULL : mmap(NULL, sizeof *m, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);

		if (mfd >= 0) close(mfd);
		if (m && m != MAP_FAILED) {
			eq_share_put(m, NULL, 4);
			report(eq_share_gain_get() == 4, "a_gain_another_writer_put_is_read_at_once");
			m->seq++;   /* that writer halfway through its next write */
			report(eq_share_gain_get() == -1, "none_while_it_is_being_written");
			m->seq++;
			report(eq_share_gain_get() == 4, "and_again_once_written");
			munmap(m, sizeof *m);
		} else {
			report(0, "a_gain_another_writer_put_is_read_at_once");
		}
	}
	unlink(path);
}

int main(void)
{
	char dev[] = "/tmp/eq-check-dev-XXXXXX", lib[] = "/tmp/eq-check-lib-XXXXXX";
	int fd = mkstemp(dev), fl = mkstemp(lib);

	printf("eq: the words\n");
	words();
	printf("eq: kept on the handheld\n");
	if (fd >= 0) close(fd);
	if (fl >= 0) close(fl);
	if (fd < 0 || fl < 0 || !db_init(dev, lib, "/dev/null")) {
		printf("  (no temporary database)\n");
		fails++;
	} else {
		store();
		db_shutdown();
	}
	unlink(dev);
	unlink(lib);
	printf("eq: the file a port's plugin reads\n");
	share();
	if (fails) {
		printf("\n%d failure%s\n", fails, fails == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}

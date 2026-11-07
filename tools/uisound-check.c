/* The interface sounds' rule, held to DESIGN.md 9 on a build machine.
 *
 * Which press plays what, the setting's four positions, where the sounds may
 * be heard - and never on a Bluetooth headset - the WAVs the launcher reads,
 * and the mix that sums them. Each is a promise a person only hears broken on
 * the handheld, a held direction buzzing against the end or a tick arriving
 * on a headset a fifth of a second late, so each is held here.
 *
 * Links src/uisound.c and src/audioout.c and NOT SDL. If it ever needs SDL,
 * the split in src/uisound.h has failed. Run from the repository's root: it
 * reads the shipped WAVs from res/sounds/.
 */
#include "../src/uisound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static bool is(uis_cue c, ds_sound s, float level)
{
	return c.sound == s && c.level == level;
}

#define BT "bluealsa:DEV=AA:BB:CC:DD:EE:FF,PROFILE=a2dp"

static aout_state st(aout_policy p, bool wired, bool usb, const char *bt)
{
	aout_state s;
	s.policy = p; s.wired = wired; s.usb = usb; s.bt_sink = bt;
	return s;
}

/* Every move, including an end-to-start wrap, uses the normal move cue.
 * A held move uses the normal held attenuation. */
static void steps(void)
{
	uis_steps s = { false };

	printf("steps:\n");
	ck(DS_SOUND_HELD_LEVEL == 0.5f, "a held step plays at half (the sound lab's pick)");
	ck(is(uisound_step(&s, true, false), DS_SOUND_MOVE, 1.0f), "a press that moves plays move");
	ck(is(uisound_step(&s, true, true), DS_SOUND_MOVE, DS_SOUND_HELD_LEVEL),
	   "a repeat plays move at the held level");
	for (int dir = -1; dir <= 1; dir += 2) {
		int from = dir > 0 ? 2 : 0;
		int to = (from + dir + 3) % 3;

		s.edged = true;
		ck(is(uisound_step(&s, from != to, false), DS_SOUND_MOVE, 1.0f),
		   "a wrap in either direction plays move and clears a blocked edge");
		ck(!s.edged, "a wrap clears the blocked-edge state");
		ck(is(uisound_step(&s, from != to, true), DS_SOUND_MOVE, DS_SOUND_HELD_LEVEL),
		   "a held wrap uses normal move attenuation");
	}
}

/* The end of a list: answered on every press, and only once for a hold, where
 * a tick every 90 ms against a wall would buzz without saying anything new. */
static void the_end(void)
{
	uis_steps s = { false };
	int i;

	printf("the end of a list:\n");
	ck(is(uisound_step(&s, false, false), DS_SOUND_EDGE, 1.0f), "a press that cannot move plays edge");
	ck(is(uisound_step(&s, false, false), DS_SOUND_EDGE, 1.0f), "and so does the next press");
	ck(uisound_step(&s, false, true).level == 0.0f, "a repeat held against it is silent");

	/* Held down a list: steps, then the end once, then nothing. */
	s.edged = false;
	ck(uisound_step(&s, true, false).sound == DS_SOUND_MOVE, "held: the press moves");
	for (i = 0; i < 3; i++)
		ck(is(uisound_step(&s, true, true), DS_SOUND_MOVE, DS_SOUND_HELD_LEVEL), "held: each repeat moves");
	ck(is(uisound_step(&s, false, true), DS_SOUND_EDGE, 1.0f),
	   "held: arriving at the end plays edge, at full");
	for (i = 0; i < 5; i++)
		ck(uisound_step(&s, false, true).level == 0.0f, "held: and then nothing");
	ck(uisound_step(&s, true, true).sound == DS_SOUND_MOVE, "moving away clears it");
	ck(is(uisound_step(&s, false, true), DS_SOUND_EDGE, 1.0f), "so the end is heard again");
}

static void setting(void)
{
	int v, k, seen[UIS_SETTINGS] = { 0 };

	printf("the setting:\n");
	ck(DS_VOICE_DEFAULT == DS_VOICE_WOOD, "a new device has Wood (the sound lab's pick)");
	ck(uisound_setting_parse(NULL) == DS_VOICE_DEFAULT, "no setting is the default voice");
	ck(uisound_setting_parse("") == DS_VOICE_DEFAULT, "an empty one too");
	ck(uisound_setting_parse("kazoo") == DS_VOICE_DEFAULT, "a voice since gone falls back to sound");
	ck(uisound_setting_parse("off") == UIS_OFF, "off is off");
	for (v = 0; v < UIS_SETTINGS; v++) {
		ck(uisound_setting_parse(uisound_setting_id(v)) == v, "every position survives the database");
		ck(uisound_setting_name(v)[0] != '\0', "and has a name for the row");
	}
	ck(!strcmp(uisound_setting_name(UIS_OFF), "Off"), "the fourth is Off");
	ck(!strcmp(uisound_setting_name(DS_VOICE_WOOD), "Wood"), "the first is Wood");
	for (v = DS_VOICE_DEFAULT, k = 0; k < UIS_SETTINGS; k++) {
		seen[v] = 1;
		v = uisound_setting_step(v, +1);
	}
	ck(v == DS_VOICE_DEFAULT, "stepping goes round the four and back");
	for (k = 0; k < UIS_SETTINGS; k++) ck(seen[k], "and passes every one");
	ck(uisound_setting_step(DS_VOICE_WOOD, -1) == UIS_OFF, "left from Wood is Off");
	ck(uisound_setting_step(UIS_OFF, +1) == DS_VOICE_WOOD, "right from Off is Wood");
	ck(uisound_setting_step(99, +1) == uisound_setting_step(DS_VOICE_DEFAULT, +1),
	   "a position out of range steps from the default");
}

/* Never on Bluetooth: in Auto with a headset connected, in either policy's
 * case that resolves to it, whatever else is plugged in that loses. And the
 * speaker and the jack are the codec's mixed default, the DAC its own. */
static void where(void)
{
	aout_state s;
	int p, w, u;
	bool l, r;

	printf("where:\n");
	s = st(AOUT_AUTO, false, false, BT);
	ck(uisound_device(&s) == NULL, "a connected headset in Auto: nowhere");
	s = st(AOUT_SPEAKER, false, false, BT);
	ck(uisound_device(&s) && !strcmp(uisound_device(&s), "default"),
	   "pinned to Speaker with a headset connected: the speaker");
	s = st(AOUT_AUTO, false, false, NULL);
	ck(uisound_device(&s) && !strcmp(uisound_device(&s), "default"), "the speaker: default");
	s = st(AOUT_AUTO, true, false, BT);
	ck(uisound_device(&s) && !strcmp(uisound_device(&s), "default"),
	   "a cable in beats the headset: default");
	s = st(AOUT_AUTO, false, true, BT);
	ck(uisound_device(&s) && !strcmp(uisound_device(&s), AOUT_USB_DEVICE), "a DAC: its own device");
	for (p = 0; p < AOUT_POLICY_COUNT; p++)
		for (w = 0; w < 2; w++)
			for (u = 0; u < 2; u++) {
				aout_state x = st((aout_policy)p, w != 0, u != 0, BT);
				ck((uisound_device(&x) == NULL) == (aout_resolve(&x) == AOUT_BT),
				   "silent exactly when the sound goes to the headset");
			}

	uisound_sides(AOUT_STEREO, AOUT_WIRED, &l, &r);
	ck(l && r, "stereo: both sides");
	uisound_sides(AOUT_MONO_LEFT, AOUT_WIRED, &l, &r);
	ck(l && !r, "mono left in the jack: the left only");
	uisound_sides(AOUT_MONO_RIGHT, AOUT_USB, &l, &r);
	ck(!l && r, "mono right on a DAC: the right only");
	uisound_sides(AOUT_MONO_LEFT, AOUT_SPK, &l, &r);
	ck(l && r, "mono left on the speaker: both, as the speaker makes plain mono of a side");
}

static uint8_t *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *b;
	long len;

	*n = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(len > 0 ? (size_t)len : 1);
	if (b && fread(b, 1, (size_t)len, f) == (size_t)len) *n = (size_t)len;
	fclose(f);
	return b;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

/* A WAV as tools/gensounds.py writes one, `count` samples of `value`. */
static size_t wav(uint8_t *b, int count, int16_t value, unsigned ch, uint32_t rate)
{
	int i;

	memcpy(b, "RIFF", 4); put32(b + 4, 36 + 2 * count); memcpy(b + 8, "WAVE", 4);
	memcpy(b + 12, "fmt ", 4); put32(b + 16, 16); put16(b + 20, 1); put16(b + 22, ch);
	put32(b + 24, rate); put32(b + 28, rate * 2 * ch); put16(b + 32, 2 * ch); put16(b + 34, 16);
	memcpy(b + 36, "data", 4); put32(b + 40, 2 * count);
	for (i = 0; i < count; i++) put16(b + 44 + 2 * i, (uint16_t)value);
	return 44 + 2 * (size_t)count;
}

static void files(void)
{
	uint8_t b[256];
	size_t n;
	int16_t *pcm;
	int frames, v, s;

	printf("the files:\n");
	n = wav(b, 8, -1234, 1, UIS_RATE);
	pcm = uisound_wav(b, n, &frames);
	ck(pcm && frames == 8 && pcm[0] == -1234 && pcm[7] == -1234, "reads what gensounds writes");
	free(pcm);
	ck(!uisound_wav(b, n - 3, &frames), "refuses one cut short");
	n = wav(b, 8, 0, 2, UIS_RATE);
	ck(!uisound_wav(b, n, &frames), "refuses two channels");
	n = wav(b, 8, 0, 1, 44100);
	ck(!uisound_wav(b, n, &frames), "refuses another rate, which would play at the wrong pitch");
	ck(!uisound_wav((const uint8_t *)"RIFF", 4, &frames), "refuses a stub");

	/* Every one the launcher will ask for is there and readable. */
	for (v = 0; v < DS_VOICES; v++)
		for (s = 0; s < DS_SOUNDS; s++) {
			char path[128], what[160];
			uint8_t *buf;

			snprintf(path, sizeof path, "res/sounds/%s/%s.wav", DS_VOICE_ID[v], DS_SOUND_ID[s]);
			buf = slurp(path, &n);
			pcm = buf ? uisound_wav(buf, n, &frames) : NULL;
			snprintf(what, sizeof what, "%s reads, and is under a second", path);
			ck(pcm && frames > 0 && frames < UIS_RATE, what);
			free(pcm);
			free(buf);
		}
}

static void mix(void)
{
	static const int16_t a[4] = { 1000, 2000, 3000, 4000 };
	static const int16_t loud[2] = { 30000, -30000 };
	uis_voice v[3];
	int16_t out[16];
	int n;

	printf("the mix:\n");
	v[0] = (uis_voice){ a, 4, 0, 32768 };
	v[1] = (uis_voice){ a, 2, 0, 16384 };
	n = uisound_mix(v, 2, out, 2, true, true);
	ck(out[0] == 1500 && out[1] == 1500, "sums the voices at their levels, on both sides");
	ck(out[2] == 3000 && out[3] == 3000, "frame by frame");
	ck(n == 1 && v[0].pos == 2, "a voice that ended is taken out, the other kept where it was");
	n = uisound_mix(v, n, out, 4, true, false);
	ck(out[0] == 3000 && out[1] == 0, "one side only, silence on the other");
	ck(out[4] == 0 && out[5] == 0, "and silence after the last voice");
	ck(n == 0, "nothing left");

	v[0] = (uis_voice){ loud, 2, 0, 32768 };
	v[1] = (uis_voice){ loud, 2, 0, 32768 };
	uisound_mix(v, 2, out, 2, true, true);
	ck(out[0] == 32767 && out[2] == -32768, "two loud ones hold at the top rather than wrap round");
}

int main(void)
{
	steps();
	the_end();
	setting();
	where();
	files();
	mix();
	if (fails) { printf("\n%d interface sound check(s) failed\n", fails); return 1; }
	printf("\nok: each press plays what DESIGN.md says, where it may be heard\n");
	return 0;
}

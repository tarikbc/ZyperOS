/* SPDX-License-Identifier: GPL-3.0-only */
/* The volume boost through a port's whole ALSA chain (sd/zyperos/eq-port.sh:
 * plug -> the equalizer's ladspa PCM -> plug -> the mix), as your PICO-8
 * plays into it: 22050 Hz mono, S16, through ALSA's "default", a second of a
 * 1 kHz tone at a sixteenth of full scale (quiet, so the gain is heard whole
 * and the limiter never acts); half way, the boost's gain written into the
 * equalizer's file in place, as portkeys writes it while the program plays
 * (src/eqdsp.h's eq_share_put on a map of $ZYPEROS_EQ_FILE). The config's
 * mix is a file PCM here (argv[1]): the level of the first half and of the
 * second, against the tone, the second `argv[2]` dB over the first, within
 * half a dB. For tools/eqchain-check.sh, on Linux.
 *
 * With `top`, a loud cart instead: three chip voices at fake08's levels (a
 * square at 5, a triangle at 5, a saw at 4, its peaks near -5 dB of full
 * scale), at cell 3 (EQ_COMP_FROM) for the first half and at the most
 * (EQ_GAIN_MAX) for the second: the top cells' compressor and soft clip
 * (src/eqdsp.h) make the second at least 3 dB louder through the chain, and
 * no sample of either passes -1 dB of full scale.
 *
 *     eqchain-gain OUT.wav DB
 *     eqchain-gain OUT.wav top
 */
#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "eqdsp.h"

#define RATE 22050
#define AMP  (32767.0 / 16.0)

static float peak_seen;   /* the loudest sample that reached the file, 16-bit scale */

/* The RMS of the first channel over [from, to) frames of the WAV, in dB
 * against the tone; the chain's format as it settled: 16-bit integers or
 * 32-bit floats (alsa-lib's file plugin tags floats as integers) */
static int levels(const char *path, double *first, double *second)
{
	FILE *f = fopen(path, "rb");
	unsigned char h[44];
	double sq[2] = { 0, 0 };
	long n = 0, cnt[2] = { 0, 0 };
	int ch, bits, rate;

	if (!f) return 0;
	if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4)) {
		fclose(f);
		return 0;
	}
	ch = h[22] | h[23] << 8;
	rate = h[24] | h[25] << 8 | h[26] << 16;
	bits = h[34] | h[35] << 8;
	for (;;) {
		unsigned char fr[32];
		double v;
		long half;

		if (ch < 1 || ch > 8 || (bits != 16 && bits != 32) || fread(fr, (size_t)bits / 8, (size_t)ch, f) != (size_t)ch)
			break;
		if (bits == 32) {
			float x;

			memcpy(&x, fr, 4);
			v = x * 32768.0;
		} else {
			v = (short)(fr[0] | fr[1] << 8);
		}
		if (fabs(v) > peak_seen) peak_seen = (float)fabs(v);
		/* A tenth of a second clear of the start, the change and the end:
		 * the resampler's start and the gain's 20 ms move */
		half = n / rate;
		if (n % rate > rate / 10 && n % rate < rate - rate / 10 && half < 2) {
			sq[half] += v * v;
			cnt[half]++;
		}
		n++;
	}
	fclose(f);
	if (!cnt[0] || !cnt[1]) return 0;
	*first = 20.0 * log10(sqrt(sq[0] / cnt[0]) / (AMP / sqrt(2.0)));
	*second = 20.0 * log10(sqrt(sq[1] / cnt[1]) / (AMP / sqrt(2.0)));
	printf("  (the chain wrote %d channels of %d-bit %s at %d Hz)\n", ch, bits, bits == 32 ? "floats" : "integers",
	       rate);
	return 1;
}

int main(int argc, char **argv)
{
	static short buf[RATE];
	const char *eqf = getenv("ZYPEROS_EQ_FILE");
	volatile eq_share *s;
	snd_pcm_t *pcm;
	double a, b;
	int fd, r, i, half, db, top;

	if (argc < 3 || !eqf) return 2;
	top = !strcmp(argv[2], "top");
	db = top ? EQ_GAIN_MAX : atoi(argv[2]);
	if ((fd = open(eqf, O_RDWR)) < 0) {
		printf("  FAIL no equalizer's file at %s\n", eqf);
		return 1;
	}
	s = mmap(NULL, sizeof(eq_share), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (s == MAP_FAILED) return 1;
	for (i = 0; i < RATE; i++) {
		const double t = (double)i / RATE, sq = fmod(t * 220, 1) < 0.5 ? 0.25 : -0.25;
		const double p = fmod(t * 110, 1), tri = (p < 0.5 ? 4 * p - 1 : 3 - 4 * p) * 0.5;
		const double saw = (fmod(t * 330, 1) * 2 - 1) * 0.33;

		buf[i] = (short)lrint(top ? 32767.0 * (5 / 7.0 * sq + 5 / 7.0 * tri + 4 / 7.0 * saw)
		                          : AMP * sin(2.0 * M_PI * 1000.0 * i / RATE));
	}
	if (top) eq_share_put(s, NULL, EQ_COMP_FROM);
	if ((r = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0 ||
	    (r = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1,
	                            100000)) < 0) {
		printf("  FAIL open default at %d Hz mono: %s\n", RATE, snd_strerror(r));
		return 1;
	}
	for (half = 0; half < 2; half++) {
		/* portkeys's write, while the program plays */
		if (half) eq_share_put(s, NULL, db);
		for (i = 0; i < RATE; ) {
			snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf + i, (snd_pcm_uframes_t)(RATE - i));

			if (w < 0) w = snd_pcm_recover(pcm, (int)w, 0);
			if (w < 0) break;
			i += (int)w;
		}
	}
	snd_pcm_drain(pcm);
	snd_pcm_close(pcm);
	munmap((void *)s, sizeof(eq_share));
	if (!levels(argv[1], &a, &b)) {
		printf("  FAIL nothing whole reached %s\n", argv[1]);
		return 1;
	}
	if (top) {
		r = b - a >= 3.0 && peak_seen <= EQ_LIM_T * (1.0f + 1e-6f);
		printf("  %s a loud cart (%d Hz mono) at cell 3, then at the most: %+.2f dB louder, its peak %.3f of "
		       "full scale (the ceiling %.3f)\n", r ? "PASS" : "FAIL", RATE, b - a, peak_seen / 32768.0,
		       EQ_LIM_T / 32768.0);
		return r ? 0 : 1;
	}
	r = fabs(a) < 0.5 && fabs(b - a - db) < 0.5;
	printf("  %s the boost's %d dB written as PICO-8 plays (%d Hz mono): %.2f dB, then %.2f dB\n",
	       r ? "PASS" : "FAIL", db, RATE, a, b);
	return r ? 0 : 1;
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* The bars' bands: see bands.h. */
#include "bands.h"

#include <math.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define RATE  48000
#define RING  65536                       /* 1.37 s: the queue (half a second decoded
                                           * ahead, pcm.h, and the output's) and a window */
#define MASK  (RING - 1)

/* The scale, in dB of a band's loudest bin against a full-scale sine's:
 * where a bar is empty and where it is full, and the tilt, dB an octave
 * from 1 kHz, up above it and down below. Set by tools/bands-check.c --song
 * on five songs, a quiet live recording to a loud electronic master
 * (2026-11-19): with these a loud song's bars reach 200 to 250 of 255 one
 * window in ten and are full under one in twenty, the bass included, and
 * the quiet one still moves every bar under 4 kHz. Flat, the bass of the
 * two loudest was full half the time and the top bars of the quiet one
 * never moved. */
#ifndef BANDS_FLOOR                       /* bands-check --song tries others */
#define BANDS_FLOOR (-64.0f)
#define BANDS_CEIL  (-16.0f)
#define BANDS_TILT  2.0f
#define BANDS_TILT_FROM 1000.0f
#endif

/* What the two threads share, under its own lock and not the player's:
 * the player holds S.mu while it opens a file, and the bars should not wait
 * on that. A copy of 1024 floats in, a copy of 1024 out. */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static float  ring[RING];
static long   written;                    /* frames ever fed */
static long   valid_from;                 /* the first frame since the last drop */
static long   queued;                     /* in the device after the last write */
static struct timespec wrote_at;

void bands_feed(const int16_t *frames, int n, long queued_after)
{
	int i;

	if (n <= 0) return;
	pthread_mutex_lock(&mu);
	for (i = 0; i < n; i++)
		ring[(written + i) & MASK] = (float)(frames[i * 2] + frames[i * 2 + 1]) * (0.5f / 32768.0f);
	written += n;
	queued = queued_after > 0 ? queued_after : 0;
	clock_gettime(CLOCK_MONOTONIC, &wrote_at);
	pthread_mutex_unlock(&mu);
}

void bands_drop(void)
{
	pthread_mutex_lock(&mu);
	valid_from = written;
	queued = 0;
	pthread_mutex_unlock(&mu);
}

/* ---- the transform -------------------------------------------------------- */

static float    hann[BANDS_FFT], cos_t[BANDS_FFT / 2], sin_t[BANDS_FFT / 2];
static uint16_t rev[BANDS_FFT];
/* Each band's edges in bins, and its middle */
static float    edge[BANDS_N + 1], mid[BANDS_N], tilt[BANDS_N];
static bool     ready;

static void tables(void)
{
	const float binw = (float)RATE / BANDS_FFT;
	int i, b, bits = 0;

	while ((1 << bits) < BANDS_FFT) bits++;
	for (i = 0; i < BANDS_FFT; i++) {
		int r = 0, k;

		for (k = 0; k < bits; k++) r |= ((i >> k) & 1) << (bits - 1 - k);
		rev[i] = (uint16_t)r;
		hann[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / BANDS_FFT);
	}
	for (i = 0; i < BANDS_FFT / 2; i++) {
		cos_t[i] = cosf(2.0f * (float)M_PI * (float)i / BANDS_FFT);
		sin_t[i] = -sinf(2.0f * (float)M_PI * (float)i / BANDS_FFT);
	}
	for (b = 0; b <= BANDS_N; b++)
		edge[b] = BANDS_LO * powf(BANDS_HI / BANDS_LO, (float)b / BANDS_N) / binw;
	for (b = 0; b < BANDS_N; b++) {
		float hz = sqrtf(edge[b] * edge[b + 1]) * binw;

		mid[b] = hz / binw;
		tilt[b] = BANDS_TILT * log2f(hz / BANDS_TILT_FROM);
	}
	ready = true;
}

/* In place, radix 2, `re` and `im` BANDS_FFT long */
static void fft(float *re, float *im)
{
	int i, size, half, step, j, k;

	for (i = 0; i < BANDS_FFT; i++) {
		int r = rev[i];

		if (r > i) {
			float t = re[i]; re[i] = re[r]; re[r] = t;
			t = im[i]; im[i] = im[r]; im[r] = t;
		}
	}
	for (size = 2; size <= BANDS_FFT; size <<= 1) {
		half = size >> 1;
		step = BANDS_FFT / size;
		for (i = 0; i < BANDS_FFT; i += size)
			for (j = i, k = 0; j < i + half; j++, k += step) {
				float wr = cos_t[k], wi = sin_t[k];
				float xr = re[j + half] * wr - im[j + half] * wi;
				float xi = re[j + half] * wi + im[j + half] * wr;

				re[j + half] = re[j] - xr;
				im[j + half] = im[j] - xi;
				re[j] += xr;
				im[j] += xi;
			}
	}
}

void bands_of(const float *win, uint8_t out[BANDS_N])
{
	static float re[BANDS_FFT], im[BANDS_FFT], pw[BANDS_FFT / 2 + 1];
	/* A full-scale sine under the window reads 0 dB at its bin: |X| is
	 * A x N/4 there, the window's gain a half */
	const float norm = 16.0f / ((float)BANDS_FFT * BANDS_FFT);
	int i, b;

	if (!ready) tables();
	for (i = 0; i < BANDS_FFT; i++) {
		re[i] = win[i] * hann[i];
		im[i] = 0.0f;
	}
	fft(re, im);
	for (i = 0; i <= BANDS_FFT / 2; i++) pw[i] = (re[i] * re[i] + im[i] * im[i]) * norm;
	for (b = 0; b < BANDS_N; b++) {
		int k0 = (int)ceilf(edge[b]), k1 = (int)ceilf(edge[b + 1]), k;
		float p = 0.0f, db, v;

		if (k1 > k0) {
			for (k = k0; k < k1 && k <= BANDS_FFT / 2; k++)
				if (pw[k] > p) p = pw[k];
		} else {
			/* No bin's middle inside: the curve between the two around
			 * its middle */
			int lo = (int)mid[b];
			float f = mid[b] - (float)lo;

			p = pw[lo] + (pw[lo + 1] - pw[lo]) * f;
		}
		db = 10.0f * log10f(p + 1e-12f) + tilt[b];
		v = (db - BANDS_FLOOR) / (BANDS_CEIL - BANDS_FLOOR);
		out[b] = (uint8_t)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f));
	}
}

bool bands_now(uint8_t out[BANDS_N])
{
	static float win[BANDS_FFT];
	struct timespec now;
	long heard, gone, i;

	clock_gettime(CLOCK_MONOTONIC, &now);
	pthread_mutex_lock(&mu);
	/* Where the listener is: the last write's end, less what was queued
	 * behind it, plus what the device has played since */
	gone = (long)((now.tv_sec - wrote_at.tv_sec) * RATE +
	              (now.tv_nsec - wrote_at.tv_nsec) / (1000000000L / RATE));
	if (gone < 0) gone = 0;
	if (gone > queued) gone = queued;
	heard = written - queued + gone;
	if (heard - BANDS_FFT < valid_from || written - (heard - BANDS_FFT) > RING) {
		pthread_mutex_unlock(&mu);
		return false;
	}
	for (i = 0; i < BANDS_FFT; i++) win[i] = ring[(heard - BANDS_FFT + i) & MASK];
	pthread_mutex_unlock(&mu);
	bands_of(win, out);
	return true;
}

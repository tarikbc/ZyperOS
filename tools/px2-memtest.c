/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* px2-memtest: is the RAM sound, and how fast is it.
 *
 *   px2-memtest [MEGABYTES] [PASSES]      (defaults: 256 MB, 4 passes)
 *
 * For changes to the memory clock (the DMC driver, the loader's DDR training):
 * a wrong DDR setting shows as flipped bits under load long before it shows
 * as a crash, and the card's files are what a crash corrupts. Each pass fills
 * the buffer with a pattern from an xorshift seed and the word's own address,
 * reads it all back and counts the words that differ, with every thread doing
 * a slice at once so the controller is as busy as a game makes it; then it
 * times a plain memcpy, the number that says what a faster clock bought.
 * Exits 1 on any error. No libraries beyond libc and pthreads.
 *
 * Build: the plastron toolchain, -O2 -pthread. Run as root on the device, with
 * ZyperOS stopped, so nothing else uses the memory meanwhile. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define THREADS 4

struct slice { uint64_t *p; size_t n; uint64_t seed; int verify; size_t bad; };

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static inline uint64_t mix(uint64_t x)
{
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

static void *run(void *arg)
{
	struct slice *s = arg;
	uint64_t x = s->seed;

	for (size_t i = 0; i < s->n; i++) {
		uint64_t want;

		x = mix(x);
		want = x ^ (uint64_t)(uintptr_t)&s->p[i];
		if (!s->verify)
			s->p[i] = want;
		else if (s->p[i] != want && s->bad++ < 8)
			fprintf(stderr, "px2-memtest: %p is %016llx, not %016llx\n",
				(void *)&s->p[i], (unsigned long long)s->p[i],
				(unsigned long long)want);
	}
	return NULL;
}

static size_t pass(uint64_t *buf, size_t words, uint64_t seed, int verify)
{
	pthread_t t[THREADS];
	struct slice s[THREADS];
	size_t per = words / THREADS, bad = 0;

	for (int i = 0; i < THREADS; i++) {
		s[i] = (struct slice){ buf + i * per, per, seed + 0x9e3779b97f4a7c15ull * (i + 1), verify, 0 };
		pthread_create(&t[i], NULL, run, &s[i]);
	}
	for (int i = 0; i < THREADS; i++) {
		pthread_join(t[i], NULL);
		bad += s[i].bad;
	}
	return bad;
}

int main(int argc, char **argv)
{
	size_t mb = argc > 1 ? strtoul(argv[1], NULL, 10) : 256;
	int passes = argc > 2 ? atoi(argv[2]) : 4;
	size_t bytes = mb << 20, words = bytes / 8, bad = 0;
	uint64_t *buf = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	double t0, best = 0;

	if (buf == MAP_FAILED) {
		perror("px2-memtest: mmap");
		return 2;
	}
	for (int p = 0; p < passes; p++) {
		uint64_t seed = 0x2545f4914f6cdd1dull * (p + 1) + (uint64_t)time(NULL);
		size_t b;

		t0 = now();
		pass(buf, words, seed, 0);
		b = pass(buf, words, seed, 1);
		bad += b;
		printf("pass %d: %zu MB written and checked in %.2f s, %zu bad words\n",
		       p + 1, mb, now() - t0, b);
	}
	/* memcpy of half the buffer onto the other half, best of three */
	for (int i = 0; i < 3; i++) {
		double t;

		t0 = now();
		memcpy(buf + words / 2, buf, bytes / 2);
		t = now() - t0;
		if (!best || t < best)
			best = t;
	}
	printf("memcpy: %.0f MB/s\n", (bytes / 2) / best / 1e6);
	munmap(buf, bytes);
	return bad ? 1 : 0;
}

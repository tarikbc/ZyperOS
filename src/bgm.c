/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Menu music's rules: no SDL, no ALSA, no threads. See src/bgm.h. */
#include "bgm.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- the settings ----------------------------------------------------------- */

bool bgm_on_parse(const char *v)
{
	return !(v && !strcmp(v, "off"));
}

const char *bgm_on_id(bool on)
{
	return on ? "on" : "off";
}

int bgm_volume_parse(const char *v)
{
	char *end;
	long x;

	if (!v || !*v) return BGM_VOLUME_DEFAULT;
	x = strtol(v, &end, 10);
	if (*end) return BGM_VOLUME_DEFAULT;
	if (x < BGM_VOLUME_MIN) return BGM_VOLUME_MIN;
	if (x > BGM_VOLUME_MAX) return BGM_VOLUME_MAX;
	return (int)x;
}

void bgm_volume_label(int volume, char *out, size_t n)
{
	snprintf(out, n, "%d%%", volume * 10);
}

int bgm_volume_step(int volume, int dir)
{
	volume += dir;
	if (volume < BGM_VOLUME_MIN) volume = BGM_VOLUME_MIN;
	if (volume > BGM_VOLUME_MAX) volume = BGM_VOLUME_MAX;
	return volume;
}

/* ---- the folder ---------------------------------------------------------------- */

bool bgm_is_track(const char *name)
{
	const char *dot;

	if (!name || name[0] == '.') return false;
	dot = strrchr(name, '.');
	return dot && dot != name &&
	       (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".ogg"));
}

static int name_cmp(const void *x, const void *y)
{
	const char *a = *(const char *const *)x, *b = *(const char *const *)y;
	int c = strcasecmp(a, b);

	return c ? c : strcmp(a, b);
}

/* Sorts names[0..n) in place, the order bgm_scan gives */
static void bgm_sort(char **names, int n)
{
	if (n > 1) qsort(names, (size_t)n, sizeof *names, name_cmp);
}

void bgm_free(bgm_list *l)
{
	int i;

	for (i = 0; i < l->n; i++) free(l->names[i]);
	free(l->names);
	l->names = NULL;
	l->n = 0;
}

void bgm_scan(const char *dir, bgm_list *l)
{
	DIR *d;
	struct dirent *e;
	int cap = 0;

	bgm_free(l);
	if (!dir || !(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		char *copy;

		if (!bgm_is_track(e->d_name)) continue;
		if (l->n == cap) {
			int nc = cap ? cap * 2 : 64;
			char **nn = realloc(l->names, (size_t)nc * sizeof *nn);

			if (!nn) break;
			l->names = nn;
			cap = nc;
		}
		if (!(copy = strdup(e->d_name))) break;
		l->names[l->n++] = copy;
	}
	closedir(d);
	bgm_sort(l->names, l->n);
}

const char *bgm_scan_either(const char *own, const char *builtin, bgm_list *l)
{
	bgm_scan(own, l);
	if (l->n > 0) return own;
	if (!builtin) return NULL;
	bgm_scan(builtin, l);
	return l->n > 0 ? builtin : NULL;
}

int bgm_find(const bgm_list *l, const char *name)
{
	int i;

	if (!name || !*name) return -1;
	for (i = 0; i < l->n; i++)
		if (!strcmp(l->names[i], name)) return i;
	return -1;
}

void bgm_title(const char *file, char *out, size_t n)
{
	const char *dot = strrchr(file, '.');
	size_t len = dot && dot != file ? (size_t)(dot - file) : strlen(file);

	if (!n) return;
	if (len >= n) len = n - 1;
	memcpy(out, file, len);
	out[len] = '\0';
}

int bgm_track_step(int at, int n, int dir)
{
	/* Positions -1 (Shuffle) .. n-1, n + 1 of them, round */
	int span = n + 1;

	if (n <= 0) return -1;
	if (at < -1 || at >= n) at = -1;
	return ((at + 1 + dir) % span + span) % span - 1;
}

/* ---- the shuffle ------------------------------------------------------------------ */

static uint32_t rnd(uint32_t *s)
{
	/* xorshift32: a deck needs no better, and a seed of 0 would stick */
	uint32_t x = *s ? *s : 0x9e3779b9u;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *s = x;
}

static void deal(bgm_deck *d, int last)
{
	int i;

	for (i = 0; i < d->n; i++) d->order[i] = i;
	for (i = d->n - 1; i > 0; i--) {
		int j = (int)(rnd(&d->seed) % (uint32_t)(i + 1)), t = d->order[i];

		d->order[i] = d->order[j];
		d->order[j] = t;
	}
	/* Not the track that was just heard, first again */
	if (d->n > 1 && d->order[0] == last) {
		int t = d->order[0];

		d->order[0] = d->order[d->n - 1];
		d->order[d->n - 1] = t;
	}
	d->at = 0;
}

void bgm_deck_init(bgm_deck *d, int n, uint32_t seed)
{
	d->order = n > 0 ? malloc((size_t)n * sizeof *d->order) : NULL;
	d->n = d->order ? n : 0;
	d->seed = seed;
	if (d->n) deal(d, -1);
}

void bgm_deck_free(bgm_deck *d)
{
	free(d->order);
	memset(d, 0, sizeof *d);
}

int bgm_deck_next(bgm_deck *d)
{
	if (d->n <= 0) return -1;
	if (d->at >= d->n) deal(d, d->order[d->n - 1]);
	return d->order[d->at++];
}

/* ---- going back -------------------------------------------------------------------- */

void bgm_hist_clear(bgm_hist *h)
{
	memset(h, 0, sizeof *h);
}

void bgm_hist_push(bgm_hist *h, int track)
{
	if (h->n > 0) h->n = h->at + 1;        /* what was ahead of it goes */
	if (h->n == BGM_HIST) {
		memmove(h->idx, h->idx + 1, (BGM_HIST - 1) * sizeof h->idx[0]);
		h->n--;
	}
	h->idx[h->n] = track;
	h->at = h->n++;
}

int bgm_hist_back(bgm_hist *h)
{
	if (h->n <= 0 || h->at <= 0) return -1;
	return h->idx[--h->at];
}

int bgm_hist_fwd(bgm_hist *h)
{
	if (h->n <= 0 || h->at >= h->n - 1) return -1;
	return h->idx[++h->at];
}

int bgm_hist_now(const bgm_hist *h)
{
	return h->n > 0 ? h->idx[h->at] : -1;
}

int bgm_file_step(int at, int n, int dir)
{
	if (n <= 0) return -1;
	if (at < 0 || at >= n) return dir < 0 ? n - 1 : 0;
	return ((at + dir) % n + n) % n;
}

/* ---- the level --------------------------------------------------------------------- */

int bgm_gain(int volume, int fade, int fade_len)
{
	long long g;

	if (volume < 0) volume = 0;
	if (volume > BGM_VOLUME_MAX) volume = BGM_VOLUME_MAX;
	if (fade_len <= 0) fade = fade_len = 1;
	if (fade < 0) fade = 0;
	if (fade > fade_len) fade = fade_len;
	g = 65536LL * volume * volume / (BGM_VOLUME_MAX * BGM_VOLUME_MAX);
	return (int)(g * fade / fade_len);
}

void bgm_apply(int16_t *pcm, int frames, int g0, int g1)
{
	int i;

	for (i = 0; i < frames; i++) {
		long long g = frames > 1 ? g0 + (long long)(g1 - g0) * i / (frames - 1) : g1;
		int c;

		for (c = 0; c < 2; c++) {
			long long s = (long long)pcm[2 * i + c] * g >> 16;

			pcm[2 * i + c] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
		}
	}
}

void bgm_one_side(int16_t *pcm, int frames, bool left)
{
	int i;

	for (i = 0; i < frames; i++) {
		int16_t m = (int16_t)(((int)pcm[2 * i] + pcm[2 * i + 1]) / 2);

		pcm[2 * i]     = left ? m : 0;
		pcm[2 * i + 1] = left ? 0 : m;
	}
}

/* ---- the rate ------------------------------------------------------------------------ */

void bgm_rs_init(bgm_rs *r, int in_rate, int out_rate)
{
	memset(r, 0, sizeof *r);
	if (in_rate <= 0 || out_rate <= 0) in_rate = out_rate = 1;
	r->step = (uint32_t)(((uint64_t)in_rate << 16) / (uint64_t)out_rate);
	if (!r->step) r->step = 1;
}

int bgm_rs_run(bgm_rs *r, const int16_t *in, int n, int16_t *out, int out_max)
{
	int k = 0;

	if (n <= 0) return 0;
	/* The same rate: as it is */
	if (r->step == 0x10000) {
		if (n > out_max) n = out_max;
		memcpy(out, in, (size_t)n * 2 * sizeof *in);
		return n;
	}
	/* The first frame of all stands before itself, so the first output is it */
	if (!r->primed) {
		r->last[0] = in[0];
		r->last[1] = in[1];
		r->primed = true;
	}
	/* x[0] is `last`, x[i + 1] is in[i]: each output frame between x[i] and
	 * x[i + 1], while x[i + 1] is in this input */
	while (k < out_max) {
		uint32_t i = r->pos >> 16, f = r->pos & 0xffff;
		const int16_t *a, *b;
		int c;

		if (i + 1 > (uint32_t)n) break;
		a = i ? in + 2 * (i - 1) : r->last;
		b = in + 2 * i;
		for (c = 0; c < 2; c++)
			out[2 * k + c] = (int16_t)(a[c] + (((int32_t)b[c] - a[c]) * (int32_t)f >> 16));
		k++;
		r->pos += r->step;
	}
	/* The input used up: the next one's x[0] is this one's last frame. An
	 * output too short to take all of it loses the rest rather than wrap. */
	if ((r->pos >> 16) < (uint32_t)n) r->pos = (uint32_t)n << 16;
	r->pos -= (uint32_t)n << 16;
	r->last[0] = in[2 * (n - 1)];
	r->last[1] = in[2 * (n - 1) + 1];
	return k;
}

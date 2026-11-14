/* SPDX-License-Identifier: GPL-3.0-only */
/* Menu music's rules: no SDL, no ALSA, no threads. See src/bgm.h. */
#include "bgm.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

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

	for (i = 0; i < l->n; i++) {
		free(l->names[i]);
		free(l->paths[i]);
	}
	free(l->names);
	free(l->paths);
	free(l->from);
	memset(l, 0, sizeof *l);
}

/* One more track at the list's end, its name and path copied. False out of
 * memory, the list as it was. */
static bool list_add(bgm_list *l, int *cap, const char *name, const char *path, int from)
{
	char *nm, *pt;

	if (l->n == *cap) {
		int nc = *cap ? *cap * 2 : 64;
		char **nn = realloc(l->names, (size_t)nc * sizeof *nn);
		char **np;
		unsigned char *nf;

		if (!nn) return false;
		l->names = nn;
		if (!(np = realloc(l->paths, (size_t)nc * sizeof *np))) return false;
		l->paths = np;
		if (!(nf = realloc(l->from, (size_t)nc))) return false;
		l->from = nf;
		*cap = nc;
	}
	nm = strdup(name);
	pt = strdup(path);
	if (!nm || !pt) { free(nm); free(pt); return false; }
	l->names[l->n] = nm;
	l->paths[l->n] = pt;
	l->from[l->n++] = (unsigned char)from;
	return true;
}

/* The tracks in `dir` and in its folders to BGM_DEPTH below, as paths in
 * it ("Pixel Quest/02 Green Fields.mp3"), onto got[]. Folders starting with a
 * dot are left alone: .media, and macOS's own. */
static void collect(const char *dir, const char *rel, int depth, char ***got, int *n, int *gc)
{
	DIR *d;
	struct dirent *e;

	if (!(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		char r[1024], sub[1024];
		struct stat st;

		if (e->d_name[0] == '.') continue;
		if (snprintf(r, sizeof r, "%s%s", rel, e->d_name) >= (int)sizeof r) continue;
		if (bgm_is_track(e->d_name)) {
			char *copy;

			if (*n == *gc) {
				int nc = *gc ? *gc * 2 : 64;
				char **nn = realloc(*got, (size_t)nc * sizeof *nn);

				if (!nn) break;
				*got = nn;
				*gc = nc;
			}
			if (!(copy = strdup(r))) break;
			(*got)[(*n)++] = copy;
		} else if (depth < BGM_DEPTH &&
		           snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name) < (int)sizeof sub &&
		           stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) {
			size_t len = strlen(r);

			if (len + 1 >= sizeof r) continue;
			r[len] = '/';
			r[len + 1] = '\0';
			collect(sub, r, depth + 1, got, n, gc);
		}
	}
	closedir(d);
}

/* A folder's tracks, sorted, onto the list's end, each named by its path in
 * the folder; one of a name the list has already is left out */
static void add_folder(bgm_list *l, int *cap, const char *dir, int from)
{
	char **got = NULL;
	int n = 0, gc = 0, i;

	if (!dir) return;
	collect(dir, "", 0, &got, &n, &gc);
	bgm_sort(got, n);
	for (i = 0; i < n; i++) {
		char path[2048];

		snprintf(path, sizeof path, "%s/%s", dir, got[i]);
		if (bgm_find(l, got[i]) < 0) list_add(l, cap, got[i], path, from);
		free(got[i]);
	}
	free(got);
}

void bgm_scan(const char *dir, bgm_list *l)
{
	int cap = 0;

	bgm_free(l);
	add_folder(l, &cap, dir, BGM_FROM_FOLDER);
	l->own = l->n;
}

void bgm_scan_all(const bgm_sources *s, bgm_list *l)
{
	int cap = 0;

	bgm_free(l);
	add_folder(l, &cap, s->dir, BGM_FROM_FOLDER);
	l->own = l->n;
	if (s->builtin && (s->with_builtin || l->own == 0))
		add_folder(l, &cap, s->builtin, BGM_FROM_BUILTIN);
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
	const char *dot;

	if (strrchr(file, '/')) file = strrchr(file, '/') + 1;
	dot = strrchr(file, '.');
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

void bgm_mono(int16_t *pcm, int frames)
{
	int i;

	for (i = 0; i < frames; i++)
		pcm[2 * i] = pcm[2 * i + 1] = (int16_t)(((int)pcm[2 * i] + pcm[2 * i + 1]) / 2);
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

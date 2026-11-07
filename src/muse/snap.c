/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See snap.h. The decoder here is a small copy of vid.c's on purpose: a snap
 * needs no seek but the loop, no frame skipping and no clock but the wall's,
 * and keeping it apart leaves MP4's player - the part people watch films on -
 * exactly as it is. */
#include "snap.h"
#include "dec.h"
#include "../mp4frame.h"
#include "../snapframe.h"

#include <alsa/asoundlib.h>

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

/* Bigger than this is scaled down as it is copied out: a snap is drawn at
 * most 384 wide on the shelf, and ScreenScraper's normalized videos are
 * smaller already. A full-size video put there by hand costs no more. */
#define SNAP_MAX_W 640
#define SNAP_MAX_H 480

static mpf_head *g_shm;
static uint32_t  g_serial;

static struct {
	pthread_mutex_t mu;
	pthread_cond_t  cv;
	unsigned        req;
	char            path[1024];
	int             vol;
	bool            quit;
} S = { .mu = PTHREAD_MUTEX_INITIALIZER };

static pthread_t g_th, g_ath;
static bool      g_started, g_sound;

typedef struct {
	AVFormatContext   *fmt;
	AVCodecContext    *cc;
	AVPacket          *pkt;
	AVFrame           *fr;
	struct SwsContext *sws;
	int                si;
	double             tb, fdur, pos;
	bool               flushed;
} sdec;

static void sdec_close(sdec *d)
{
	if (!d) return;
	sws_freeContext(d->sws);
	av_frame_free(&d->fr);
	av_packet_free(&d->pkt);
	avcodec_free_context(&d->cc);
	avformat_close_input(&d->fmt);
	free(d);
}

static sdec *sdec_open(const char *path)
{
	sdec *d = calloc(1, sizeof *d);
	const AVCodec *codec;
	AVStream *st;
	unsigned i;

	if (!d) return NULL;
	if (avformat_open_input(&d->fmt, path, NULL, NULL) < 0 ||
	    avformat_find_stream_info(d->fmt, NULL) < 0)
		goto fail;
	d->si = -1;
	for (i = 0; i < d->fmt->nb_streams; i++) {
		AVStream *s = d->fmt->streams[i];

		if (d->si < 0 && s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		    !(s->disposition & AV_DISPOSITION_ATTACHED_PIC) &&
		    s->codecpar->width > 0 && s->codecpar->height > 0)
			d->si = (int)i;
		else
			s->discard = AVDISCARD_ALL;     /* the sound too: a snap is muted */
	}
	if (d->si < 0) goto fail;
	st = d->fmt->streams[d->si];
	if (!(codec = avcodec_find_decoder(st->codecpar->codec_id)) ||
	    !(d->cc = avcodec_alloc_context3(codec)) ||
	    avcodec_parameters_to_context(d->cc, st->codecpar) < 0)
		goto fail;
	/* One thread: a 320x240 clip is a few percent of one core, and every
	 * frame thread is a frame of delay and buffers of its own. */
	d->cc->thread_count = 1;
	if (avcodec_open2(d->cc, codec, NULL) < 0) goto fail;
	if (!(d->pkt = av_packet_alloc()) || !(d->fr = av_frame_alloc())) goto fail;
	d->tb = av_q2d(st->time_base);
	d->fdur = st->avg_frame_rate.num > 0 ? 1.0 / av_q2d(st->avg_frame_rate) : 1.0 / 30;
	fprintf(stderr, "muse: snap: %s %dx%d %.2f fps\n", codec->name,
	        d->cc->width, d->cc->height, 1.0 / d->fdur);
	return d;
fail:
	sdec_close(d);
	return NULL;
}

/* The next frame into d->fr: 1, 0 at the end, <0 on an error. */
static int sdec_next(sdec *d, double *pts)
{
	for (;;) {
		int r = avcodec_receive_frame(d->cc, d->fr);

		if (r == 0) {
			int64_t ts = d->fr->best_effort_timestamp;

			d->pos = ts == AV_NOPTS_VALUE ? d->pos + d->fdur : ts * d->tb;
			*pts = d->pos;
			return 1;
		}
		if (r == AVERROR_EOF) return 0;
		if (r != AVERROR(EAGAIN)) return -1;
		if (d->flushed) return 0;
		if (av_read_frame(d->fmt, d->pkt) < 0) {
			avcodec_send_packet(d->cc, NULL);
			d->flushed = true;
			continue;
		}
		if (d->pkt->stream_index == d->si) avcodec_send_packet(d->cc, d->pkt);
		av_packet_unref(d->pkt);
	}
}

static bool sdec_rewind(sdec *d)
{
	if (av_seek_frame(d->fmt, d->si, 0, AVSEEK_FLAG_BACKWARD) < 0) return false;
	avcodec_flush_buffers(d->cc);
	d->flushed = false;
	d->pos = 0;
	return true;
}

static void plane(uint8_t *dst, uint32_t w, uint32_t h, const uint8_t *src, int pitch)
{
	uint32_t y;

	for (y = 0; y < h; y++) memcpy(dst + (size_t)y * w, src + (size_t)y * pitch, w);
}

static void publish(sdec *d, double pts)
{
	AVFrame *f = d->fr;
	uint32_t w = (uint32_t)f->width, h = (uint32_t)f->height, ow = w, oh = h;
	bool direct, fit = w > SNAP_MAX_W || h > SNAP_MAX_H;
	int s;
	uint8_t *y, *u, *v;

	if (fit) {
		double k = fmin((double)SNAP_MAX_W / w, (double)SNAP_MAX_H / h);

		ow = (uint32_t)(w * k) & ~1u;
		oh = (uint32_t)(h * k) & ~1u;
	}
	direct = !fit && (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P);
	s = mpf_begin(g_shm);
	y = mpf_slot_data(g_shm, s);
	u = y + (size_t)ow * oh;
	v = u + (size_t)mpf_chroma(ow) * mpf_chroma(oh);
	if (direct) {
		plane(y, w, h, f->data[0], f->linesize[0]);
		plane(u, mpf_chroma(w), mpf_chroma(h), f->data[1], f->linesize[1]);
		plane(v, mpf_chroma(w), mpf_chroma(h), f->data[2], f->linesize[2]);
	} else {
		uint8_t *dst[4] = { y, u, v, NULL };
		int pitch[4] = { (int)ow, (int)mpf_chroma(ow), (int)mpf_chroma(ow), 0 };

		d->sws = sws_getCachedContext(d->sws, (int)w, (int)h, (enum AVPixelFormat)f->format,
		                              (int)ow, (int)oh, AV_PIX_FMT_YUV420P,
		                              SWS_BILINEAR, NULL, NULL, NULL);
		if (d->sws) sws_scale(d->sws, (const uint8_t *const *)f->data, f->linesize, 0,
		                      (int)h, dst, pitch);
	}
	g_shm->slot[s].w = ow;
	g_shm->slot[s].h = oh;
	g_shm->slot[s].color = f->color_range == AVCOL_RANGE_JPEG ? MPF_FULL : MPF_BT601;
	g_shm->slot[s].serial = ++g_serial ? g_serial : ++g_serial;
	g_shm->slot[s].file = g_shm->file;
	g_shm->slot[s].pts = pts;
	g_shm->sar_num = f->sample_aspect_ratio.num > 0 ? (uint32_t)f->sample_aspect_ratio.num : 1;
	g_shm->sar_den = f->sample_aspect_ratio.den > 0 ? (uint32_t)f->sample_aspect_ratio.den : 1;
	mpf_publish(g_shm, s);
}

static double mono(void)
{
	struct timespec n;

	clock_gettime(CLOCK_MONOTONIC, &n);
	return (double)n.tv_sec + n.tv_nsec / 1e9;
}

/* Waits on S.cv until `when` (monotonic seconds) or a new request. Under S.mu. */
static void wait_until(double when)
{
	struct timespec t;
	double s = floor(when);

	t.tv_sec = (time_t)s;
	t.tv_nsec = (long)((when - s) * 1e9);
	pthread_cond_timedwait(&S.cv, &S.mu, &t);
}

static void *sthread(void *arg)
{
	sdec *d = NULL;
	unsigned seen = 0;
	double start = 0, first = -1, last = 0;

	(void)arg;
	/* Below the sound and below MP4's pictures (vid.c is 5): a snap that
	 * falls behind drops a frame, which nobody browsing a shelf sees. */
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);

	pthread_mutex_lock(&S.mu);
	while (!S.quit) {
		double pts, due;
		int r;

		if (S.req != seen) {
			char path[1024];

			seen = S.req;
			snprintf(path, sizeof path, "%s", S.path);
			pthread_mutex_unlock(&S.mu);
			sdec_close(d);
			d = path[0] ? sdec_open(path) : NULL;
			if (path[0] && !d) fprintf(stderr, "muse: snap: cannot play it\n");
			g_shm->file++;
			__atomic_store_n(&g_shm->active, d ? 1u : 0u, __ATOMIC_RELEASE);
			start = mono();
			first = -1;
			pthread_mutex_lock(&S.mu);
			continue;
		}
		if (!d) {
			pthread_cond_wait(&S.cv, &S.mu);
			continue;
		}
		pthread_mutex_unlock(&S.mu);
		r = sdec_next(d, &pts);
		if (r == 0 && sdec_rewind(d)) {
			/* Round again, the first frame one frame after the last */
			start += (last - (first < 0 ? 0 : first)) + d->fdur;
			first = -1;
			r = sdec_next(d, &pts);
		}
		pthread_mutex_lock(&S.mu);
		if (r <= 0) {
			/* A clip that will not decode: shown as far as it got. */
			pthread_mutex_unlock(&S.mu);
			sdec_close(d);
			d = NULL;
			pthread_mutex_lock(&S.mu);
			continue;
		}
		if (first < 0) first = pts;
		last = pts;
		due = start + (pts - first);
		/* Far behind (the CPU was busy elsewhere): start the clock again
		 * here rather than racing through the frames to catch up. */
		if (mono() - due > 0.5) {
			start = mono() - (pts - first);
			due = start + (pts - first);
		}
		while (!S.quit && S.req == seen && mono() < due) wait_until(due);
		if (S.quit || S.req != seen) continue;
		pthread_mutex_unlock(&S.mu);
		publish(d, pts);
		pthread_mutex_lock(&S.mu);
	}
	pthread_mutex_unlock(&S.mu);
	sdec_close(d);
	return NULL;
}

/* ---- the sound, when it is not muted ------------------------------------
 *
 * Its own thread, decoder (dec.c, a second instance of it) and ALSA handle on
 * "default", the system's mix, so it plays beside a button's tick without
 * either knowing. The write paces it; the picture runs on the wall clock
 * from the same moment, which keeps the two together across a clip of a few
 * seconds. A new request drops what is queued at once. */
#define SNAP_SLICE 1024

static void *athread(void *arg)
{
	static int16_t buf[SNAP_SLICE * DEC_CHANNELS];
	unsigned seen = 0;

	(void)arg;
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
	pthread_mutex_lock(&S.mu);
	while (!S.quit) {
		char path[1024], err[128];
		snd_pcm_t *pcm = NULL;
		dec *d = NULL;
		int vol;

		if (S.req == seen) {
			pthread_cond_wait(&S.cv, &S.mu);
			continue;
		}
		seen = S.req;
		snprintf(path, sizeof path, "%s", S.path);
		vol = S.vol;
		pthread_mutex_unlock(&S.mu);
		if (path[0] && vol > 0 && (d = dec_open(path, 0, 1.0, err, sizeof err)) &&
		    snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) == 0 &&
		    snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
		                       DEC_CHANNELS, DEC_RATE, 0, 100000) == 0) {
			for (;;) {
				int n, i;
				bool again;

				pthread_mutex_lock(&S.mu);
				again = !S.quit && S.req == seen;
				pthread_mutex_unlock(&S.mu);
				if (!again) break;
				n = dec_read(d, buf, SNAP_SLICE);
				if (n <= 0) {
					/* The end: round again from the start, as the picture does */
					dec_close(d);
					if (!(d = dec_open(path, 0, 1.0, err, sizeof err))) break;
					continue;
				}
				if (vol < 100)
					for (i = 0; i < n * DEC_CHANNELS; i++) buf[i] = (int16_t)(buf[i] * vol / 100);
				if (snd_pcm_writei(pcm, buf, (snd_pcm_uframes_t)n) < 0)
					snd_pcm_recover(pcm, -EPIPE, 1);
			}
			snd_pcm_drop(pcm);
		}
		if (pcm) snd_pcm_close(pcm);
		if (d) dec_close(d);
		pthread_mutex_lock(&S.mu);
	}
	pthread_mutex_unlock(&S.mu);
	return NULL;
}

bool snap_init(void)
{
	pthread_condattr_t ca;
	void *p;
	int fd;

	fd = open(SNAP_FRAMES, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0 || ftruncate(fd, MPF_SIZE) != 0) {
		fprintf(stderr, "muse: snap: cannot make %s\n", SNAP_FRAMES);
		if (fd >= 0) close(fd);
		return false;
	}
	p = mmap(NULL, MPF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) return false;
	g_shm = p;
	if (g_shm->magic != MPF_MAGIC || g_shm->version != MPF_VERSION) {
		memset(g_shm, 0, sizeof *g_shm);
		g_shm->version = MPF_VERSION;
		__atomic_store_n(&g_shm->magic, MPF_MAGIC, __ATOMIC_RELEASE);
	}
	g_serial = g_shm->serial;
	g_shm->active = 0;

	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&S.cv, &ca);
	pthread_condattr_destroy(&ca);
	if (pthread_create(&g_th, NULL, sthread, NULL) != 0) return false;
	g_started = true;
	/* No sound thread: snaps play muted, which is how they start anyway */
	g_sound = pthread_create(&g_ath, NULL, athread, NULL) == 0;
	if (!g_sound) fprintf(stderr, "muse: snap: no sound thread\n");
	return true;
}

void snap_quit(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&S.mu);
	S.quit = true;
	pthread_cond_broadcast(&S.cv);
	pthread_mutex_unlock(&S.mu);
	pthread_join(g_th, NULL);
	if (g_sound) pthread_join(g_ath, NULL);
	g_started = false;
}

void snap_play(const char *path, int vol)
{
	if (!g_started) return;
	if (!path) path = "";
	if (vol < 0) vol = 0;
	if (vol > 100) vol = 100;
	pthread_mutex_lock(&S.mu);
	if (strcmp(S.path, path) != 0 || S.vol != vol) {
		snprintf(S.path, sizeof S.path, "%s", path);
		S.vol = vol;
		S.req++;
		/* Both threads wait on the one condition: wake them both */
		pthread_cond_broadcast(&S.cv);
	}
	pthread_mutex_unlock(&S.mu);
}

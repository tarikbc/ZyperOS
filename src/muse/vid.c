/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See vid.h; the frames' layout is src/mp4frame.h.
 *
 * WHAT THIS CPU PLAYS. No hardware video decode is usable on the RK3326 under
 * this mainline kernel, so every picture is FFmpeg's software decoder on the
 * four Cortex-A35 cores. Measured on the Pixel 2 2026-11-05, decoding as fast
 * as it would go with four threads (a concert film, 23.976 fps):
 *
 *   H.264  640x360   242 fps     H.264 1280x720   68 fps
 *   H.264  854x480   142 fps     H.264 1920x1080  31 fps, all four cores
 *   HEVC   640x360    86 fps     VP9   640x360   161 fps
 *
 * So H.264 plays at full rate up to 720p, and 480p is the size worth making
 * for a 640x480 panel; HEVC and VP9 up to about 480p; 1080p only just keeps
 * up, and leans on the frame skipping below. AV1 does not play: this FFmpeg
 * has no software AV1 decoder. */
#include "vid.h"
#include "../mp4frame.h"

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

/* A picture goes out this long before its time: what the launcher takes to
 * notice it (a 4 ms poll), upload it and wait for the panel's next refresh
 * (16.7 ms at most). The sound's own delay is in the clock already - the
 * player counts what is still queued in ALSA. */
#define LEAD    0.025
/* Later than this, a picture is dropped rather than shown late... */
#define LATE    0.060
/* ...unless nothing has been shown for this long, so a file too heavy for
 * the CPU still moves rather than freezing. */
#define STILL   0.200
/* Further behind than this, the decoder skips the frames no other frame is
 * built from (B-frames, mostly) until it has caught up. */
#define BEHIND  0.250
/* vid_sync to within this of where the picture is moves nothing. */
#define NEAR    0.150

static mpf_head *g_shm;
static uint32_t  g_serial;

/* MUSE_VIDEO_DEBUG=1: every two seconds, how many pictures went out and were
 * dropped, and how far from their time they went out (the clock minus the
 * picture's time: about -LEAD when all is well). */
static struct { bool on; double since, sum, worst; long shown, dropped, n; } g_dbg;

static void dbg_note(double off, bool shown)
{
	struct timespec t;
	double now;

	if (!g_dbg.on) return;
	clock_gettime(CLOCK_MONOTONIC, &t);
	now = (double)t.tv_sec + t.tv_nsec / 1e9;
	if (shown) {
		g_dbg.shown++;
		g_dbg.sum += off;
		g_dbg.n++;
		if (fabs(off) > fabs(g_dbg.worst)) g_dbg.worst = off;
	} else {
		g_dbg.dropped++;
	}
	if (g_dbg.since == 0) g_dbg.since = now;
	if (now - g_dbg.since >= 2.0) {
		fprintf(stderr, "muse: video: %.1f fps shown, %ld dropped, off %+.1f ms avg, %+.1f worst\n",
		        g_dbg.shown / (now - g_dbg.since), g_dbg.dropped,
		        g_dbg.n ? g_dbg.sum / g_dbg.n * 1000 : 0, g_dbg.worst * 1000);
		memset(&g_dbg, 0, sizeof g_dbg);
		g_dbg.on = true;
		g_dbg.since = now;
	}
}

static struct {
	pthread_mutex_t mu;
	pthread_cond_t  cv;
	unsigned        req;          /* bumped by every vid_sync and vid_close */
	char            path[1024];
	double          at;
	bool            open;         /* that request is a file, not a close */
	double          heard;        /* the sound's clock... */
	struct timespec heard_t;      /* ...and when it said so */
	bool            playing;
	bool            show;
	bool            resync;       /* show came on: catch up with the sound */
	bool            quit;
} V = { .mu = PTHREAD_MUTEX_INITIALIZER };

static pthread_t g_th;
static bool      g_started;

typedef struct {
	char             path[1024];
	AVFormatContext *fmt;
	AVCodecContext  *cc;
	AVPacket        *pkt;
	AVFrame         *fr;
	struct SwsContext *sws;
	int              si;
	double           tb;          /* the stream's time base, in seconds */
	double           fdur;        /* one frame, from the stream's rate */
	double           skip_before; /* after a seek: frames before this are not shown */
	double           pos;         /* the newest frame's time */
	bool             flushed, eof;
} vdec;

static void say(const char *what, const char *path)
{
	const char *slash = strrchr(path, '/');

	fprintf(stderr, "muse: video: %s %s\n", what, slash ? slash + 1 : path);
}

/* A stream that is a picture to watch: not a cover carried as a one-frame
 * "video", which an MP3 or an M4A with art has. */
static int video_stream(AVFormatContext *fmt)
{
	unsigned i;

	for (i = 0; i < fmt->nb_streams; i++) {
		AVStream *st = fmt->streams[i];

		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		    !(st->disposition & (AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_TIMED_THUMBNAILS)) &&
		    st->codecpar->width > 0 && st->codecpar->height > 0)
			return (int)i;
	}
	return -1;
}

static void vdec_close(vdec *d)
{
	if (!d) return;
	sws_freeContext(d->sws);
	av_frame_free(&d->fr);
	av_packet_free(&d->pkt);
	avcodec_free_context(&d->cc);
	avformat_close_input(&d->fmt);
	free(d);
}

static vdec *vdec_open(const char *path)
{
	vdec *d = calloc(1, sizeof *d);
	const AVCodec *codec;
	AVStream *st;
	unsigned i;

	if (!d) return NULL;
	snprintf(d->path, sizeof d->path, "%s", path);
	if (avformat_open_input(&d->fmt, path, NULL, NULL) < 0 ||
	    avformat_find_stream_info(d->fmt, NULL) < 0 ||
	    (d->si = video_stream(d->fmt)) < 0)
		goto fail;
	st = d->fmt->streams[d->si];
	/* Only the picture's packets: the sound has its own demuxer. */
	for (i = 0; i < d->fmt->nb_streams; i++)
		if ((int)i != d->si) d->fmt->streams[i]->discard = AVDISCARD_ALL;
	if (!(codec = avcodec_find_decoder(st->codecpar->codec_id))) {
		say("no decoder for", path);
		goto fail;
	}
	if (!(d->cc = avcodec_alloc_context3(codec)) ||
	    avcodec_parameters_to_context(d->cc, st->codecpar) < 0)
		goto fail;
	/* Two threads carry anything up to 480p with room to spare (H.264 at
	 * 360p is a quarter of one core); more only for what needs them, since
	 * each frame thread holds a frame of delay and its own buffers. */
	d->cc->thread_count = st->codecpar->width * st->codecpar->height <= 640 * 480 ? 2 : 4;
	d->cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	if (avcodec_open2(d->cc, codec, NULL) < 0) goto fail;
	d->pkt = av_packet_alloc();
	d->fr = av_frame_alloc();
	if (!d->pkt || !d->fr) goto fail;
	d->tb = av_q2d(st->time_base);
	d->fdur = st->avg_frame_rate.num > 0 ? 1.0 / av_q2d(st->avg_frame_rate) : 1.0 / 25;
	d->skip_before = -1;
	fprintf(stderr, "muse: video: %s %dx%d %.2f fps, %d threads\n", codec->name,
	        d->cc->width, d->cc->height, 1.0 / d->fdur, d->cc->thread_count);
	return d;
fail:
	vdec_close(d);
	return NULL;
}

static void vdec_seek(vdec *d, double at)
{
	av_seek_frame(d->fmt, d->si, (int64_t)(at / d->tb), AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(d->cc);
	d->flushed = d->eof = false;
	d->skip_before = at;
	d->pos = at;
}

/* The next frame into d->fr: 1, or 0 at the end, <0 on an error. Frames
 * before a seek's target are decoded - they have to be - and not handed out. */
static int vdec_next(vdec *d, double *pts)
{
	for (;;) {
		int r = avcodec_receive_frame(d->cc, d->fr);

		if (r == 0) {
			int64_t ts = d->fr->best_effort_timestamp;
			double t = ts == AV_NOPTS_VALUE ? d->pos + d->fdur : ts * d->tb;

			d->pos = t;
			if (d->skip_before >= 0 && t < d->skip_before - d->fdur / 2) {
				av_frame_unref(d->fr);
				continue;
			}
			d->skip_before = -1;
			*pts = t;
			return 1;
		}
		if (r == AVERROR_EOF) return 0;
		if (r != AVERROR(EAGAIN)) return -1;
		if (d->flushed) return 0;
		if (av_read_frame(d->fmt, d->pkt) < 0) {
			avcodec_send_packet(d->cc, NULL);      /* drain what is left */
			d->flushed = true;
			continue;
		}
		if (d->pkt->stream_index == d->si) avcodec_send_packet(d->cc, d->pkt);
		av_packet_unref(d->pkt);
	}
}

static uint32_t color_of(const AVFrame *f)
{
	if (f->color_range == AVCOL_RANGE_JPEG || f->format == AV_PIX_FMT_YUVJ420P)
		return MPF_FULL;
	if (f->colorspace == AVCOL_SPC_BT709) return MPF_BT709;
	/* Untagged, the size says it: HD is 709 and SD 601, as players assume. */
	if (f->colorspace == AVCOL_SPC_UNSPECIFIED && f->height >= 720) return MPF_BT709;
	return MPF_BT601;
}

static void copy_plane(uint8_t *dst, uint32_t w, uint32_t h, const uint8_t *src, int pitch)
{
	uint32_t y;

	for (y = 0; y < h; y++) memcpy(dst + (size_t)y * w, src + (size_t)y * pitch, w);
}

/* d->fr into the next slot, and the launcher told. */
static void publish(vdec *d, double pts)
{
	AVFrame *f = d->fr;
	uint32_t w = (uint32_t)f->width, h = (uint32_t)f->height, ow = w, oh = h;
	bool direct = (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) &&
	              w <= MPF_MAX_W && h <= MPF_MAX_H;
	int s = mpf_begin(g_shm);
	uint8_t *y = mpf_slot_data(g_shm, s), *u, *v;

	if (!direct && (w > MPF_MAX_W || h > MPF_MAX_H)) {
		double k = fmin((double)MPF_MAX_W / w, (double)MPF_MAX_H / h);

		ow = (uint32_t)(w * k) & ~1u;
		oh = (uint32_t)(h * k) & ~1u;
	}
	u = y + (size_t)ow * oh;
	v = u + (size_t)mpf_chroma(ow) * mpf_chroma(oh);
	if (direct) {
		copy_plane(y, w, h, f->data[0], f->linesize[0]);
		copy_plane(u, mpf_chroma(w), mpf_chroma(h), f->data[1], f->linesize[1]);
		copy_plane(v, mpf_chroma(w), mpf_chroma(h), f->data[2], f->linesize[2]);
	} else {
		/* 10-bit HEVC, 4:2:2, NV12 and the rest: once through swscale. */
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
	g_shm->slot[s].color = color_of(f);
	g_shm->slot[s].serial = ++g_serial ? g_serial : ++g_serial;
	g_shm->slot[s].file = g_shm->file;
	g_shm->slot[s].pts = pts;
	if (f->sample_aspect_ratio.num > 0 && f->sample_aspect_ratio.den > 0) {
		g_shm->sar_num = (uint32_t)f->sample_aspect_ratio.num;
		g_shm->sar_den = (uint32_t)f->sample_aspect_ratio.den;
	}
	mpf_publish(g_shm, s);
}

/* The sound's clock, now: where it last said, and on from there while it
 * plays - a tenth of a second at most, so a stalled sound stalls the picture
 * rather than leaving it running alone. Under V.mu. */
static double clock_now(void)
{
	double t = V.heard;

	if (V.playing) {
		struct timespec n;
		double dt;

		clock_gettime(CLOCK_MONOTONIC, &n);
		dt = (double)(n.tv_sec - V.heard_t.tv_sec) + (n.tv_nsec - V.heard_t.tv_nsec) / 1e9;
		if (dt > 0.1) dt = 0.1;
		if (dt > 0) t += dt;
	}
	return t;
}

static double mono(void)
{
	struct timespec n;

	clock_gettime(CLOCK_MONOTONIC, &n);
	return (double)n.tv_sec + n.tv_nsec / 1e9;
}

/* Waits on V.cv for up to `sec`. Under V.mu. */
static void wait_for(double sec)
{
	struct timespec t;
	long ns;

	clock_gettime(CLOCK_MONOTONIC, &t);
	ns = t.tv_nsec + (long)(sec * 1e9);
	t.tv_sec += ns / 1000000000L;
	t.tv_nsec = ns % 1000000000L;
	pthread_cond_timedwait(&V.cv, &V.mu, &t);
}

static void *vthread(void *arg)
{
	vdec *d = NULL;
	unsigned seen = 0;
	bool have = false, preroll = false;
	double pts = 0, shown_at = 0;
	long dropped = 0, shown = 0;

	(void)arg;
	/* The sound first: a picture late by a frame is dropped and nobody
	 * notices, a sound late by a frame is a click. The decoder's own
	 * threads are made from this one and keep its niceness. */
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 5);

	pthread_mutex_lock(&V.mu);
	while (!V.quit) {
		double now;

		if (V.req != seen) {
			char path[1024];
			double at = V.at;
			bool open = V.open;

			seen = V.req;
			snprintf(path, sizeof path, "%s", V.path);
			V.resync = false;
			pthread_mutex_unlock(&V.mu);
			if (d && (!open || strcmp(d->path, path))) {
				if (dropped) fprintf(stderr, "muse: video: %ld shown, %ld dropped\n", shown, dropped);
				vdec_close(d);
				d = NULL;
				have = false;
			}
			if (!open) {
				__atomic_store_n(&g_shm->active, 0u, __ATOMIC_RELEASE);
			} else if (!d) {
				d = vdec_open(path);
				g_shm->file++;
				g_shm->sar_num = g_shm->sar_den = 1;
				__atomic_store_n(&g_shm->active, d ? 1u : 0u, __ATOMIC_RELEASE);
				if (d) vdec_seek(d, at);
				have = false;
				preroll = true;
				shown = dropped = 0;
			} else if (fabs((have ? pts : d->pos) - at) >= NEAR) {
				vdec_seek(d, at);
				have = false;
				preroll = true;
			}
			pthread_mutex_lock(&V.mu);
			continue;
		}
		if (!d || !V.show || (d->eof && !have)) {
			pthread_cond_wait(&V.cv, &V.mu);
			continue;
		}
		if (V.resync) {
			now = clock_now();
			V.resync = false;
			if (fabs((have ? pts : d->pos) - now) >= NEAR) {
				pthread_mutex_unlock(&V.mu);
				vdec_seek(d, now);
				pthread_mutex_lock(&V.mu);
				have = false;
				preroll = true;
			}
			continue;
		}
		if (!have) {
			int r;

			pthread_mutex_unlock(&V.mu);
			r = vdec_next(d, &pts);
			pthread_mutex_lock(&V.mu);
			if (r <= 0) {
				if (r < 0) say("decoding stopped in", d->path);
				d->eof = true;
				continue;
			}
			have = true;
			continue;               /* a request may have come meanwhile */
		}
		/* The first picture after an open or a seek goes out at once, playing
		 * or not: a seek while paused shows where it landed. */
		if (preroll) {
			pthread_mutex_unlock(&V.mu);
			publish(d, pts);
			av_frame_unref(d->fr);
			pthread_mutex_lock(&V.mu);
			have = preroll = false;
			shown_at = mono();
			continue;
		}
		if (!V.playing) { wait_for(0.25); continue; }
		now = clock_now();
		if (now + LEAD < pts) {
			double w = pts - LEAD - now;

			wait_for(w < 0.05 ? w : 0.05);
			continue;
		}
		pthread_mutex_unlock(&V.mu);
		if (now - pts > LATE && mono() - shown_at < STILL) {
			dropped++;
			dbg_note(now - pts, false);
		} else {
			publish(d, pts);
			shown++;
			shown_at = mono();
			dbg_note(now - pts, true);
		}
		av_frame_unref(d->fr);
		/* Read before the next decode, which is what it changes. */
		d->cc->skip_frame = now - pts > BEHIND ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
		pthread_mutex_lock(&V.mu);
		have = false;
	}
	pthread_mutex_unlock(&V.mu);
	vdec_close(d);
	return NULL;
}

bool vid_init(void)
{
	pthread_condattr_t ca;
	void *p;
	int fd;

	/* The same file every time, never removed: a launcher that mapped it
	 * from the daemon before this one keeps reading the right pages. */
	fd = open(MPF_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0 || ftruncate(fd, MPF_SIZE) != 0) {
		fprintf(stderr, "muse: video: cannot make %s\n", MPF_PATH);
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
	g_dbg.on = getenv("MUSE_VIDEO_DEBUG") != NULL;

	pthread_condattr_init(&ca);
	pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
	pthread_cond_init(&V.cv, &ca);
	pthread_condattr_destroy(&ca);
	if (pthread_create(&g_th, NULL, vthread, NULL) != 0) return false;
	g_started = true;
	return true;
}

void vid_quit(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&V.mu);
	V.quit = true;
	pthread_cond_signal(&V.cv);
	pthread_mutex_unlock(&V.mu);
	pthread_join(g_th, NULL);
	g_started = false;
}

void vid_sync(const char *path, double at)
{
	if (!g_started) return;
	pthread_mutex_lock(&V.mu);
	snprintf(V.path, sizeof V.path, "%s", path);
	V.at = at;
	V.open = true;
	V.req++;
	pthread_cond_signal(&V.cv);
	pthread_mutex_unlock(&V.mu);
}

void vid_close(void)
{
	if (!g_started) return;
	pthread_mutex_lock(&V.mu);
	if (V.open) {
		V.open = false;
		V.req++;
		pthread_cond_signal(&V.cv);
	}
	pthread_mutex_unlock(&V.mu);
}

void vid_clock(double heard, bool playing)
{
	bool changed;

	if (!g_started) return;
	pthread_mutex_lock(&V.mu);
	changed = playing != V.playing;
	V.heard = heard;
	clock_gettime(CLOCK_MONOTONIC, &V.heard_t);
	V.playing = playing;
	/* Only a change wakes the thread: the clock itself moves every 21 ms,
	 * and the thread already knows when its next picture is due. */
	if (changed) pthread_cond_signal(&V.cv);
	pthread_mutex_unlock(&V.mu);
}

void vid_show(bool on)
{
	if (!g_started) return;
	pthread_mutex_lock(&V.mu);
	if (on != V.show) {
		V.show = on;
		if (on) V.resync = true;
		pthread_cond_signal(&V.cv);
	}
	pthread_mutex_unlock(&V.mu);
}

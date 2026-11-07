/* SPDX-License-Identifier: GPL-3.0-only */
/* See cover.h. */
#include "cover.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

/* Which of a file's pictures is the cover. ID3 and FLAC say what each picture
 * is, and FFmpeg passes that on as the stream's comment - a file can carry the
 * back of the sleeve and the disc as well, and the first one is not always the
 * front. MP4 says nothing, and there the first picture is the only one. */
static AVStream *pick(AVFormatContext *fmt, const char **ext)
{
	AVStream *first = NULL;
	const char *first_ext = NULL;
	unsigned i;

	for (i = 0; i < fmt->nb_streams; i++) {
		AVStream *st = fmt->streams[i];
		AVDictionaryEntry *c;
		const char *e;

		if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;
		if (st->attached_pic.size <= 0) continue;
		/* The two formats the launcher's SDL_image was built with and that
		 * taggers write. A GIF or a BMP cover exists and is not worth one. */
		if (st->codecpar->codec_id == AV_CODEC_ID_MJPEG)    e = "jpg";
		else if (st->codecpar->codec_id == AV_CODEC_ID_PNG) e = "png";
		else continue;

		c = av_dict_get(st->metadata, "comment", NULL, 0);
		if (c && !strcmp(c->value, "Cover (front)")) { *ext = e; return st; }
		if (!first) { first = st; first_ext = e; }
	}
	*ext = first_ext;
	return first;
}

/* A VIDEO'S COVER, when it carries none: one of its own frames, MP4's poster
 * for the card on the shelf. A tenth of the way in, between 10 s and 2 min,
 * past the black of a fade-in and short of anything spoiled; the keyframe at
 * or before it, which needs no frames before it decoded. Scaled to 480 high
 * at most - the shelf draws a cover 461 high, and crops it square - and
 * written as a JPEG at high quality by FFmpeg's own encoder. A few hundred ms
 * on this CPU for a 1080p film, once: the launcher keeps the file.
 *
 * 1 written, 0 no picture to take, -1 an error, as cover_extract. */
static int poster(AVFormatContext *fmt, const char *base, char *out, size_t n)
{
	const AVCodec *dec = NULL, *enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	AVCodecContext *dc = NULL, *ec = NULL;
	AVPacket *pkt = av_packet_alloc(), *jp = av_packet_alloc();
	AVFrame *fr = av_frame_alloc(), *sc = av_frame_alloc();
	struct SwsContext *sws = NULL;
	char dir[1024], part[1100], *slash;
	double len = fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0, at;
	int si = -1, rc = -1, got = 0, tries = 0, ow, oh;
	unsigned i;
	FILE *f;

	for (i = 0; i < fmt->nb_streams && si < 0; i++) {
		AVStream *st = fmt->streams[i];

		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
		    !(st->disposition & (AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_TIMED_THUMBNAILS)) &&
		    st->codecpar->width > 0)
			si = (int)i;
	}
	if (si < 0) { rc = 0; goto done; }
	if (!pkt || !jp || !fr || !sc || !enc ||
	    !(dec = avcodec_find_decoder(fmt->streams[si]->codecpar->codec_id)) ||
	    !(dc = avcodec_alloc_context3(dec)) ||
	    avcodec_parameters_to_context(dc, fmt->streams[si]->codecpar) < 0 ||
	    avcodec_open2(dc, dec, NULL) < 0)
		goto done;
	for (i = 0; i < fmt->nb_streams; i++)
		if ((int)i != si) fmt->streams[i]->discard = AVDISCARD_ALL;
	at = len * 0.1;
	if (at < 10) at = len > 20 ? 10 : 0;
	if (at > 120) at = 120;
	if (at > 0)
		av_seek_frame(fmt, -1, (int64_t)(at * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
	/* The first frame out of the decoder after the seek: the keyframe. */
	while (!got && tries++ < 600) {
		int r = avcodec_receive_frame(dc, fr);

		if (r == 0) { got = 1; break; }
		if (r != AVERROR(EAGAIN)) break;
		if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(dc, NULL); continue; }
		if (pkt->stream_index == si) avcodec_send_packet(dc, pkt);
		av_packet_unref(pkt);
	}
	if (!got) goto done;

	oh = fr->height > 480 ? 480 : fr->height;
	ow = (int)((double)fr->width * oh / fr->height + 0.5) & ~1;
	oh &= ~1;
	if (fr->sample_aspect_ratio.num > 0 && fr->sample_aspect_ratio.den > 0)
		ow = (int)(ow * av_q2d(fr->sample_aspect_ratio) + 0.5) & ~1;
	sc->format = AV_PIX_FMT_YUVJ420P;
	sc->width = ow;
	sc->height = oh;
	if (av_frame_get_buffer(sc, 0) < 0) goto done;
	sws = sws_getContext(fr->width, fr->height, (enum AVPixelFormat)fr->format, ow, oh,
	                     AV_PIX_FMT_YUVJ420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (!sws) goto done;
	sws_scale(sws, (const uint8_t *const *)fr->data, fr->linesize, 0, fr->height,
	          sc->data, sc->linesize);

	if (!(ec = avcodec_alloc_context3(enc))) goto done;
	ec->width = ow;
	ec->height = oh;
	ec->pix_fmt = AV_PIX_FMT_YUVJ420P;
	ec->time_base = (AVRational){ 1, 25 };
	ec->flags |= AV_CODEC_FLAG_QSCALE;
	ec->global_quality = FF_QP2LAMBDA * 3;
	sc->quality = ec->global_quality;
	sc->pts = 0;
	if (avcodec_open2(ec, enc, NULL) < 0 || avcodec_send_frame(ec, sc) < 0 ||
	    avcodec_receive_packet(ec, jp) < 0)
		goto done;

	snprintf(dir, sizeof dir, "%s", base);
	slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; mkdir(dir, 0755); }
	snprintf(out, n, "%s.jpg", base);
	snprintf(part, sizeof part, "%s.part", out);
	f = fopen(part, "wb");
	rc = f && fwrite(jp->data, 1, (size_t)jp->size, f) == (size_t)jp->size ? 1 : -1;
	if (f && fclose(f) != 0) rc = -1;
	if (rc < 0 || rename(part, out) != 0) {
		remove(part);
		out[0] = '\0';
		rc = -1;
	}
done:
	sws_freeContext(sws);
	avcodec_free_context(&ec);
	avcodec_free_context(&dc);
	av_frame_free(&fr);
	av_frame_free(&sc);
	av_packet_free(&pkt);
	av_packet_free(&jp);
	return rc;
}

int cover_extract(const char *path, const char *base, char *out, size_t n)
{
	AVFormatContext *fmt = NULL;
	AVStream *st;
	const char *ext = NULL;
	char dir[1024], part[1100], *slash;
	FILE *f;
	int ok;

	out[0] = '\0';
	/* The header alone: every container puts its pictures there, so nothing
	 * has to be decoded or even probed past it. */
	if (avformat_open_input(&fmt, path, NULL, NULL) < 0) return -1;
	st = pick(fmt, &ext);
	if (!st) {
		/* No picture carried: a video's own frame, if it is a video. Only
		 * then is the file probed past its header - a song with no cover,
		 * the common case, stays the few milliseconds it always was. */
		unsigned i;

		ok = 0;
		for (i = 0; i < fmt->nb_streams; i++)
			if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
			    !(fmt->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC))
				break;
		if (i < fmt->nb_streams && avformat_find_stream_info(fmt, NULL) >= 0)
			ok = poster(fmt, base, out, n);
		avformat_close_input(&fmt);
		return ok;
	}

	snprintf(dir, sizeof dir, "%s", base);
	slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; mkdir(dir, 0755); }

	snprintf(out, n, "%s.%s", base, ext);
	snprintf(part, sizeof part, "%s.part", out);
	f = fopen(part, "wb");
	ok = f && fwrite(st->attached_pic.data, 1, (size_t)st->attached_pic.size, f)
	          == (size_t)st->attached_pic.size;
	if (f && fclose(f) != 0) ok = 0;
	avformat_close_input(&fmt);
	if (!ok || rename(part, out) != 0) {
		remove(part);
		out[0] = '\0';
		return -1;
	}
	return 1;
}

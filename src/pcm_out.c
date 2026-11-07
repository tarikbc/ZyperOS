/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See pcm_out.h. */
#include "pcm_out.h"

#include <stdio.h>
#include <string.h>

/* Each line once: the output is opened again after every game and every
 * quiet spell, and the same line each time says nothing new. */
static void say(pcm_out *o, const char *msg)
{
	if (strcmp(msg, o->said)) fprintf(stderr, "%s\n", msg);
	snprintf(o->said, sizeof o->said, "%s", msg);
}

#ifdef __linux__
#include <alsa/asoundlib.h>

bool pcm_out_open(pcm_out *o, const char *dev)
{
	snd_pcm_t *pcm;
	snd_pcm_uframes_t size = 0, period = 0, start;
	snd_pcm_sw_params_t *sw;
	char msg[160];
	int r;

	/* Opened without blocking, so a device someone holds whole says so at
	 * once instead of keeping the player's thread - and the launcher's exit,
	 * which waits for it - until they let go; then blocking, for the writes. */
	r = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
	if (r >= 0 && (r = snd_pcm_nonblock(pcm, 0)) < 0) snd_pcm_close(pcm);
	if (r < 0) {
		snprintf(msg, sizeof msg, "%s: open %s at %d Hz: %s", o->who, dev, o->rate,
		         snd_strerror(r));
		say(o, msg);
		return false;
	}
	/* Resampling allowed: "default" runs at 48 kHz and needs none, but a
	 * DAC's mixed device may run at the DAC's own rate. */
	r = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
	                       2, (unsigned)o->rate, 1, o->buffer_us);
	if (r >= 0) r = snd_pcm_get_params(pcm, &size, &period);
	/* set_params starts a stream only once its whole buffer is full, and a
	 * sound may be shorter than that: it would wait for the next one to be
	 * heard at all. */
	if (r >= 0 && o->start > 0) {
		start = (snd_pcm_uframes_t)o->start;
		snd_pcm_sw_params_alloca(&sw);
		r = snd_pcm_sw_params_current(pcm, sw);
		if (r >= 0)
			r = snd_pcm_sw_params_set_start_threshold(pcm, sw,
			        period < start ? period : start);
		if (r >= 0) r = snd_pcm_sw_params(pcm, sw);
	}
	if (r < 0) {
		snprintf(msg, sizeof msg, "%s: set up %s at %d Hz: %s", o->who, dev, o->rate,
		         snd_strerror(r));
		snd_pcm_close(pcm);
		say(o, msg);
		return false;
	}
	snprintf(msg, sizeof msg, "%s: %s open at %d Hz, %lu frames in %lu-frame periods",
	         o->who, dev, o->rate, (unsigned long)size, (unsigned long)period);
	say(o, msg);
	o->pcm = pcm;
	return true;
}

bool pcm_out_write(pcm_out *o, const int16_t *buf, int frames)
{
	while (frames > 0) {
		snd_pcm_sframes_t w = snd_pcm_writei(o->pcm, buf, (snd_pcm_uframes_t)frames);

		/* Running dry is ordinary - between two sounds nothing is written -
		 * and recover starts the stream again. */
		if (w < 0) {
			if (snd_pcm_recover(o->pcm, (int)w, 1) < 0) return false;
			continue;
		}
		buf += w * 2;
		frames -= (int)w;
	}
	return true;
}

long pcm_out_delay(pcm_out *o)
{
	snd_pcm_sframes_t d;

	return o->pcm && snd_pcm_delay(o->pcm, &d) == 0 ? (long)d : -1;
}

void pcm_out_close(pcm_out *o, bool drain)
{
	if (o->pcm && drain) snd_pcm_drain(o->pcm);
	if (o->pcm) snd_pcm_close(o->pcm);
	o->pcm = NULL;
}

#else   /* not __linux__: the desktop build, through SDL */
#include <SDL.h>

bool pcm_out_open(pcm_out *o, const char *dev)
{
	SDL_AudioSpec want, have;
	char msg[160];

	(void)dev;
	memset(&want, 0, sizeof want);
	want.freq = o->rate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = (Uint16)o->samples;
	if (!(o->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0))) {
		snprintf(msg, sizeof msg, "%s: %s", o->who, SDL_GetError());
		say(o, msg);
		return false;
	}
	SDL_PauseAudioDevice(o->dev, 0);
	return true;
}

bool pcm_out_write(pcm_out *o, const int16_t *buf, int frames)
{
	while (SDL_GetQueuedAudioSize(o->dev) > (Uint32)o->ahead * 4)
		SDL_Delay((Uint32)o->poll_ms);
	return SDL_QueueAudio(o->dev, buf, (Uint32)frames * 4) == 0;
}

long pcm_out_delay(pcm_out *o)
{
	(void)o;
	return -1;
}

void pcm_out_close(pcm_out *o, bool drain)
{
	/* SDL has no drain: the queue waited out, then the device's own buffer. */
	if (o->dev && drain) {
		while (SDL_GetQueuedAudioSize(o->dev) > 0) SDL_Delay(5);
		SDL_Delay((Uint32)o->tail_ms);
	}
	if (o->dev) SDL_CloseAudioDevice(o->dev);
	o->dev = 0;
}

#endif

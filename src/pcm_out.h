/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PCM_OUT_H
#define ZYPEROS_PCM_OUT_H

#include <stdbool.h>
#include <stdint.h>

/* A stereo S16 output, for menu music (src/bgm_out.c) and the interface
 * sounds (src/uisound_out.c). On a device it is ALSA's, opened by name:
 * "default" is the system's dmix, so the two mix with each other, with Muse
 * and with a paused game without any of them knowing. On the desktop build
 * it is SDL's default audio device, so the host build is heard too.
 *
 * Each player keeps one, on its own thread, so nothing here takes a lock. The
 * player sets the first fields once; the rest are the output's. */
typedef struct {
	const char *who;        /* the log lines' prefix: "bgm", "uisound" */
	int         rate;       /* frames a second */
	unsigned    buffer_us;  /* ALSA: how far ahead of what is heard it holds */
	int         start;      /* ALSA: frames that start the stream; 0, a full buffer */
	int         samples;    /* SDL: the device's own buffer, in frames */
	int         ahead;      /* SDL: frames queued before a write waits... */
	int         poll_ms;    /* ...and how often it looks again */
	int         tail_ms;    /* SDL: a drain's wait for the device's own buffer */

	void       *pcm;        /* ALSA's snd_pcm_t, NULL while closed */
	uint32_t    dev;        /* SDL's device, 0 while closed */
	char        said[160];  /* the last line logged, so each is said once */
} pcm_out;

bool pcm_out_open(pcm_out *o, const char *dev);

/* Every frame written, or false: the device has gone. */
bool pcm_out_write(pcm_out *o, const int16_t *buf, int frames);

/* ALSA: the frames written and not yet heard, or -1 when it cannot say, as a
 * stream that ran dry cannot. Always -1 on SDL, whose writes wait by `ahead`. */
long pcm_out_delay(pcm_out *o);

/* `drain`: what was written is heard first. A close drops it. */
void pcm_out_close(pcm_out *o, bool drain);

#endif

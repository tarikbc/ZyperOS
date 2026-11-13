/* SPDX-License-Identifier: MPL-2.0 */
/* NES player 2's share: see player2.h, and p2share.h for the file. */
#include "player2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "diatom.h"
#include "p2share.h"

/* Five of p2d's beats missed */
#define P2_BEAT_MS 500u

static char     g_path[512];
static void    *g_base;        /* the mapping, NULL while off */
static uint32_t g_beat;        /* pad_beat as last seen */
static unsigned g_beat_ms;     /* and when it last changed */
static bool     g_beat_seen;   /* it has changed since the file was made */

static void fail(const char *what)
{
	char msg[640];

	snprintf(msg, sizeof msg, "player2: %s %s: %s", what, g_path, strerror(errno));
	diatom_port_log(DIATOM_LOG_WARN, msg);
}

bool p2_set(bool on)
{
	p2s_head *hd;
	void *base;
	int fd;

	if (on == (g_base != NULL)) return true;
	if (!on) {
		/* Not a game's any more, for a p2d that still has it mapped */
		__atomic_store_n(&((p2s_head *)g_base)->magic, 0u, __ATOMIC_RELEASE);
		munmap(g_base, P2S_SIZE);
		g_base = NULL;
		unlink(g_path);
		return true;
	}

	/* Read once, here, so the off removes the file the on made. Another
	 * path in the desktop build only, so a test never meets a real share;
	 * a device build has P2S_PATH alone, where p2d looks. */
	{
		const char *p = NULL;
#ifdef DIATOM_P2_SHARE_ENV
		p = getenv("DIATOM_P2_SHARE");
#endif
		snprintf(g_path, sizeof g_path, "%s", p && *p ? p : P2S_PATH);
	}
	/* A new file, never one a Diatom that died left behind: it would hold
	 * that game's magic and frames before this one's. O_EXCL, so a name
	 * someone else made again in between is not written through. */
	unlink(g_path);
	fd = open(g_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0) { fail("open"); return false; }
	if (ftruncate(fd, P2S_SIZE) != 0) {
		fail("ftruncate");
		close(fd);
		unlink(g_path);
		return false;
	}
	base = mmap(NULL, P2S_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (base == MAP_FAILED) {
		fail("mmap");
		unlink(g_path);
		return false;
	}

	/* The file is zeros: no frame yet, no pad, no beat. The version, then
	 * the magic with a release, so a reader that sees the magic sees a file
	 * it can use (p2share.h). */
	hd = base;
	__atomic_store_n(&hd->version, P2S_VERSION, __ATOMIC_RELAXED);
	__atomic_store_n(&hd->magic, P2S_MAGIC, __ATOMIC_RELEASE);
	g_beat = 0;
	g_beat_seen = false;
	g_base = base;
	return true;
}

bool p2_on(void) { return g_base != NULL; }

void p2_frame(const void *data, unsigned w, unsigned h, size_t pitch, int fmt)
{
	if (!g_base || !data || data == RETRO_HW_FRAME_BUFFER_VALID) return;
	p2s_write(g_base, data, w, h, pitch, fmt);
}

uint32_t p2_pad(unsigned now_ms)
{
	p2s_head *hd = g_base;
	uint32_t beat;

	if (!hd) return 0;
	beat = __atomic_load_n(&hd->pad_beat, __ATOMIC_ACQUIRE);
	if (beat != g_beat) {
		g_beat = beat;
		g_beat_ms = now_ms;
		g_beat_seen = true;
	}
	if (!g_beat_seen || now_ms - g_beat_ms >= P2_BEAT_MS) return 0;
	return __atomic_load_n(&hd->pad, __ATOMIC_RELAXED) & P2S_PAD_MASK;
}

void p2_abandon(void)
{
	p2s_head *hd = g_base;

	if (!hd) return;
	/* A store and unlink(), both a signal handler's to make */
	__atomic_store_n(&hd->magic, 0u, __ATOMIC_RELEASE);
	unlink(g_path);
}

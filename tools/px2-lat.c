/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-lat: how fast the GKD Pixel 2 answers a button, and how even its frames
 * are, measured at the panel with nothing in the program changed.
 *
 *   px2-lat press BUTTON [N] [GAP_MS]   N presses (5), GAP_MS apart (600): the
 *                                       time from each press to the vertical
 *                                       blank that starts showing a new frame,
 *                                       each press just after a blank
 *   px2-lat pressr BUTTON [N] [GAP_MS]  the same, each press at a random point
 *                                       of the frame, as a thumb presses
 *   px2-lat hold BUTTON SECONDS         BUTTON held down: frames a second, and
 *                                       how many blanks each frame stayed up
 *   px2-lat watch SECONDS               the same with no button, for motion
 *                                       that runs by itself
 *   px2-lat owner BUTTON COMM [TIMEOUT_MS]
 *                                       a press, then the time until the panel
 *                                       shows a frame COMM made (a game's first
 *                                       frame: diatom; back on the shelf:
 *                                       zyperos.elf). Needs debugfs mounted.
 *
 * A frame is new when the CRTC scans out another framebuffer than at the
 * blank before, as px2-shot --fps counts them. Times come from the blank's own
 * timestamp (CLOCK_MONOTONIC) against the press's, so they include the input
 * path, the program's frame, its page flip and the wait for the blank, and
 * not the panel's scan from there (up to 16.7 ms more for the last line).
 * BUTTON is a px2-input name (a b x y up down left right l1 r1 l2 r2 select
 * start menu); the event goes to the joypad's node as px2-input sends it.
 *
 * Build: the plastron toolchain, no libraries. Run as root on the device. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NLONGS(n)       (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define HAS(bits, b)    ((bits[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

static const struct { const char *name; int code; } buttons[] = {
	{ "up", BTN_DPAD_UP },     { "down", BTN_DPAD_DOWN },
	{ "left", BTN_DPAD_LEFT }, { "right", BTN_DPAD_RIGHT },
	{ "a", BTN_SOUTH },        { "b", BTN_EAST },
	{ "x", BTN_NORTH },        { "y", BTN_WEST },
	{ "l1", BTN_TL },          { "r1", BTN_TR },
	{ "l2", BTN_TL2 },         { "r2", BTN_TR2 },
	{ "select", BTN_SELECT },  { "start", BTN_START },
	{ "menu", BTN_TRIGGER_HAPPY1 },
};

static int drm = -1, pad = -1;
static uint32_t crtc_on;

static double now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void sleep_ms(int ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static int button(const char *name)
{
	for (size_t b = 0; b < sizeof buttons / sizeof buttons[0]; b++)
		if (!strcmp(name, buttons[b].name))
			return buttons[b].code;
	fprintf(stderr, "px2-lat: no button \"%s\"\n", name);
	exit(2);
}

static void send(int code, int value)
{
	struct input_event ev[2];

	memset(ev, 0, sizeof ev);
	ev[0].type = EV_KEY;
	ev[0].code = (unsigned short)code;
	ev[0].value = value;
	ev[1].type = EV_SYN;
	ev[1].code = SYN_REPORT;
	if (write(pad, ev, sizeof ev) != (ssize_t)sizeof ev)
		perror("px2-lat: write");
}

static int open_pad(void)
{
	for (int i = 0; i < 32; i++) {
		char path[32];
		unsigned long keys[NLONGS(KEY_MAX)] = { 0 };
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && HAS(keys, BTN_SOUTH))
			return fd;
		close(fd);
	}
	return -1;
}

static int open_drm(void)
{
	for (int i = 0; i < 4; i++) {
		char path[32];
		struct drm_mode_card_res res;
		uint32_t crtcs[8];
		int fd;

		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
			continue;
		/* Never hold the master a program starting now needs (px2-shot) */
		ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
		memset(&res, 0, sizeof res);
		if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0) {
			if (res.count_crtcs > 8)
				res.count_crtcs = 8;
			res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
			res.count_fbs = res.count_connectors = res.count_encoders = 0;
			if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) == 0)
				for (uint32_t c = 0; c < res.count_crtcs; c++) {
					struct drm_mode_crtc crtc = { .crtc_id = crtcs[c] };

					if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 && crtc.mode_valid) {
						crtc_on = crtcs[c];
						return fd;
					}
				}
		}
		close(fd);
	}
	return -1;
}

/* The next blank: its time (ms, CLOCK_MONOTONIC) and the framebuffer it
 * starts scanning out */
static double blank(uint32_t *fb)
{
	union drm_wait_vblank vbl = { .request = { .type = _DRM_VBLANK_RELATIVE, .sequence = 1 } };
	struct drm_mode_crtc crtc = { .crtc_id = crtc_on };

	ioctl(drm, DRM_IOCTL_WAIT_VBLANK, &vbl);
	*fb = ioctl(drm, DRM_IOCTL_MODE_GETCRTC, &crtc) == 0 ? crtc.fb_id : 0;
	return vbl.reply.tval_sec * 1e3 + vbl.reply.tval_usec / 1e3;
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static int jitter;    /* press at a random point of the frame, not just after a blank */

static int press(int code, int n, int gap)
{
	double lat[64];
	int got = 0;

	if (n > 64)
		n = 64;
	for (int i = 0; i < n; i++) {
		uint32_t last, fb;
		double t0, t;

		sleep_ms(gap);
		blank(&last);
		if (jitter) sleep_ms(rand() % 17);
		t0 = now_ms();
		send(code, 1);
		for (;;) {
			t = blank(&fb);
			if (fb != last)
				break;
			if (t - t0 > 2000) {
				t = -1;
				break;
			}
		}
		/* Let go after a press's usual 80 ms, or at once if that has passed */
		if (now_ms() - t0 < 80)
			sleep_ms((int)(80 - (now_ms() - t0)));
		send(code, 0);
		if (t < 0) {
			printf("press %d: no new frame in 2 s\n", i + 1);
			continue;
		}
		lat[got++] = t - t0;
		printf("press %d: %.1f ms\n", i + 1, t - t0);
	}
	if (!got)
		return 1;
	qsort(lat, (size_t)got, sizeof lat[0], cmp_double);
	{
		double sum = 0;
		for (int i = 0; i < got; i++) sum += lat[i];
		printf("latency: median %.1f ms, mean %.1f, min %.1f, max %.1f over %d presses\n",
		       lat[got / 2], sum / got, lat[0], lat[got - 1], got);
	}
	return 0;
}

/* Frames over SECONDS: a frame's life in blanks (1 = a new frame at every
 * blank, 60 fps), the longest, and the share of blanks that brought nothing
 * while the picture was moving */
static int count(int code, int seconds)
{
	unsigned life[5] = { 0 }, longest = 0, frames = 0, run = 0, blanks;
	uint32_t last, fb;
	double t0, t;

	blank(&last);
	if (code >= 0)
		send(code, 1);
	t0 = now_ms();
	blanks = 0;
	do {
		t = blank(&fb);
		blanks++;
		run++;
		if (fb != last) {
			if (frames)	/* the first frame's life began before we looked */
				life[run > 4 ? 4 : run - 1]++, longest = run > longest ? run : longest;
			frames++;
			run = 0;
			last = fb;
		}
	} while (t - t0 < seconds * 1000.0);
	if (code >= 0)
		send(code, 0);
	printf("frames %u in %.2f s: %.1f fps\n", frames, (t - t0) / 1e3, frames * 1e3 / (t - t0));
	printf("frame life in blanks: 1:%u 2:%u 3:%u 4:%u 5+:%u, longest %u (%.0f ms)\n",
	       life[0], life[1], life[2], life[3], life[4], longest, longest * 1000.0 / 60);
	return 0;
}

/* The process that made framebuffer `id`, from debugfs's list:
 *   framebuffer[51]:
 *   	allocated by = diatom */
static int owner_of(uint32_t id, char *comm, size_t size)
{
	FILE *f = fopen("/sys/kernel/debug/dri/0/framebuffer", "r");
	char line[256];
	unsigned cur = 0;
	int found = 0;

	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		char name[64];

		if (sscanf(line, "framebuffer[%u]:", &cur) == 1)
			continue;
		if (cur == id && sscanf(line, " allocated by = %63s", name) == 1) {
			snprintf(comm, size, "%s", name);
			found = 1;
			break;
		}
	}
	fclose(f);
	return found ? 0 : -1;
}

static int owner(int code, const char *want, int timeout)
{
	uint32_t last = 0, fb;
	char comm[64] = "";
	double t0, t;

	if (access("/sys/kernel/debug/dri/0/framebuffer", R_OK)) {
		fprintf(stderr, "px2-lat: no /sys/kernel/debug/dri/0/framebuffer (mount debugfs)\n");
		return 1;
	}
	blank(&last);
	t0 = now_ms();
	send(code, 1);
	sleep_ms(80);
	send(code, 0);
	for (;;) {
		t = blank(&fb);
		if (fb != last && fb) {
			if (owner_of(fb, comm, sizeof comm) == 0 && !strcmp(comm, want)) {
				printf("%s on screen %.0f ms after the press\n", want, t - t0);
				return 0;
			}
			last = fb;
		}
		if (t - t0 > timeout) {
			printf("no frame from %s in %d ms (last: %s)\n", want, timeout, comm);
			return 1;
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: px2-lat press BUTTON [N] [GAP_MS] | hold BUTTON SECONDS"
		                " | watch SECONDS | owner BUTTON COMM [TIMEOUT_MS]\n");
		return 2;
	}
	if ((drm = open_drm()) < 0) {
		fprintf(stderr, "px2-lat: no display\n");
		return 1;
	}
	if (strcmp(argv[1], "watch") && (pad = open_pad()) < 0) {
		fprintf(stderr, "px2-lat: no joypad\n");
		return 1;
	}
	if (!strcmp(argv[1], "pressr")) {
		jitter = 1;
		srand((unsigned)now_ms());
		return press(button(argv[2]), argc > 3 ? atoi(argv[3]) : 5, argc > 4 ? atoi(argv[4]) : 600);
	}
	if (!strcmp(argv[1], "press"))
		return press(button(argv[2]), argc > 3 ? atoi(argv[3]) : 5, argc > 4 ? atoi(argv[4]) : 600);
	if (!strcmp(argv[1], "hold") && argc > 3)
		return count(button(argv[2]), atoi(argv[3]));
	if (!strcmp(argv[1], "watch"))
		return count(-1, atoi(argv[2]));
	if (!strcmp(argv[1], "owner") && argc > 3)
		return owner(button(argv[2]), argv[3], argc > 4 ? atoi(argv[4]) : 10000);
	fprintf(stderr, "px2-lat: unknown mode %s\n", argv[1]);
	return 2;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * splash-fb: ZyperOS's boot animation on the TrimUI Brick, from the moment its
 * framebuffer exists until ZyperOS draws its first frame.
 *
 * The Pixel 2's splash.c does the same through DRM; the Brick's display is
 * Allwinner's framebuffer driver, so this draws into /dev/fb0 instead. Its
 * frames (scripts/make-splash-frames.py --brick) are at the panel's own
 * 1024x768, and only the band of rows that changed from the frame before is
 * stored, zlib-compressed, so a frame is unpacked into the last one and only
 * that band copied to the screen. On the Brick that is ZyperOS's boot
 * animation (its tools/genboot.py), Zyper over the wordmark, played once
 * and cut wherever ZyperOS is ready; a loop in it, which the Pixel 2 plays
 * until ZyperOS is ready, is played once through here and held.
 *
 * It draws into the framebuffer's last page and shows it with a pan. ZyperOS,
 * drawing through the GPU, takes the screen in two steps: when it sets up EGL
 * the pan moves to the first of EGL's own pages, still empty, and about 0.6 s
 * later its first present moves it again. The first move, before ZyperOS has
 * said it is presenting, is undone: the pan goes straight back to this page
 * and the animation goes on. EGL's pages are never written to: followed into
 * one instead, the animation stayed under ZyperOS's first frames as they faded in,
 * as ZyperOS draws them over what its buffer held (tried 2026-10-13). A move
 * after that is ZyperOS's first frame, and this stops at once. ZyperOS says so
 * with a SIGUSR1 just before its first present, as on the Pixel 2; it used to
 * be its renderer's line in /tmp/zyperos.log, and the log is on the card now.
 *
 * EGL's setup clears the framebuffer's pages, this one too, so after each
 * frame the page is spot-checked against the frame and written again whole if
 * anything is gone (copying only each frame's band, the wordmark was wiped
 * and never came back). When the animation is over it holds the last frame
 * the same way, putting back EGL's pan if that comes later, until ZyperOS
 * draws (half an hour at the most; it was 15 s until a slow start had lines
 * to show).
 *
 * A start that takes longer than it should says so, as the Pixel 2's
 * splash.c does: from 5 s the step under way, from 20 s "Taking longer than
 * usual" and what to do; SELECT held 2 s shows the kernel's latest lines;
 * and every 2 s from the start a beat goes into the kernel's log (slow_tick;
 * slowline.h, slowio.h; ZyperOS's DESIGN.md 11.20).
 *
 * Started first thing, by inittab's first line; the framebuffer is there long
 * before the panel lights, so the first frame is waiting when it does.
 *
 * A line under the animation, when the start has one to say: which panel
 * the kernel found (init.d/brick puts it in /tmp/splash-say), looked for
 * every 100 ms and drawn over every frame from then on, in the letters of
 * ZyperOS's tools/genclosed.py (glyphs.bin), so a tester sees it with no
 * computer.
 *
 * `splash missing` is the start that can't go on: the card lacks the Brick's
 * closed files (the PowerVR driver and the XR829 firmware, the player's own
 * copy, which the ZyperOS Installer puts in the boot partition's closed/), so
 * init.d/brick stops the animation and starts this instead. It shows
 * ZyperOS's picture saying so (its tools/genclosed.py), in the language the
 * boot screen has (zylang, as the Pixel 2's splash reads it; English
 * without), and when POWER is pressed it powers the Brick off.
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "slowio.h"

#define FRAMES     "/usr/share/splash/frames.bin"
#define MISSING    "/usr/share/splash/closed-missing"
#define GLYPHS     "/usr/share/splash/glyphs.bin"
#define SAY        "/tmp/splash-say"
#define SAY_COLOR  0xc3c3ba         /* text-soft on the boot screen's ground */
#define BACKGROUND 0xff111310   /* the Brick's boot screen, if there are no frames */

/* Seconds since the kernel started, as the kernel log counts them */
static double since_boot(void)
{
	struct timespec now;

	clock_gettime(CLOCK_BOOTTIME, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

static void log_to_kernel(const char *text)
{
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (write(fd, text, strlen(text)) < 0) { /* a missing line is not fatal */ }
		close(fd);
	}
}

/* The frames file, mapped: header, offset table, then each frame's band */
static const uint8_t *frames;
static size_t frames_size;
static unsigned frame_count, fps, frame_w, frame_h;

static int frames_open(const char *path)
{
	struct stat st;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0 || fstat(fd, &st) < 0 || st.st_size < 12) return -1;
	frames = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (frames == MAP_FAILED || memcmp(frames, "TSPZ", 4)) return -1;
	frames_size = st.st_size;
	frame_w = frames[4] | frames[5] << 8;
	frame_h = frames[6] | frames[7] << 8;
	frame_count = frames[8] | frames[9] << 8;
	fps = frames[10] | frames[11] << 8;
	return frame_count && fps ? 0 : -1;
}

static uint32_t get32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/*
 * Frame n over frame n - 1 in pixels (frame_w * 4 bytes a row): its band
 * unpacked, then each byte given back the same byte of the pixel to its left
 * (the encoder took it away, as PNG's Sub filter does). The band's rows are
 * returned in *first and *rows, the only ones to copy to the screen; 0 rows
 * is a frame like the last, and -1 a frame that wouldn't unpack.
 */
static int frame_decode(unsigned n, uint8_t *pixels, unsigned *first, unsigned *rows)
{
	size_t stride = (size_t)frame_w * 4;
	uint32_t at = get32(frames + 12 + 4 * n);

	if (at + 8 > frames_size) return -1;
	*first = frames[at] | frames[at + 1] << 8;
	*rows = frames[at + 2] | frames[at + 3] << 8;
	uint32_t length = get32(frames + at + 4);
	if (!*rows) return 0;
	if (*first + *rows > frame_h || at + 8 + length > frames_size) return -1;

	uint8_t *band = pixels + *first * stride;
	uLongf size = *rows * stride;
	if (uncompress(band, &size, frames + at + 8, length) != Z_OK || size != *rows * stride)
		return -1;
	for (unsigned y = 0; y < *rows; y++) {
		uint8_t *row = band + y * stride;

		for (size_t i = 4; i < stride; i++)
			row[i] += row[i - 4];
	}
	return 0;
}

/* The page still shows the frame: 64 pixels spread over it compared */
static int page_intact(const uint32_t *page, const uint32_t *pixels, size_t words)
{
	for (size_t i = 0; i < 64; i++) {
		size_t at = i * (words / 64) + (i * 7919) % (words / 64);

		if (page[at] != pixels[at]) return 0;
	}
	return 1;
}

/* Where the screen is panned to now */
static uint32_t pan_now(int fd)
{
	struct fb_var_screeninfo var;

	return ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 ? var.yoffset : (uint32_t)-1;
}

/* ZyperOS is about to present (its SIGUSR1): from here a pan is its first frame */
static volatile sig_atomic_t zyperos_presenting;

static void on_zyperos(int sig)
{
	(void)sig;
	zyperos_presenting = 1;
}

/*
 * Still this one's screen: 1 to draw on, 0 when ZyperOS has presented. EGL's
 * first pan, when ZyperOS sets it up, is put back.
 */
static int still_ours(int fd, uint32_t yoffset, int *took_back)
{
	struct fb_var_screeninfo var;
	uint32_t now = pan_now(fd);

	if (now == yoffset) return 1;
	if (now == (uint32_t)-1 || *took_back || zyperos_presenting) return 0;
	*took_back = 1;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var)) return 0;
	var.xoffset = 0;
	var.yoffset = yoffset;
	return ioctl(fd, FBIOPAN_DISPLAY, &var) == 0;
}

/* The line's letters (glyphs.bin): "TSPG", height, baseline, first, count,
 * then each letter's advance and its advance x height coverage */
static const uint8_t *glyphs;
static size_t glyphs_size;
static const uint8_t *glyph_at[128];
static unsigned glyph_h;

static void glyphs_open(void)
{
	struct stat st;
	int fd = open(GLYPHS, O_RDONLY | O_CLOEXEC);

	if (fd < 0 || fstat(fd, &st) < 0 || st.st_size < 12) {
		if (fd >= 0) close(fd);
		return;
	}
	glyphs = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (glyphs == MAP_FAILED || memcmp(glyphs, "TSPG", 4)) {
		glyphs = NULL;
		return;
	}
	glyphs_size = st.st_size;
	glyph_h = glyphs[4] | glyphs[5] << 8;
	unsigned first = glyphs[8] | glyphs[9] << 8, count = glyphs[10] | glyphs[11] << 8;
	size_t at = 12;
	for (unsigned c = first; c < first + count && c < 128; c++) {
		if (at >= glyphs_size || at + 1 + (size_t)glyphs[at] * glyph_h > glyphs_size) break;
		glyph_at[c] = glyphs + at;
		at += 1 + (size_t)glyphs[at] * glyph_h;
	}
}

/* The start's line, "" for none: what /tmp/splash-say says, its first line */
static char say[96];

/* Whether /tmp/splash-say changed it */
static int say_read(void)
{
	char text[sizeof say] = "";
	int fd = open(SAY, O_RDONLY | O_CLOEXEC);

	if (fd >= 0) {
		ssize_t n = read(fd, text, sizeof text - 1);

		close(fd);
		text[n > 0 ? n : 0] = 0;
		text[strcspn(text, "\n")] = 0;
	}
	if (!strcmp(text, say)) return 0;
	strcpy(say, text);
	return 1;
}

/* The line over `pixels` (a whole frame, w x h), centered 48 rows above its
 * foot: each letter's coverage blended in SAY_COLOR. Rows it covers in *top
 * and *rows. */
static void say_draw(uint32_t *pixels, unsigned w, unsigned h, unsigned *top, unsigned *rows)
{
	unsigned width = 0, x, y0;
	const char *c;

	*rows = 0;
	if (!glyphs || !say[0] || glyph_h + 48 > h) return;
	for (c = say; *c; c++)
		if ((unsigned char)*c < 128 && glyph_at[(unsigned char)*c]) width += glyph_at[(unsigned char)*c][0];
	if (!width || width > w) return;
	x = (w - width) / 2;
	y0 = h - 48 - glyph_h;
	for (c = say; *c; c++) {
		const uint8_t *g = (unsigned char)*c < 128 ? glyph_at[(unsigned char)*c] : NULL;

		if (!g) continue;
		unsigned adv = g[0];
		for (unsigned y = 0; y < glyph_h; y++)
			for (unsigned i = 0; i < adv; i++) {
				unsigned a = g[1 + y * adv + i];
				uint32_t *p = &pixels[(size_t)(y0 + y) * w + x + i], d = *p;

				if (!a) continue;
				uint32_t r = ((d >> 16 & 0xff) * (255 - a) + (SAY_COLOR >> 16 & 0xff) * a) / 255;
				uint32_t gg = ((d >> 8 & 0xff) * (255 - a) + (SAY_COLOR >> 8 & 0xff) * a) / 255;
				uint32_t b = ((d & 0xff) * (255 - a) + (SAY_COLOR & 0xff) * a) / 255;
				*p = 0xff000000u | r << 16 | gg << 8 | b;
			}
		x += adv;
	}
	*top = y0;
	*rows = glyph_h;
}

/*
 * A start slower than usual (slowline.h; ZyperOS's DESIGN.md 11.20), as the
 * Pixel 2's splash.c says it: from 5 s the step under way at the foot, from
 * 20 s "Taking longer than usual" and what to do; SELECT held 2 s shows the
 * kernel's latest lines; and every 2 s a beat in the kernel's log. The
 * Brick's and the Smart Pro's boot screen is Salt & Pepper's whatever the
 * theme (they have its frames only). The panel line (/tmp/splash-say) sits
 * where the step goes, so it gives way to it.
 */
static slow_run slow;
static struct {
	int      loaded, said_off, hold;
	double   stage_at, beat_at;
	char     stage[4];
	unsigned frames;
	slowio_keys keys;
	slowio_kmsg kmsg;
} sl = { .beat_at = SLOW_BEAT_S };

/* One look at `now` over `pixels` (w x h): the rows that changed into
 * [*lo, *hi), and 1 when there are some */
static int slow_tick(uint32_t *pixels, unsigned w, unsigned h, double now, unsigned *lo, unsigned *hi)
{
	char line[96], foot[96];
	int l = 0, k = 0, held, changed;

	if (now >= sl.beat_at) {
		double stage_at = 0;

		slowio_stage(sl.stage, now, &stage_at);
		slow_beat(line, sizeof line, sl.beat_at, sl.frames, sl.stage);
		log_to_kernel(line);
		while (sl.beat_at <= now) sl.beat_at += SLOW_BEAT_S;
	}
	if (now < SLOW_STAGE_S - 0.5) return 0;
	if (!sl.loaded) {
		sl.loaded = 1;
		slow_run_init(&slow, w, h, w, 0, BACKGROUND, 0xffc3c3ba, 0xff969998);
		snprintf(line, sizeof line, "splash: slow, %s\n",
		         slowio_load(&slow, getenv("zylang")) == 0 ? "its lines ready" : "no lines to say it with");
		log_to_kernel(line);
	}
	slow.keep = sl.hold;
	slowio_stage(sl.stage, now, &sl.stage_at);
	held = slowio_select(&sl.keys, now) >= SLOW_TESTER_S;
	if (held) slowio_kmsg_poll(&sl.kmsg);
	snprintf(foot, sizeof foot, "%s  %.1f s  %u frames", sl.stage[0] ? sl.stage : "-", now, sl.frames);
	changed = slow_frame(&slow, pixels, now, sl.stage, held, sl.kmsg.line, sl.kmsg.n, foot, &l, &k);
	/* The panel line gives way the first time the lines say something */
	if (!sl.said_off && (slow.id[1] >= 0 || slow.tester)) {
		sl.said_off = 1;
		if (say[0] && glyph_h + 48 <= h) {
			unsigned y0 = h - 48 - glyph_h;

			slow_clear(pixels, w, w, h, 0, 0, (int)y0, w, glyph_h, BACKGROUND);
			if (!changed || (unsigned)k <= (unsigned)l) { l = (int)y0; k = (int)(y0 + glyph_h); }
			else { if ((int)y0 < l) l = (int)y0; if ((int)(y0 + glyph_h) > k) k = (int)(y0 + glyph_h); }
			changed = 1;
		}
		say[0] = 0;
		slow_frame(&slow, pixels, now, sl.stage, held, sl.kmsg.line, sl.kmsg.n, foot, &l, &k);
	}
	if (!changed || k <= l) return 0;
	*lo = (unsigned)l;
	*hi = (unsigned)k;
	return 1;
}

/* The missing-files picture for the boot screen's language, or English's */
static const char *missing_path(void)
{
	static char path[96];
	const char *lang = getenv("zylang");

	if (lang && *lang && strcmp(lang, "-") && strcmp(lang, "en") && !strchr(lang, '/')) {
		snprintf(path, sizeof path, MISSING "-%s.bin", lang);
		if (access(path, R_OK) == 0) return path;
	}
	return MISSING ".bin";
}

/* Until POWER is pressed on any input device that has it: the pad's, the
 * PMIC's power key */
static void wait_for_power(void)
{
	struct pollfd fds[16];
	int n = 0;
	DIR *dir = opendir("/dev/input");
	struct dirent *e;

	while (dir && n < 16 && (e = readdir(dir))) {
		char path[300];
		unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };

		if (strncmp(e->d_name, "event", 5)) continue;
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
		    keys[KEY_POWER / (8 * sizeof(long))] >> (KEY_POWER % (8 * sizeof(long))) & 1) {
			fds[n].fd = fd;
			fds[n++].events = POLLIN;
		} else {
			close(fd);
		}
	}
	if (dir) closedir(dir);
	if (!n) {
		/* No power key to wait for: the picture stays */
		for (;;) pause();
	}
	for (;;) {
		if (poll(fds, n, -1) <= 0) continue;
		for (int i = 0; i < n; i++) {
			struct input_event ev;

			if (!(fds[i].revents & POLLIN)) continue;
			while (read(fds[i].fd, &ev, sizeof ev) == sizeof ev)
				if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) return;
		}
	}
}

int main(int argc, char **argv)
{
	int missing = argc > 1 && !strcmp(argv[1], "missing");

	/* The boot screen's first word in the kernel's log: a console that
	 * ends before it (crashlog's report after a restart) stopped before
	 * init had started this */
	if (!missing) log_to_kernel("splash: started\n");

	/* First: SIGUSR1 left at its default would end this */
	struct sigaction on = { .sa_handler = on_zyperos };
	sigemptyset(&on.sa_mask);
	sigaction(SIGUSR1, &on, NULL);

	int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix) ||
	    var.bits_per_pixel != 32) {
		perror("splash: /dev/fb0");
		return 1;
	}
	uint32_t pages = var.yres_virtual / var.yres;
	if (pages < 2) {
		fprintf(stderr, "splash: the framebuffer has no spare page\n");
		return 1;
	}
	uint8_t *fbmem = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fbmem == MAP_FAILED) {
		perror("splash: mapping the framebuffer");
		return 1;
	}
	/* The last page, which ZyperOS's first presents don't use */
	uint32_t yoffset = (pages - 1) * var.yres;
	uint8_t *page = fbmem + (size_t)yoffset * fix.line_length;
	size_t size = (size_t)var.yres * fix.line_length;
	uint32_t *pixels = malloc(size);
	if (!pixels) {
		perror("splash: memory");
		return 1;
	}

	unsigned first_row, rows;
	int animated = frames_open(missing ? missing_path() : FRAMES) == 0 &&
		       frame_w == var.xres && frame_h == var.yres &&
		       fix.line_length == frame_w * 4 &&
		       frame_decode(0, (uint8_t *)pixels, &first_row, &rows) == 0;
	if (!animated) {
		for (size_t i = 0; i < size / 4; i++) pixels[i] = BACKGROUND;
		frame_count = 1;
	}
	/* The start's line over each frame (not over the missing-files picture,
	 * which says all there is to say) */
	unsigned say_top = 0, say_rows = 0;
	double say_looked = 0;
	if (!missing) {
		glyphs_open();
		say_read();
		say_draw(pixels, fix.line_length / 4, var.yres, &say_top, &say_rows);
	}
	memcpy(page, pixels, size);
	var.xoffset = 0;
	var.yoffset = yoffset;
	if (ioctl(fd, FBIOPAN_DISPLAY, &var)) {
		perror("splash: pan");
		return 1;
	}
	double first = since_boot();
	char line[256];
	snprintf(line, sizeof(line), "splash: first frame at %.3f s\n", first);
	log_to_kernel(line);
	if (missing) {
		log_to_kernel("splash: the Brick's closed files are missing; POWER powers it off\n");
		wait_for_power();
		log_to_kernel("splash: POWER pressed, powering off\n");
		execl("/sbin/poweroff", "poweroff", (char *)NULL);
		return 1;
	}

	/*
	 * The rest at the video's rate, each on its own deadline from the first.
	 * While waiting, the pan is looked at every 4 ms, so ZyperOS taking the
	 * screen is seen well before its presents come round to this page.
	 * Each frame goes in just after a vertical blank, and the copy (about
	 * 3 ms) stays ahead of the scan down the panel (16 ms), so it doesn't
	 * tear.
	 */
	unsigned shown = 0, repaired = 0;
	int stopped = 0, took_back = 0;
	for (unsigned n = 1; n < frame_count && !stopped; n++) {
		double due = first + (double)n / fps;

		if (frame_decode(n, (uint8_t *)pixels, &first_row, &rows))
			break;
		if (!sl.said_off && since_boot() - say_looked >= 0.1) {
			say_looked = since_boot();
			say_read();
		}
		say_draw(pixels, fix.line_length / 4, var.yres, &say_top, &say_rows);
		/* The band to copy grows to the line's rows */
		if (say_rows) {
			unsigned last = rows ? first_row + rows : say_top + say_rows;

			if (!rows || say_top < first_row) first_row = say_top;
			if (say_top + say_rows > last) last = say_top + say_rows;
			rows = last - first_row;
		}
		/* ...and to the slow start's, when they changed */
		{
			unsigned lo, hi;

			if (slow_tick(pixels, fix.line_length / 4, var.yres, since_boot(), &lo, &hi)) {
				unsigned last = rows ? first_row + rows : hi;

				if (!rows || lo < first_row) first_row = lo;
				if (hi > last) last = hi;
				rows = last - first_row;
			}
		}
		for (;;) {
			if (!still_ours(fd, yoffset, &took_back)) {
				stopped = 1;
				break;
			}
			double wait = due - since_boot();
			if (wait <= 0) break;
			struct timespec t = { 0, (long)((wait < 0.004 ? wait : 0.004) * 1e9) };
			nanosleep(&t, NULL);
		}
		if (stopped) break;
		uint32_t zero = 0;
		ioctl(fd, FBIO_WAITFORVSYNC, &zero);
		if (!still_ours(fd, yoffset, &took_back)) {
			stopped = 1;
			break;
		}
		memcpy(page + (size_t)first_row * fix.line_length,
		       (uint8_t *)pixels + (size_t)first_row * fix.line_length,
		       (size_t)rows * fix.line_length);
		if (!page_intact((uint32_t *)page, pixels, size / 4)) {
			memcpy(page, pixels, size);
			repaired++;
		}
		shown = n;
		sl.frames++;
	}

	/* The last frame held until ZyperOS draws: half an hour at the most, as
	 * long as a start that is stuck may want its lines (it was 15 s) */
	double give_up = since_boot() + 1800;
	sl.hold = 1;        /* no frame writes over the lines from here */
	while (!stopped && since_boot() < give_up) {
		struct timespec t = { 0, 8000000 };
		unsigned lo, hi;

		if (!still_ours(fd, yoffset, &took_back)) {
			stopped = 1;
			break;
		}
		/* The slow start's lines, their changed rows straight to the page */
		if (slow_tick(pixels, fix.line_length / 4, var.yres, since_boot(), &lo, &hi))
			memcpy(page + (size_t)lo * fix.line_length, (uint8_t *)pixels + (size_t)lo * fix.line_length,
			       (size_t)(hi - lo) * fix.line_length);
		if (!sl.said_off && since_boot() - say_looked >= 0.1) {
			say_looked = since_boot();
			if (say_read()) {
				say_draw(pixels, fix.line_length / 4, var.yres, &say_top, &say_rows);
				memcpy(page, pixels, size);
			}
		}
		if (!page_intact((uint32_t *)page, pixels, size / 4)) {
			memcpy(page, pixels, size);
			repaired++;
		}
		nanosleep(&t, NULL);
	}
	snprintf(line, sizeof(line), "splash: %s at frame %u of %u, %.3f s%s, page rewritten %u times\n",
		 stopped ? "ZyperOS took over" : "gave up waiting for ZyperOS", shown, frame_count, since_boot(),
		 took_back ? " (after putting EGL's first pan back)" : "", repaired);
	log_to_kernel(line);
	/* The picture stays in the framebuffer's memory without this process */
	return 0;
}

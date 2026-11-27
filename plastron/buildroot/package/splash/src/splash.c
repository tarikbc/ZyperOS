/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * splash: ZyperOS's boot animation on the Pixel 2, from the moment the panel
 * lights until ZyperOS is ready.
 *
 * The frames are ZyperOS's own boot animation (res/boot/, from its
 * tools/genboot.py), turned onto the portrait panel ahead of time by
 * scripts/make-splash-frames.py: the first frame whole, then each one as the
 * band of rows that changed from the one before, zlib'd. An animation may
 * end in a loop, played over and over from a given frame (an idle Zyper
 * after his intro), with one more band that leads from the last frame back
 * to the loop's first.
 *
 * It plays at the animation's own rate and never holds anything up: ZyperOS
 * starts alongside it, and when ZyperOS has its first frame ready it sends
 * SIGUSR1. The splash stops on the frame it is showing, lets go of the
 * display, and keeps that frame up until ZyperOS's replaces it. An animation
 * with no loop that finishes first simply stays on its last frame.
 *
 * Logs "splash: panel lit at ... s", which is how the boot's first picture is
 * timed, and where it stopped.
 *
 * While the image's init copies ZyperOS onto the card (a card's first start,
 * or the first start of a newer system image: 468 MB, about 54 s on a Pixel
 * 2), it says so: once the intro has written the word, a line under the
 * lockup and a bar that fills as the copy lands on the card, over the loop
 * (ZyperOS's DESIGN.md 11.20, first-start.bin; see copy_ask below).
 *
 * In the theme the player chose (ZyperOS's DESIGN.md 11.21): Beard's frames
 * and line (frames-light.bin, first-start-light.bin) or Salt & Pepper's.
 * The choice is on the kernel's line, put there by boot.scr from theme.txt
 * on the boot partition, and comes here in the environment init was given:
 * the kernel hands init every word without a dot. See theme_light.
 *
 * splash --still FILE shows one picture instead and holds it the same way:
 * the loading screen ZyperOS saves for a port (raw XRGB8888 at the panel's
 * own 480x640, already turned), up from the moment ZyperOS lets go of the
 * display until the port draws its first frame, so a port that loads for a
 * minute shows what it is doing rather than a black panel. launch.sh starts
 * it and kills it when the port ends.
 *
 * With SPLASH_LATER=FILE2 in its environment it writes FILE2, the same
 * screen with the line after a minute ("Still starting. Hold MENU and START
 * to go back."), into its own buffer SPLASH_LATER_S seconds later (60). Its
 * own buffer only: a port that has taken the display by then, through DRM or
 * the fbdev console, shows its own picture, and the write is seen by nobody.
 * An older splash ignores the variable and keeps the first picture.
 *
 * With SPLASH_OUT and SPLASH_OUT_LOG it also draws the port's latest output
 * at the screen's foot, the same way, into its own buffer only: the letters
 * and the box ZyperOS's launcher wrote, and the log the port prints into,
 * followed from where it ends as this starts, a few times a second while the
 * panel shows this buffer and not at all while it shows anything else, and
 * drawn once the screen has been up 5 s in a row, so a quick start shows
 * nothing new (portout.h, out_tick below; ZyperOS's DESIGN.md 11.11).
 */
#define _GNU_SOURCE             /* nftw */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <zlib.h>

#include "bright.h"
#include "portout.h"
#include "zytheme.h"

#define FRAMES     "/usr/share/splash/frames.bin"
#define FIRST      "/usr/share/splash/first-start.bin"
#define FRAMES_LIGHT "/usr/share/splash/frames-light.bin"
#define FIRST_LIGHT  "/usr/share/splash/first-start-light.bin"
#define COPY_ASK   "/tmp/splash-copy"
#define BACKGROUND 0xff111310   /* the Brick's boot screen, if there are no frames */
#define GROUND_LIGHT 0xffe6e4da /* Beard's ground, ZyperOS's DESIGN.md 3.1 */

static volatile sig_atomic_t stop;

static void on_stop(int sig) { (void)sig; stop = 1; }

/* Seconds since the kernel started, as the kernel log counts them */
static double since_boot(void)
{
	struct timespec now;

	clock_gettime(CLOCK_BOOTTIME, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

/* A line in the kernel log, where boot timing is read from */
static void log_to_kernel(const char *text)
{
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (write(fd, text, strlen(text)) < 0) { /* a missing line is not fatal */ }
		close(fd);
	}
}

/* The frames file, mapped: "TSPY", the size, the frame count and rate, the
 * loop's first frame (frame_count for none), then the offset of each band:
 * every frame's, and after them the one from the last back to the loop */
/* The theme's files (theme_light, in main) */
static const char *frames_path = FRAMES, *first_path = FIRST;

#define HEADER 16
static const uint8_t *frames;
static size_t frames_size;
static unsigned frame_count, fps, frame_w, frame_h, loop_from;

static int frames_open(void)
{
	struct stat st;
	int fd = open(frames_path, O_RDONLY | O_CLOEXEC);

	if (fd < 0 || fstat(fd, &st) < 0 || st.st_size < HEADER) return -1;
	frames = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (frames == MAP_FAILED || memcmp(frames, "TSPY", 4)) return -1;
	frames_size = st.st_size;
	frame_w = frames[4] | frames[5] << 8;
	frame_h = frames[6] | frames[7] << 8;
	frame_count = frames[8] | frames[9] << 8;
	fps = frames[10] | frames[11] << 8;
	loop_from = frames[12] | frames[13] << 8;
	return frame_count && fps && HEADER + 4 * (frame_count + 1) <= frames_size ? 0 : -1;
}

static uint32_t get32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/*
 * Band n over the frame before it, in ordinary memory (frame_w * 4 bytes a
 * row; the dumb buffer's pitch is checked to be the same), and the rows it
 * covers in *first and *rows: unpacked, then each byte given back the same
 * byte of the pixel to its left, which the encoder took away as PNG's Sub
 * filter does. Ordinary memory and a copy of the rows after, because the
 * buffer the display reads is uncached and writing it piecemeal is slow.
 * 0 rows is a frame like the one before; -1, a band that would not unpack.
 * The Brick's splash-fb.c reads the same bands.
 */
static int frame_decode(unsigned n, uint8_t *pixels, unsigned *first, unsigned *rows)
{
	size_t stride = (size_t)frame_w * 4;
	uint32_t at = get32(frames + HEADER + 4 * n);

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

/*
 * The first start's line and bar (first-start.bin, from
 * scripts/make-splash-frames.py --first-start): a rect of the panel, three
 * pictures of it (the setup's line with the bar empty, the update's, the bar
 * full) and where the bar is in it. The image's init writes COPY_ASK, one
 * line, "setup CARD SOURCE" or "update CARD SOURCE", just before it copies
 * SOURCE onto CARD; the loop looks for it each frame (a file, not a signal: a
 * signal before this has its handler would end the splash and blank the
 * panel). The bar fills as the copy reaches the card: the sectors written to
 * CARD's partition (its /sys stat) against SOURCE's files in 4 KB pages, as
 * the page cache writes them. Not the card's used space: exFAT allocates as
 * cp writes into the cache, 80 MB ahead of the card, and the init's sync
 * after the copy takes the last 6 s (measured on a Pixel 2, 2026-11-06:
 * 480 MB in pages, 483 MB written, the rest exFAT's own). It never goes back.
 */
static struct {
	int asked, ready, shown;
	unsigned x, y, w, h, bx0, bx1, by0, by1;
	uint32_t *line, *bar;
	char card[128], stat[64];   /* CARD, and its partition's /sys stat */
	unsigned long long written0, total;
	unsigned filled;            /* the bar's rows filled, from by1 up */
	int dirty;                  /* buffers still to be given the rect */
} copy;

/* Bytes written to CARD's partition since the system started: the seventh
 * field of its stat, in 512-byte sectors */
static unsigned long long copy_written(void)
{
	char text[256];
	unsigned long long f[7] = { 0 };
	int fd = open(copy.stat, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) return copy.written0;
	n = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (n <= 0) return copy.written0;
	text[n] = 0;
	if (sscanf(text, "%llu %llu %llu %llu %llu %llu %llu",
		   &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7)
		return copy.written0;
	return f[6] * 512;
}

static unsigned long long copy_sum;

static int copy_add(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)path; (void)ftw;
	if (type == FTW_F) copy_sum += ((unsigned long long)st->st_size + 4095) / 4096 * 4096;
	return 0;
}

static uint32_t *copy_picture(const uint8_t *file, size_t size, size_t *at)
{
	uLongf want = (uLongf)copy.w * copy.h * 4, got = want;
	uint32_t *px;

	if (*at + 4 > size) return NULL;
	uint32_t length = get32(file + *at);
	*at += 4;
	if (*at + length > size || !(px = malloc(want))) return NULL;
	if (uncompress((uint8_t *)px, &got, file + *at, length) != Z_OK || got != want) {
		free(px);
		return NULL;
	}
	*at += length;
	return px;
}

/* The pictures of the line in a language (ZyperOS's src/i18n.h code): the
 * theme's file with the code before .bin, first-start-pt-BR.bin, when there
 * is one; the theme's own, English's, for "en", "-", nothing, a word that is
 * no code, or a language with no pictures here. ZyperOS's DESIGN.md 11.20. */
static const char *first_for_lang(const char *lang, char *buf, size_t n)
{
	size_t base = strlen(first_path) - 4;   /* less ".bin" */

	if (!lang || !*lang || !strcmp(lang, "en") || !strcmp(lang, "-") || strlen(lang) > 15)
		return first_path;
	for (const char *c = lang; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '-')) return first_path;
	snprintf(buf, n, "%.*s-%s.bin", (int)base, first_path, lang);
	return access(buf, R_OK) == 0 ? buf : first_path;
}

/* COPY_ASK is there: read it, the pictures and the copy's size. 0 when the
 * line and bar can go up; -1 leaves the loop as it was. Its fourth word is
 * the card's language, which zyperos.init reads off the card (a system
 * update's copy, on a card whose boot.scr may be older than zylang); without
 * it, the kernel line's zylang, from theme.txt; without either, English. */
static int copy_start(void)
{
	char mode[16] = "", source[256] = "", lang[16] = "", lpath[128];
	FILE *f = fopen(COPY_ASK, "r");
	const char *path;
	struct stat st;

	if (!f) return -1;
	int n = fscanf(f, "%15s %127s %255s %15s", mode, copy.card, source, lang);
	fclose(f);
	if (n < 3) return -1;
	path = first_for_lang(n == 4 ? lang : getenv("zylang"), lpath, sizeof lpath);

	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0 || fstat(fd, &st) < 0 || st.st_size < 24) {
		if (fd >= 0) close(fd);
		return -1;
	}
	uint8_t *file = malloc(st.st_size);
	if (!file || read(fd, file, st.st_size) != st.st_size || memcmp(file, "TSPC", 4)) {
		close(fd);
		free(file);
		return -1;
	}
	close(fd);
	unsigned g[11];
	for (int i = 0; i < 11; i++) g[i] = file[4 + 2 * i] | file[5 + 2 * i] << 8;
	copy.x = g[0]; copy.y = g[1]; copy.w = g[2]; copy.h = g[3];
	copy.bx0 = g[4]; copy.bx1 = g[5]; copy.by0 = g[6]; copy.by1 = g[7];
	size_t at = 24;
	uint32_t *pics[3] = { NULL, NULL, NULL };
	for (unsigned i = 0; i < 3 && i < g[8]; i++) pics[i] = copy_picture(file, st.st_size, &at);
	free(file);
	if (g[8] < 3 || !pics[0] || !pics[1] || !pics[2] ||
	    copy.x + copy.w > frame_w || copy.y + copy.h > frame_h ||
	    copy.bx1 > copy.w || copy.by1 > copy.h || copy.bx0 >= copy.bx1 || copy.by0 >= copy.by1) {
		for (int i = 0; i < 3; i++) free(pics[i]);
		return -1;
	}
	int update = !strcmp(mode, "update");
	copy.line = pics[update];
	copy.bar = pics[2];
	free(pics[!update]);

	if (stat(copy.card, &st)) return -1;
	snprintf(copy.stat, sizeof(copy.stat), "/sys/dev/block/%u:%u/stat",
		 major(st.st_dev), minor(st.st_dev));
	if (access(copy.stat, R_OK)) return -1;
	copy.written0 = copy_written();
	copy_sum = 0;
	nftw(source, copy_add, 16, FTW_PHYS);
	copy.total = copy_sum;
	if (!copy.total) return -1;

	char line[160];
	snprintf(line, sizeof(line), "splash: the copy's line and bar up (%s, %s), %llu MB to copy at %.3f s\n",
		 mode, path + strlen("/usr/share/splash/"), copy.total >> 20, since_boot());
	log_to_kernel(line);
	return 0;
}

/* The bar to how much of the copy is on the card, and the rect into the
 * frame being made: every frame, since a band of the loop rewrites whole
 * rows, the rect's among them. */
static void copy_draw(uint32_t *pixels)
{
	unsigned long long written = copy_written(), done = written > copy.written0 ? written - copy.written0 : 0;
	/* The Pixel's bar is tall: the landscape picture was turned, so left to
	 * right became bottom to top. The R36S's is wide: its picture is not
	 * turned, and the bar fills left to right. */
	int across = (copy.bx1 - copy.bx0) > (copy.by1 - copy.by0);
	unsigned len = across ? copy.bx1 - copy.bx0 : copy.by1 - copy.by0;
	unsigned rows = done >= copy.total ? len : (unsigned)(done * len / copy.total);

	if (rows > copy.filled) {
		copy.filled = rows;
		copy.dirty = 2;
	}
	for (unsigned r = 0; r < copy.h; r++) {
		uint32_t *dst = pixels + (size_t)(copy.y + r) * frame_w + copy.x;

		memcpy(dst, copy.line + (size_t)r * copy.w, copy.w * 4);
		if (across) {
			if (copy.filled && r >= copy.by0 && r < copy.by1)
				memcpy(dst + copy.bx0,
				       copy.bar + (size_t)r * copy.w + copy.bx0,
				       copy.filled * 4);
		} else if (r >= copy.by1 - copy.filled && r < copy.by1) {
			memcpy(dst + copy.bx0, copy.bar + (size_t)r * copy.w + copy.bx0,
			       (copy.bx1 - copy.bx0) * 4);
		}
	}
}

struct buffer {
	uint32_t fb;
	uint32_t *map;
};

static int buffer_create(int fd, const drmModeModeInfo *mode, struct buffer *b,
			 uint32_t *pitch, uint64_t *size)
{
	struct drm_mode_create_dumb create = {
		.width = mode->hdisplay, .height = mode->vdisplay, .bpp = 32
	};

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) return -1;
	if (drmModeAddFB(fd, mode->hdisplay, mode->vdisplay, 24, 32, create.pitch,
			 create.handle, &b->fb)) return -1;
	struct drm_mode_map_dumb map = { .handle = create.handle };
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) return -1;
	b->map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (b->map == MAP_FAILED) return -1;
	*pitch = create.pitch;
	*size = create.size;
	return 0;
}

/*
 * No color correction: the display controller's gamma table stays empty, so
 * the panel shows each frame's colors as they are, as the stock firmware
 * does. Up to ZyperOS 1.0.4 this filled it (red and green as in^1.5, blue as
 * it was, set by eye against a TrimUI Brick), which darkened everything and
 * took the warmth out of it: a mid grey reached the panel as (91,91,134). A
 * player compared a SNES game with the stock firmware and saw it (2026-11-12).
 * ZyperOS's launcher and Diatom empty a table an older splash filled
 * (ZyperOS's src/panelgamma.h).
 */

static void flip_done(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
	(void)fd; (void)seq; (void)sec; (void)usec;
	*(int *)data = 1;
}

static int still(const char *path);

/*
 * Beard (1) or Salt & Pepper (0), as the launcher would pick it: zytheme is
 * the Appearance setting's word (ZyperOS's src/theme.h), anything else and
 * nothing at all Beard, the launcher's default, which is also what a card
 * with no choice yet starts in. Auto is Salt & Pepper in the Night Hours its
 * word carries (auto-1260-420: 21:00 to 7:00; a 1.0.5 launcher's plain auto,
 * 19:00 to 7:00), local time in zyzone (the clock's zone; the kernel set the
 * clock from the RTC, which keeps UTC), as theme_resolve_min has it
 * (zytheme.h).
 */
static int theme_light(char *why, size_t n)
{
	const char *t = getenv("zytheme"), *z = getenv("zyzone");
	struct tm tm;
	time_t now;

	if (!t || strncmp(t, "auto", 4)) {
		snprintf(why, n, "%s", t ? t : "no choice");
		return splash_zytheme_light(t, -1);
	}
	if (z && *z && strcmp(z, "-")) setenv("TZ", z, 1);
	tzset();
	now = time(NULL);
	if (!localtime_r(&now, &tm)) { snprintf(why, n, "%s, no clock", t); return 1; }
	snprintf(why, n, "%s, %02d:%02d in %s", t, tm.tm_hour, tm.tm_min, z && *z ? z : "UTC");
	return splash_zytheme_light(t, tm.tm_hour * 60 + tm.tm_min);
}

/* The backlight at the player's brightness before the panel lights (zybright,
 * bright.h): every backlight the kernel has, each within its own
 * max_brightness. None kept, or a boot.scr from before zybright: the
 * kernel's own level, as before. */
static void bright_apply(void)
{
	const char *w = getenv("zybright");
	char path[300], line[96];
	struct dirent *e;
	DIR *d;
	int set = -1;

	if (!w || !strcmp(w, "-")) return;
	if (!(d = opendir("/sys/class/backlight"))) return;
	while ((e = readdir(d))) {
		FILE *f;
		int max = 0, v;

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/backlight/%s/max_brightness", e->d_name);
		if (!(f = fopen(path, "r"))) continue;
		if (fscanf(f, "%d", &max) != 1) max = 0;
		fclose(f);
		if ((v = splash_bright_value(w, max)) < 0) continue;
		snprintf(path, sizeof path, "/sys/class/backlight/%s/brightness", e->d_name);
		if ((f = fopen(path, "w"))) {
			fprintf(f, "%d\n", v);
			if (fclose(f) == 0) set = v;
		}
	}
	closedir(d);
	if (set > 0) snprintf(line, sizeof line, "splash: the backlight at %d, the player's\n", set);
	else snprintf(line, sizeof line, "splash: no backlight takes zybright=%.16s\n", w);
	log_to_kernel(line);
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = on_stop };
	char why[96], line0[160];
	int light, light_ground;
	/* A start a charger made (zycharge=1 on the kernel's line, from
	 * boot.scr when U-Boot saw the PMIC woken by the plug): the handheld is
	 * not starting, so no boot animation, only the theme's plain ground, lit
	 * and kept until ZyperOS draws its charging screen on the same ground
	 * (its DESIGN.md 11.25) */
	const char *zc = getenv("zycharge");
	int charging = zc && !strcmp(zc, "1");

	/* No SA_RESTART: a wait in progress returns, and the loop sees the flag */
	sigaction(SIGUSR1, &sa, NULL);

	if (argc == 3 && !strcmp(argv[1], "--still"))
		return still(argv[2]);

	light = light_ground = theme_light(why, sizeof why);
	if (light && access(FRAMES_LIGHT, R_OK) == 0) {
		frames_path = FRAMES_LIGHT;
		first_path = FIRST_LIGHT;
	} else {
		light = 0;
	}
	snprintf(line0, sizeof(line0), "splash: %s (%s)%s\n", light_ground ? "Beard" : "Salt & Pepper",
		 why, charging ? ", a charger's start: the ground only" : "");
	log_to_kernel(line0);

	int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("splash: /dev/dri/card0");
		return 1;
	}

	drmModeRes *res = drmModeGetResources(fd);
	drmModeConnector *conn = NULL;
	for (int i = 0; res && i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn || !res->count_crtcs) {
		fprintf(stderr, "splash: no connected display\n");
		return 1;
	}
	drmModeModeInfo mode = conn->modes[0];
	uint32_t crtc = res->crtcs[0];

	struct buffer buf[2];
	uint32_t pitch;
	uint64_t size;
	if (buffer_create(fd, &mode, &buf[0], &pitch, &size) ||
	    buffer_create(fd, &mode, &buf[1], &pitch, &size)) {
		perror("splash: buffers");
		return 1;
	}
	uint32_t *pixels = malloc(size);
	if (!pixels) {
		perror("splash: memory");
		return 1;
	}

	/* The frames, or the plain boot color if they are missing, do not fit
	 * the panel or will not unpack: a picture either way. */
	unsigned first, rows;
	int animated = !charging && frames_open() == 0 &&
		       frame_w == mode.hdisplay && frame_h == mode.vdisplay &&
		       pitch == frame_w * 4 &&
		       frame_decode(0, (uint8_t *)pixels, &first, &rows) == 0;
	if (!animated) {
		uint32_t ground = charging && light_ground ? GROUND_LIGHT : BACKGROUND;

		for (uint64_t i = 0; i < size / 4; i++) pixels[i] = ground;
		frame_count = 1;
		loop_from = 1;
	}
	memcpy(buf[0].map, pixels, size);

	/* The player's brightness before the first light */
	bright_apply();
	double started = since_boot();
	if (drmModeSetCrtc(fd, crtc, buf[0].fb, 0, 0, &conn->connector_id, 1, &mode)) {
		perror("splash: set mode");
		return 1;
	}
	double lit = since_boot();
	char line[128];
	snprintf(line, sizeof(line), "splash: panel lit at %.3f s, the mode-set took %.0f ms\n",
		 lit, (lit - started) * 1000);
	log_to_kernel(line);


	/* The rest of the frames at the animation's rate, each on its own
	 * deadline from the first, so a late frame does not push every later
	 * one back; after the last, the loop's first through the band that leads
	 * back to it, until ZyperOS is ready. Into the two buffers in turn, so
	 * each holds the frame from two before: the rows of the last band and
	 * this one are all it needs, the whole frame only the first time. */
	unsigned shown = 0, loops = 0, n = 0;
	drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };
	for (unsigned seq = 1; !stop; seq++) {
		struct buffer *next = &buf[seq & 1];
		double due = lit + (double)seq / fps;
		unsigned to = n + 1, band = to;

		if (to == frame_count) {
			if (loop_from >= frame_count) break;   /* no loop: the last frame stays */
			to = loop_from;
			band = frame_count;
			loops++;
		}
		unsigned prev_first = first, prev_rows = rows;
		if (frame_decode(band, (uint8_t *)pixels, &first, &rows) < 0) break;
		/* The copy's line and bar, asked for by the image's init, up once
		 * the intro has written the word: over the loop only */
		if (animated && !copy.asked && access(COPY_ASK, F_OK) == 0) {
			copy.asked = 1;
			copy.ready = copy_start() == 0;
		}
		if (copy.ready && to >= loop_from) {
			if (!copy.shown) {
				copy.shown = 1;
				copy.dirty = 2;
			}
			copy_draw(pixels);
		}
		if (seq == 1) {
			memcpy(next->map, pixels, size);
		} else {
			unsigned lo = frame_h, hi = 0;
			if (prev_rows) { lo = prev_first; hi = prev_first + prev_rows; }
			if (rows && first < lo) lo = first;
			if (rows && first + rows > hi) hi = first + rows;
			/* Both buffers get the rect when it first goes up and when the
			 * bar grows: each holds the frame from two before */
			if (copy.dirty) {
				if (copy.y < lo) lo = copy.y;
				if (copy.y + copy.h > hi) hi = copy.y + copy.h;
				copy.dirty--;
			}
			if (hi > lo)
				memcpy(next->map + lo * frame_w, pixels + lo * frame_w,
				       (size_t)(hi - lo) * frame_w * 4);
		}
		while (!stop) {
			double wait = due - since_boot();
			if (wait <= 0) break;
			struct timespec t = { (time_t)wait, (long)((wait - (time_t)wait) * 1e9) };
			nanosleep(&t, NULL);
		}
		if (stop) break;
		int flipped = 0;
		if (drmModePageFlip(fd, crtc, next->fb, DRM_MODE_PAGE_FLIP_EVENT, &flipped))
			break;
		/* Until it is on glass: a flip still queued when ZyperOS takes the
		 * display would come back busy on its side. */
		while (!flipped) {
			if (drmHandleEvent(fd, &ev) && errno != EINTR) break;
		}
		n = to;
		shown = n;
	}

	/*
	 * Let go of the display but keep the picture: dropping DRM master leaves
	 * the mode and the frame on screen, so ZyperOS can take the display and
	 * replace the picture directly, with nothing in between. Exiting would
	 * free the framebuffers and blank the panel.
	 */
	drmDropMaster(fd);
	snprintf(line, sizeof(line), "splash: %s at frame %u of %u, %u loop(s), %.3f s\n",
		 stop ? "stopped for ZyperOS" : "finished", shown, frame_count, loops,
		 since_boot());
	log_to_kernel(line);
	if (copy.ready) {
		snprintf(line, sizeof(line), "splash: the copy's bar was at %u%%\n",
			 copy.filled * 100 / ((copy.bx1 - copy.bx0) > (copy.by1 - copy.by0)
					      ? copy.bx1 - copy.bx0 : copy.by1 - copy.by0));
		log_to_kernel(line);
	}

	/* The picture lasts as long as the buffer, so stay */
	for (;;)
		pause();
}

/* The port's output at the foot of the loading screen (portout.h), when
 * launch.sh asks for it: SPLASH_OUT, the letters and the box the launcher
 * wrote; SPLASH_OUT_LOG, the log the port's output goes to, followed from
 * where it ends as this starts; SPLASH_OUT_PAUSE, a file that is there while
 * the lines going into the log are not the port's (the in-game menu over the
 * loading screen, ZyperOS's tools/portkeys.c). Drawn into this process's own
 * buffer only, as the line after a minute is: a port or PortMaster that has
 * taken the display by then shows its own picture, and the write is seen by
 * nobody. And looked for only while the panel shows that buffer: the log is
 * read a few times a second then, and not at all while a port draws. */
#define OUT_TICK_MS  250        /* while the loading screen is on the panel */
#define OUT_AWAY_MS  1000       /* while something else is */
#define OUT_READ_MAX (256 * 1024)  /* bytes read in a tick; past that, from the end */

static struct {
	int on, turned, away, log, dirty;
	const char *pause;
	portout_wait W;
	portout_atlas A;
	uint8_t *file;
	portout_reader R;
	uint32_t *under, *under_later, *px;
	uint32_t shown[PORTOUT_CHARS];
	size_t shown_n;
} out = { .log = -1 };

/* The log, from its end, first of all: the port starts beside this, and
 * what it prints from then on is its own */
static void out_follow(void)
{
	const char *path = getenv("SPLASH_OUT_LOG"), *letters = getenv("SPLASH_OUT");

	if (!path || !*path || !letters || !*letters) return;
	out.log = open(path, O_RDONLY | O_CLOEXEC);
	if (out.log >= 0) lseek(out.log, 0, SEEK_END);
}

/* The letters and the box, and the screen's own pixels of the box: on when
 * they hold together and fit this panel. `later`, the picture after a
 * minute, or NULL. */
static void out_open(int fd, const drmModeModeInfo *mode, const struct buffer *b, uint32_t pitch,
		     const uint8_t *later)
{
	const char *path = getenv("SPLASH_OUT");
	struct stat st;
	char line[160];
	drmVersionPtr v;
	int afd;

	if (out.log < 0 || !path) return;
	afd = open(path, O_RDONLY | O_CLOEXEC);
	if (afd < 0 || fstat(afd, &st) < 0 || st.st_size < PORTOUT_HEADER || st.st_size > (1 << 20) ||
	    !(out.file = malloc(st.st_size)) || read(afd, out.file, st.st_size) != st.st_size ||
	    portout_load(out.file, st.st_size, &out.A) < 0) {
		if (afd >= 0) close(afd);
		log_to_kernel("splash: no port's output on its loading screen: its letters would not read\n");
		return;
	}
	close(afd);
	if (mode->hdisplay == out.A.land_w && mode->vdisplay == out.A.land_h)
		out.turned = 0;
	else if (mode->hdisplay == out.A.land_h && mode->vdisplay == out.A.land_w)
		out.turned = 1;
	else {
		log_to_kernel("splash: no port's output on its loading screen: not this panel's size\n");
		return;
	}
	size_t box = (size_t)out.A.w * out.A.h * 4;
	if (pitch % 4 || !(out.under = malloc(box)) || !(out.px = malloc(box)) ||
	    (later && !(out.under_later = malloc(box))))
		return;
	portout_get_box(&out.A, b->map, pitch / 4, out.turned, out.under);
	if (later) portout_get_box(&out.A, (const uint32_t *)later, mode->hdisplay, out.turned, out.under_later);
	/* A display that keeps its own copy of the buffer (virtio-gpu in QEMU)
	 * is told of each new line; the Pixel's VOP reads the buffer itself */
	v = drmGetVersion(fd);
	out.dirty = !v || strcmp(v->name, "rockchip") != 0;
	drmFreeVersion(v);
	const char *p = getenv("SPLASH_OUT_PAUSE");
	out.pause = p && *p ? p : NULL;
	out.on = 1;
	/* The screen is on the panel from now: the line may show in a few
	 * seconds (portout_wait) */
	portout_wait_init(&out.W);
	portout_wait_tick(&out.W, 1, since_boot());
	snprintf(line, sizeof(line), "splash: the port's output at its loading screen's foot, %ux%u at %u,%u\n",
		 out.A.w, out.A.h, out.A.x, out.A.y);
	log_to_kernel(line);
}

/* A display that keeps its own copy, told; only while the panel shows this
 * buffer, which takes master, back for that one call and dropped at once */
static void out_told(int fd, uint32_t crtc, uint32_t fb)
{
	drmModeCrtc *c = drmModeGetCrtc(fd, crtc);
	int ours = c && c->buffer_id == fb;

	drmModeFreeCrtc(c);
	if (ours && drmSetMaster(fd) == 0) {
		drmModeDirtyFB(fd, fb, NULL, 0);
		drmDropMaster(fd);
	}
}

/* One look: the panel's buffer, the log's new lines, the line drawn when it
 * changed. Whether the loading screen is on the panel. */
static int out_tick(int fd, uint32_t crtc, const struct buffer *b, uint32_t pitch)
{
	drmModeCrtc *c = drmModeGetCrtc(fd, crtc);
	int ours = c && c->buffer_id == b->fb;
	uint8_t chunk[4096];
	uint32_t now[PORTOUT_CHARS];
	size_t got = 0, n;
	ssize_t r;

	drmModeFreeCrtc(c);
	if (!ours || (out.pause && access(out.pause, F_OK) == 0)) {
		portout_wait_tick(&out.W, 0, since_boot());
		out.away = 1;
		return ours;
	}
	/* Back: what went into the log meanwhile was not shown, and need not be */
	if (out.away) {
		lseek(out.log, 0, SEEK_END);
		portout_skip(&out.R);
		out.away = 0;
	}
	while ((r = read(out.log, chunk, sizeof chunk)) > 0) {
		portout_feed(&out.R, &out.A, chunk, (size_t)r);
		if ((got += (size_t)r) >= OUT_READ_MAX) {
			lseek(out.log, 0, SEEK_END);
			portout_skip(&out.R);
			break;
		}
	}
	/* Read all the while, drawn only once the screen has been up a few
	 * seconds */
	n = portout_wait_tick(&out.W, 1, since_boot()) ? portout_shown(&out.R, &out.A, now) : 0;
	if (n == out.shown_n && !memcmp(now, out.shown, n * sizeof now[0])) return ours;
	portout_compose(&out.A, out.under, now, n, out.px);
	portout_put_box(&out.A, out.px, b->map, pitch / 4, out.turned);
	memcpy(out.shown, now, n * sizeof now[0]);
	out.shown_n = n;
	if (out.dirty) out_told(fd, crtc, b->fb);
	return ours;
}

/* splash --still FILE: one picture, held until something else draws (see the
 * top of the file). Exits at once, drawing nothing, when the file is not the
 * panel's size: a wrong picture is worse than the black it replaces. */
static int still(const char *path)
{
	int fd;
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	struct buffer b;
	uint32_t pitch;
	uint64_t size;
	FILE *f;

	out_follow();
	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if (fd < 0 || !(res = drmModeGetResources(fd))) return 1;
	for (int i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn || !res->count_crtcs) return 1;
	drmModeModeInfo mode = conn->modes[0];
	uint32_t crtc = res->crtcs[0];

	if (buffer_create(fd, &mode, &b, &pitch, &size)) return 1;
	if (!(f = fopen(path, "rb"))) return 1;
	for (unsigned y = 0; y < mode.vdisplay; y++) {
		uint8_t *row = (uint8_t *)b.map + (size_t)y * pitch;
		if (fread(row, 4, mode.hdisplay, f) != mode.hdisplay) {
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	if (drmModeSetCrtc(fd, crtc, b.fb, 0, 0, &conn->connector_id, 1, &mode)) return 1;
	drmDropMaster(fd);
	log_to_kernel("splash: holding a port's loading screen\n");
	/* After a minute, the same screen saying how to leave, when launch.sh
	 * gave one (see the top of the file): read whole first, so a short file
	 * changes nothing */
	const char *later = getenv("SPLASH_LATER");
	const char *secs = getenv("SPLASH_LATER_S");
	unsigned wait = secs && atoi(secs) > 0 ? (unsigned)atoi(secs) : 60;
	size_t row = (size_t)mode.hdisplay * 4;
	uint8_t *pic = later && *later ? malloc(row * mode.vdisplay) : NULL;

	if (pic && (f = fopen(later, "rb"))) {
		bool whole = fread(pic, row, mode.vdisplay, f) == mode.vdisplay;

		fclose(f);
		if (!whole) {
			free(pic);
			pic = NULL;
		}
	} else {
		free(pic);
		pic = NULL;
	}
	out_open(fd, &mode, &b, pitch, pic);
	double due = since_boot() + wait;
	int ours = 1;

	while (pic || out.on) {
		/* Until the picture after a minute is due, a look at the output a
		 * few times a second; with no output, the whole wait at once */
		if (out.on) {
			double left = pic ? due - since_boot() : 1e9;
			double tick = (ours ? OUT_TICK_MS : OUT_AWAY_MS) / 1e3;

			if (left > tick) left = tick;
			if (left > 0) {
				struct timespec t = { (time_t)left, (long)((left - (time_t)left) * 1e9) };
				nanosleep(&t, NULL);
			}
		} else {
			while (wait) wait = sleep(wait);
		}
		if (pic && (!out.on || since_boot() >= due)) {
			drmModeCrtc *c;

			/* The line as it is now goes in with it, so the swap shows no
			 * frame without the line */
			if (out.on) {
				free(out.under);
				out.under = out.under_later;
				out.under_later = NULL;
				portout_compose(&out.A, out.under, out.shown, out.shown_n, out.px);
				portout_put_box(&out.A, out.px, (uint32_t *)pic, mode.hdisplay, out.turned);
			}
			/* Into this buffer whether or not the panel shows it: the
			 * in-game menu over a port still starting covers it for a
			 * while, and portkeys puts this buffer back after it */
			for (unsigned y = 0; y < mode.vdisplay; y++)
				memcpy((uint8_t *)b.map + (size_t)y * pitch, pic + (size_t)y * row, row);
			/* A display that keeps its own copy of the buffer
			 * (virtio-gpu in QEMU) shows new pixels only once told; the
			 * Pixel's VOP reads the buffer itself. Told only while the
			 * panel shows this buffer, which takes master, back for that
			 * one call and dropped at once; never while a port or the
			 * menu holds it (SetMaster fails then) */
			c = drmModeGetCrtc(fd, crtc);
			ours = c && c->buffer_id == b.fb;
			drmModeFreeCrtc(c);
			if (ours && drmSetMaster(fd) == 0) {
				drmModeDirtyFB(fd, b.fb, NULL, 0);
				drmDropMaster(fd);
			}
			log_to_kernel(ours ? "splash: a minute in, the loading screen says how to leave\n"
			                   : "splash: a minute in; the next loading screen shown says how to leave\n");
			free(pic);
			pic = NULL;
		}
		if (out.on) ours = out_tick(fd, crtc, &b, pitch);
	}
	/* The picture lasts as long as the buffer: stay until launch.sh says */
	for (;;)
		pause();
}

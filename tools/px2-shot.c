/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-shot: a screenshot of whatever the GKD Pixel 2's panel shows.
 *
 *   px2-shot [FILE.ppm]          (default /tmp/px2-shot.ppm)
 *   px2-shot --fps [SECONDS]     (frames a second, default 10 s)
 *
 * Reads the framebuffer the display controller is scanning out, so it works
 * for the launcher, a libretro core and a port alike, with nothing in them
 * changed: the CRTC's current framebuffer (GETFB2, root only), mapped as a
 * dumb buffer, or exported as a dma-buf where that fails. The panel is
 * 480x640 and mounted turned, so the picture is turned back to the 640x480
 * the player sees before it is written, as a binary PPM. A landscape display
 * (QEMU's, plastron's qemu_defconfig) is written as it is.
 *
 * For checking a change on a device nobody is holding: px2-input presses the
 * buttons, this shows what happened.
 *
 * --fps counts, at every vertical blank, whether the panel is showing a new
 * framebuffer: the frame rate of whatever runs, up to the panel's 60, without
 * an FPS counter in the program itself.
 *
 * Build: the plastron toolchain, no libraries (the kernel's DRM headers).
 * Run as root on the device. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

static uint32_t crtc_on;		/* the CRTC find_fb found scanning out */

static int find_fb(int fd, struct drm_mode_fb_cmd2 *fb)
{
	struct drm_mode_card_res res;
	uint32_t crtcs[8];

	memset(&res, 0, sizeof res);
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		return -1;
	if (res.count_crtcs > 8)
		res.count_crtcs = 8;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
	res.count_fbs = res.count_connectors = res.count_encoders = 0;
	if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		return -1;
	for (uint32_t i = 0; i < res.count_crtcs; i++) {
		struct drm_mode_crtc crtc;

		memset(&crtc, 0, sizeof crtc);
		crtc.crtc_id = crtcs[i];
		if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) < 0 || !crtc.fb_id)
			continue;
		memset(fb, 0, sizeof *fb);
		fb->fb_id = crtc.fb_id;
		if (ioctl(fd, DRM_IOCTL_MODE_GETFB2, fb) == 0 && fb->handles[0]) {
			crtc_on = crtcs[i];
			return 0;
		}
	}
	return -1;
}

static void *map_fb(int fd, const struct drm_mode_fb_cmd2 *fb, size_t size)
{
	struct drm_mode_map_dumb dumb = { .handle = fb->handles[0] };
	struct drm_prime_handle prime = { .handle = fb->handles[0], .flags = DRM_CLOEXEC };
	void *p;

	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &dumb) == 0) {
		p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, (off_t)dumb.offset);
		if (p != MAP_FAILED)
			return p;
	}
	if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) == 0) {
		p = mmap(NULL, size, PROT_READ, MAP_SHARED, prime.fd, 0);
		if (p != MAP_FAILED)
			return p;
	}
	return NULL;
}

static int count_frames(int fd, int seconds)
{
	uint32_t last = 0;
	int total = 0;

	for (int s = 0; s < seconds; s++) {
		int frames = 0;

		for (int v = 0; v < 60; v++) {
			union drm_wait_vblank vbl = { .request = { .type = _DRM_VBLANK_RELATIVE, .sequence = 1 } };
			struct drm_mode_crtc crtc = { .crtc_id = crtc_on };

			ioctl(fd, DRM_IOCTL_WAIT_VBLANK, &vbl);
			if (ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) < 0)
				return 1;
			if (crtc.fb_id != last)
				frames++;
			last = crtc.fb_id;
		}
		printf("%2d fps\n", frames);
		fflush(stdout);
		total += frames;
	}
	printf("average %.1f fps over %d s\n", (double)total / seconds, seconds);
	return 0;
}

int main(int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "/tmp/px2-shot.ppm";
	int fps = argc > 1 && !strcmp(argv[1], "--fps");
	struct drm_mode_fb_cmd2 fb;
	uint32_t w, h, pitch;
	size_t size;
	const uint8_t *px;
	uint8_t *rgb;
	FILE *f;
	int fd = -1, turned;

	for (int i = 0; i < 4 && fd < 0; i++) {
		char path[32];

		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
			continue;
		/* Whoever opens a card with no master becomes it, and a program
		 * starting then (a port between the splash and its first frame)
		 * cannot set its mode: Stardew Valley failed with
		 * NoSuitableGraphicsDevice while this polled. Give it back. */
		ioctl(fd, DRM_IOCTL_DROP_MASTER, 0);
		if (find_fb(fd, &fb) < 0) {
			close(fd);
			fd = -1;
		}
	}
	if (fd < 0) {
		fprintf(stderr, "px2-shot: no framebuffer on screen\n");
		return 1;
	}
	if (fps)
		return count_frames(fd, argc > 2 ? atoi(argv[2]) : 10);
	w = fb.width;
	h = fb.height;
	pitch = fb.pitches[0];
	size = (size_t)pitch * h + fb.offsets[0];
	if (!(px = map_fb(fd, &fb, size))) {
		fprintf(stderr, "px2-shot: cannot map framebuffer %u: %s\n", fb.fb_id, strerror(errno));
		return 1;
	}
	px += fb.offsets[0];

	/* XRGB/ARGB8888, little endian: B, G, R, X in memory. Turned 90 degrees
	 * counterclockwise, so the panel's 480x640 comes out as the 640x480 seen;
	 * a landscape display's picture is already the way it is seen */
	turned = h > w;
	rgb = malloc((size_t)w * h * 3);
	for (uint32_t y = 0; y < h; y++)
		for (uint32_t x = 0; x < w; x++) {
			const uint8_t *s = px + (size_t)y * pitch + x * 4;
			uint8_t *d = rgb + (turned ? (size_t)x * h + (h - 1 - y)
			                           : (size_t)y * w + x) * 3;

			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
		}
	if (!(f = fopen(out, "wb"))) {
		perror("px2-shot");
		return 1;
	}
	fprintf(f, "P6\n%u %u\n255\n", turned ? h : w, turned ? w : h);
	fwrite(rgb, 3, (size_t)w * h, f);
	fclose(f);
	printf("%s: %ux%u from framebuffer %u (%.4s)\n", out, turned ? h : w, turned ? w : h,
	       fb.fb_id, (const char *)&fb.pixel_format);
	return 0;
}

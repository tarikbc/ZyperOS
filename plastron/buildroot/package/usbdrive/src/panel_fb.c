/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Flash Drive page on the TrimUI Brick's panel (panel.h; device.h says
 * which build has it): /dev/fb0, Allwinner's disp2 framebuffer, which
 * ZyperOS's launcher and Diatom also draw into. Its top page, which a
 * launcher drawing two pages through GL never reaches (Diatom parks there
 * too), holds the page: the 640x480 landscape picture scaled to the panel's
 * 1024x768, every pixel opaque (the display engine shows a pixel by its
 * alpha byte), then panned to. Its own copy of the picture is kept, so a
 * changed rect or a small picture over it is scaled again from what is
 * around it. Nothing is handed back: the launcher pans to its own pages
 * when it starts again.
 */
#include "panel.h"
#include "atlas.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static struct {
	int fd;
	uint8_t *map, *page;          /* fb0's memory, and its top page in it */
	size_t size;
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	uint32_t opaque;
	uint32_t *src;                /* the landscape picture, w x h */
	int w, h;
} P = { .fd = -1 };

/* The picture's pixel at (x, y), clamped: 0x00RRGGBB */
static inline uint32_t at(int x, int y)
{
	if (x >= P.w) x = P.w - 1;
	if (y >= P.h) y = P.h - 1;
	return P.src[(size_t)y * P.w + x];
}

/* fb0's pixels [x0, x1) x [y0, y1) from the picture, two taps each way
 * (fixed point, eight bits), in fb0's own channel order */
static void draw(int x0, int y0, int x1, int y1)
{
	const unsigned ro = P.v.red.offset, go = P.v.green.offset, bo = P.v.blue.offset;
	int dx, dy;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > (int)P.v.xres) x1 = (int)P.v.xres;
	if (y1 > (int)P.v.yres) y1 = (int)P.v.yres;
	for (dy = y0; dy < y1; dy++) {
		uint32_t *row = (uint32_t *)(P.page + (size_t)dy * P.f.line_length);
		unsigned sy = (unsigned)((dy * 2 + 1) * P.h * 128 / (int)P.v.yres);
		int y = sy >= 128 ? (int)((sy - 128) >> 8) : 0;
		unsigned fy = sy >= 128 ? (sy - 128) & 255 : 0;

		for (dx = x0; dx < x1; dx++) {
			unsigned sx = (unsigned)((dx * 2 + 1) * P.w * 128 / (int)P.v.xres);
			int x = sx >= 128 ? (int)((sx - 128) >> 8) : 0;
			unsigned fx = sx >= 128 ? (sx - 128) & 255 : 0;
			uint32_t a = at(x, y), b = at(x + 1, y), c = at(x, y + 1), d = at(x + 1, y + 1);
			unsigned ch[3], k;

			for (k = 0; k < 3; k++) {
				unsigned s = 16 - 8 * k;
				unsigned top = ((a >> s) & 255) * (256 - fx) + ((b >> s) & 255) * fx;
				unsigned bot = ((c >> s) & 255) * (256 - fx) + ((d >> s) & 255) * fx;

				ch[k] = (top * (256 - fy) + bot * fy + 32768) >> 16;
			}
			row[dx] = ch[0] << ro | ch[1] << go | ch[2] << bo | P.opaque;
		}
	}
}

/* fb0's pixels the picture's rect [x, x + rw) x [y, y + rh) reaches, with a
 * pixel of margin for the taps */
static void draw_rect(int x, int y, int rw, int rh)
{
	draw(x * (int)P.v.xres / P.w - 2, y * (int)P.v.yres / P.h - 2,
	     (x + rw) * (int)P.v.xres / P.w + 3, (y + rh) * (int)P.v.yres / P.h + 3);
}

/* fb0 mapped, its top page found and panned to, and the picture's copy for
 * w x h; false with no fb0, not 32 bits, or no memory */
static bool take(int w, int h)
{
	int pages;

	if (P.map && P.src && P.w == w && P.h == h) return true;
	if (!P.src || P.w != w || P.h != h) {
		free(P.src);
		if (!(P.src = calloc((size_t)w * h, 4))) return false;
		P.w = w;
		P.h = h;
	}
	if (P.map) return true;
	if ((P.fd = open("/dev/fb0", O_RDWR | O_CLOEXEC)) < 0) return false;
	if (ioctl(P.fd, FBIOGET_FSCREENINFO, &P.f) || ioctl(P.fd, FBIOGET_VSCREENINFO, &P.v) ||
	    P.v.bits_per_pixel != 32 || !P.v.yres || !P.f.line_length)
		goto fail;
	pages = (int)(P.f.smem_len / ((size_t)P.v.yres * P.f.line_length));
	if (pages < 1) goto fail;
	P.size = (size_t)pages * P.v.yres * P.f.line_length;
	P.map = mmap(NULL, P.size, PROT_READ | PROT_WRITE, MAP_SHARED, P.fd, 0);
	if (P.map == MAP_FAILED) { P.map = NULL; goto fail; }
	P.page = P.map + (size_t)(pages - 1) * P.v.yres * P.f.line_length;
	P.opaque = P.v.transp.length ? ((1u << P.v.transp.length) - 1) << P.v.transp.offset : 0;
	return true;
fail:
	close(P.fd);
	P.fd = -1;
	return false;
}

/* What is in the top page up on the panel */
static void pan(void)
{
	struct fb_var_screeninfo v = P.v;
	int pages = (int)(P.size / ((size_t)P.v.yres * P.f.line_length));

	v.yoffset = (uint32_t)(pages - 1) * v.yres;
	v.activate = FB_ACTIVATE_VBL;
	ioctl(P.fd, FBIOPAN_DISPLAY, &v);
}

bool panel_open(const uint32_t *page, int w, int h)
{
	if (!page || w <= 0 || h <= 0 || !take(w, h)) return false;
	memcpy(P.src, page, (size_t)w * h * 4);
	draw(0, 0, (int)P.v.xres, (int)P.v.yres);
	pan();
	return true;
}

void panel_update(const uint32_t *page, int x, int y, int rw, int rh)
{
	int j;

	if (!P.map || !page || x < 0 || y < 0 || rw > P.w - x || rh > P.h - y) return;
	for (j = y; j < y + rh; j++)
		memcpy(P.src + (size_t)j * P.w + x, page + (size_t)j * P.w + x, (size_t)rw * 4);
	draw_rect(x, y, rw, rh);
}

void panel_blit(const uint32_t *px, int w, int h, int x, int y)
{
	int j;

	if (!P.map || !px || x < 0 || y < 0 || w > P.w - x || h > P.h - y) return;
	for (j = 0; j < h; j++)
		memcpy(P.src + (size_t)(y + j) * P.w + x, px + (size_t)j * w, (size_t)w * 4);
	draw_rect(x, y, w, h);
}

bool panel_show(const char *path, int w, int h, bool turned)
{
	if (!take(w, h) || !at_load(path, w, h, turned, P.src)) return false;
	draw(0, 0, (int)P.v.xres, (int)P.v.yres);
	pan();
	return true;
}

void panel_close(void)
{
	if (P.map) munmap(P.map, P.size);
	if (P.fd >= 0) close(P.fd);
	free(P.src);
	P.map = P.page = NULL;
	P.src = NULL;
	P.fd = -1;
}

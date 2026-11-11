/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Flash Drive page on the panel: see panel.h. */
#include "panel.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static struct {
	int fd;
	uint8_t *map;
	uint64_t size;
	uint32_t pitch, fb, handle;
	int pw, ph;          /* the panel's mode */
	int w, h;            /* the landscape picture */
	bool turned;
} P = { .fd = -1 };

/* A landscape (x, y) on the panel: the turned panel's row W-1-x, column y */
static inline uint32_t *px(int x, int y)
{
	return P.turned ? (uint32_t *)(P.map + (size_t)(P.w - 1 - x) * P.pitch) + y
	                : (uint32_t *)(P.map + (size_t)y * P.pitch) + x;
}

void panel_update(const uint32_t *page, int x, int y, int rw, int rh)
{
	int i, j;

	if (!P.map) return;
	for (i = x; i < x + rw; i++)
		for (j = y; j < y + rh; j++)
			*px(i, j) = page[(size_t)j * P.w + i];
}

bool panel_open(const uint32_t *page, int w, int h)
{
	drmModeRes *res = NULL;
	drmModeConnector *conn = NULL;
	drmModeModeInfo mode;
	uint32_t crtc;
	int i;

	if ((P.fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC)) < 0) return false;
	if (!(res = drmModeGetResources(P.fd))) goto fail;
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(P.fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes) break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn || !res->count_crtcs) goto fail;
	mode = conn->modes[0];
	crtc = res->crtcs[0];
	P.pw = mode.hdisplay;
	P.ph = mode.vdisplay;
	P.w = w;
	P.h = h;
	if (P.pw == h && P.ph == w) P.turned = true;
	else if (P.pw == w && P.ph == h) P.turned = false;
	else goto fail;
	{
		struct drm_mode_create_dumb c = { .width = P.pw, .height = P.ph, .bpp = 32 };
		struct drm_mode_map_dumb m = { 0 };

		if (drmIoctl(P.fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) goto fail;
		P.handle = c.handle;
		P.pitch = c.pitch;
		P.size = c.size;
		if (drmModeAddFB(P.fd, P.pw, P.ph, 24, 32, c.pitch, c.handle, &P.fb)) goto fail;
		m.handle = c.handle;
		if (drmIoctl(P.fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) goto fail;
		P.map = mmap(NULL, P.size, PROT_READ | PROT_WRITE, MAP_SHARED, P.fd, m.offset);
		if (P.map == MAP_FAILED) { P.map = NULL; goto fail; }
	}
	panel_update(page, 0, 0, w, h);
	if (drmModeSetCrtc(P.fd, crtc, P.fb, 0, 0, &conn->connector_id, 1, &mode)) goto fail;
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	/* The display is free for the next picture; this one stays until then */
	drmDropMaster(P.fd);
	return true;
fail:
	if (conn) drmModeFreeConnector(conn);
	if (res) drmModeFreeResources(res);
	panel_close();
	return false;
}

void panel_close(void)
{
	if (P.map) munmap(P.map, P.size);
	P.map = NULL;
	if (P.fd >= 0) {
		if (P.fb) drmModeRmFB(P.fd, P.fb);
		close(P.fd);
	}
	memset(&P, 0, sizeof P);
	P.fd = -1;
}

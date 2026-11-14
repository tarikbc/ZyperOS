/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Vendored from ZyperOS src/panelgamma.c, which is where it is changed: the
 * launcher and this port empty the display's gamma table with the same code.
 * Copy it back here whole, below this note, when it changes there: make check
 * fails while the two differ (tools/check-copies.py). */
/* The display controller's gamma table, emptied. See src/panelgamma.h. */
#include "panelgamma.h"

#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

/* Linux's _IOWR('d', nr, type): read and write, the structure's size, DRM's
 * letter. Written out because a Mac's _IOWR packs the size differently. */
#define PG_IOWR(nr, size) \
	((unsigned long)(3u << 30 | (unsigned)(size) << 16 | 'd' << 8 | (nr)))

/* From the kernel's include/uapi/drm/drm_mode.h, field for field */
struct pg_card_res {                   /* DRM_IOCTL_MODE_GETRESOURCES */
	uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
	uint32_t min_width, max_width, min_height, max_height;
};
struct pg_obj_props {                  /* DRM_IOCTL_MODE_OBJ_GETPROPERTIES */
	uint64_t props_ptr, prop_values_ptr;
	uint32_t count_props, obj_id, obj_type;
};
struct pg_prop {                       /* DRM_IOCTL_MODE_GETPROPERTY */
	uint64_t values_ptr, enum_blob_ptr;
	uint32_t prop_id, flags;
	char     name[32];
	uint32_t count_values, count_enum_blobs;
};
struct pg_set_prop {                   /* DRM_IOCTL_MODE_OBJ_SETPROPERTY */
	uint64_t value;
	uint32_t prop_id, obj_id, obj_type;
};
_Static_assert(sizeof(struct pg_card_res) == 64, "drm_mode_card_res");
_Static_assert(sizeof(struct pg_obj_props) == 32, "drm_mode_obj_get_properties");
_Static_assert(sizeof(struct pg_prop) == 64, "drm_mode_get_property");
_Static_assert(sizeof(struct pg_set_prop) == 24, "drm_mode_obj_set_property");

#define PG_GETRESOURCES   PG_IOWR(0xA0, sizeof(struct pg_card_res))
#define PG_GETPROPERTY    PG_IOWR(0xAA, sizeof(struct pg_prop))
#define PG_OBJ_GETPROPS   PG_IOWR(0xB9, sizeof(struct pg_obj_props))
#define PG_OBJ_SETPROP    PG_IOWR(0xBA, sizeof(struct pg_set_prop))
#define PG_OBJECT_CRTC    0xccccccccu

static int sys_ioctl(int fd, unsigned long request, void *arg)
{
	return ioctl(fd, request, arg);
}

panelgamma_ioctl_fn panelgamma_ioctl = sys_ioctl;

#define PG_MAX 8                       /* CRTCs; the Pixel 2 has 2 */
#define PG_MAX_PROPS 64                /* a CRTC's properties; the VOP's have 9 */

/* Empty CRTC crtc's GAMMA_LUT if it holds a table: 1 emptied, 0 nothing
 * to empty, -1 failed. */
static int clear_one(int fd, uint32_t crtc)
{
	uint32_t ids[PG_MAX_PROPS];
	uint64_t values[PG_MAX_PROPS];
	struct pg_obj_props op;
	uint32_t i;

	memset(&op, 0, sizeof op);
	op.obj_id = crtc;
	op.obj_type = PG_OBJECT_CRTC;
	if (panelgamma_ioctl(fd, PG_OBJ_GETPROPS, &op) != 0) return -1;
	if (op.count_props > PG_MAX_PROPS) op.count_props = PG_MAX_PROPS;
	op.props_ptr = (uint64_t)(uintptr_t)ids;
	op.prop_values_ptr = (uint64_t)(uintptr_t)values;
	if (panelgamma_ioctl(fd, PG_OBJ_GETPROPS, &op) != 0) return -1;
	if (op.count_props > PG_MAX_PROPS) op.count_props = PG_MAX_PROPS;

	for (i = 0; i < op.count_props; i++) {
		struct pg_prop p;
		struct pg_set_prop set;

		memset(&p, 0, sizeof p);
		p.prop_id = ids[i];
		if (panelgamma_ioctl(fd, PG_GETPROPERTY, &p) != 0) continue;
		p.name[sizeof p.name - 1] = '\0';
		if (strcmp(p.name, "GAMMA_LUT")) continue;
		if (values[i] == 0) return 0;  /* empty already */

		memset(&set, 0, sizeof set);
		set.value = 0;                 /* no blob: the table off */
		set.prop_id = ids[i];
		set.obj_id = crtc;
		set.obj_type = PG_OBJECT_CRTC;
		return panelgamma_ioctl(fd, PG_OBJ_SETPROP, &set) == 0 ? 1 : -1;
	}
	return 0;                          /* no GAMMA_LUT on this kernel */
}

int panelgamma_clear(int fd)
{
	uint32_t crtcs[PG_MAX];
	struct pg_card_res res;
	uint32_t i;
	int cleared = 0, failed = 0;

	if (fd < 0) return -1;
	memset(&res, 0, sizeof res);
	if (panelgamma_ioctl(fd, PG_GETRESOURCES, &res) != 0) return -1;
	if (res.count_crtcs > PG_MAX) res.count_crtcs = PG_MAX;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
	res.count_fbs = res.count_connectors = res.count_encoders = 0;
	if (panelgamma_ioctl(fd, PG_GETRESOURCES, &res) != 0) return -1;
	if (res.count_crtcs > PG_MAX) res.count_crtcs = PG_MAX;

	for (i = 0; i < res.count_crtcs; i++) {
		int r = clear_one(fd, crtcs[i]);

		if (r > 0) cleared++;
		else if (r < 0) failed = 1;
	}
	return failed && !cleared ? -1 : cleared;
}

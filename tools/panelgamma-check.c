/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-panelgamma: src/panelgamma.c against a stand-in display, the
 * kernel's DRM ioctls answered here. The request numbers it answers are
 * Linux's own, written as the kernel's headers make them, so a number
 * panelgamma.c packs wrong is a refusal here as it would be on the Pixel 2.
 *
 * The cases: a table an older splash filled (emptied, on that CRTC alone,
 * GAMMA_LUT_SIZE beside it untouched); a table already empty (nothing done:
 * emptying makes the controller wait a refresh); a kernel with no GAMMA_LUT
 * (QEMU's display); no DRM master (the kernel refuses: -1); no display. */
#include "../src/panelgamma.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* include/uapi/drm/drm.h's numbers on Linux, as the kernel's headers give
 * them: DRM_IOWR(nr, struct ...) */
#define LINUX_GETRESOURCES 0xC04064A0ul
#define LINUX_GETPROPERTY  0xC04064AAul
#define LINUX_OBJ_GETPROPS 0xC02064B9ul
#define LINUX_OBJ_SETPROP  0xC01864BAul
#define OBJECT_CRTC        0xccccccccu

struct card_res {
	uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
	uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
	uint32_t min_width, max_width, min_height, max_height;
};
struct obj_props {
	uint64_t props_ptr, prop_values_ptr;
	uint32_t count_props, obj_id, obj_type;
};
struct prop {
	uint64_t values_ptr, enum_blob_ptr;
	uint32_t prop_id, flags;
	char     name[32];
	uint32_t count_values, count_enum_blobs;
};
struct set_prop {
	uint64_t value;
	uint32_t prop_id, obj_id, obj_type;
};

/* The stand-in: CRTCs with their properties' values, and the property names */
struct fake_prop { uint32_t id; const char *name; };
struct fake_crtc { uint32_t id; int nprops; uint32_t props[8]; uint64_t values[8]; };

static struct fake_prop names[] = {
	{ 20, "ACTIVE" }, { 21, "MODE_ID" }, { 22, "OUT_FENCE_PTR" }, { 23, "VRR_ENABLED" },
	{ 24, "GAMMA_LUT_SIZE" }, { 25, "GAMMA_LUT" },
};
static struct fake_crtc crtcs[2];
static int ncrtcs, master, broken, unknown;
static int sets;
static struct set_prop last_set;

static const char *name_of(uint32_t id)
{
	for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
		if (names[i].id == id) return names[i].name;
	return NULL;
}

static struct fake_crtc *crtc_of(uint32_t id)
{
	for (int i = 0; i < ncrtcs; i++)
		if (crtcs[i].id == id) return &crtcs[i];
	return NULL;
}

static int fake_ioctl(int fd, unsigned long request, void *arg)
{
	(void)fd;
	if (broken) { errno = ENOTTY; return -1; }
	switch (request) {
	case LINUX_GETRESOURCES: {
		struct card_res *r = arg;
		uint32_t *ids = (uint32_t *)(uintptr_t)r->crtc_id_ptr;

		if (ids)
			for (int i = 0; i < ncrtcs && i < (int)r->count_crtcs; i++) ids[i] = crtcs[i].id;
		r->count_crtcs = (uint32_t)ncrtcs;
		return 0;
	}
	case LINUX_OBJ_GETPROPS: {
		struct obj_props *o = arg;
		struct fake_crtc *c = crtc_of(o->obj_id);
		uint32_t *ids = (uint32_t *)(uintptr_t)o->props_ptr;
		uint64_t *vals = (uint64_t *)(uintptr_t)o->prop_values_ptr;

		if (!c || o->obj_type != OBJECT_CRTC) { errno = ENOENT; return -1; }
		if (ids && vals)
			for (int i = 0; i < c->nprops && i < (int)o->count_props; i++) {
				ids[i] = c->props[i];
				vals[i] = c->values[i];
			}
		o->count_props = (uint32_t)c->nprops;
		return 0;
	}
	case LINUX_GETPROPERTY: {
		struct prop *p = arg;
		const char *n = name_of(p->prop_id);

		if (!n) { errno = ENOENT; return -1; }
		snprintf(p->name, sizeof p->name, "%s", n);
		return 0;
	}
	case LINUX_OBJ_SETPROP: {
		struct set_prop *s = arg;
		struct fake_crtc *c = crtc_of(s->obj_id);

		if (!master) { errno = EACCES; return -1; }
		if (!c || s->obj_type != OBJECT_CRTC) { errno = ENOENT; return -1; }
		for (int i = 0; i < c->nprops; i++)
			if (c->props[i] == s->prop_id) {
				c->values[i] = s->value;
				sets++;
				last_set = *s;
				return 0;
			}
		errno = EINVAL;
		return -1;
	}
	default:
		unknown++;
		errno = EINVAL;
		return -1;
	}
}

/* The Pixel 2's two VOPs as plastron's kernel has them: the big one lit with
 * the splash's table (blob 47) when `table`, the little one never lit; with
 * `has_lut` 0, a kernel with no GAMMA_LUT at all */
static void pixel2(int table, int has_lut)
{
	static const uint32_t with[] = { 20, 21, 22, 23, 24, 25 };
	static const uint32_t without[] = { 20, 21, 22, 23 };

	memset(crtcs, 0, sizeof crtcs);
	ncrtcs = 2;
	crtcs[0].id = 49;
	crtcs[1].id = 67;
	for (int c = 0; c < 2; c++) {
		const uint32_t *p = has_lut ? with : without;
		int n = has_lut ? 6 : 4;

		crtcs[c].nprops = n;
		for (int i = 0; i < n; i++) crtcs[c].props[i] = p[i];
		crtcs[c].values[0] = c == 0;                  /* ACTIVE */
		if (has_lut) crtcs[c].values[4] = 256;        /* GAMMA_LUT_SIZE */
	}
	if (table && has_lut) crtcs[0].values[5] = 47;
	master = 1;
	broken = 0;
	unknown = 0;
	sets = 0;
	memset(&last_set, 0, sizeof last_set);
}

static int fails;

#define CHECK(cond, what) do { \
	if (cond) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

int main(void)
{
	int n;

	printf("check-panelgamma:\n");
	panelgamma_ioctl = fake_ioctl;

	pixel2(1, 1);
	n = panelgamma_clear(3);
	CHECK(n == 1, "an older splash's table: emptied, and said so (1)");
	CHECK(sets == 1 && last_set.obj_id == 49 && last_set.obj_type == OBJECT_CRTC &&
	      last_set.prop_id == 25 && last_set.value == 0,
	      "  ...the lit CRTC's GAMMA_LUT set to no blob, nothing else set");
	CHECK(crtcs[0].values[4] == 256 && crtcs[1].values[5] == 0,
	      "  ...GAMMA_LUT_SIZE beside it and the other CRTC left as they were");
	CHECK(unknown == 0, "  ...every request a number Linux knows");
	n = panelgamma_clear(3);
	CHECK(n == 0 && sets == 1, "  ...and asked again, nothing more to do");

	pixel2(0, 1);
	n = panelgamma_clear(3);
	CHECK(n == 0 && sets == 0, "a table already empty (this splash): left alone, no refresh's wait");

	pixel2(1, 0);
	n = panelgamma_clear(3);
	CHECK(n == 0 && sets == 0, "a kernel with no GAMMA_LUT (QEMU's display): nothing to do");

	pixel2(1, 1);
	master = 0;
	n = panelgamma_clear(3);
	CHECK(n == -1 && crtcs[0].values[5] == 47, "without DRM master the kernel refuses: -1, the table as it was");

	pixel2(1, 1);
	broken = 1;
	CHECK(panelgamma_clear(3) == -1, "not a display (the ioctls refused): -1");
	broken = 0;
	CHECK(panelgamma_clear(-1) == -1 && sets == 0, "no display at all (fd -1): -1, nothing asked");

	if (fails) {
		printf("check-panelgamma: %d failed\n", fails);
		return 1;
	}
	printf("check-panelgamma: all passed\n");
	return 0;
}

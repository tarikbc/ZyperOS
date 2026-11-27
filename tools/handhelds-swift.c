/* SPDX-License-Identifier: GPL-3.0-only */
/* handhelds-swift: the Simulator's handhelds (simulator/Sources/SimCore/
 * Handhelds.swift) from the device files themselves. Every handheld the
 * host links (src/profile.c's list) with a model of its own and a flat
 * drawing of it (src/device/td_model.h's td_flat), in the places
 * td_flat_parts works out, so the Simulator and desk mode (deskface.c) draw
 * the same keys in the same places. `make handhelds` writes the file;
 * `make check-handhelds` fails when it is behind the device files.
 *
 * It also checks what a device file can get wrong that the compiler can't:
 * a flat key that names a key the model has not, a key the Simulator has no
 * button for, a screen outside its glass or a glass outside the body. */
#include "../src/device/profile.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void fail(const char *id, const char *what)
{
	fprintf(stderr, "handhelds-swift: %s: %s\n", id, what);
	fails++;
}

/* The Simulator's button (SimCore's PadButton) for each of a handheld's
 * keys, by px2-input's names; the d-pad is its own */
static const char *button(td_key k)
{
	switch (k) {
	case TD_A:      return ".a";
	case TD_B:      return ".b";
	case TD_X:      return ".x";
	case TD_Y:      return ".y";
	case TD_SELECT: return ".select";
	case TD_START:  return ".start";
	case TD_MENU:   return ".menu";
	case TD_VOLUP:  return ".volup";
	case TD_VOLDN:  return ".voldown";
	case TD_POWER:  return ".power";
	case TD_L1:     return ".l1";
	case TD_R1:     return ".r1";
	case TD_L2:     return ".l2";
	case TD_R2:     return ".r2";
	case TD_F1:     return ".brightdown";
	case TD_F2:     return ".brightup";
	default:        return NULL;
	}
}

/* A number as Swift writes it */
static const char *num(double v)
{
	static char buf[8][32];
	static int at;
	char *b = buf[at++ % 8];

	if (v > -0.00005 && v < 0.00005) v = 0;
	snprintf(b, sizeof buf[0], "%.6g", v);
	return b;
}

static void str(const char *s)
{
	if (!s) {
		fputs("nil", stdout);
		return;
	}
	putchar('"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') putchar('\\');
		putchar(*s);
	}
	putchar('"');
}

static void rect(const char *name, td_rect r)
{
	if (name) printf("%s: ", name);
	printf("Rect(x: %s, y: %s, w: %s, h: %s)", num(r.x), num(r.y), num(r.w), num(r.h));
}

static bool inside(td_rect in, td_rect out)
{
	return in.x - in.w / 2 >= out.x - out.w / 2 - 0.01f && in.x + in.w / 2 <= out.x + out.w / 2 + 0.01f &&
	       in.y - in.h / 2 >= out.y - out.h / 2 - 0.01f && in.y + in.h / 2 <= out.y + out.h / 2 + 0.01f;
}

static void handheld(const device_profile *p)
{
	static const char *const KIND[] = { ".round", ".pill", ".dpad", ".side", ".shoulder" };
	static const char *const HINT[] = { ".below", ".right", ".left", ".above", ".on" };
	const td_model *m = p->model;
	const td_flat *f = m->flat;
	td_flat_part parts[TD_KEYS];
	int n = td_flat_parts(m, parts, TD_KEYS), i, j;
	int pw = p->screen.turn % 180 ? p->screen.panel_h : p->screen.panel_w;
	int ph = p->screen.turn % 180 ? p->screen.panel_w : p->screen.panel_h;
	td_rect body = { 0, 0, m->body_w, m->body_h };
	td_rect face = { 0, (f->margin_top - f->margin_bottom) / 2, m->body_w + 2 * f->margin_x,
	                 m->body_h + f->margin_top + f->margin_bottom };

	for (i = 0; i < f->n_keys; i++)
		if (!td_part_of(m, f->keys[i].key)) fail(p->id, "a flat key names a key its model has not");
	if (!inside(f->screen, f->glass)) fail(p->id, "the screen is not inside its glass");
	if (!inside(f->glass, body)) fail(p->id, "the glass is not inside the body");
	if (f->recess.w > 0 && !inside(f->recess, body)) fail(p->id, "the well is not inside the body");
	for (i = 0; i < f->n_sticks; i++) {
		const td_flat_stick *k = &f->sticks[i];
		td_rect w = { k->x, k->y, 2 * k->well, 2 * k->well };

		if (!inside(w, body) || k->cap <= 0 || k->cap >= k->well) fail(p->id, "a stick not inside the body, or its cap not in its well");
	}

	printf("        Handheld(\n");
	printf("            id: ");
	str(p->id);
	printf(", name: ");
	str(m->name);
	printf(", panel: Size(w: %d, h: %d),\n", pw, ph);
	printf("            body: Size(w: %s, h: %s), corner: %s,\n", num(m->body_w), num(m->body_h), num(f->corner));
	printf("            margin: Margins(x: %s, top: %s, bottom: %s),\n", num(f->margin_x), num(f->margin_top),
	       num(f->margin_bottom));
	printf("            ");
	rect("glass", f->glass);
	printf(", glassRadius: %s,\n            ", num(f->glass_r));
	rect("screen", f->screen);
	printf(",\n            word: ");
	str(f->word);
	printf(", wordY: %s,\n            recess: ", num(f->word_y));
	if (f->recess.w > 0) rect(NULL, f->recess); else printf("nil");
	printf(", recessRadius: %s, dpadArm: %s,\n", num(f->recess_r), num(f->dpad_arm));
	printf("            lightX: %s, light: Size(w: %s, h: %s),\n", num(f->light_x), num(f->light_w), num(f->light_h));
	printf("            lights: [");
	for (i = 0; i < f->n_lights; i++) {
		const td_flat_light *l = &f->lights[i];
		printf("%sLight(y: %s, green: ", i ? ", " : "", num(l->y));
		if (l->green >= 0) printf("%d", l->green); else printf("nil");
		printf(", red: ");
		if (l->red >= 0) printf("%d", l->red); else printf("nil");
		printf(")");
	}
	printf("],\n");
	printf("            colors: Colors(bodyHi: 0x%06X, body: 0x%06X, bodyLo: 0x%06X, recess: 0x%06X,\n"
	       "                           cap: 0x%06X, capDown: 0x%06X, marking: 0x%06X, glass: 0x%06X,\n"
	       "                           lightOff: 0x%06X),\n",
	       f->body_hi, f->body, f->body_lo, f->recess_c, f->cap, f->cap_down, f->marking, f->glass_c, f->light_off);
	printf("            sticks: [");
	for (i = 0; i < f->n_sticks; i++)
		printf("%sStick(x: %s, y: %s, well: %s, cap: %s)", i ? ", " : "", num(f->sticks[i].x),
		       num(f->sticks[i].y), num(f->sticks[i].well), num(f->sticks[i].cap));
	printf("],\n");
	printf("            keys: [\n");
	for (i = 0; i < n; i++) {
		const td_flat_part *k = &parts[i];
		const char *b = button(k->key);
		td_rect box = { k->x, k->y, k->w, k->h };

		if (k->kind != TD_FLAT_DPAD && !b) fail(p->id, "a key the Simulator has no button for");
		if (!inside(box, face)) fail(p->id, "a key outside the drawing (its margins too small?)");
		printf("                Key(button: %s, kind: %s, x: %s, y: %s, w: %s, h: %s, label: ",
		       k->kind == TD_FLAT_DPAD ? "nil" : b ? b : "nil", KIND[k->kind], num(k->x), num(k->y), num(k->w),
		       num(k->h));
		str(k->look->label ? k->look->label : "");
		printf(", under: ");
		str(k->look->under);
		printf(", hint: %s, hintDX: %s),\n", HINT[k->look->hint], num(k->look->hint_dx));
	}
	printf("            ],\n");
	printf("            labels: [");
	for (j = 0; j < f->n_labels; j++) {
		const td_flat_label *l = &f->labels[j];
		printf("%sLabel(text: ", j ? ", " : "");
		str(l->text);
		printf(", x: %s, y: %s, turned: %s)", num(l->x), num(l->y), l->turned ? "true" : "false");
	}
	printf("]),\n");
}

int main(void)
{
	const td_model *seen[16];
	const device_profile *p;
	int i, j, n_seen = 0;

	printf("// SPDX-License-Identifier: GPL-3.0-only\n"
	       "// The handhelds the Simulator draws, from the device files themselves\n"
	       "// (src/device/<id>_model.c, td_model.h's td_flat), as desk mode draws them.\n"
	       "// Written by `make handhelds` (tools/handhelds-swift.c): do not edit. Change\n"
	       "// the model file and run `make handhelds`; `make check-handhelds` fails while\n"
	       "// this is behind it.\n\n"
	       "extension Handheld {\n"
	       "    public static let all: [Handheld] = [\n");
	for (i = 0; (p = plat_profile_at(i)); i++) {
		bool dup = false;

		if (!p->model || p->model_borrowed || !p->model->flat) continue;
		for (j = 0; j < n_seen; j++) dup |= seen[j] == p->model;
		if (dup || n_seen == (int)(sizeof seen / sizeof seen[0])) continue;
		seen[n_seen++] = p->model;
		handheld(p);
	}
	printf("    ]\n}\n");
	if (!n_seen) fail("profiles", "no handheld with a flat drawing");
	return fails ? 1 : 0;
}

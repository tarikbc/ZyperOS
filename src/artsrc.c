/* SPDX-License-Identifier: GPL-3.0-only */
/* Where a game's cover came from: src/artsrc.h. */
#include "artsrc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"

static const char *const SRC[AS_SOURCES] = { "lr", "ss", "sg" };
static const char *const KIND[AK_KINDS] = { "box", "box3d", "title", "shot", "fan" };

const char *artsrc_name(as_source s)
{
	switch (s) {
	case AS_LR: return "libretro";
	case AS_SS: return "ScreenScraper";
	case AS_SG: return "SteamGridDB";
	default:    return "";
	}
}

static bool plain(const char *s)
{
	return !strchr(s, '|') && !strchr(s, '\n');
}

bool artsrc_format(const art_source *s, char *out, size_t n)
{
	int len;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!s || s->src < 0 || s->src >= AS_SOURCES || s->kind < 0 || s->kind >= AK_KINDS) return false;
	if (!plain(s->region) || !plain(s->by)) return false;
	len = snprintf(out, n, "%s|%s|%s|%s", SRC[s->src], KIND[s->kind], s->region, s->by);
	if (len < 0 || (size_t)len >= n) { out[0] = '\0'; return false; }
	return true;
}

/* The next "|"-separated part of *p into out, *p past it */
static bool part(const char **p, char *out, size_t n)
{
	const char *bar = strchr(*p, '|');
	size_t len = bar ? (size_t)(bar - *p) : strlen(*p);

	if (len >= n) return false;
	memcpy(out, *p, len);
	out[len] = '\0';
	*p += len + (bar ? 1 : 0);
	return true;
}

bool artsrc_parse(const char *v, art_source *out)
{
	char src[8], kind[8];
	int i;

	if (!v || !out) return false;
	memset(out, 0, sizeof *out);
	out->src = AS_NONE;
	/* Four parts, three bars: anything else is not a record */
	{
		const char *q;
		int bars = 0;

		for (q = v; *q; q++) bars += *q == '|';
		if (bars != 3) return false;
	}
	if (!part(&v, src, sizeof src) || !part(&v, kind, sizeof kind) ||
	    !part(&v, out->region, sizeof out->region) || !part(&v, out->by, sizeof out->by))
		return false;
	for (i = 0; i < AS_SOURCES; i++) if (!strcmp(src, SRC[i])) out->src = (as_source)i;
	for (i = 0; i < AK_KINDS; i++) if (!strcmp(kind, KIND[i])) break;
	if (out->src == AS_NONE || i == AK_KINDS) { out->src = AS_NONE; return false; }
	out->kind = (as_kind)i;
	return true;
}

bool artsrc_same(const art_source *a, const art_source *b)
{
	return a && b && a->src != AS_NONE && a->src == b->src && a->kind == b->kind &&
	       !strcmp(a->region, b->region) && !strcmp(a->by, b->by);
}

bool artsrc_key(const char *prefix, const char *folder, const char *name, char *out, size_t n)
{
	int len;

	if (!out || !n || !prefix || !folder || !name || !folder[0] || !name[0]) return false;
	len = snprintf(out, n, "%s.%s/%s", prefix, folder, name);
	return len > 0 && (size_t)len < n;
}

static bool get(const char *prefix, const char *folder, const char *name, art_source *out)
{
	char key[1024], v[320];

	if (out) { memset(out, 0, sizeof *out); out->src = AS_NONE; }
	if (!db_lib() || !artsrc_key(prefix, folder, name, key, sizeof key)) return false;
	if (!db_get_str(db_lib(), key, v, sizeof v, NULL) || !v[0]) return false;
	return artsrc_parse(v, out);
}

static bool put(const char *prefix, const char *folder, const char *name, const art_source *s)
{
	char key[1024], v[320];

	if (!db_lib() || !artsrc_key(prefix, folder, name, key, sizeof key)) return false;
	if (!s || s->src == AS_NONE) return db_del(db_lib(), key);
	if (!artsrc_format(s, v, sizeof v)) return false;
	return db_set_str(db_lib(), key, v);
}

bool artsrc_get(const char *folder, const char *name, art_source *out)
{
	return get("artsrc", folder, name, out);
}

bool artsrc_set(const char *folder, const char *name, const art_source *s)
{
	return put("artsrc", folder, name, s);
}

bool artsrc_get_before(const char *folder, const char *name, art_source *out)
{
	return get("artbefore", folder, name, out);
}

void artsrc_swap(const char *folder, const char *name)
{
	art_source now, before;

	artsrc_get(folder, name, &now);
	artsrc_get_before(folder, name, &before);
	put("artsrc", folder, name, &before);
	put("artbefore", folder, name, &now);
}

void artsrc_forget(const char *folder, const char *name)
{
	put("artsrc", folder, name, NULL);
	put("artbefore", folder, name, NULL);
}

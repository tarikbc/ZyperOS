/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamern.h"

#include "db.h"
#include "favorites.h"
#include "library.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- names ---------------------------------------------------------------- */

static const char *base_of(const char *path)
{
	const char *s = strrchr(path, '/');

	return s ? s + 1 : path;
}

/* The last part's: a dot in a folder of games' name is no extension */
static const char *ext_of(const char *name)
{
	const char *b = base_of(name);
	const char *dot = strrchr(b, '.');

	return dot && dot != b ? dot + 1 : NULL;
}

static bool ext_is(const char *name, const char *ext)
{
	const char *e = ext_of(name);

	return e && !strcasecmp(e, ext);
}

/* The launch path past the folders of games it is kept in (gd_game.dir):
 * "Pokemon.gba", or a disc folder's "FF7/ff7.cue". NULL for a `dir` that
 * is not a run of whole folder names in front of it. */
static const char *own_part(const char *file, unsigned dir)
{
	size_t len = strlen(file);

	if (!dir) return file;
	if (dir >= len || file[dir] != '/' || file[dir - 1] == '/') return NULL;
	for (const char *p = file, *s; p < file + dir; p = s + 1) {
		size_t l;

		s = memchr(p, '/', (size_t)(file + dir - p));
		if (!s) s = file + dir;
		l = (size_t)(s - p);
		if (!l || (l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.')) return NULL;
	}
	return file + dir + 1;
}

/* The base without its last extension, as Diatom names a save */
static void stem_of(const char *path, char *out, size_t n)
{
	const char *b = base_of(path);
	const char *dot = strrchr(b, '.');

	snprintf(out, n, "%.*s", dot && dot != b ? (int)(dot - b) : (int)strlen(b), b);
}

bool gr_name_ok(const char *typed, char *out, size_t n)
{
	const unsigned char *p;
	size_t len;

	if (!typed || !out || !n) return false;
	while (*typed == ' ') typed++;
	len = strlen(typed);
	while (len && typed[len - 1] == ' ') len--;
	if (!len || len > GR_NAME_MAX || len >= n) return false;
	snprintf(out, n, "%.*s", (int)len, typed);
	/* A dot first is a hidden file to every shelf; a dot or a space last is
	 * one FAT and exFAT drop as they write the name */
	if (out[0] == '.' || out[len - 1] == '.') return false;
	for (p = (const unsigned char *)out; *p; ) {
		int more;

		if (*p < 0x20 || *p == 0x7f || strchr("/\\:*?\"<>|", *p)) return false;
		if (*p < 0x80) { p++; continue; }
		more = *p >= 0xf0 && *p <= 0xf4 ? 3 : *p >= 0xe0 ? 2 : *p >= 0xc2 && *p < 0xe0 ? 1 : -1;
		if (more < 0 || *p > 0xf4) return false;
		for (p++; more; more--, p++)
			if ((*p & 0xc0) != 0x80) return false;
	}
	return true;
}

/* An arcade set (its name is the game, for the core), a BIOS set, a port */
bool gr_renames(const char *core, const char *file, unsigned dir)
{
	const char *own;

	if (!core || !file || !(own = own_part(file, dir))) return false;
	/* Its own part: "Arcade/neogeo.zip" in a folder of games is the set */
	if (!strcmp(core, "port") || gd_is_bios(own)) return false;
	if (!strcmp(core, "fbneo") || !strncmp(core, "mame", 4)) return false;
	if (!strncmp(core, "flycast", 7) && !strchr(own, '/') && (ext_is(own, "zip") || ext_is(own, "7z")))
		return false;
	return true;
}

/* ---- the plan --------------------------------------------------------------- */

static bool exists(const char *p)
{
	struct stat st;

	return lstat(p, &st) == 0;
}

/* The same file under two names: a change of case on a card that ignores
 * case */
static bool same_file(const char *a, const char *b)
{
	struct stat x, y;

	return stat(a, &x) == 0 && stat(b, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
}

/* A path, made from a format */
static bool fmt(char *out, size_t n, const char *f, ...)
{
	va_list ap;
	int w;

	va_start(ap, f);
	w = vsnprintf(out, n, f, ap);
	va_end(ap);
	return w >= 0 && (size_t)w < n;
}

static void pair(gr_plan *p, const char *from, const char *to, bool copy, bool must)
{
	if (p->why != GR_OK) return;
	for (int i = 0; i < p->n; i++)
		if (!strcmp(p->from[i], from)) return;               /* once */
	if (!exists(from)) {
		if (must) { p->why = GR_GONE; snprintf(p->bad, sizeof p->bad, "%s", from); }
		return;
	}
	if (exists(to) && !same_file(from, to)) {
		p->why = GR_TAKEN;
		snprintf(p->bad, sizeof p->bad, "%s", to);
		return;
	}
	if (p->n >= GR_MAX) { p->why = GR_TOO_MANY; return; }
	snprintf(p->from[p->n], GD_PATH, "%s", from);
	snprintf(p->to[p->n], GD_PATH, "%s", to);
	p->copy[p->n] = copy;
	p->n++;
}

static void too_long(gr_plan *p)
{
	if (p->why == GR_OK) p->why = GR_TOO_MANY;
}

/* What the other games share with this one, as Delete Game holds them */
static bool shares_stem(const gd_other *o, int n, const char *stem)
{
	char s[LIB_NAME];

	for (int i = 0; i < n; i++) {
		stem_of(o[i].file, s, sizeof s);
		if (!strcasecmp(s, stem)) return true;
	}
	return false;
}

static bool shares_name(const gd_other *o, int n, const char *folder, const char *name)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(o[i].folder, folder) && !strcasecmp(o[i].name, name)) return true;
	return false;
}

static bool shares_state(const gd_other *o, int n, const char *folder, const char *base)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(o[i].folder, folder) &&
		    !strcasecmp(gd_state_base(o[i].file, o[i].name, o[i].hooked), base))
			return true;
	return false;
}

static bool shares_file(const gd_other *o, int n, const char *folder, const char *rel)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(o[i].folder, folder) && !strcasecmp(o[i].file, rel)) return true;
	return false;
}

bool gr_plan_make(const gd_card *card, const gd_game *g, const gd_other *others, int nothers,
                  const char *typed, gr_plan *p)
{
	static const char *const SLOTS[] = { "auto", "1", "2", "3", "4", "5", "6" };
	static const char *const MEDIA[] = { "png", "mp4" };
	char to[LIB_NAME], romdir[GD_PATH], stem[LIB_NAME], ob[LIB_PATH], nb[LIB_PATH], a[GD_PATH], b[GD_PATH];
	char pre[LIB_PATH];
	const char *own, *slash, *core = g->core ? g->core : "";
	bool hooked = lib_shelf_hooked(g->exts);
	size_t i;

	memset(p, 0, sizeof *p);
	p->why = GR_OK;
	if (!gr_name_ok(typed, to, sizeof to)) { p->why = GR_BAD_NAME; return false; }
	if (!card || !card->roms || !card->saves || !card->states || !g->folder || !g->tag ||
	    !g->file || !g->name || !(own = own_part(g->file, g->dir))) {
		p->why = GR_TOO_MANY;
		return false;
	}
	if (!strcmp(to, g->name)) { p->why = GR_SAME; return false; }
	if (!gr_renames(core, g->file, g->dir)) { p->why = GR_KEEPS_NAME; return false; }
	snprintf(p->roms, sizeof p->roms, "%s", card->roms);
	snprintf(p->folder, sizeof p->folder, "%s", g->folder);
	snprintf(p->file, sizeof p->file, "%s", g->file);
	snprintf(p->name, sizeof p->name, "%s", g->name);
	snprintf(p->new_name, sizeof p->new_name, "%s", to);
	if (!fmt(romdir, sizeof romdir, "%s/%s", card->roms, g->folder)) { too_long(p); return false; }
	/* The folder of games it is kept in stays as it is: "Rom Hacks/" of
	 * "Rom Hacks/Pokemon.gba", nothing at the top. Only the game's own part
	 * takes the name: its file, or the disc folder that is the game. */
	if (!fmt(pre, sizeof pre, "%.*s", (int)(own - g->file), g->file)) { too_long(p); return false; }
	slash = strchr(own, '/');

	/* Another game on the shelf by that name: the covers, the video and the
	 * achievement set would be its. Any case: the card ignores it. */
	for (int k = 0; k < nothers; k++)
		if (!strcmp(others[k].folder, g->folder) && !strcasecmp(others[k].name, to)) {
			p->why = GR_TAKEN;
			snprintf(p->bad, sizeof p->bad, "%s", others[k].file);
			return false;
		}

	/* 1. The game: its file, its extension kept, or the folder that is
	 * the game, the disc in it as it is */
	if (slash) {
		if (!fmt(p->new_file, sizeof p->new_file, "%s%s%s", pre, to, slash) ||
		    !fmt(a, sizeof a, "%s/%.*s", romdir, (int)(slash - g->file), g->file) ||
		    !fmt(b, sizeof b, "%s/%s%s", romdir, pre, to)) { too_long(p); return false; }
	} else {
		const char *ext = ext_of(own);

		if (!fmt(p->new_file, sizeof p->new_file, "%s%s%s%s", pre, to, ext ? "." : "", ext ? ext : "") ||
		    !fmt(a, sizeof a, "%s/%s", romdir, g->file) ||
		    !fmt(b, sizeof b, "%s/%s", romdir, p->new_file)) { too_long(p); return false; }
	}
	pair(p, a, b, false, true);
	if (p->why != GR_OK) return false;

	/* 2. A file of its own (no disc folder): what its format keeps beside it
	 * by its stem (a .ccd's image and subchannel, an .mds's data, the .sbi
	 * PCSX ReARMed reads beside a .cue), in the same folder of games; its
	 * saves flat in Saves/ by the stem, and OpenBOR's under its own */
	stem_of(g->file, stem, sizeof stem);
	if (!slash) {
		static const struct { const char *ext, *with[2]; } SIDE[] = {
			{ "ccd", { "img", "sub" } }, { "mds", { "mdf", NULL } },
			{ "mdf", { "mds", NULL } },  { "cue", { "sbi", NULL } },
		};
		static const char *const FLAT[] = { "srm", "sav", "dsv" };
		bool shared = shares_stem(others, nothers, stem);

		for (i = 0; i < sizeof SIDE / sizeof SIDE[0]; i++) {
			if (!ext_is(g->file, SIDE[i].ext)) continue;
			for (int k = 0; k < 2 && SIDE[i].with[k]; k++) {
				char rel[LIB_PATH];

				if (!fmt(rel, sizeof rel, "%s%s.%s", pre, stem, SIDE[i].with[k]) ||
				    !fmt(a, sizeof a, "%s/%s", romdir, rel) ||
				    !fmt(b, sizeof b, "%s/%s%s.%s", romdir, pre, to, SIDE[i].with[k])) { too_long(p); return false; }
				pair(p, a, b, shares_file(others, nothers, g->folder, rel), false);
			}
		}
		for (i = 0; i < sizeof FLAT / sizeof FLAT[0]; i++) {
			if (!fmt(a, sizeof a, "%s/%s.%s", card->saves, stem, FLAT[i]) ||
			    !fmt(b, sizeof b, "%s/%s.%s", card->saves, to, FLAT[i])) { too_long(p); return false; }
			pair(p, a, b, shared, false);
		}
		if (ext_is(g->file, "pak") && strstr(core, "OpenBOR")) {
			static const char *const OB[] = { "sav", "hi", "scr", "cfg", "inp", "controls" };

			for (i = 0; i < sizeof OB / sizeof OB[0]; i++) {
				if (!fmt(a, sizeof a, "%s/openbor/Saves/%s.%s", card->saves, stem, OB[i]) ||
				    !fmt(b, sizeof b, "%s/openbor/Saves/%s.%s", card->saves, to, OB[i])) {
					too_long(p);
					return false;
				}
				pair(p, a, b, shared, false);
			}
		}
	}

	/* 3. Its states and their pictures, the six slots and Auto, and the
	 * language a DS game's Auto was left in: by the launch file's base, or
	 * a hooked shelf's folder name (gd_state_base) */
	snprintf(ob, sizeof ob, "%s", gd_state_base(g->file, g->name, hooked));
	snprintf(nb, sizeof nb, "%s", gd_state_base(p->new_file, to, hooked));
	if (strcmp(ob, nb)) {
		bool shared = shares_state(others, nothers, g->folder, ob);

		for (i = 0; i < sizeof SLOTS / sizeof SLOTS[0]; i++) {
			if (!fmt(a, sizeof a, "%s/%s/%s.%s.state", card->states, g->folder, ob, SLOTS[i]) ||
			    !fmt(b, sizeof b, "%s/%s/%s.%s.state", card->states, g->folder, nb, SLOTS[i]))
				{ too_long(p); return false; }
			pair(p, a, b, shared, false);
			if (!fmt(a, sizeof a, "%s/%s/%s.%s.bmp", card->states, g->folder, ob, SLOTS[i]) ||
			    !fmt(b, sizeof b, "%s/%s/%s.%s.bmp", card->states, g->folder, nb, SLOTS[i]))
				{ too_long(p); return false; }
			pair(p, a, b, shared, false);
		}
		if (!fmt(a, sizeof a, "%s/%s/%s.auto.lang", card->states, g->folder, ob) ||
		    !fmt(b, sizeof b, "%s/%s/%s.auto.lang", card->states, g->folder, nb)) { too_long(p); return false; }
		pair(p, a, b, shared, false);
	}

	/* 4. Its cover and video snap, Before's cover, and its cached
	 * achievement set, by the name */
	{
		bool shared = shares_name(others, nothers, g->folder, g->name);

		for (i = 0; i < sizeof MEDIA / sizeof MEDIA[0]; i++) {
			if (!fmt(a, sizeof a, "%s/.media/%s.%s", romdir, g->name, MEDIA[i]) ||
			    !fmt(b, sizeof b, "%s/.media/%s.%s", romdir, to, MEDIA[i])) { too_long(p); return false; }
			pair(p, a, b, shared, false);
		}
		if (!fmt(a, sizeof a, "%s/.media/.before/%s.png", romdir, g->name) ||
		    !fmt(b, sizeof b, "%s/.media/.before/%s.png", romdir, to)) { too_long(p); return false; }
		pair(p, a, b, shared, false);
		if (!fmt(a, sizeof a, "%s/.cheevos/%s.set", romdir, g->name) ||
		    !fmt(b, sizeof b, "%s/.cheevos/%s.set", romdir, to)) { too_long(p); return false; }
		pair(p, a, b, shared, false);

		/* 5. EmulationStation's pictures and video named after it, and
		 * what its gamelist.xml entry names (src/esmedia.h) */
		p->nes = es_files(card->roms, g->folder, g->file, g->name, to, p->es_rel, p->es_to, GR_ES);
		for (int k = 0; k < p->nes; k++) {
			if (!fmt(a, sizeof a, "%s/%s", romdir, p->es_rel[k]) ||
			    !fmt(b, sizeof b, "%s/%s", romdir, p->es_to[k])) { too_long(p); return false; }
			pair(p, a, b, shared, false);
		}
	}
	return p->why == GR_OK;
}

/* ---- running it --------------------------------------------------------------- */

static bool copy_file(const char *from, const char *to)
{
	char tmp[GD_PATH + 8], buf[65536];
	int in, out;
	ssize_t r;
	bool ok = true;

	if (snprintf(tmp, sizeof tmp, "%s.part", to) >= (int)sizeof tmp) return false;
	if ((in = open(from, O_RDONLY)) < 0) return false;
	if ((out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) { close(in); return false; }
	while (ok && (r = read(in, buf, sizeof buf)) > 0)
		ok = write(out, buf, (size_t)r) == r;
	ok = ok && r == 0 && fsync(out) == 0;
	close(in);
	ok = close(out) == 0 && ok;
	if (ok && !exists(to)) ok = rename(tmp, to) == 0;
	else ok = false;
	if (!ok) unlink(tmp);
	return ok;
}

/* `from` to `to`, never over another file. A change of case alone goes
 * through a name of its own: on a card that ignores case, `to` is `from`,
 * and a rename onto it leaves the old spelling. */
static bool move(const char *from, const char *to)
{
	if (same_file(from, to)) {
		char tmp[GD_PATH + 16];

		if (!strcmp(from, to)) return true;
		if (snprintf(tmp, sizeof tmp, "%s.zyperos-rn", from) >= (int)sizeof tmp || exists(tmp))
			return false;
		if (rename(from, tmp) != 0) return false;
		if (rename(tmp, to) == 0) return true;
		rename(tmp, from);
		return false;
	}
	if (exists(to)) return false;
	return rename(from, to) == 0;
}

static void mkdir_for(const char *path)
{
	char d[GD_PATH], *s;

	snprintf(d, sizeof d, "%s", path);
	if ((s = strrchr(d, '/'))) { *s = '\0'; mkdir(d, 0755); }
}

void gr_plan_run(const gr_plan *p, gr_done *d)
{
	memset(d, 0, sizeof *d);
	if (p->why != GR_OK || p->n <= 0) return;
	d->game_moved = move(p->from[0], p->to[0]);
	if (!d->game_moved) { d->failed = 1; return; }
	for (int i = 1; i < p->n; i++) {
		bool ok;

		mkdir_for(p->to[i]);
		ok = p->copy[i] ? copy_file(p->from[i], p->to[i]) : move(p->from[i], p->to[i]);
		if (!ok) d->failed++;
		else if (p->copy[i]) d->copied++;
		else d->moved++;
	}
	/* gamelist.xml's entry, with what moved of its */
	d->es_list = es_list_rename(p->roms, p->folder, p->file, p->name, p->new_file,
	                            (char (*)[ES_PATH])p->es_rel, (char (*)[ES_PATH])p->es_to, p->nes);
}

/* ---- the library -------------------------------------------------------------- */

typedef struct key_row { char key[LIB_PATH + 128]; struct key_row *next; } key_row;
typedef struct { const char *tag, *file; key_row *head; } sess_ctx;

/* sess.<start>.<t0>.<tag>\t<file>, for this tag and file exactly (stats.c) */
static bool sess_row(const char *key, const char *value, void *ctx)
{
	sess_ctx *c = ctx;
	const char *dot = strchr(key + strlen("sess."), '.'), *tab;
	size_t tl = strlen(c->tag);
	key_row *r;

	(void)value;
	if (!dot || !(dot = strchr(dot + 1, '.')) || !(tab = strchr(dot + 1, '\t'))) return true;
	if ((size_t)(tab - dot - 1) != tl || strncmp(dot + 1, c->tag, tl) || strcmp(tab + 1, c->file))
		return true;
	if (!(r = malloc(sizeof *r))) return false;
	snprintf(r->key, sizeof r->key, "%s", key);
	r->next = c->head;
	c->head = r;
	return true;
}

int gr_follow(const char *folder, const char *tag, const char *file, const char *name,
              const char *new_file, const char *new_name)
{
	char a[LIB_PATH * 2 + 64], b[LIB_PATH * 2 + 64];
	sess_ctx c = { tag, file, NULL };
	key_row *r;
	int n = 0;

	if (!db_lib() || !folder || !tag || !file || !name || !new_file || !new_name) return 0;
	if (db_game_move(db_lib(), folder, file, new_file)) n++;
	if (fav_is(tag, file)) {
		fav_forget(tag, file);
		fav_toggle(tag, new_file);
		fav_save();
		n++;
	}
	snprintf(a, sizeof a, "pak.%s.%s", tag, file);
	snprintf(b, sizeof b, "pak.%s.%s", tag, new_file);
	n += db_key_move(db_lib(), a, b);
	snprintf(a, sizeof a, "artsrc.%s/%s", folder, name);
	snprintf(b, sizeof b, "artsrc.%s/%s", folder, new_name);
	n += db_key_move(db_lib(), a, b);
	snprintf(a, sizeof a, "artbefore.%s/%s", folder, name);
	snprintf(b, sizeof b, "artbefore.%s/%s", folder, new_name);
	n += db_key_move(db_lib(), a, b);
	/* Collected first, then moved: the listing does not see its own writes */
	db_each_prefix(db_lib(), "sess.", sess_row, &c);
	while ((r = c.head)) {
		size_t keep = strlen(r->key) - strlen(file);

		c.head = r->next;
		snprintf(b, sizeof b, "%.*s%s", (int)keep, r->key, new_file);
		n += db_key_move(db_lib(), r->key, b);
		free(r);
	}
	return n;
}

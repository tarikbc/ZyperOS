/* SPDX-License-Identifier: GPL-3.0-only */
/* Delete Game. See gamedel.h for the rules; this is the paths. */
#include "gamedel.h"
#include "portinfo.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "db.h"
#include "favorites.h"
#include "library.h"
#include "stats.h"

/* ---- BIOS sets ----------------------------------------------------------
 *
 * The boards' own sets, which the arcade cores read beside a game or in
 * Bios/: FinalBurn Neo's BDF_BOARDROM sets, MAME 2003-Plus's BIOS sets, and
 * Flycast's NAOMI and Atomiswave boards. A shelf lists one when it is there
 * (Arcade and Neo Geo take every .zip), and deleting it from the shelf would
 * stop every game on the board. Sorted, for the reader. */
static const char *const BIOS_SETS[] = {
	"acpsx", "airlbios", "atpsx", "awbios", "bubsys", "cchip", "channelf",
	"coleco", "cpzn1", "cpzn2", "cvs", "decocass", "f355bios", "f355dlx",
	"fdsbios", "galgbios", "hng64", "hod2bios", "isgsm", "konamigx",
	"megaplay", "megatech", "midssio", "msx_msx", "namcoc69", "namcoc70",
	"namcoc75", "naomi", "naomi2", "naomigd", "neocdz", "neogeo", "ngp",
	"nmk004", "nss", "pgm", "playch10", "psarc95", "skns", "spec128",
	"spec1282a", "spectrum", "stvbios", "sys573", "taitofx1", "taitogn",
	"tps", "ym2608", NULL
};

static const char *ext_of(const char *name)
{
	const char *slash = strrchr(name, '/');
	const char *base = slash ? slash + 1 : name;
	const char *dot = strrchr(base, '.');

	return dot && dot != base ? dot + 1 : NULL;
}

static bool ext_is(const char *name, const char *ext)
{
	const char *e = ext_of(name);

	return e && !strcasecmp(e, ext);
}

static const char *base_of(const char *path)
{
	const char *s = strrchr(path, '/');

	return s ? s + 1 : path;
}

/* The base without its last extension, as Diatom names a save (save.c) */
static void stem_of(const char *path, char *out, size_t n)
{
	const char *b = base_of(path);
	const char *dot = strrchr(b, '.');

	snprintf(out, n, "%.*s", dot && dot != b ? (int)(dot - b) : (int)strlen(b), b);
}

bool gd_is_bios(const char *file)
{
	char stem[LIB_NAME];
	int i;

	if (!file || strchr(file, '/') || !(ext_is(file, "zip") || ext_is(file, "7z")))
		return false;
	stem_of(file, stem, sizeof stem);
	for (i = 0; BIOS_SETS[i]; i++)
		if (!strcasecmp(stem, BIOS_SETS[i])) return true;
	return false;
}

bool gd_shelf_deletes(const char *core)
{
	return core != NULL;
}

/* ---- names --------------------------------------------------------------- */

/* One part of a path the key gave: not empty, not "." or "..", no slash,
 * and not hidden - the shelf never lists a dot name, so a key holding one
 * did not come from it */
static bool part_ok(const char *s, size_t n)
{
	if (n == 0 || s[0] == '.') return false;
	for (size_t i = 0; i < n; i++)
		if (s[i] == '/' || s[i] == '\\' || s[i] == '\0') return false;
	return true;
}

static bool name_ok(const char *s)
{
	return s && part_ok(s, strlen(s));
}

/* The launch path: one part, or a folder and the disc in it, after `dir`
 * bytes of the folders of games it is kept in, each a part as well */
static bool file_ok(const char *f, unsigned dir)
{
	const char *slash, *own;

	if (!f || !*f || dir >= strlen(f) || (dir && f[dir] != '/')) return false;
	for (const char *p = f; dir && p < f + dir; p = slash + 1) {
		slash = memchr(p, '/', (size_t)(f + dir - p));
		if (!slash) slash = f + dir;
		if (!part_ok(p, (size_t)(slash - p))) return false;
	}
	own = f + (dir ? dir + 1 : 0);
	slash = strchr(own, '/');
	if (!slash) return name_ok(own);
	return part_ok(own, (size_t)(slash - own)) && !strchr(slash + 1, '/') && name_ok(slash + 1);
}

/* A name a sheet gives, against the folder the sheet is in (`dir`, relative
 * to the console's folder, "" for its top): the path relative to the
 * console's folder, or false for one that leaves it - "..", an absolute
 * path, a drive letter. Windows' backslashes are slashes; "." parts go. */
static bool sheet_join(const char *dir, const char *name, char *out, size_t n)
{
	char tmp[LIB_PATH * 2], *p, *save = NULL;
	size_t o = 0;

	if (!name[0] || name[0] == '/' || name[0] == '\\' ||
	    (((name[0] | 0x20) >= 'a' && (name[0] | 0x20) <= 'z') && name[1] == ':'))
		return false;
	if (snprintf(tmp, sizeof tmp, "%s%s%s", dir, dir[0] ? "/" : "", name) >= (int)sizeof tmp)
		return false;
	for (p = tmp; *p; p++)
		if (*p == '\\') *p = '/';
	out[0] = '\0';
	for (p = strtok_r(tmp, "/", &save); p; p = strtok_r(NULL, "/", &save)) {
		int w;

		if (!strcmp(p, ".")) continue;
		if (!strcmp(p, "..")) return false;
		w = snprintf(out + o, n - o, "%s%s", o ? "/" : "", p);
		if (w < 0 || (size_t)w >= n - o) return false;
		o += (size_t)w;
	}
	return o > 0;
}

/* ---- sheets ---------------------------------------------------------------
 *
 * A .cue, .gdi or .toc names its track files and an .m3u its discs; the
 * names are the game's files too. Read only for a game that is a sheet at
 * the top of its console's folder: a game in a folder of its own goes with
 * its folder. */
#define SHEET_MAX   (64 * 1024)
#define SHEET_NAMES 128

typedef struct {
	char name[SHEET_NAMES][LIB_PATH];
	int  n;
	bool outside;              /* a name that leaves the folder */
} sheet_names;

static bool is_sheet(const char *name)
{
	return ext_is(name, "cue") || ext_is(name, "gdi") || ext_is(name, "toc") ||
	       ext_is(name, "m3u");
}

static void sheet_add(sheet_names *s, const char *dir, const char *raw)
{
	char rel[LIB_PATH];
	int i;

	if (!sheet_join(dir, raw, rel, sizeof rel)) { s->outside = true; return; }
	for (i = 0; i < s->n; i++)
		if (!strcasecmp(s->name[i], rel)) return;
	if (s->n < SHEET_NAMES) snprintf(s->name[s->n++], LIB_PATH, "%s", rel);
}

/* The quoted string at `p`, or the run up to whitespace */
static const char *token(const char *p, char *out, size_t n)
{
	size_t o = 0;

	while (*p == ' ' || *p == '\t') p++;
	if (*p == '"') {
		for (p++; *p && *p != '"'; p++)
			if (o + 1 < n) out[o++] = *p;
		if (*p == '"') p++;
	} else {
		for (; *p && *p != ' ' && *p != '\t'; p++)
			if (o + 1 < n) out[o++] = *p;
	}
	out[o] = '\0';
	return p;
}

/* What the sheet at `rel` (relative to the console's folder `root`) names,
 * into `s`; a .m3u's sheets are read for their tracks too, one level down */
static void sheet_read(const char *root, const char *rel, sheet_names *s, int depth)
{
	char path[GD_PATH], dir[LIB_PATH], word[LIB_PATH];
	char *buf, *line, *save = NULL;
	const char *slash = strrchr(rel, '/');
	size_t got;
	FILE *f;
	bool m3u = ext_is(rel, "m3u"), gdi = ext_is(rel, "gdi");
	int first = 1;

	snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - rel) : 0, rel);
	if (snprintf(path, sizeof path, "%s/%s", root, rel) >= (int)sizeof path) return;
	if (!(f = fopen(path, "rb"))) return;
	buf = malloc(SHEET_MAX + 1);
	if (!buf) { fclose(f); return; }
	got = fread(buf, 1, SHEET_MAX, f);
	fclose(f);
	buf[got] = '\0';
	for (line = strtok_r(buf, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
		const char *p = line;
		size_t len;

		while (*p == ' ' || *p == '\t') p++;
		/* A UTF-8 byte order mark, which Windows' editors put first */
		if (first && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
		    (unsigned char)p[2] == 0xBF)
			p += 3;
		first = 0;
		if (!*p) continue;
		if (m3u) {
			if (*p == '#') continue;
			snprintf(word, sizeof word, "%s", p);
			for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
				word[len - 1] = '\0';
			if (word[0]) sheet_add(s, dir, word);
			continue;
		}
		if (gdi) {
			/* "1 0 4 2352 track01.bin 0": the fifth field. The first line
			 * is the count of tracks, one field. */
			int k;

			for (k = 0; k < 5 && *p; k++) {
				p = token(p, word, sizeof word);
				while (*p == ' ' || *p == '\t') p++;
			}
			if (k == 5 && word[0]) sheet_add(s, dir, word);
			continue;
		}
		/* .cue and .toc: FILE "name" TYPE, and a .toc's DATAFILE and
		 * AUDIOFILE */
		if (!strncasecmp(p, "FILE", 4) && (p[4] == ' ' || p[4] == '\t'))
			p += 4;
		else if (!strncasecmp(p, "DATAFILE", 8) && (p[8] == ' ' || p[8] == '\t'))
			p += 8;
		else if (!strncasecmp(p, "AUDIOFILE", 9) && (p[9] == ' ' || p[9] == '\t'))
			p += 9;
		else continue;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '"') {
			token(p, word, sizeof word);
		} else {
			/* Unquoted, which a hand or an old tool writes even around
			 * spaces: the rest of the line less its last word, the type
			 * (BINARY, WAVE), when there is more than one */
			char *sp;

			snprintf(word, sizeof word, "%s", p);
			for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
				word[len - 1] = '\0';
			if ((sp = strrchr(word, ' ')) || (sp = strrchr(word, '\t'))) {
				*sp = '\0';
				for (len = strlen(word); len && (word[len - 1] == ' ' || word[len - 1] == '\t'); len--)
					word[len - 1] = '\0';
			}
		}
		if (word[0]) sheet_add(s, dir, word);
	}
	free(buf);

	/* An .m3u's discs that are sheets themselves, for their tracks */
	if (m3u && depth == 0) {
		int i, n = s->n;

		for (i = 0; i < n; i++)
			if (is_sheet(s->name[i]) && !ext_is(s->name[i], "m3u"))
				sheet_read(root, s->name[i], s, depth + 1);
	}
}

/* ---- the plan ------------------------------------------------------------ */

static bool plan_add(gd_plan *p, gd_kind kind, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

static bool plan_add(gd_plan *p, gd_kind kind, const char *fmt, ...)
{
	va_list ap;
	int w, i;

	if (p->why != GD_OK) return false;
	if (p->n >= GD_MAX) { p->why = GD_TOO_MANY; return false; }
	va_start(ap, fmt);
	w = vsnprintf(p->path[p->n], GD_PATH, fmt, ap);
	va_end(ap);
	if (w < 0 || w >= GD_PATH) {
		snprintf(p->bad, sizeof p->bad, "%.*s", GD_PATH - 1, p->path[p->n]);
		p->why = GD_TOO_MANY;
		return false;
	}
	for (i = 0; i < p->n; i++)
		if (!strcmp(p->path[i], p->path[p->n])) return true;      /* once */
	p->kind[p->n++] = (unsigned char)kind;
	return true;
}

static void outside(gd_plan *p, const char *what)
{
	if (p->why != GD_OK) return;
	p->why = GD_OUTSIDE;
	snprintf(p->bad, sizeof p->bad, "%s", what);
}

/* What the states folder calls a game: its launch file's base, or on a
 * hooked shelf its folder's name, and a PSP game's folder name for its
 * EBOOT.PBP (persist_base in main.c) */
static const char *state_base(const char *file, const char *name, bool hooked)
{
	const char *base = base_of(file);

	return base != file && (hooked || !strcasecmp(base, "EBOOT.PBP")) ? name : base;
}

const char *gd_state_base(const char *file, const char *name, bool hooked)
{
	return state_base(file, name, hooked);
}

/* The other games, asked what they share with this one */
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
		    !strcasecmp(state_base(o[i].file, o[i].name, o[i].hooked), base))
			return true;
	return false;
}

/* A file in the console's folder that is a game on the shelf itself */
static bool shares_file(const gd_other *o, int n, const char *folder, const char *rel)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(o[i].folder, folder) && !strcasecmp(o[i].file, rel)) return true;
	return false;
}

/* A file of the game's in its console's folder, unless another game has it:
 * as a game of its own, or named by its sheet too (`theirs`) */
static void add_rom(gd_plan *p, const char *roms, const gd_game *g, const char *rel,
                    const gd_other *o, int n, bool theirs)
{
	if (theirs || shares_file(o, n, g->folder, rel)) { p->kept++; return; }
	plan_add(p, GD_FILE, "%s/%s/%s", roms, g->folder, rel);
}

/* Is `path` inside `root`? Lexically, and then where the links really go:
 * the deepest part of it that exists, resolved, under the root resolved. A
 * path whose parent is not there is nothing to delete, and says so. */
typedef enum { IN_YES, IN_NOTHING, IN_NO } in_root;

static in_root inside(const char *root, const char *path, bool tree)
{
	char rr[PATH_MAX], pr[PATH_MAX], parent[GD_PATH];
	size_t n = strlen(root), rn;
	const char *rel = path + n, *slash;

	if (strncmp(path, root, n) || *rel != '/') return IN_NO;
	for (rel++; *rel; rel = slash + 1) {
		size_t len;

		slash = strchr(rel, '/');
		len = slash ? (size_t)(slash - rel) : strlen(rel);
		if (len == 0 || (len == 1 && rel[0] == '.') || (len == 2 && !strncmp(rel, "..", 2)))
			return IN_NO;
		if (!slash) break;
	}
	if (!realpath(root, rr)) return IN_NOTHING;     /* no root, nothing under it */
	rn = strlen(rr);
	if (tree) {
		struct stat st;

		if (lstat(path, &st) != 0) return IN_NOTHING;
		if (!S_ISDIR(st.st_mode)) return IN_NO;       /* a link where its folder was */
		if (!realpath(path, pr)) return IN_NOTHING;
		return !strncmp(pr, rr, rn) && pr[rn] == '/' ? IN_YES : IN_NO;
	}
	snprintf(parent, sizeof parent, "%.*s", (int)(strrchr(path, '/') - path), path);
	if (!realpath(parent, pr)) return IN_NOTHING;
	return !strncmp(pr, rr, rn) && (pr[rn] == '/' || pr[rn] == '\0') ? IN_YES : IN_NO;
}

static bool under(const char *path, const char *root)
{
	size_t n = strlen(root);

	return !strncmp(path, root, n) && path[n] == '/';
}

/* Every path held to its root - the console's folder in Roms/, Saves/, or
 * the states - and the ones with nothing behind them dropped */
static void plan_hold(gd_plan *p, const gd_card *card, const char *romdir)
{
	int i, k = 0;

	for (i = 0; i < p->n && p->why == GD_OK; i++) {
		const char *root = under(p->path[i], romdir) ? romdir
		                 : under(p->path[i], card->states) ? card->states
		                 : under(p->path[i], card->saves) ? card->saves : NULL;
		in_root r = root ? inside(root, p->path[i], p->kind[i] == GD_TREE) : IN_NO;

		if (r == IN_NO) { outside(p, p->path[i]); break; }
		/* The game's own file stays first even when it is not there any
		 * more: a game already taken off the card by File Transfer still
		 * has its saves and media to go */
		if (r == IN_NOTHING && i > 0) continue;
		if (k != i) {
			memcpy(p->path[k], p->path[i], sizeof p->path[k]);
			p->kind[k] = p->kind[i];
		}
		k++;
	}
	if (p->why == GD_OK) p->n = k;
}

static void plan_hold(gd_plan *p, const gd_card *card, const char *romdir);
static bool shares_name(const gd_other *o, int n, const char *folder, const char *name);

/* A port's plan: its script, the folders its port.json or its GAMEDIR names
 * (src/portinfo.h) that no other port on the shelf names too, and its cover
 * and snap. PortMaster's is its script and its folder, made for its size
 * and refused (GD_RUNTIME); a port nothing names a folder for has its
 * script alone, and is refused (GD_PORT_UNKNOWN). A port keeps its saves in
 * its own folders, so nothing of Saves/ or the states is its. */
static bool port_plan(const gd_card *card, const gd_game *g, const gd_other *others,
                      int nothers, gd_plan *p, const char *romdir)
{
	static const char *const MEDIA[] = { "png", "mp4", "png.part", "mp4.part", "mp4.snap",
	                                     "mp4.snap.part", "png.sgdb", "png.tmp", "png.pick",
	                                     "png.swap" };
	pi_port *ports = malloc(PI_MAX * sizeof *ports);
	char (*mine)[PI_NAME] = malloc(PI_ITEMS * sizeof *mine);
	char (*theirs)[PI_NAME] = malloc(PI_ITEMS * sizeof *theirs);
	bool runtime = pi_runtime(g->file);
	int np, nm = 0, k, j, t;
	size_t i;

	if (!ports || !mine || !theirs) {
		free(ports); free(mine); free(theirs);
		p->why = GD_TOO_MANY;
		return false;
	}
	np = pi_scan(romdir, ports, PI_MAX);
	plan_add(p, GD_FILE, "%s/%s", romdir, g->file);
	if (runtime) {
		snprintf(mine[0], PI_NAME, "PortMaster");
		nm = 1;
	} else {
		nm = pi_folders(romdir, ports, np, g->file, mine, PI_ITEMS);
	}
	for (k = 0; k < nm; k++) {
		bool shared = false;

		for (j = 0; j < nothers && !shared; j++) {
			const char *slash = strchr(others[j].file, '/');
			int nt;

			if (strcmp(others[j].folder, g->folder)) continue;
			/* A folder the shelf lists as a game of its own is that game's */
			if (slash) {
				if (strlen(mine[k]) == (size_t)(slash - others[j].file) &&
				    !strncasecmp(others[j].file, mine[k], (size_t)(slash - others[j].file)))
					shared = true;
				continue;
			}
			nt = pi_folders(romdir, ports, np, others[j].file, theirs, PI_ITEMS);
			for (t = 0; t < nt; t++)
				if (!strcasecmp(theirs[t], mine[k])) shared = true;
		}
		if (shared) { p->kept++; continue; }
		plan_add(p, GD_TREE, "%s/%s", romdir, mine[k]);
	}
	free(ports);
	free(mine);
	free(theirs);
	if (shares_name(others, nothers, g->folder, g->name)) {
		p->kept++;
	} else {
		for (i = 0; i < sizeof MEDIA / sizeof MEDIA[0]; i++)
			plan_add(p, GD_FILE, "%s/.media/%s.%s", romdir, g->name, MEDIA[i]);
		plan_add(p, GD_FILE, "%s/.media/.before/%s.png", romdir, g->name);
	}
	plan_add(p, GD_EMPTY_DIR, "%s/.media/.before", romdir);
	plan_add(p, GD_EMPTY_DIR, "%s/.media", romdir);
	if (p->why != GD_OK) return false;
	plan_hold(p, card, romdir);
	if (p->why != GD_OK) return false;
	if (runtime || nm == 0) {
		p->why = runtime ? GD_RUNTIME : GD_PORT_UNKNOWN;
		snprintf(p->bad, sizeof p->bad, "%s", g->file);
		return false;
	}
	return true;
}

bool gd_plan_make(const gd_card *card, const gd_game *g,
                  const gd_other *others, int nothers, gd_plan *p)
{
	static sheet_names mine, theirs;
	static bool shared[SHEET_NAMES];
	static const char *const SLOTS[] = { "auto", "1", "2", "3", "4", "5", "6" };
	static const char *const MEDIA[] = { "png", "mp4", "png.part", "mp4.part", "mp4.snap",
	                                     "mp4.snap.part", "png.sgdb", "png.tmp", "png.pick",
	                                     "png.swap" };
	char romdir[GD_PATH], stem[LIB_NAME], sbase[LIB_PATH];
	const char *slash, *core = g->core ? g->core : "";
	bool hooked = lib_shelf_hooked(g->exts), arcade_set;
	size_t i;

	memset(p, 0, sizeof *p);
	p->why = GD_OK;
	if (!card || !card->roms || !card->saves || !card->states ||
	    !name_ok(g->folder) || !g->tag || !file_ok(g->file, g->dir) || !name_ok(g->name)) {
		outside(p, g->file ? g->file : "");
		return false;
	}
	if (gd_is_bios(g->file)) {
		p->why = GD_BIOS;
		snprintf(p->bad, sizeof p->bad, "%s", g->file);
		return false;
	}
	snprintf(romdir, sizeof romdir, "%s/%s", card->roms, g->folder);
	/* A port is a script at the top; a folder the shelf lists as a game of
	 * its own (a disc image inside it, lib_scan_listing's second pass) goes
	 * whole, as on any shelf */
	if (!strcmp(core, "port") && !strchr(g->file, '/'))
		return port_plan(card, g, others, nothers, p, romdir);
	/* The game's own folder, if it is one: past the folders of games it is
	 * kept in, which are no game's and never go with it */
	slash = strchr(g->file + (g->dir ? g->dir + 1 : 0), '/');
	stem_of(g->file, stem, sizeof stem);
	snprintf(sbase, sizeof sbase, "%s", state_base(g->file, g->name, hooked));
	arcade_set = !slash && (ext_is(g->file, "zip") || ext_is(g->file, "7z"));

	/* 1. The game. A folder of its own goes whole; a file goes with what its
	 * sheet names and the files its format keeps beside it. */
	if (slash) {
		plan_add(p, GD_TREE, "%s/%.*s", romdir, (int)(slash - g->file), g->file);
	} else {
		plan_add(p, GD_FILE, "%s/%s", romdir, g->file);
		mine.n = 0;
		mine.outside = false;
		memset(shared, 0, sizeof shared);
		if (is_sheet(g->file)) {
			sheet_read(romdir, g->file, &mine, 0);
			if (mine.outside) { outside(p, g->file); return false; }
			/* What each other sheet at the top of this folder names, so a
			 * track two sheets share stays with the one that is left. One
			 * sheet at a time, held against this one's names. */
			for (int k = 0; k < nothers && mine.n; k++) {
				const char *of = others[k].file;

				/* Loose in the same folder of games: no folder of its own */
				if (strcmp(others[k].folder, g->folder) || others[k].dir != g->dir ||
				    strncmp(of, g->file, g->dir) || strchr(of + (g->dir ? g->dir + 1 : 0), '/') ||
				    !is_sheet(of))
					continue;
				theirs.n = 0;
				theirs.outside = false;
				sheet_read(romdir, others[k].file, &theirs, 0);
				for (int t = 0; t < theirs.n; t++)
					for (int m = 0; m < mine.n; m++)
						if (!strcasecmp(theirs.name[t], mine.name[m])) shared[m] = true;
			}
		}
		for (int k = 0; k < mine.n; k++) {
			if (!strcasecmp(mine.name[k], g->file)) continue;
			add_rom(p, card->roms, g, mine.name[k], others, nothers, shared[k]);
		}
		/* Beside their sheet, by its stem rather than named in it: a .ccd's
		 * image and subchannel, an .mds's data and the other way round, and
		 * the .sbi PCSX ReARMed reads beside a .cue */
		{
			static const struct { const char *ext, *with[2]; } SIDE[] = {
				{ "ccd", { "img", "sub" } }, { "mds", { "mdf", NULL } },
				{ "mdf", { "mds", NULL } },  { "cue", { "sbi", NULL } },
			};
			char rel[LIB_PATH];

			for (i = 0; i < sizeof SIDE / sizeof SIDE[0]; i++) {
				if (!ext_is(g->file, SIDE[i].ext)) continue;
				for (int k = 0; k < 2 && SIDE[i].with[k]; k++) {
					/* Beside it, in the folder of games it is in */
					snprintf(rel, sizeof rel, "%.*s%s%s.%s", (int)g->dir, g->file,
					         g->dir ? "/" : "", stem, SIDE[i].with[k]);
					add_rom(p, card->roms, g, rel, others, nothers, false);
				}
			}
		}
		/* A NAOMI or Atomiswave GD-ROM game: its set and a folder of the
		 * set's name holding the CHD, where Flycast looks; the shelf hides
		 * the folder behind the set (tools/from-rogue.sh, "keep") */
		if (arcade_set && !strncmp(core, "flycast", 7)) {
			struct stat st;
			char dir[GD_PATH];

			snprintf(dir, sizeof dir, "%s/%.*s%s%s", romdir, (int)g->dir, g->file,
			         g->dir ? "/" : "", stem);
			if (lstat(dir, &st) == 0 && S_ISDIR(st.st_mode) &&
			    !shares_name(others, nothers, g->folder, stem))
				plan_add(p, GD_TREE, "%s", dir);
		}
	}
	if (p->why != GD_OK) return false;

	/* 2. Its saves, flat in Saves/ by the ROM's stem (diatom/src/save.c):
	 * the .srm Diatom keeps, a .sav, and DraStic's .dsv; and the ones the
	 * cores keep in folders of their own, by the set's name */
	if (shares_stem(others, nothers, stem)) {
		p->kept++;
	} else {
		static const char *const FLAT[] = { "srm", "sav", "dsv" };

		for (i = 0; i < sizeof FLAT / sizeof FLAT[0]; i++)
			plan_add(p, GD_FILE, "%s/%s.%s", card->saves, stem, FLAT[i]);
		if (!strcmp(core, "fbneo")) {
			static const char *const FB[] = { "nv", "hi", "fs", "memcard" };

			for (i = 0; i < sizeof FB / sizeof FB[0]; i++)
				plan_add(p, GD_FILE, "%s/fbneo/%s.%s", card->saves, stem, FB[i]);
		}
		if (!strcmp(core, "mame2003_plus")) {
			static const char *const MAME[][2] = {
				{ "nvram", "nv" }, { "hi", "hi" }, { "cfg", "cfg" }, { "diff", "dif" }
			};

			for (i = 0; i < sizeof MAME / sizeof MAME[0]; i++)
				plan_add(p, GD_FILE, "%s/mame2003-plus/%s/%s.%s", card->saves,
				         MAME[i][0], stem, MAME[i][1]);
		}
		if (arcade_set && !strncmp(core, "flycast", 7)) {
			static const char *const NV[] = { "eeprom", "nvmem", "nvmem2" };

			for (i = 0; i < sizeof NV / sizeof NV[0]; i++)
				plan_add(p, GD_FILE, "%s/reicast/%s.%s", card->saves, g->file, NV[i]);
		}
		/* OpenBOR's, in Saves/openbor where its script starts it
		 * (sd/pixel2/OpenBOR.sh), under its own Saves/, by the module's name */
		if (ext_is(g->file, "pak") && strstr(core, "OpenBOR")) {
			static const char *const OB[] = { "sav", "hi", "scr", "cfg", "inp", "controls" };

			for (i = 0; i < sizeof OB / sizeof OB[0]; i++)
				plan_add(p, GD_FILE, "%s/openbor/Saves/%s.%s", card->saves, stem, OB[i]);
		}
	}

	/* 3. Its states and their pictures, the six slots and Auto (slot_state_path
	 * in main.c), and the console's states folder if that leaves it empty */
	if (shares_state(others, nothers, g->folder, sbase)) {
		p->kept++;
	} else {
		for (i = 0; i < sizeof SLOTS / sizeof SLOTS[0]; i++) {
			plan_add(p, GD_FILE, "%s/%s/%s.%s.state", card->states, g->folder, sbase, SLOTS[i]);
			plan_add(p, GD_FILE, "%s/%s/%s.%s.bmp", card->states, g->folder, sbase, SLOTS[i]);
		}
		/* and the language a DS game's Auto was left in (src/dsresume.h) */
		plan_add(p, GD_FILE, "%s/%s/%s.auto.lang", card->states, g->folder, sbase);
	}

	/* 4. Its cover and video snap, and what a fetch stopped halfway leaves
	 * (src/net.c's .part, ssrun.c's .snap, sgdbrun.c's .sgdb, artshrink.c's
	 * .tmp); and 5. its cached achievement set. By the name, which a .cue and
	 * a .chd of one name share. */
	if (shares_name(others, nothers, g->folder, g->name)) {
		p->kept++;
	} else {
		for (i = 0; i < sizeof MEDIA / sizeof MEDIA[0]; i++)
			plan_add(p, GD_FILE, "%s/.media/%s.%s", romdir, g->name, MEDIA[i]);
		/* and the cover Cover and Video put aside, its Before (DESIGN.md
		 * 11.34) */
		plan_add(p, GD_FILE, "%s/.media/.before/%s.png", romdir, g->name);
		plan_add(p, GD_FILE, "%s/.cheevos/%s.set", romdir, g->name);
		plan_add(p, GD_FILE, "%s/.cheevos/%s.set.part", romdir, g->name);
	}
	plan_add(p, GD_EMPTY_DIR, "%s/.media/.before", romdir);
	plan_add(p, GD_EMPTY_DIR, "%s/.media", romdir);
	plan_add(p, GD_EMPTY_DIR, "%s/.cheevos", romdir);
	plan_add(p, GD_EMPTY_DIR, "%s/%s", card->states, g->folder);
	if (p->why != GD_OK) return false;

	plan_hold(p, card, romdir);
	return p->why == GD_OK;
}

/* ---- running it ------------------------------------------------------------
 *
 * lstat, so a link is removed and never followed (the card has none; a host
 * can). Deep enough for any disc set or ScummVM game. */
#define TREE_DEPTH 12

static bool tree_delete(const char *dir, int depth, gd_done *d)
{
	DIR *dh;
	struct dirent *e;
	bool ok = true;

	if (depth > TREE_DEPTH || !(dh = opendir(dir))) return false;
	while ((e = readdir(dh))) {
		char p[GD_PATH];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) { ok = false; continue; }
		if (lstat(p, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			ok = tree_delete(p, depth + 1, d) && ok;
		} else if (unlink(p) == 0) {
			d->files++;
			d->bytes += (unsigned long long)st.st_size;
		} else {
			ok = false;
		}
	}
	closedir(dh);
	if (rmdir(dir) != 0) return false;
	d->folders++;
	return ok;
}

/* One path: GD_WENT, GD_NOT_THERE, or GD_STAYED when it would not go */
static int run_one(const char *path, gd_kind kind, gd_done *d)
{
	struct stat st;

	if (lstat(path, &st) != 0) return errno == ENOENT ? GD_NOT_THERE : GD_STAYED;
	switch (kind) {
	case GD_TREE:
		return S_ISDIR(st.st_mode) && tree_delete(path, 0, d) ? GD_WENT : GD_STAYED;
	case GD_EMPTY_DIR:
		/* One with something left in it stays, as it should */
		if (S_ISDIR(st.st_mode) && rmdir(path) == 0) { d->folders++; return GD_WENT; }
		return GD_NOT_THERE;
	default:
		if (S_ISDIR(st.st_mode) || unlink(path) != 0) return GD_STAYED;
		d->files++;
		d->bytes += (unsigned long long)st.st_size;
		return GD_WENT;
	}
}

void gd_plan_run(const gd_plan *p, gd_done *out)
{
	int i;

	memset(out, 0, sizeof *out);
	if (p->why != GD_OK || p->n <= 0) return;
	/* The game first; its other files from the plan's next entries that are
	 * still in its console's folder (a sheet's tracks, the set's CHD) */
	out->went[0] = (unsigned char)run_one(p->path[0], (gd_kind)p->kind[0], out);
	out->game_gone = out->went[0] != GD_STAYED;
	if (!out->game_gone) { out->failed = 1; return; }
	for (i = 1; i < p->n; i++)
		if ((out->went[i] = (unsigned char)run_one(p->path[i], (gd_kind)p->kind[i], out)) == GD_STAYED)
			out->failed++;
}

int gd_forget(const char *folder, const char *tag, const char *file)
{
	int n = 0;

	if (db_game_del(db_lib(), folder, file)) n++;
	if (fav_forget(tag, file)) n++;
	n += stats_forget(tag, file);
	return n;
}

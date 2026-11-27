/* Loading a core, and its lifecycle.
 *
 * Every core is dlopen'd once and NEVER unloaded, but for the ones that run a
 * single game per load (one_game_per_load below), and every core whose file
 * ZyperOS did not ship (core_is_shipped below), which are closed after each
 * game and opened again for the next, as RetroArch does with every core.
 * RTLD_LOCAL always. It is the only thing preventing symbol collision between
 * resident cores - measured, picodrive exports 1069 non-retro_* symbols
 * including a complete static zlib. RTLD_GLOBAL would let its crc32 answer for
 * everyone, silently.
 */
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cheevos.h"
#include "corefail.h"
#include "diatom.h"
#include "sgb.h"

static bool bind_sym(void *h, void *slot, const char *name)
{
	void *p = dlsym(h, name);
	if (!p) {
		fprintf(stderr, "diatom: core is missing %s\n", name);
		return false;
	}
	memcpy(slot, &p, sizeof p);
	return true;
}

#define BIND(field, name) \
	if (!bind_sym(c->handle, &c->field, name)) return false

/* Why the last core that would not load would not (corefail.h), for the
 * launcher's ERROR line: the path it was asked by and the reason. Written
 * with the registry's lock held, since a core is opened only under it, and
 * read under it (diatom_core_failure). One is enough: a RUN asks about the
 * core it just asked for, and a core that fails at the premap fails again,
 * and is described again, when a RUN names it. */
static corefail g_fail;
static char     g_fail_path[1024];

static void note_failure(const char *path, corefail_why why, const char *name, const char *version)
{
	memset(&g_fail, 0, sizeof g_fail);
	g_fail.why = why;
	if (name) snprintf(g_fail.name, sizeof g_fail.name, "%s", name);
	if (version) snprintf(g_fail.version, sizeof g_fail.version, "%s", version);
	snprintf(g_fail_path, sizeof g_fail_path, "%s", path ? path : "");
}

/* What a core costs to open, logged rather than remembered.
 *
 * Timed separately because they are separate costs and only one of them is
 * large: measured on the device 2026-09-08, a COLD dlopen runs 10-179 ms
 * depending on the core - genesis_plus_gx is the big one - against 0.8-26 ms
 * warm, while retro_init is under 3 ms for five of the six. The README's
 * "~170 ms" was right and never said it was cold, which is how it read as
 * wrong for a year.
 *
 * Kept because these are the numbers that decide whether premapping is worth
 * doing, and they will drift when a core is repinned. */
static double dbg_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static const char *dbg_base(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

/* Is this core's file one ZyperOS shipped?
 *
 * ZyperOS/.files is the answer: the card's index of every file the folder
 * shipped, "<sha256> <size> <path>" a line (mk/payload.sh writes it, System
 * Update writes it again for every update; docs/updates.md). A core is shipped
 * when the index names it, by its path from the ZyperOS folder, at the size it
 * has on the card. One that is not named is one a person added (freeintv, #304).
 * One that is named at another size is a shipped core's file with another
 * build put over it, and it is not what ZyperOS shipped either.
 *
 * The size, not the hash: a hash is the whole file read through, which on the
 * devices is more than a core costs to map, and this is asked of every core at
 * every boot. A build that comes to the very same size is taken for the
 * shipped one, which is the behaviour before this existed, not a new risk.
 *
 * The folder is the core's folder's parent: a core at <ZyperOS>/cores/x.so is
 * "cores/x.so" in <ZyperOS>/.files. Where there is no index to read (a card
 * from before System Update, a desktop build), or one with no entry in it, there
 * is nothing to say a core was not shipped, and it is taken to be: the cores
 * stay open between games as they always did. */
static bool core_is_shipped(const char *path)
{
	const char *file = strrchr(path, '/'), *dir, *rel;
	char idx[1100], line[1300];
	unsigned long long size = 0;
	bool listed = false, any = false;
	struct stat st;
	FILE *f;

	if (!file || file == path) return true;
	for (dir = file - 1; dir > path && *dir != '/'; dir--)
		;
	if (*dir != '/') return true;
	rel = dir + 1;
	snprintf(idx, sizeof idx, "%.*s/.files", (int)(dir - path), path);

	f = fopen(idx, "r");
	if (!f) return true;
	while (fgets(line, sizeof line, f)) {
		char *sp1 = strchr(line, ' '), *sp2;

		line[strcspn(line, "\n")] = '\0';
		if (!sp1 || sp1 - line != 64) continue;
		sp2 = strchr(sp1 + 1, ' ');
		if (!sp2) continue;
		any = true;
		if (!strcmp(sp2 + 1, rel)) {
			size = strtoull(sp1 + 1, NULL, 10);
			listed = true;
			break;
		}
	}
	fclose(f);

	/* An index with no line of the form in it is no index */
	if (!any) return true;
	if (!listed) return false;
	if (stat(path, &st) != 0) return true;
	return (unsigned long long)st.st_size == size;
}

static bool bind_core(diatom_core *c)
{
	BIND(set_environment,            "retro_set_environment");
	BIND(set_video_refresh,          "retro_set_video_refresh");
	BIND(set_audio_sample,           "retro_set_audio_sample");
	BIND(set_audio_sample_batch,     "retro_set_audio_sample_batch");
	BIND(set_input_poll,             "retro_set_input_poll");
	BIND(set_input_state,            "retro_set_input_state");
	BIND(set_controller_port_device, "retro_set_controller_port_device");
	BIND(init,                       "retro_init");
	BIND(deinit,                     "retro_deinit");
	BIND(get_system_info,            "retro_get_system_info");
	BIND(get_system_av_info,         "retro_get_system_av_info");
	BIND(load_game,                  "retro_load_game");
	BIND(unload_game,                "retro_unload_game");
	BIND(run,                        "retro_run");
	BIND(reset,                      "retro_reset");
	BIND(serialize_size,             "retro_serialize_size");
	BIND(serialize,                  "retro_serialize");
	BIND(unserialize,                "retro_unserialize");
	BIND(get_memory_data,            "retro_get_memory_data");
	BIND(get_memory_size,            "retro_get_memory_size");

	/* Every core exports these, but nothing is lost when one does not: its
	 * games run, and the cheats it is handed go nowhere (cheats.c) */
	{
		void *r = dlsym(c->handle, "retro_cheat_reset");
		void *s = dlsym(c->handle, "retro_cheat_set");

		if (r && s) {
			memcpy(&c->cheat_reset, &r, sizeof r);
			memcpy(&c->cheat_set, &s, sizeof s);
		}
	}

	return true;
}

bool diatom_core_open(diatom_core *c, const char *path)
{
	double t0;

	memset(c, 0, sizeof *c);
	c->path = path;
	c->shipped = core_is_shipped(path);

	t0 = dbg_now_ms();
	c->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	fprintf(stderr, "diatom: dlopen %-28s %6.1f ms\n",
	        dbg_base(path), dbg_now_ms() - t0);
	if (!c->handle) {
		/* The whole text stays in the log; the launcher is sent what it
		 * says in a few words (corefail.h) */
		const char *err = dlerror();
		corefail why;

		fprintf(stderr, "diatom: dlopen %s: %s\n", path, err ? err : "(no reason given)");
		corefail_classify(&why, path, err, corefail_machine());
		note_failure(path, why.why, why.name, why.version);
		fprintf(stderr, "diatom: core load failed: %s%s%s%s%s\n", corefail_word(why.why),
		        why.name[0] ? " " : "", why.name, why.version[0] ? " " : "", why.version);
		return false;
	}

	if (!bind_core(c)) {
		/* It loads and is not a libretro core: no retro_run, or the like */
		note_failure(path, COREFAIL_OTHER, NULL, NULL);
		return false;
	}

	return true;
}

/* Resident cores.
 *
 * Diatom never calls dlclose, so a core opened once is opened forever. This
 * registry is what makes that true across games: the second RUN naming the same
 * core skips dlopen and retro_init entirely, which measured 6 ms and 43 ms
 * respectively for FCEUmm and is most of what a warm launch saves.
 *
 * Bounded rather than dynamic because the bound is the point: six cores mapped
 * plus one running measured 15.0 MB against 975 MB of RAM, and a registry that
 * grows without limit would quietly turn a measured decision into an unmeasured
 * one.
 *
 * Locked, because the premap fills it on a thread of its own while a RUN may
 * already be asking for a core on the main thread. The lock is held across the
 * dlopen, so a RUN for the core being mapped waits for that one map and then
 * finds it, rather than opening it a second time. Entries are never moved or
 * removed, so a pointer handed out stays good after the lock is let go.
 */
/* Sixteen since 2026-11-03. Eight held the six console cores with room to
 * spare, and the four small systems ZyperOS added that day (Atari 2600, Atari
 * 7800, Game & Watch, arcade) made ten. The premap fills the registry in
 * readdir order, and in a container on 2026-11-03 the eight it held left out
 * mgba and genesis_plus_gx: "core registry full" twice at boot, so every
 * GBA, Game Boy and Genesis game would be refused at RUN - systems nobody
 * had touched, broken by four that were added. The bound is still the
 * point: a slot costs what its core costs mapped, which is what the RAM
 * figure has to account for, not the slot.
 *
 * Sixteen was exact by 2026-11-04 (a Pixel 2 card premapped 16), the day
 * ROGUE OS's other systems brought ten more cores: 26, and Nintendo 64, MAME
 * and the rest refused at RUN. Thirty-two left room again, until 1.0.5's
 * PSP, Doom, ColecoVision and TIC-80 made 34 (2026-11-12): the Pixel 2 card
 * premapped 32 and refused the last two in folder order, vecx and
 * yabasanshiro, so every Vectrex and Saturn game started in the launcher's
 * one-off Diatom instead. Sixty-four, and tools/core-room-check.sh (make
 * check) and mk/payload.sh now fail a build whose cores outnumber it, so the
 * next shelf that crosses it is caught before a card has it. An empty slot
 * costs its path, 1 kB, and options.c's table for it, untouched BSS until a
 * core uses it. The number is DIATOM_MAX_RESIDENT, in diatom.h, which
 * options.c sizes its tables by. */
#define MAX_RESIDENT DIATOM_MAX_RESIDENT
static diatom_core      g_resident[MAX_RESIDENT];
static char             g_resident_path[MAX_RESIDENT][1024];
static int              g_nresident;
static pthread_mutex_t  g_resident_lock = PTHREAD_MUTEX_INITIALIZER;

static diatom_core *resident_locked(const char *path)
{
	int i;

	for (i = 0; i < g_nresident; i++)
		if (!strcmp(g_resident_path[i], path)) {
			/* Closed after its last game (diatom_core_stop): opened again
			 * into the same slot, so a pointer handed out stays good. */
			if (!g_resident[i].handle) {
				if (!diatom_core_open(&g_resident[i], g_resident_path[i]))
					return NULL;
				g_resident[i].path = g_resident_path[i];
			}
			return &g_resident[i];
		}

	if (g_nresident >= MAX_RESIDENT) {
		fprintf(stderr, "diatom: core registry full (%d)\n", MAX_RESIDENT);
		return NULL;
	}
	if (!diatom_core_open(&g_resident[g_nresident], path)) return NULL;

	/* The caller's path buffer is reused between messages, so own a copy. */
	snprintf(g_resident_path[g_nresident], sizeof g_resident_path[0], "%s", path);
	g_resident[g_nresident].path = g_resident_path[g_nresident];
	return &g_resident[g_nresident++];
}

bool diatom_core_failure(const char *path, corefail *out)
{
	bool is;

	pthread_mutex_lock(&g_resident_lock);
	is = path && !strcmp(path, g_fail_path) && g_fail_path[0];
	if (is) *out = g_fail;
	pthread_mutex_unlock(&g_resident_lock);
	return is;
}

diatom_core *diatom_core_resident(const char *path)
{
	diatom_core *c;

	if (!path || !*path) return NULL;
	pthread_mutex_lock(&g_resident_lock);
	c = resident_locked(path);
	pthread_mutex_unlock(&g_resident_lock);
	return c;
}

/* Map every core in `dir` before anyone asks for one.
 *
 * launch.sh used to warm the page cache by reading the cores with cat during
 * the boot animation - 480 ms cold to read every byte, and it bought only the
 * I/O. This does the same work in the same idle seconds for less: dlopen maps
 * what it needs rather than reading the file through, measured 382 ms cold,
 * and it also pays the dynamic linker so a first launch does not.
 *
 * Run on a thread of its own once the socket is listening, so the launcher's
 * connect is answered at once rather than after every core is mapped, and
 * only once the launcher has drawn (its PREMAP), so the mapping doesn't read
 * the card beside the launcher's own start; see main.c. A RUN that arrives meanwhile waits at most for the core being mapped
 * at that moment (see the registry's lock above).
 *
 * A core that fails here is not fatal: it will be tried again by name when a
 * RUN asks for it, and fail there with the launcher listening. */
int diatom_core_premap(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (!d) return 0;
	while ((e = readdir(d))) {
		char path[1024];
		size_t len = strlen(e->d_name);

		if (len < 12 || strcmp(e->d_name + len - 12, "_libretro.so") != 0)
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (diatom_core_resident(path)) n++;
	}
	closedir(d);
	return n;
}

/* Cores that set need_fullpath want a path and read the file themselves;
 * the rest want the bytes. Getting this backwards is a silent load failure. */
static bool read_file(const char *path, void **out, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *buf;

	*out = NULL; *len = 0;
	if (!f) return false;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
	n = ftell(f);
	if (n <= 0) { fclose(f); return false; }
	rewind(f);

	buf = malloc((size_t)n);
	if (!buf) { fclose(f); return false; }
	if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf); fclose(f); return false;
	}
	fclose(f);
	*out = buf; *len = (size_t)n;
	return true;
}

/* Does this core want the ARCHIVE, not what is inside it?
 *
 * An arcade game is not one ROM in a zip: ssf2.zip is 23 chips the core
 * finds by name and checks by CRC, and the archive's name is the game's
 * name - the core picks its driver from "ssf2". Extracting the largest entry,
 * which is right for every console core here, handed FinalBurn Neo a lone
 * ssf.13m staged in /tmp; it named no driver, the core drew its own "romset
 * not found" screen and reported the load a success.
 *
 * libretro has a flag for exactly this, block_extract, and FinalBurn Neo and
 * MAME 2003-Plus both set it. A core that lists zip among its own extensions
 * says the same thing another way, and is taken at its word too. No console
 * core ZyperOS ships does either - read from each one's retro_system_info on
 * 2026-11-03, and Stella 2014, ProSystem and Game & Watch neither - so they
 * keep the extraction. */
static bool takes_archives(const struct retro_system_info *si)
{
	const char *p = si->valid_extensions;

	if (si->block_extract) return true;
	while (p && *p) {
		size_t n = strcspn(p, "|");

		if (n == 3 && !strncasecmp(p, "zip", 3)) return true;
		p += n;
		if (*p == '|') p++;
	}
	return false;
}

/* A game Diatom unzipped for a core that takes the bytes, described for
 * GET_GAME_INFO_EXT (env.c) as RetroArch describes content found in an
 * archive (tasks/task_content.c, content_file_list_set_info): full_path
 * "<zip>#<entry>", the zip and the entry apart, the extension the entry's in
 * lower case, the folder and the name the zip's.
 *
 * Handy (bc55d46) needs it. It takes its cart only from this answer; refused,
 * it opens retro_game_info's path, the zip's, and so it read the zip:
 * "Invalid Cart (type)", and no zipped Lynx game on ROGUE OS's card started,
 * where the same .lnx unzipped did (QEMU, 2026-11-06). Answered, it loads the
 * bytes, and names its EEPROM after the zip, as Diatom names its own saves.
 *
 * retro_game_info's path stays the zip's, where RetroArch puts
 * "<zip>#<entry>" there too. A core that names a file of its own after that
 * path would name it after the entry: PokeMini cuts the path at its last '/'
 * and '.', so its EEPROM became "Game.zip#Entry.eep", and every Pokemon Mini
 * zip on ROGUE OS's card names its entry in an encoding that is not UTF-8,
 * which an exFAT card refuses in a file name - the game kept no save (QEMU,
 * 2026-11-06). The zip's own path names it "Game.eep", as it always did.
 *
 * Static strings, copied, as they must outlive this call and the caller's
 * buffers: libretro allows the call only inside retro_load_game, and env.c
 * answers it only there. */
static void describe_in_archive(struct retro_game_info_ext *x, const char *zip,
                                const char *entry, const void *data, size_t len)
{
	static char full[1600], archive[1024], file[512], dir[1024], name[512], ext[32];
	const char *base, *dot;
	char *cut;
	size_t k;

	snprintf(archive, sizeof archive, "%s", zip);
	snprintf(file, sizeof file, "%s", entry);
	snprintf(full, sizeof full, "%s#%s", archive, file);

	snprintf(dir, sizeof dir, "%s", zip);
	cut = strrchr(dir, '/');
	if (cut && cut != dir) *cut = '\0';
	else snprintf(dir, sizeof dir, "%s", cut ? "/" : ".");

	base = strrchr(zip, '/');
	snprintf(name, sizeof name, "%s", base ? base + 1 : zip);
	cut = strrchr(name, '.');
	if (cut && cut != name) *cut = '\0';

	base = strrchr(file, '/');
	base = base ? base + 1 : file;
	dot = strrchr(base, '.');
	snprintf(ext, sizeof ext, "%s", dot && dot != base ? dot + 1 : "");
	for (k = 0; ext[k]; k++)
		if (ext[k] >= 'A' && ext[k] <= 'Z') ext[k] += 'a' - 'A';

	memset(x, 0, sizeof *x);
	x->full_path       = full;
	x->archive_path    = archive;
	x->archive_file    = file;
	x->dir             = dir;
	x->name            = name;
	x->ext             = ext;
	x->data            = data;
	x->size            = len;
	x->file_in_archive = true;
	x->persistent_data = false;     /* freed once retro_load_game returns */
}

/* The file a zipped game was staged to for a core that wants a file, removed
 * once the core is done with it: after its game is unloaded, or after a load
 * that failed. /tmp is tmpfs on the devices, RAM, and a staged ROM nobody
 * removed kept its size there until the next boot, once per game played that
 * way. The next game to stage the same name writes a new file. */
static void unstage(diatom_core *c)
{
	if (!c->staged[0]) return;
	if (unlink(c->staged) != 0 && errno != ENOENT)
		fprintf(stderr, "diatom: cannot remove %s: %s\n", c->staged, strerror(errno));
	c->staged[0] = '\0';
}

/* A core that asked for the GPU gets its context once the game has loaded
 * (hw.c). Without one it cannot draw a frame, so the game is put down again
 * and the load fails, rather than start a game nobody can see. */
static bool start_gpu(diatom_core *c)
{
	if (diatom_hw_start(c)) return true;
	c->unload_game();
	diatom_hw_unload();
	c->game_loaded = false;
	unstage(c);
	return false;
}

bool diatom_core_start(diatom_core *c, const char *rom_path)
{
	struct retro_system_info si;
	struct retro_game_info gi;
	struct retro_game_info_ext ext;   /* for a game out of an archive */
	bool have_ext = false;
	void *data = NULL;
	size_t len = 0;
	bool ok;

	memset(&si, 0, sizeof si);
	c->get_system_info(&si);

	/* Before the core can declare anything. A core re-declares its memory map
	 * on every load, and a pointer kept from the previous game points into
	 * memory that core has since freed. */
	diatom_cheevos_reset();
	/* Same reason, one declaration further: forget the last game's pixel
	 * format so this load's silence can be told from its agreement. */
	diatom_env_pixfmt_begin();
	/* And its rotation: a vertical arcade game turned, the next one upright. */
	diatom_env_rotation_begin();
	diatom_hw_load_begin();

	if (!c->initialized) {
		double t0 = dbg_now_ms();

		c->init();
		c->initialized = true;
		fprintf(stderr, "diatom: retro_init %-25s %6.1f ms\n",
		        dbg_base(c->path), dbg_now_ms() - t0);
	}

	/* A core that declared SET_SUPPORT_NO_GAME expects NULL, not a path. */
	if (!rom_path) {
		double t0 = dbg_now_ms();
		bool started = c->load_game(NULL);

		fprintf(stderr, "diatom: retro_load_game %-21s %6.1f ms\n",
		        dbg_base(c->path), dbg_now_ms() - t0);
		if (!started) {
			fprintf(stderr, "diatom: core refused to start without content\n");
			return false;
		}
		c->game_loaded = true;
		diatom_env_pixfmt_settle(c);
		diatom_cheevos_resolve(c);
		return start_gpu(c);
	}

	memset(&gi, 0, sizeof gi);
	gi.path = rom_path;

	/* Zipped content - how a launcher's library actually arrives. The ROM is
	 * extracted here rather than by the core: of the pinned set only some
	 * cores unzip for themselves, and the first real launcher found that out
	 * on the first game it handed over. One entry per archive (the largest);
	 * a need_fullpath core gets the extraction as a tmpfs file, since it
	 * wants to read from disk, and everything else gets the buffer. */
	if (diatom_zip_is(rom_path) && !takes_archives(&si)) {
		char inner[512];

		if (!diatom_zip_load(rom_path, &data, &len, inner, sizeof inner)) {
			fprintf(stderr, "diatom: cannot extract %s\n", rom_path);
			return false;
		}
		fprintf(stderr, "diatom: zip: %s -> %s (%zu bytes)\n",
		        rom_path, inner, len);
		if (si.need_fullpath) {
			FILE *tf;
			const char *base = strrchr(inner, '/');
			bool wrote;

			/* Kept in the core, which outlives the game, as the static
			 * it replaces did: a core may hold on to the path it was
			 * handed. And the core's stop knows what to remove. */
			snprintf(c->staged, sizeof c->staged, "/tmp/diatom-%s",
			         base ? base + 1 : inner);
			tf = fopen(c->staged, "wb");
			if (!tf) {
				fprintf(stderr, "diatom: cannot stage %s\n", c->staged);
				c->staged[0] = '\0';
				free(data);
				return false;
			}
			wrote = fwrite(data, 1, len, tf) == len;
			if (fclose(tf) != 0) wrote = false;
			free(data);
			data = NULL;
			if (!wrote) {
				/* Part of a ROM, and held in RAM: removed, not left */
				fprintf(stderr, "diatom: cannot stage %s\n", c->staged);
				unstage(c);
				return false;
			}
			gi.path = c->staged;
		} else {
			describe_in_archive(&ext, rom_path, inner, data, len);
			gi.data = data;
			gi.size = len;
			have_ext = true;
		}
	} else if (!si.need_fullpath) {
		if (!read_file(rom_path, &data, &len)) {
			fprintf(stderr, "diatom: cannot read %s\n", rom_path);
			return false;
		}
		gi.data = data;
		gi.size = len;
	}

	/* The Super Game Boy for this game, by its header, read in the bytes the
	 * core is handed (src/sgb.h): its options are read at the load */
	diatom_sgb_apply((const unsigned char *)gi.data, gi.data ? gi.size : 0);
	if (have_ext) diatom_env_set_game_info_ext(&ext);
	{
		/* Timed, as dlopen and retro_init are: a core that unpacks its
		 * own zip does it in here (DSperate, into .dsperate beside the
		 * zip, src/dscache.h), and this line is how long it took */
		double t0 = dbg_now_ms();

		ok = c->load_game(&gi);
		fprintf(stderr, "diatom: retro_load_game %-21s %6.1f ms\n",
		        dbg_base(c->path), dbg_now_ms() - t0);
	}
	diatom_env_set_game_info_ext(NULL);

	/* The core has copied whatever it needs by now; libretro does not promise
	 * the buffer stays valid, and holding it would waste anonymous memory on a
	 * device with no swap. */
	free(data);

	if (!ok) {
		fprintf(stderr, "diatom: core refused %s\n", rom_path);
		unstage(c);
		return false;
	}
	c->game_loaded = true;
	/* Before the first frame can be presented, and after the core has had
	 * every chance to speak: whatever it declared is this core's, and its
	 * silence means it is still what it said the first time. */
	diatom_env_pixfmt_settle(c);

	/* Now, and not before: a core declares its memory map during load, and
	 * retro_get_memory_data has nothing to hand back until there is a game.
	 * A map that arrives later still counts - cheevos.c re-resolves. */
	diatom_cheevos_resolve(c);
	return start_gpu(c);
}

/* A core that runs one game per load. ScummVM's makes its OSystem in
 * retro_init, and its engines keep singletons of their own for the life of
 * the library: a second game in the same loaded library ended with the core
 * calling exit(), and after retro_deinit and retro_init it aborted instead
 * (Diatom's desktop port, 2026-11-06, Flight of the Amazon Queen and then
 * Beneath a Steel Sky; on a Pixel 2 the launcher killed a Diatom that never
 * finished the second game). Closed and opened again between games, three in a
 * row ran: RetroArch unloads a core between games, and this core expects it.
 *
 * FreeJ2ME-Plus too. It runs its game in a Java process of its own, started
 * in retro_init and ended only in retro_deinit (its retro_unload_game does
 * nothing), so in a resident Diatom the Java process outlived its game: 110
 * MB of the Pixel 2's 1 GB, after the game had quit to the shelf. Put down
 * with retro_deinit and initialized again, Java ended, but the next game
 * hung on the last one's frame: the core keeps its state in statics that
 * retro_init does not reset (a Pixel 2, 2026-11-06). Unloaded, the next game
 * starts on a fresh library, as under RetroArch.
 *
 * ParaLLEl N64 too, and Mupen64Plus-Next, which is built on the same
 * Mupen64Plus core. Its core starts once per library: the second game in a
 * resident Diatom, after retro_deinit and retro_init, logged "mupen64plus:
 * Failed to initialize core", "Failed to load ROM" and "Running Dead N64
 * Emulator" and was refused; the launcher's fallback ran it, and the game
 * after that resumed into a dead emulator (the same line 6005 times, 0.15 ms
 * a frame), which the player saw as controls gone wrong and which ended in
 * "state: serialize failed" when they left (a Pixel 2, 2026-11-13, Mario
 * Kart 64 three times in a row). Unloaded, each game starts on a fresh
 * library, as under RetroArch; dlopen takes about 2 ms.
 *
 * And PrBoom, whose menus and episodes are statics its next game does not
 * reset: M_Init turns Read This! into a second Quit Game for Doom II and
 * never turns it back, so The Ultimate Doom after Doom II had two Quit Game
 * rows, and a mod's episodes stayed in the next game's menu (1.0.5, Discord;
 * the first seen on a Mac with the pinned core and Freedoom).
 *
 * blueMSX too. A second load_game in the same library, especially with
 * bluemsx_additional_cart, SIGSEGVs in the SCC-plus mapper create path:
 * the additional cart's chip object is left corrupt after unload. Cold
 * retro_init after a process restart works. Unloading the .so between
 * games does the same without leaving the shelf.
 * A fix has been submitted in blueMSX core, but not yet merged:
 * https://github.com/libretro/blueMSX-libretro/pull/213
 * Once merged, this core will not need to unload between games.
 *
 * By name, because nothing a core declares says so.
 *
 * And every core ZyperOS did not ship, which is the case nobody can name
 * ahead of time: a core a person adds to the cores folder is one nobody here
 * has read. freeintv, added by hand to a Brick (#304), keeps its last game in
 * statics too: the next game, with no resume point, started on the first
 * game's screen under "LOAD CART OKAY / INTELLIVISION HALTED", and that
 * screen was saved as the next game's resume point when the player left. The
 * cores ZyperOS ships were each read and measured to stay open; a core nobody
 * here has read starts clean, as under RetroArch, and costs a dlopen and a
 * retro_init a game. See core_is_shipped. */
static bool one_game_per_load(diatom_core *c)
{
	struct retro_system_info si;

	if (!c->shipped) return true;
	memset(&si, 0, sizeof si);
	c->get_system_info(&si);
	return si.library_name && (!strcmp(si.library_name, "ScummVM") ||
	                           !strcmp(si.library_name, "FreeJ2ME-Plus") ||
	                           !strcmp(si.library_name, "ParaLLEl N64") ||
	                           !strcmp(si.library_name, "Mupen64Plus-Next") ||
	                           !strcmp(si.library_name, "PrBoom") ||
	                           !strcmp(si.library_name, "blueMSX"));
}

/* Stops the GAME, not the core. The core stays initialized and stays mapped -
 * that is the whole of keeping cores resident. */
void diatom_core_stop(diatom_core *c)
{
	if (c->game_loaded) {
		bool gpu = diatom_hw_active();

		c->unload_game();
		diatom_hw_unload();
		c->game_loaded = false;
		/* Nothing a game asked of the motor outlives it, and a stop that
		 * ends a pause ends the pause's hold with it. */
		diatom_rumble_reset();
		/* Except a core that drew with the GPU, which is put down whole and
		 * initialized again for its next game, as RetroArch does between
		 * games: flycast2021 starts its emulation thread on the first
		 * retro_run after retro_init and never again, so its second game in
		 * a resident Diatom ran no emulation at all (2026-11-04). Its
		 * retro_init takes 0.1 ms. */
		if (gpu && c->initialized) {
			c->deinit();
			c->initialized = false;
		}
		/* And the core that runs one game per load is put down and
		 * unloaded; the registry opens it again for its next game. Its
		 * option table goes first: it borrows the library's strings. */
		if (one_game_per_load(c)) {
			if (c->initialized) {
				c->deinit();
				c->initialized = false;
			}
			diatom_options_forget(c);
			dlclose(c->handle);
			c->handle = NULL;
		}
	}
	/* Last, once the core has unloaded its game and, if it is put down,
	 * ended too: nothing of it reads the staged file after this. */
	unstage(c);
}

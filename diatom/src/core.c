/* Loading a core, and its lifecycle.
 *
 * Every core is dlopen'd once and NEVER unloaded, but for the one that runs a
 * single game per load (one_game_per_load below), which is closed after each
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
#include <unistd.h>

#include "cheevos.h"
#include "diatom.h"

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

bool diatom_core_open(diatom_core *c, const char *path)
{
	double t0;

	memset(c, 0, sizeof *c);
	c->path = path;

	t0 = dbg_now_ms();
	c->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	fprintf(stderr, "diatom: dlopen %-28s %6.1f ms\n",
	        dbg_base(path), dbg_now_ms() - t0);
	if (!c->handle) {
		fprintf(stderr, "diatom: dlopen %s: %s\n", path, dlerror());
		return false;
	}

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
 * and the rest refused at RUN. Thirty-two leaves room again. The number is
 * DIATOM_MAX_RESIDENT, in diatom.h, which options.c sizes its tables by. */
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
		if (!c->load_game(NULL)) {
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

	if (have_ext) diatom_env_set_game_info_ext(&ext);
	ok = c->load_game(&gi);
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
 * By name, because nothing a core declares says so. */
static bool one_game_per_load(diatom_core *c)
{
	struct retro_system_info si;

	memset(&si, 0, sizeof si);
	c->get_system_info(&si);
	return si.library_name && (!strcmp(si.library_name, "ScummVM") ||
	                           !strcmp(si.library_name, "FreeJ2ME-Plus"));
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

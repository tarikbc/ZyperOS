/* Persistence.
 *
 * Host-side entirely. The port deals in pixels, samples, buttons and time, and
 * a file is none of those.
 *
 * Two different things live here and they are not variants of each other:
 *
 *   SRAM is the cartridge battery - the GAME's own save. Automatic and not
 *   optional, because losing it is a defect rather than a missing feature.
 *
 *   A save state is a whole-emulator snapshot the game knows nothing about. A
 *   frontend convenience, so it is opt-in and takes an explicit path.
 *
 * Everything here writes through write_atomic. Measured on the Brick's card:
 * an 8 KB write costs about 8 ms and a 528 KB write about 68 ms, against a
 * 16.6 ms frame budget of which present already takes 8.4 ms. So a save write
 * can NEVER happen on the frame thread; that is what the writer thread is for.
 */
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <time.h>
#include <unistd.h>

#include "cheevos.h"
#include "diatom.h"
#include "statefile.h"

/* Long enough that a churning game is not writing constantly, short enough
 * that a hard power cut cannot cost much. Graceful shutdown is covered
 * separately: SIGTERM arrives about 810 ms before death (measured), which is
 * ~95x what an 8 KB flush needs. */
#define WRITE_INTERVAL_US 1000000

/* A state's file - its header, the core's bytes packed, the achievement
 * trailer - is statefile.c's: here are the core and the card. */

static char        g_sram_path[1024];
static uint8_t    *g_sram;        /* the core's buffer; the core owns it */
static uint8_t    *g_shadow;      /* what we last handed to the writer */
static uint8_t    *g_staging;     /* the writer's private copy */
static size_t      g_sram_size;
static uint64_t    g_last_write;

/* Battery RAM a core sizes only after the game has started: dsperate-lr
 * gives a DS game its save chip at load when DSperate's save list names the
 * game, or when an .srm is there to size it from, and otherwise from the
 * game's first access to the chip - after Diatom asked, at load, and was told
 * there was none. Without a second look that game's progress was never
 * written. So a game with no battery at load is looked at again once a second
 * while it runs, and its chip adopted when one appears. */
static diatom_core    *g_late_core;
static unsigned        g_late_ticks;
#define LATE_EVERY 60

static pthread_t       g_writer;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static bool            g_pending, g_stop, g_running;

static uint64_t now_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000ull + t.tv_nsec / 1000;
}

static void logf_(diatom_log_level lvl, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	diatom_port_log(lvl, buf);
}

/* write, fsync, rename. The rename is what makes it atomic: a power cut can
 * lose the new save but can never corrupt the old one. fsync measured free on
 * this filesystem, so there is no argument for skipping it. */
static bool write_atomic(const char *path, const void *data, size_t n)
{
	char tmp[1088];
	int  fd;

	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		logf_(DIATOM_LOG_ERROR, "save: open %s: %s", tmp, strerror(errno));
		return false;
	}
	if (write(fd, data, n) != (ssize_t)n) {
		logf_(DIATOM_LOG_ERROR, "save: write %s: %s", tmp, strerror(errno));
		close(fd);
		unlink(tmp);
		return false;
	}
	fsync(fd);
	close(fd);
	if (rename(tmp, path) != 0) {
		logf_(DIATOM_LOG_ERROR, "save: rename %s: %s", path, strerror(errno));
		unlink(tmp);
		return false;
	}
	return true;
}

static void *writer(void *arg)
{
	pthread_mutex_lock(&g_mx);
	for (;;) {
		while (!g_pending && !g_stop)
			pthread_cond_wait(&g_cv, &g_mx);
		if (g_stop && !g_pending) break;
		g_pending = false;
		pthread_mutex_unlock(&g_mx);

		write_atomic(g_sram_path, g_staging, g_sram_size);

		pthread_mutex_lock(&g_mx);
	}
	pthread_mutex_unlock(&g_mx);
	return arg;
}

/* Basename with the extension stripped. Used only for the standalone
 * defaults; a launcher passes explicit paths and never reaches this. */
static void stem(const char *path, char *out, size_t n)
{
	const char *base = strrchr(path, '/');
	const char *dot;

	base = base ? base + 1 : path;
	dot  = strrchr(base, '.');
	snprintf(out, n, "%.*s", dot ? (int)(dot - base) : (int)strlen(base), base);
}

bool diatom_save_init(diatom_core *c, const char *save_dir, const char *rom_path)
{
	char  name[512];
	FILE *f;

	if (!c->get_memory_data || !c->get_memory_size || !rom_path) return true;

	g_sram_size = c->get_memory_size(RETRO_MEMORY_SAVE_RAM);
	g_sram      = c->get_memory_data(RETRO_MEMORY_SAVE_RAM);

	/* Measured: SRAM size is per GAME, not per system. Probotector reports 0,
	 * Final Fantasy on the same core reports 8 KB. A game with no battery must
	 * produce no file at all rather than an empty one. */
	stem(rom_path, name, sizeof name);
	snprintf(g_sram_path, sizeof g_sram_path, "%s/%s.srm",
	         save_dir ? save_dir : ".", name);

	g_late_core = NULL;
	if (!g_sram_size || !g_sram) {
		logf_(DIATOM_LOG_INFO, "save: no battery in this game, or none yet");
		g_sram_size = 0;
		g_late_core = c;
		g_late_ticks = 0;
		return true;
	}

	f = fopen(g_sram_path, "rb");
	if (f) {
		size_t got = fread(g_sram, 1, g_sram_size, f);
		fseek(f, 0, SEEK_END);
		if ((size_t)ftell(f) != g_sram_size)
			/* A core that changed its layout between versions. Load what fits
			 * and say so: a partial load usually still yields a working save,
			 * whereas refusing guarantees the player loses it. */
			logf_(DIATOM_LOG_WARN,
			      "save: %s is %ld bytes, core wants %zu; loaded %zu",
			      g_sram_path, ftell(f), g_sram_size, got);
		else
			logf_(DIATOM_LOG_INFO, "save: loaded %zu bytes from %s",
			      got, g_sram_path);
		fclose(f);
	}

	g_shadow  = malloc(g_sram_size);
	g_staging = malloc(g_sram_size);
	if (!g_shadow || !g_staging) {
		free(g_shadow); free(g_staging);
		g_shadow = g_staging = NULL;
		g_sram_size = 0;
		return false;
	}
	memcpy(g_shadow, g_sram, g_sram_size);
	g_last_write = now_us();

	if (pthread_create(&g_writer, NULL, writer, NULL) == 0)
		g_running = true;
	else
		logf_(DIATOM_LOG_WARN, "save: no writer thread; flushing on exit only");
	return true;
}

/* See g_late_core. Adopted only when there is no file: a file would have sized
 * the chip at load, so one that is there now and did not is a file this game
 * did not make, and is not written over. The chip is written at once, since
 * what made it appear was the game saving. */
static void late_look(void)
{
	size_t n;
	uint8_t *d;

	if (++g_late_ticks % LATE_EVERY) return;
	n = g_late_core->get_memory_size(RETRO_MEMORY_SAVE_RAM);
	d = g_late_core->get_memory_data(RETRO_MEMORY_SAVE_RAM);
	if (!n || !d) return;
	g_late_core = NULL;
	if (access(g_sram_path, F_OK) == 0) {
		logf_(DIATOM_LOG_WARN, "save: the game made %zu bytes of battery late, and %s "
		      "is there already; leaving it, nothing saved", n, g_sram_path);
		return;
	}
	g_shadow  = malloc(n);
	g_staging = malloc(n);
	if (!g_shadow || !g_staging) {
		free(g_shadow); free(g_staging);
		g_shadow = g_staging = NULL;
		return;
	}
	g_sram = d;
	g_sram_size = n;
	memcpy(g_shadow, g_sram, n);
	g_shadow[0] ^= 0xff;            /* differs, so the next tick writes it */
	g_last_write = 0;
	logf_(DIATOM_LOG_INFO, "save: the game made %zu bytes of battery; saving to %s",
	      n, g_sram_path);
	if (pthread_create(&g_writer, NULL, writer, NULL) == 0)
		g_running = true;
	else
		logf_(DIATOM_LOG_WARN, "save: no writer thread; flushing on exit only");
}

/* How often the battery RAM is compared with its shadow: every 8th frame,
 * 133 ms at 60 fps. Every frame read 2 x 128 KB a frame for a GBA game (and
 * found nothing on almost all of them) for a write that the interval below
 * holds to one a second anyway; this adds at most 133 ms to when a save is
 * seen, and the flush on every way out compares whatever this skipped. */
#define LOOK_EVERY 8

void diatom_save_tick(void)
{
	static unsigned frame;

	if (g_late_core) late_look();
	if (!g_sram_size || !g_running) return;
	if (++frame % LOOK_EVERY) return;
	/* memcmp of at most 128 KB, tens of microseconds. Most looks find
	 * nothing: only games that actually save touch this. NES carts are the
	 * exception - they use battery RAM as work RAM and change it constantly,
	 * which is what the interval is really bounding. */
	if (!memcmp(g_sram, g_shadow, g_sram_size)) return;
	if (now_us() - g_last_write < WRITE_INTERVAL_US) return;

	pthread_mutex_lock(&g_mx);
	memcpy(g_staging, g_sram, g_sram_size);
	memcpy(g_shadow,  g_sram, g_sram_size);
	g_pending = true;
	pthread_cond_signal(&g_cv);
	pthread_mutex_unlock(&g_mx);
	g_last_write = now_us();
}

void diatom_save_flush(void)
{
	if (!g_sram_size) return;
	if (!memcmp(g_sram, g_shadow, g_sram_size)) return;

	/* Synchronous on purpose. This runs on the exit and signal paths, where a
	 * stall costs nothing and a missed write costs the player's progress. */
	if (write_atomic(g_sram_path, g_sram, g_sram_size)) {
		memcpy(g_shadow, g_sram, g_sram_size);
		logf_(DIATOM_LOG_INFO, "save: flushed %zu bytes to %s",
		      g_sram_size, g_sram_path);
	}
}

void diatom_save_shutdown(void)
{
	if (g_running) {
		pthread_mutex_lock(&g_mx);
		g_stop = true;
		pthread_cond_signal(&g_cv);
		pthread_mutex_unlock(&g_mx);
		pthread_join(g_writer, NULL);
		g_running = false;
	}
	diatom_save_flush();
	free(g_shadow); free(g_staging);
	g_shadow = g_staging = NULL;
	g_sram_size = 0;
	g_late_core = NULL;
}

/* A state as its file holds it before it is packed - header, the core's
 * bytes, the achievement trailer - in one malloc'd buffer: serialized here, on
 * the frame thread, between two frames, so it is one frame's state. The
 * packing is the card's half (sf_pack), done wherever the write is. NULL when
 * the core keeps none. */
static uint8_t *state_build(diatom_core *c, size_t *len, size_t *core_n)
{
	struct retro_system_info si;
	uint8_t *buf;
	size_t   n;
	uint32_t prog;

	*len = 0;
	if (!c->serialize_size || !c->serialize) return NULL;

	/* Called immediately before every serialize, never cached. Measured: mGBA
	 * moved 528448 to 462912 within 600 frames of Golden Sun while reporting
	 * quirks 0x0, having never declared CORE_VARIABLE_SIZE. A cached size that
	 * later grows makes saving fail silently. */
	n = c->serialize_size();
	if (!n) { logf_(DIATOM_LOG_ERROR, "state: core reports no state"); return NULL; }
	/* Achievement progress after the core's own, in a trailer the header's
	 * payload length leaves outside: a Diatom from before it reads the state
	 * and never sees it (diatom_cheevos_progress_restore) */
	prog = diatom_cheevos_progress_size();

	buf = malloc(SF_HEADER + n + (prog ? SF_PROGRESS_HDR + prog : 0));
	if (!buf) return NULL;

	if (!c->serialize(buf + SF_HEADER, n)) {
		logf_(DIATOM_LOG_ERROR, "state: serialize failed");
		free(buf);
		return NULL;
	}
	c->get_system_info(&si);
	sf_header(buf, si.library_name, si.library_version, n);
	if (prog) {
		uint8_t *t = buf + SF_HEADER + n;

		memcpy(t, SF_PROGRESS_MAGIC, 4);
		t[4] = (uint8_t)prog;
		t[5] = (uint8_t)(prog >> 8);
		t[6] = (uint8_t)(prog >> 16);
		t[7] = (uint8_t)(prog >> 24);
		if (!diatom_cheevos_progress_save(t + SF_PROGRESS_HDR, prog)) prog = 0;
	}
	*len = SF_HEADER + n + (prog ? SF_PROGRESS_HDR + prog : 0);
	*core_n = n;
	return buf;
}

/* What a state cost, in microseconds, for its log line: the core's half
 * (serialize or unserialize), the packing or unpacking, and the card's (the
 * write with its fsync, or the read). The device's numbers come from these
 * lines: `grep 'state:'` in the boot's zyperos.log. */
typedef struct { uint64_t core, pack, card; } state_cost;

static double ms(uint64_t us) { return us / 1000.0; }

/* Both lines give the file's bytes on the card and the core's, as it
 * serialized them */
static void log_saved(const char *path, size_t len, size_t core_n, const state_cost *t)
{
	logf_(DIATOM_LOG_INFO, "state: wrote %zu bytes to %s (%zu from the core; serialize %.1f ms, "
	      "pack %.1f ms, write %.1f ms)", len, path, core_n, ms(t->core), ms(t->pack),
	      ms(t->card));
}

/* The file to write and its length: `buf` packed, timed into `t` */
static uint8_t *state_pack(uint8_t *buf, size_t *len, state_cost *t)
{
	uint64_t t0 = now_us();

	buf = sf_pack(buf, len);
	t->pack = now_us() - t0;
	return buf;
}

bool diatom_state_save(diatom_core *c, const char *path)
{
	state_cost t = { 0 };
	uint8_t *buf;
	size_t   len, core_n;
	uint64_t t0;
	bool     ok;

	if (!path) return false;
	diatom_state_bg_poll(c, true, false, NULL, 0, NULL);
	t0 = now_us();
	if (!(buf = state_build(c, &len, &core_n))) return false;
	t.core = now_us() - t0;
	buf = state_pack(buf, &len, &t);
	t0 = now_us();
	ok = write_atomic(path, buf, len);
	t.card = now_us() - t0;
	free(buf);
	if (ok) log_saved(path, len, core_n, &t);
	return ok;
}

/* ============================================================================
 * DELETE THIS THE DAY LIBRETRO SYNCS ITS mGBA FORK.
 * ============================================================================
 *
 * Two builds that write interchangeable states. This is the ONLY thing in
 * Diatom that knows a particular core by name and version, and it is here
 * against its own scope on purpose and temporarily.
 *
 * WHY IT EXISTS. libretro's mGBA segfaults on every MBC2 Game Boy cartridge -
 * a regression in mgba a1b2b23, reported as mgba-emu/mgba#3859 and fixed
 * upstream the same day in 543a1975. That fix is in no downloadable core:
 * libretro builds from its own fork, unsynced since 2026-08-06. ZyperOS
 * therefore ships a bridge build - libretro's tree at the pinned commit plus
 * upstream's own one-hunk fix - and it reports `0.11.0` rather than
 * `0.11-219-e31759b` only because there is no git in the build container.
 *
 * WHY IT IS SAFE, which is a claim about these two binaries and not a general
 * one. They are the same commit. The patch touches GBMBCSwitchSramBank, which
 * is mapper code; mGBA's state stores sramCurrentBank as an integer and
 * re-derives the pointer on load, so the fix makes loading MORE correct rather
 * than incompatible. Nothing about the serialized layout differs.
 *
 * WHAT IT BUYS. Without it, swapping between the two rejects every save state
 * - gracefully, the game just starts normally - so the day the official core
 * becomes usable again, every player silently loses their resume points.
 * Battery saves are unaffected either way; .srm is raw cartridge RAM with no
 * version in it.
 *
 * WHEN TO REMOVE IT: as soon as github.com/libretro/mgba carries 543a1975 and
 * ZyperOS goes back to the fetched core. Then this table is not merely dead, it
 * is WRONG - it would let a genuinely foreign state through. Delete the table,
 * delete version_twins, and restore the plain strcmp above, at the same time
 * as ZyperOS deletes its mk/build-mgba-bridge.sh. */
static const struct { const char *a, *b; } STATE_TWINS[] = {
	{ "0.11.0", "0.11-219-e31759b" },   /* the MBC2 bridge, and what it patches */
};

static bool version_twins(const char *have, const char *want)
{
	size_t i;

	if (strcmp(have, want) == 0) return true;
	for (i = 0; i < sizeof STATE_TWINS / sizeof STATE_TWINS[0]; i++) {
		const char *a = STATE_TWINS[i].a, *b = STATE_TWINS[i].b;

		if ((strcmp(have, a) == 0 && strcmp(want, b) == 0) ||
		    (strcmp(have, b) == 0 && strcmp(want, a) == 0))
			return true;
	}
	return false;
}

/* A state read back (sf_unpack), handed to the core: on the frame thread, as a
 * load always was. False, said, when the file was no state for it. */
static bool state_apply(diatom_core *c, const char *path, sf_error e, const sf_state *st,
                        state_cost *t)
{
	struct retro_system_info si;
	uint64_t t0;
	bool     ok;

	if (e != SF_OK) {
		logf_(DIATOM_LOG_WARN, "state: %s %s", path, sf_error_text(e));
		return false;
	}

	/* A state is only meaningful to the exact core that wrote it. Refusing is
	 * the whole point of the header: the caller falls back to starting the
	 * game normally, which is right, where handing a core a foreign state is
	 * undefined behavior. */
	c->get_system_info(&si);
	if (strcmp(st->core_name, si.library_name ? si.library_name : "?") != 0 ||
	    !version_twins(st->core_version,
	                   si.library_version ? si.library_version : "?")) {
		logf_(DIATOM_LOG_WARN, "state: %s was written by %s %s, this is %s %s",
		      path, st->core_name, st->core_version,
		      si.library_name ? si.library_name : "?",
		      si.library_version ? si.library_version : "?");
		return false;
	}

	/* The core gets the bytes it serialized, every one: a state that was
	 * packed is unpacked to the whole size first. PPSSPP's unserialize reads
	 * the buffer with no regard for its size (CChunkFileReader::LoadPtr), so
	 * a shorter one would be read past its end. */
	t0 = now_us();
	ok = c->unserialize(st->payload, st->payload_n);
	t->core = now_us() - t0;
	logf_(DIATOM_LOG_INFO, "state: %s %s (%zu bytes, %zu from the core%s; read %.1f ms, "
	      "unpack %.1f ms, unserialize %.1f ms)",
	      ok ? "restored" : "REFUSED by core:", path, st->file_n, st->payload_n,
	      st->version == 1 ? ", version 1" : "", ms(t->card), ms(t->pack), ms(t->core));

	/* Here rather than at the call sites. A loaded state is a different
	 * point in the game's history, so the achievement progress the game had
	 * goes with the game's own: the progress the state carries, or none -
	 * hit counts and the previous frame every delta is measured against,
	 * from a timeline that no longer exists, would credit progress nobody
	 * made. A call site added later inherits this instead of having to
	 * remember it. */
	if (ok) diatom_cheevos_progress_restore(st->prog, st->nprog);
	return ok;
}

/* The whole file into a malloc'd buffer, or NULL. Absent is not an error. */
static uint8_t *read_whole(const char *path, size_t *n)
{
	FILE    *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	long     len;

	*n = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
	    (buf = malloc((size_t)len)) && fread(buf, 1, (size_t)len, f) == (size_t)len)
		*n = (size_t)len;
	else {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

/* A state file read and unpacked, timed into `t`: where the load's card half
 * runs, on the frame thread or the job's. `*file` is what was read, freed by
 * the caller after `st`. */
static sf_error state_read(const char *path, uint8_t **file, sf_state *st, state_cost *t)
{
	uint64_t t0 = now_us();
	size_t   n;
	sf_error e;

	memset(st, 0, sizeof *st);
	*file = read_whole(path, &n);
	t->card = now_us() - t0;
	if (!*file) return SF_NOT_STATE;
	t0 = now_us();
	e = sf_unpack(*file, n, st);
	t->pack = now_us() - t0;
	return e;
}

bool diatom_state_load(diatom_core *c, const char *path)
{
	state_cost t = { 0 };
	uint8_t *buf;
	sf_state st;
	sf_error e;
	bool     ok;

	if (!c->unserialize || !path) return false;
	/* A state still being written to this path is the one asked for */
	diatom_state_bg_poll(c, true, false, NULL, 0, NULL);
	e = state_read(path, &buf, &st, &t);
	/* Absent is not an error, and is not said */
	ok = buf && state_apply(c, path, e, &st, &t);
	sf_state_free(&st);
	free(buf);
	return ok;
}

/* ---- states while the game runs ------------------------------------------
 *
 * A quick save or load (SELECT+R1, SELECT+L1) happens while the game runs,
 * and the card is the slow half of it: an 8 KB write is about 8 ms here and a
 * 528 KB one 68 ms (measured, above), and a PlayStation state is 4 MB, an N64
 * one 16 MB and a PSP one 40 MB before they are packed. So the file goes to a
 * thread of its own, packed or unpacked there too, and the frames keep
 * coming. The core's half, serialize or unserialize, stays on the frame
 * thread, between two frames, where it always was: a state is one frame's.
 *
 * One at a time. A save or load asked for while one is in flight, or any
 * state read or written the old way (the menu's, the exit's), waits for it
 * first, so a load right after a save reads the save. */
static struct {
	pthread_t     th;
	bool          busy;            /* a thread to join */
	diatom_bg_kind kind;
	char          path[1024];
	char          pic_path[1024];  /* a save's picture beside it, or "" */
	uint8_t      *data, *pic;      /* a save's bytes; a load's, once read */
	size_t        n, core_n, pic_n; /* core_n: a save's bytes from the core */
	sf_state      st;              /* a load's, unpacked */
	sf_error      err;
	state_cost    cost;
	bool          ok;
	bool          threaded;        /* run on a thread, which is joined */
} g_bg;
/* Raised by the job as its last act: the frame thread may join without
 * waiting. Atomic, because it is the one thing both threads read. */
static int g_bg_done;

static void *bg_run(void *arg)
{
#ifdef __linux__
	/* Below the game's threads, as the launcher's own background work is:
	 * packing is CPU the emulator may want, and nothing waits on it but the
	 * slot's answer. A core's real-time threads are above it anyway. */
	if (g_bg.threaded) setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
	if (g_bg.kind == DIATOM_BG_SAVE) {
		uint64_t t0;

		g_bg.data = state_pack(g_bg.data, &g_bg.n, &g_bg.cost);
		t0 = now_us();
		g_bg.ok = write_atomic(g_bg.path, g_bg.data, g_bg.n);
		g_bg.cost.card = now_us() - t0;
		/* The picture after the state, so a slot that has one has a game
		 * behind it */
		if (g_bg.ok && g_bg.pic && g_bg.pic_path[0])
			write_atomic(g_bg.pic_path, g_bg.pic, g_bg.pic_n);
	} else {
		g_bg.err = state_read(g_bg.path, &g_bg.data, &g_bg.st, &g_bg.cost);
		g_bg.ok = g_bg.data != NULL;
	}
	__atomic_store_n(&g_bg_done, 1, __ATOMIC_RELEASE);
	return arg;
}

/* On a thread, or here when none will start */
static void bg_start(void)
{
	__atomic_store_n(&g_bg_done, 0, __ATOMIC_RELEASE);
	g_bg.busy = true;
	g_bg.threaded = pthread_create(&g_bg.th, NULL, bg_run, NULL) == 0;
	if (!g_bg.threaded) bg_run(NULL);
}

bool diatom_state_save_bg(diatom_core *c, const char *path, const char *pic_path,
                          uint8_t *pic, size_t pic_n)
{
	uint8_t *buf;
	size_t   len;
	uint64_t t0;

	diatom_state_bg_poll(c, true, false, NULL, 0, NULL);
	t0 = now_us();
	if (!path || !(buf = state_build(c, &len, &g_bg.core_n))) { free(pic); return false; }
	memset(&g_bg.cost, 0, sizeof g_bg.cost);
	g_bg.cost.core = now_us() - t0;
	g_bg.kind = DIATOM_BG_SAVE;
	snprintf(g_bg.path, sizeof g_bg.path, "%s", path);
	snprintf(g_bg.pic_path, sizeof g_bg.pic_path, "%s", pic_path ? pic_path : "");
	g_bg.data = buf;
	g_bg.n = len;
	g_bg.pic = pic;
	g_bg.pic_n = pic_n;
	bg_start();
	return true;
}

bool diatom_state_load_bg(diatom_core *c, const char *path)
{
	diatom_state_bg_poll(c, true, false, NULL, 0, NULL);
	if (!c->unserialize || !path) return false;
	g_bg.kind = DIATOM_BG_LOAD;
	snprintf(g_bg.path, sizeof g_bg.path, "%s", path);
	g_bg.pic_path[0] = '\0';
	g_bg.data = g_bg.pic = NULL;
	memset(&g_bg.cost, 0, sizeof g_bg.cost);
	bg_start();
	return true;
}

/* A job that finished, or `wait` for it: joined, a load handed to the core
 * (when `apply`; a game that is ending takes no state), and what it was.
 * Nothing the job writes is read here before g_bg_done says it is done, and
 * the join after it orders the rest, so there is no lock. */
diatom_bg_kind diatom_state_bg_poll(diatom_core *c, bool wait, bool apply,
                                    char *path, size_t n, bool *ok)
{
	diatom_bg_kind k;
	bool good;

	if (!g_bg.busy) return DIATOM_BG_NONE;
	if (!wait && !__atomic_load_n(&g_bg_done, __ATOMIC_ACQUIRE)) return DIATOM_BG_NONE;
	if (g_bg.threaded) pthread_join(g_bg.th, NULL);
	g_bg.busy = false;
	k = g_bg.kind;
	good = g_bg.ok;
	if (k == DIATOM_BG_SAVE) {
		if (good) log_saved(g_bg.path, g_bg.n, g_bg.core_n, &g_bg.cost);
		else logf_(DIATOM_LOG_ERROR, "state: could not write %s", g_bg.path);
	} else if (good) {
		good = apply && state_apply(c, g_bg.path, g_bg.err, &g_bg.st, &g_bg.cost);
	}
	sf_state_free(&g_bg.st);
	free(g_bg.data);
	free(g_bg.pic);
	g_bg.data = g_bg.pic = NULL;
	if (path && n) snprintf(path, n, "%s", g_bg.path);
	if (ok) *ok = good;
	return k;
}

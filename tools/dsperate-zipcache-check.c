/* The cap on the zipped games a built DSperate core unpacks
 * (mk/dsperate-zipcache-check.sh, which mk/build-dsperate.sh runs on each
 * core it builds).
 *
 * DSperate inflates a zipped game once into .dsperate beside its zip and
 * keeps it (its src/core/cart/zip_cache.h). Its own frontend keeps that folder
 * under 2 GB, the least recently launched first; the libretro wrapper set no
 * limit, so on ZyperOS every zipped DS game ever played stayed there.
 * mk/patches/dsperate-lr-zip-cache.patch sets the same 2 GB, which the
 * launcher holds after each game too (src/dscache.h).
 *
 * This loads the core the way Diatom does, on a folder holding the zip of a
 * ROM it writes itself (tools/dsperate-state-check.c's: its ARM9 counts
 * VBlanks in main RAM) and a .dsperate of three unpacked games, 2.5 GB of
 * sparse files whose zips are there too, each tag a launch older than the
 * next. Then:
 *
 *   - the core loads the zip and its copy is in .dsperate, the ROM's size;
 *   - the oldest launch's copy went with its tag, and the two after it
 *     stayed: 1.5 GB and the new copy fit in 2 GB, so nothing more went;
 *   - the game runs from the copy: 60 frames on, the count is 60.
 *
 * The zip is written here, deflated with one stored block (method 8, so the
 * core inflates it, as it would any zip tool's), with no zlib needed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "libretro.h"

#define COUNT_AT 0x100000u   /* 0x02100000, from the start of main RAM */
#define ROM_SIZE 0xA000
#define GB       (1ull << 30)
#define MB       (1ull << 20)

/* A path into `buf`: whole, or the check stops and says so */
#define PATHF(buf, ...) do { \
	int w_ = snprintf((buf), sizeof (buf), __VA_ARGS__); \
	if (w_ < 0 || (size_t)w_ >= sizeof (buf)) { \
		printf("  FAIL %s:%d: a path longer than its %zu bytes\n", __FILE__, __LINE__, sizeof (buf)); \
		exit(2); \
	} \
} while (0)

static char sysdir[512], savedir[512];
static int fails;

static void core_log(enum retro_log_level l, const char *fmt, ...)
{
	va_list a;

	(void)l;
	va_start(a, fmt);
	printf("    core: ");
	vprintf(fmt, a);
	va_end(a);
}

static bool env(unsigned cmd, void *data)
{
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:   *(const char **)data = savedir; return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:     return true;
	case RETRO_ENVIRONMENT_SET_VARIABLES:        return true;
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:      return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback *)data)->log = core_log;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable *v = data;

		/* the interpreter, as the other core checks run it: a container
		 * may not map code */
		if (!strcmp(v->key, "dsperate_jit")) { v->value = "disabled"; return true; }
		return false;
	}
	default: return false;
	}
}

static void video(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void audio1(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio(const int16_t *d, size_t f) { (void)d; return f; }
static void poll_(void) {}
static int16_t input(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a; (void)b; (void)c; (void)d; return 0; }

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* tools/dsperate-state-check.c's ROM: the ARM9 counts VBlanks at 0x02100000 */
static const uint32_t ARM9[] = {
	0xE3A00621, 0xE3A01000, 0xE5801000, 0xE59F2024, 0xE1D230B0, 0xE3130001, 0x1AFFFFFC, 0xE1D230B0,
	0xE3130001, 0x0AFFFFFC, 0xE5901000, 0xE2811001, 0xE5801000, 0xEAFFFFF5, 0x04000004,
};

static void make_rom(uint8_t *rom)
{
	size_t i;

	memset(rom, 0, ROM_SIZE);
	memcpy(rom, "ZYPEROS ZIP ZZIP01", 18);
	put32(rom + 0x20, 0x8000);      put32(rom + 0x24, 0x02000000);
	put32(rom + 0x28, 0x02000000);  put32(rom + 0x2C, sizeof ARM9);
	put32(rom + 0x30, 0x9000);      put32(rom + 0x34, 0x037F8000);
	put32(rom + 0x38, 0x037F8000);  put32(rom + 0x3C, 4);
	put32(rom + 0x80, ROM_SIZE);    put32(rom + 0x84, 0x4000);
	for (i = 0; i < sizeof ARM9 / 4; i++) put32(rom + 0x8000 + 4 * i, ARM9[i]);
	put32(rom + 0x9000, 0xEAFFFFFE);   /* the ARM7: b . */
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;

	while (n--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
	}
	return ~c;
}

/* A zip of one entry, `name`, deflated as one stored block */
static int write_zip(const char *path, const char *name, const uint8_t *data, uint16_t n)
{
	uint16_t nl = (uint16_t)strlen(name);
	uint32_t crc = crc32_of(data, n), csize = 5u + n;
	uint8_t lh[30], blk[5], cd[46], eocd[22];
	uint32_t cd_at;
	FILE *f = fopen(path, "wb");

	if (!f) return -1;
	memset(lh, 0, sizeof lh);
	put32(lh, 0x04034b50); put16(lh + 4, 20); put16(lh + 8, 8);
	put32(lh + 14, crc); put32(lh + 18, csize); put32(lh + 22, n); put16(lh + 26, nl);
	blk[0] = 1; put16(blk + 1, n); put16(blk + 3, (uint16_t)~n);
	fwrite(lh, 1, sizeof lh, f); fwrite(name, 1, nl, f);
	fwrite(blk, 1, sizeof blk, f); fwrite(data, 1, n, f);
	cd_at = (uint32_t)(sizeof lh + nl + csize);
	memset(cd, 0, sizeof cd);
	put32(cd, 0x02014b50); put16(cd + 4, 20); put16(cd + 6, 20); put16(cd + 10, 8);
	put32(cd + 16, crc); put32(cd + 20, csize); put32(cd + 24, n); put16(cd + 28, nl);
	fwrite(cd, 1, sizeof cd, f); fwrite(name, 1, nl, f);
	memset(eocd, 0, sizeof eocd);
	put32(eocd, 0x06054b50); put16(eocd + 8, 1); put16(eocd + 10, 1);
	put32(eocd + 12, (uint32_t)(sizeof cd + nl)); put32(eocd + 16, cd_at);
	fwrite(eocd, 1, sizeof eocd, f);
	return fclose(f);
}

/* An unpacked game in .dsperate: a sparse image of `bytes`, its zip beside
 * the folder (the core sweeps an image whose zip is gone), and its tag, last
 * launched `age` seconds ago */
static void cached(const char *dir, const char *stem, unsigned long long bytes, long age)
{
	char p[1024], zip[1024];
	struct timeval tv[2];
	FILE *f;

	PATHF(zip, "%s/%s.zip", dir, stem);
	f = fopen(zip, "wb");
	if (f) { fputs("not read", f); fclose(f); }
	PATHF(p, "%s/.dsperate/%s.nds", dir, stem);
	f = fopen(p, "wb");
	if (!f || ftruncate(fileno(f), (off_t)bytes) != 0) { perror(p); exit(2); }
	fclose(f);
	PATHF(p, "%s/.dsperate/%s.nds.tag", dir, stem);
	f = fopen(p, "wb");
	if (!f) { perror(p); exit(2); }
	fprintf(f, "dsperate zip cache 2\narchive %s\nzip 8 0\nentry %s.nds\ncrc 0 size %llu\n",
	        zip, stem, bytes);
	fclose(f);
	gettimeofday(&tv[0], NULL);
	tv[0].tv_sec -= age;
	tv[1] = tv[0];
	utimes(p, tv);
}

static bool there(const char *dir, const char *rel)
{
	char p[1024];
	struct stat st;

	PATHF(p, "%s/%s", dir, rel);
	return stat(p, &st) == 0;
}

static void expect(bool ok, const char *what)
{
	printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) fails++;
}

int main(int argc, char **argv)
{
	static uint8_t rom[ROM_SIZE];
	char zip[512], cache[512], img[600];
	struct retro_game_info game;
	struct stat st;
	const uint8_t *ram;
	uint32_t count;
	void *h;
	int i;

	if (argc != 3) {
		fprintf(stderr, "usage: %s core.so workdir\n", argv[0]);
		return 2;
	}
	PATHF(sysdir, "%s/system", argv[2]);
	PATHF(savedir, "%s/saves", argv[2]);
	PATHF(cache, "%s/.dsperate", argv[2]);
	mkdir(argv[2], 0755);
	mkdir(sysdir, 0755);   /* empty: FreeBIOS and a generated firmware */
	mkdir(savedir, 0755);
	mkdir(cache, 0755);
	make_rom(rom);
	PATHF(zip, "%s/Count.zip", argv[2]);
	if (write_zip(zip, "Count.nds", rom, ROM_SIZE)) { perror(zip); return 2; }
	cached(argv[2], "Oldest", 1 * GB, 300);
	cached(argv[2], "Older", 1 * GB, 200);
	cached(argv[2], "Recent", 512 * MB, 100);

	h = dlopen(argv[1], RTLD_NOW);
	if (!h) { printf("  FAIL dlopen: %s\n", dlerror()); return 1; }
#define SYM(ret, name, args) ret (*name)args = (ret (*)args)dlsym(h, #name); \
	if (!name) { printf("  FAIL no %s in the core\n", #name); return 1; }
	SYM(void, retro_set_environment, (retro_environment_t))
	SYM(void, retro_set_video_refresh, (retro_video_refresh_t))
	SYM(void, retro_set_audio_sample, (retro_audio_sample_t))
	SYM(void, retro_set_audio_sample_batch, (retro_audio_sample_batch_t))
	SYM(void, retro_set_input_poll, (retro_input_poll_t))
	SYM(void, retro_set_input_state, (retro_input_state_t))
	SYM(void, retro_init, (void))
	SYM(bool, retro_load_game, (const struct retro_game_info *))
	SYM(void, retro_run, (void))
	SYM(void, retro_unload_game, (void))
	SYM(void *, retro_get_memory_data, (unsigned))

	retro_set_environment(env);
	retro_set_video_refresh(video);
	retro_set_audio_sample(audio1);
	retro_set_audio_sample_batch(audio);
	retro_set_input_poll(poll_);
	retro_set_input_state(input);
	retro_init();
	printf("a zipped game in a built DSperate core, beside 2.5 GB of unpacked ones\n");
	memset(&game, 0, sizeof game);
	game.path = zip;
	if (!retro_load_game(&game)) { printf("  FAIL the core did not load the zip\n"); return 1; }

	PATHF(img, "%s/Count.nds", cache);
	expect(stat(img, &st) == 0 && st.st_size == ROM_SIZE, "the zip's copy is in .dsperate, the ROM's size");
	expect(!there(cache, "Oldest.nds") && !there(cache, "Oldest.nds.tag"),
	       "the oldest launch's copy went, with its tag (the cap: 2 GB)");
	expect(there(cache, "Older.nds") && there(cache, "Recent.nds"),
	       "the two after it stayed: 1.5 GB and the new copy fit");
	ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	for (i = 0; i < 60; i++) retro_run();
	count = 0;
	if (ram) memcpy(&count, ram + COUNT_AT, 4);
	{
		char what[96];

		snprintf(what, sizeof what, "the game runs from the copy: 60 frames on, the count is %u", count);
		expect(count == 60, what);
	}
	retro_unload_game();

	printf(fails ? "%d FAILED\n" : "all ok\n", fails);
	return fails != 0;
}

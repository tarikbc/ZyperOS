/* A libretro core that is not an emulator.
 *
 * Exists so Diatom can be developed and tested with no third-party binary
 * present. It implements the API and draws a test pattern, which is enough to
 * exercise every path the frontend has: dlopen and symbol binding, the
 * environment callback, the copy-then-present video path, integer scaling,
 * resampling, input, and pacing.
 *
 * It deliberately reproduces the awkward cases the environment-call inventory
 * (src/env.c) measured in real cores, because those are exactly what a
 * happy-path stub would hide:
 *
 *   - 59.7275 fps, not 60      : no console runs at 60, so pacing must not assume it
 *   - 32040 Hz sample rate     : forces the resampler to actually resample
 *   - SET_GEOMETRY mid-run     : 3 of 6 measured cores change geometry while running
 *   - RGB565                   : what all six chose when offered a choice
 *
 * It can also be told to die, which is the only way to test the crash side of
 * the protocol - `EXIT reason=crash` versus `ERROR code=crash`. That belongs
 * here rather than in Diatom: a frontend with a --crash flag would be test code
 * living in the shipped binary, and the whole point is that Diatom learns about
 * the crash the same way it will in the field, from a signal it did not raise.
 *
 *   STUBCORE_CRASH=segv[@N]   null dereference, at frame N (default 60)
 *   STUBCORE_CRASH=stack[@N]  unbounded recursion - needs the altstack
 *   STUBCORE_CRASH=abort[@N]  abort(), so SIGABRT
 *   STUBCORE_CRASH=fpe[@N]    integer divide by zero - a NO-OP on ARM, which
 *                             does not trap it; kept because it does trap on
 *                             x86 and the difference is worth being able to see
 *   STUBCORE_CRASH=exit[@N]   exit(1), which raises no signal at all
 *   STUBCORE_CRASH=load       die inside retro_load_game, before RUNNING
 *
 * It also exposes a small, entirely predictable block of system RAM, so the
 * achievement path has bytes to watch without an emulator. See sysram below.
 *
 * And a disc tray with two discs in it, handed over from retro_init the way
 * pcsx_rearmed hands over its own - once, for the life of the process - so the
 * frontend's disc control can be tested with no PlayStation core present. It
 * refuses a disc change with the lid shut, as libretro says a core may, so a
 * frontend that forgets to open the tray first is caught here. See tray below.
 *
 * And battery RAM that is not there at load and appears later, as a DS core
 * sizes the save chip of a game its save list does not name from the game's
 * first access to it (dsperate-lr): STUBCORE_LATE_SRAM=N reports 512 bytes of
 * SAVE_RAM from frame N, none before, and writes a pattern into them then.
 *
 * And one core option, stubcore_probe (default "probe-default"), and what it
 * was handed for a game, printed on stderr as it loads - the option's value,
 * the content's path, size and first bytes, and GET_GAME_INFO_EXT's answer -
 * so test/resident.py can see that every resident core keeps its options and
 * that a zipped game arrives as what the zip held. STUBCORE_EXTRACT=1 clears
 * block_extract, so Diatom unzips for this core as for a console core, and
 * STUBCORE_FULLPATH=1 sets need_fullpath, so it is handed a file, not bytes,
 * and reads the file's first bytes from its path while it loads.
 *
 * And a rumble, STUBCORE_RUMBLE=30:on,60:off: the motor on and off on those
 * frames, as an N64 Rumble Pak game under ParallelN64 asks for it (see
 * rumble_configure below; test/rumble.py).
 *
 * And its controller ports: STUBCORE_PORTS=N says it has N (each a joypad,
 * SET_CONTROLLER_INFO), and STUBCORE_DEVICE_LOG=path writes down each
 * device the frontend gives a port, a line each, "port device", so
 * test/player2.py can see the guests' ports given a joypad.
 *
 * And a core as slow as a DS game on the Pixel 2: STUBCORE_FRAME_US=N takes
 * N microseconds in each retro_run (a sleep), and STUBCORE_SKIPPABLE=1 offers a
 * frameskip (SET_AUDIO_BUFFER_STATUS_CALLBACK), as dsperate-lr does, so
 * Diatom runs some frames unshown. An unshown frame costs as much as a shown
 * one, as a DS game's does: its drawing is on threads of its own
 * (test/fastforward.py). STUBCORE_FRAME_US_AT=frame:us,... changes the
 * cost from those frames on, and STUBCORE_SHOWN_LOG=path writes which frames
 * the frontend had drawn, a 1 or a 0 each, and when each started
 * (test/lateskip.py).
 *
 * And a core that draws less often than it runs, the two ways real cores do
 * (test/hudspeed.py): STUBCORE_DUPE=N hands over a new picture on every Nth
 * retro_run and a repeat (a NULL frame) on the rest, as a core that runs 60
 * frames a second and draws 30 does; STUBCORE_SPAN=N makes each retro_run N
 * frames long - N frames of sound and one picture - as PPSSPP's, Flycast's
 * and ParallelN64's do when a game shows a new picture every Nth vblank. The
 * time such a call takes is STUBCORE_FRAME_US's, so a span of 2 at 33500 is a
 * 30 fps game at full speed; with STUBCORE_PRECISE=1 it is that to the
 * microsecond, slept and then spun, where a sleep alone overshoots.
 *
 * And a picture of its own in place of the pattern: STUBCORE_IMAGE=path, a
 * BASE_W x BASE_H RGB565 frame in the host's byte order (what
 * tools/shaderdemo.c --rgb565 writes), drawn on every frame with the
 * geometry held at the base, so a filter's output on a device can be set
 * beside what the launcher's Shader pictures say it is (tools/shaderdemo-device.sh).
 *
 * And on Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK): a frame's
 * cost, its overshoot and the shown log's times are on that clock, not the
 * wall's, so a check of the pacing is the same on any machine.
 *
 * Test fixture. Not part of Diatom's runtime.
 */
#define _GNU_SOURCE           /* RTLD_DEFAULT, on glibc */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "libretro.h"

#define BASE_W 256
#define BASE_H 224
#define WIDE_W 512          /* the "hires" mode, as SNES and PC Engine do */
#define MAX_W  512
#define MAX_H  448
#define FPS    59.7275
#define RATE   32040.0
/* 32040/59.7275 = 536.44, so 1024 is ample headroom, and for a run of up to
 * four frames (STUBCORE_SPAN) four times that. */
#define AUDIO_MAX_FRAMES 4096

static retro_environment_t   env;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_cb;
static retro_input_poll_t    poll_cb;
static retro_input_state_t   input_cb;

static uint16_t fb[MAX_W * MAX_H];
static int      cur_w = BASE_W, cur_h = BASE_H;
static unsigned frame;
static uint8_t  late_sram[512];
static long     late_at = -1;          /* STUBCORE_LATE_SRAM, or -1 */
static int      box_x = 96, box_y = 80;
static double   phase;
static double   audio_accum;
static void     rumble_configure(void);   /* STUBCORE_RUMBLE, below */
static uint16_t image[BASE_W * BASE_H];  /* STUBCORE_IMAGE's frame */
static bool     image_on;

static struct retro_core_option_v2_definition probe_defs[] = {
	{ "stubcore_probe", "Probe", NULL, "Read back as each game loads", NULL, NULL,
	  { { "probe-default", NULL }, { "probe-other", NULL }, { NULL, NULL } },
	  "probe-default" },
	{ NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL },
};
static struct retro_core_options_v2 probe_opts = { NULL, probe_defs };
/* STUBCORE_MGBA: the probe and mGBA's two Super Game Boy options too, as
 * libretro/mgba e31759b declares them (values and defaults), for
 * test/sgb.py, which reads what each game loaded with */
static struct retro_core_option_v2_definition mgba_defs[] = {
	{ "stubcore_probe", "Probe", NULL, "Read back as each game loads", NULL, NULL,
	  { { "probe-default", NULL }, { "probe-other", NULL }, { NULL, NULL } },
	  "probe-default" },
	{ "mgba_gb_model", "Game Boy Model (Restart)", NULL, NULL, NULL, NULL,
	  { { "Autodetect", NULL }, { "Game Boy", NULL }, { "Super Game Boy", NULL },
	    { "Game Boy Color", NULL }, { "Super Game Boy Color", NULL }, { "Game Boy Advance", NULL },
	    { NULL, NULL } },
	  "Autodetect" },
	{ "mgba_sgb_borders", "Use Super Game Boy Borders (Restart)", NULL, NULL, NULL, NULL,
	  { { "ON", "enabled" }, { "OFF", "disabled" }, { NULL, NULL } },
	  "ON" },
	{ NULL, NULL, NULL, NULL, NULL, NULL, { { NULL, NULL } }, NULL },
};
static struct retro_core_options_v2 mgba_opts = { NULL, mgba_defs };
/* STUBCORE_MESSAGE "F:text": the core says `text` on frame F with
 * RETRO_ENVIRONMENT_SET_MESSAGE, as FCEUmm says what an FDS disk did */
static long message_at = -1;
static char message_text[128];

void retro_set_environment(retro_environment_t cb)
{
	static const struct retro_controller_description joypad[] = { { "Joypad", RETRO_DEVICE_JOYPAD } };
	static struct retro_controller_info ports[5];
	bool no_game = true;
	const char *np = getenv("STUBCORE_PORTS");

	env = cb;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
	/* As real cores declare theirs, from inside this call */
	cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, getenv("STUBCORE_MGBA") ? &mgba_opts : &probe_opts);
	if (np && *np) {
		int n = atoi(np);

		for (int i = 0; i < 4; i++) {
			ports[i].types = i < n ? joypad : NULL;
			ports[i].num_types = i < n ? 1 : 0;
		}
		cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, ports);
	}
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb)       { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb)     { input_cb = cb; }

/* The tray. Two discs, nothing in them; what is tested is the frontend's
 * handling of the interface, not anything a disc holds. */
static bool     tray_open;
static unsigned tray_disc;

static bool tray_set_eject(bool open)   { tray_open = open; return true; }
static bool tray_get_eject(void)        { return tray_open; }
static unsigned tray_get_index(void)    { return tray_disc; }
static unsigned tray_count(void)        { return 2; }
static bool tray_set_index(unsigned i)
{
	if (!tray_open || i >= 2) return false;
	tray_disc = i;
	return true;
}
static bool tray_replace(unsigned i, const struct retro_game_info *g)
{ (void)i; (void)g; return false; }
static bool tray_add(void)              { return false; }
static bool tray_initial(unsigned i, const char *path)
{ (void)i; (void)path; return false; }
static bool tray_path(unsigned i, char *out, size_t n)
{ (void)i; (void)out; (void)n; return false; }
static bool tray_label(unsigned i, char *out, size_t n)
{
	if (i >= 2 || !n) return false;
	snprintf(out, n, "stub disc %u", i + 1);
	return true;
}

static struct retro_disk_control_ext_callback tray = {
	tray_set_eject, tray_get_eject, tray_get_index, tray_set_index,
	tray_count, tray_replace, tray_add, tray_initial, tray_path, tray_label,
};

/* How many games this library has loaded since it was opened, written to
 * STUBCORE_INIT_LOG at each load: 1 every time for a core Diatom opens afresh
 * for each game (core.c, one_game_per_load), counting up for one it keeps
 * open, as a static a real core forgets to reset carries over. STUBCORE_NAME
 * names the library as a real core does, which is what that rule goes by;
 * STUBCORE_VERSION gives it a version, which a save state is checked
 * against (save.c, test/statereads.py). */
static int loads;

void retro_init(void)
{
	unsigned v = 0;

	frame = 0;
	/* From retro_init and from nowhere else, as pcsx_rearmed does it: a
	 * resident core never says this again, which is the case worth testing. */
	if (env && env(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &v) && v >= 1)
		env(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &tray);
}
void retro_deinit(void) { }
unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof *info);
	info->library_name     = getenv("STUBCORE_NAME") ? getenv("STUBCORE_NAME") : "diatom-stub";
	info->library_version  = getenv("STUBCORE_VERSION") ? getenv("STUBCORE_VERSION") : "1";
	info->valid_extensions = "stub";
	info->need_fullpath    = getenv("STUBCORE_FULLPATH") != NULL;
	info->block_extract    = !getenv("STUBCORE_EXTRACT");
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	memset(info, 0, sizeof *info);
	info->geometry.base_width   = cur_w;
	info->geometry.base_height  = cur_h;
	info->geometry.max_width    = MAX_W;
	info->geometry.max_height   = MAX_H;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps            = FPS;
	info->timing.sample_rate    = RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
	const char *path = getenv("STUBCORE_DEVICE_LOG");
	FILE *f = path && *path ? fopen(path, "a") : NULL;

	if (!f) return;
	fprintf(f, "%u %u\n", port, device);
	fclose(f);
}

/* Deliberate faults. `volatile` throughout and no inlining, because -O2 is
 * entitled to assume undefined behavior never happens: an unguarded null store
 * can be deleted outright, and a self-call with no side effect can be turned
 * into a loop that never grows the stack. Both were observed while writing
 * this - the segv mode compiled to nothing at all. */
static volatile int *const null_ptr = (volatile int *)0;

/* Somewhere for a frame address to go, so the frame cannot be discarded. */
static volatile char *frame_sink;

__attribute__((noinline)) static int recurse(volatile int depth)
{
	volatile char eat[512];
	int r;

	eat[0] = (char)depth;
	/* The bound is for the compiler, not for correctness: without it this is
	 * provably infinite and -Winfinite-recursion fires, and a build that warns
	 * on purpose is a build whose warnings get ignored. 2^30 frames of 512
	 * bytes is half a terabyte of stack, so the guard never runs. */
	if (depth > (1 << 30)) return eat[0];

	r = recurse(depth + 1);
	/* Touching the frame AFTER the call is what makes this recursion. `volatile`
	 * and `noinline` are not enough on their own: aarch64-linux-gnu-gcc at -O2
	 * turned the obvious version into `add sp, sp, #0x210; b recurse` - a
	 * sibling call that pops its frame before branching, so the stack never
	 * grew and the fixture quietly tested nothing. It still crashed on macOS,
	 * which is how the disagreement surfaced. Taking the frame's address here
	 * means the frame must outlive the call, and a tail call becomes illegal. */
	frame_sink = &eat[0];
	return r + eat[0];
}

static void die(const char *how)
{
	if (!strcmp(how, "segv"))  { *null_ptr = 1; }
	if (!strcmp(how, "stack")) { recurse(0); }
	if (!strcmp(how, "abort")) { abort(); }
	if (!strcmp(how, "exit"))  { exit(1); }
	if (!strcmp(how, "fpe"))   { volatile int z = 0; volatile int r = 1 / z; (void)r; }
}

/* `mode` is the word before any '@', `at` the frame after it. */
static char     crash_mode[16];
static unsigned crash_at = 60;

static void crash_configure(void)
{
	const char *e = getenv("STUBCORE_CRASH");
	const char *at;
	size_t n;

	if (!e || !*e) return;
	at = strchr(e, '@');
	n  = at ? (size_t)(at - e) : strlen(e);
	if (n >= sizeof crash_mode) n = sizeof crash_mode - 1;
	memcpy(crash_mode, e, n);
	crash_mode[n] = '\0';
	if (at) crash_at = (unsigned)strtoul(at + 1, NULL, 10);
}

/* Deliberately partial and deliberately not in enum order: real cores describe
 * only the buttons a game uses, so a frontend that assumed a full array or an
 * ordered one would pass here and fail on hardware. */
static const struct retro_input_descriptor descriptors[] = {
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Fire" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Jump" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Pause" },
	{ 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "P2 Fire" },
	{ 0 }
};

/* What this load was handed, for test/resident.py */
static void report_load(const struct retro_game_info *game)
{
	struct retro_variable v = { "stubcore_probe", NULL };
	const struct retro_game_info_ext *x = NULL;

	if (!env || !env(RETRO_ENVIRONMENT_GET_VARIABLE, &v)) v.value = NULL;
	fprintf(stderr, "stubcore: probe=%s\n", v.value ? v.value : "(none)");
	if (getenv("STUBCORE_MGBA")) {
		struct retro_variable m = { "mgba_gb_model", NULL }, b = { "mgba_sgb_borders", NULL };

		if (!env || !env(RETRO_ENVIRONMENT_GET_VARIABLE, &m)) m.value = NULL;
		if (!env || !env(RETRO_ENVIRONMENT_GET_VARIABLE, &b)) b.value = NULL;
		fprintf(stderr, "stubcore: model=%s borders=%s\n", m.value ? m.value : "(none)",
		        b.value ? b.value : "(none)");
	}
	if (game)
		fprintf(stderr, "stubcore: path=%s size=%zu head=%.4s\n",
		        game->path ? game->path : "(none)", game->size,
		        game->data && game->size >= 4 ? (const char *)game->data : "");
	/* Handed a file and no bytes (need_fullpath): what the file holds, read
	 * as such a core reads it, from the path while it loads */
	if (game && game->path && !game->data) {
		FILE *f = fopen(game->path, "rb");
		char head[5] = "";

		if (f) {
			head[fread(head, 1, 4, f)] = '\0';
			fclose(f);
		}
		fprintf(stderr, "stubcore: file %s head=%s\n", f ? "read" : "missing", head);
	}
	if (env && env(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &x) && x)
		fprintf(stderr, "stubcore: ext full=%s name=%s ext=%s dir=%s archive=%s "
		        "file=%s in_archive=%d size=%zu head=%.4s\n",
		        x->full_path ? x->full_path : "(none)",
		        x->name ? x->name : "(none)", x->ext ? x->ext : "(none)",
		        x->dir ? x->dir : "(none)", x->archive_path ? x->archive_path : "(none)",
		        x->archive_file ? x->archive_file : "(none)", x->file_in_archive,
		        x->size, x->data && x->size >= 4 ? (const char *)x->data : "");
	else
		fprintf(stderr, "stubcore: ext none\n");
}

/* STUBCORE_FRAME_US and STUBCORE_SKIPPABLE: the frame's cost, shown or not.
 * STUBCORE_SHOWN_LOG=path: a line a frame, as it runs: 1 drawn or 0 left
 * undrawn, as the frontend said through the status callback, and when the
 * frame started (CLOCK_MONOTONIC, microseconds), so a check can tell a
 * frame the frontend left out from one this machine was too busy to start
 * on time (test/lateskip.py) */
static long frame_cost_us;
/* STUBCORE_DRAWN_US=N: a drawn frame costs N in place of STUBCORE_FRAME_US,
 * as a DS game's frame costs far more drawn than left undrawn on a slow
 * device, so frameskip=late leaves out two in three at full speed
 * (test/hudspeed.py) */
static long drawn_cost_us;
static bool frame_unshown;
static long overshoot_us;         /* STUBCORE_OVERSHOOT_US: up to this much late, at random */
static bool precise_cost;         /* STUBCORE_PRECISE: the cost to the microsecond */
#define PRECISE_SPIN_US 6000      /* spun, not slept, for the last of it */
static unsigned dupe_every = 1;   /* STUBCORE_DUPE: a new picture on every Nth run */
static unsigned span = 1;         /* STUBCORE_SPAN: frames in one retro_run */
static FILE *shown_log;
static void frame_status(bool active, unsigned occupancy, bool underrun_likely)
{
	(void)active; (void)occupancy;
	frame_unshown = underrun_likely;
}

/* STUBCORE_FRAME_US_AT=frame:us,frame:us,...: the cost from each frame on,
 * in place of STUBCORE_FRAME_US, so a game is slow for a stretch */
static long cost_at_frame[8], cost_at_us[8];
static int cost_at_n;

/* STUBCORE_SOUND_BURST=frame:times,...: that frame's retro_run hands over
 * `times` frames of sound at once, as PPSSPP does when its emulation thread
 * has run on ahead while no picture came (diatom/src/audio.c, HOLD) */
static long burst_frame[8], burst_times[8];
static int burst_n;

/* STUBCORE_OVERSHOOT_US=N: each frame runs 0 to N us late, at random (a
 * fixed sequence), as a sleep does on a runner busy with other work: GitHub's
 * macOS runner kept a 9 ms core for 45 to 50 ms a frame, and not evenly. The checks of a speed
 * (test/fastforward.py, test/hudspeed.py) are tried with it on, to see they
 * judge only what a slow machine can show. */
/* Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK): when it is on,
 * a frame's cost is spent on it, not slept, and the shown log's times are
 * its own; looked up by name in the program that loaded this core, so a
 * frontend without one runs the stub on the wall clock as before */
static void (*tclock_spend)(uint64_t us);
static uint64_t (*tclock_now)(void);

static void tclock_find(void)
{
	tclock_spend = NULL;
	tclock_now = NULL;
	if (!getenv("DIATOM_TEST_CLOCK")) return;
	tclock_spend = (void (*)(uint64_t))dlsym(RTLD_DEFAULT, "diatom_test_clock_spend");
	tclock_now = (uint64_t (*)(void))dlsym(RTLD_DEFAULT, "diatom_port_now_us");
	if (!tclock_spend || !tclock_now) {
		fprintf(stderr, "stubcore: DIATOM_TEST_CLOCK set, but no diatom_test_clock_spend "
		        "in this program: the wall clock\n");
		tclock_spend = NULL;
		tclock_now = NULL;
	}
}

/* `us` of the frame's time: on the test clock, or slept */
static void spend_us(long us)
{
	struct timespec d;

	if (us <= 0) return;
	if (tclock_spend) {
		tclock_spend((uint64_t)us);
		return;
	}
	d.tv_sec = us / 1000000L;
	d.tv_nsec = (us % 1000000L) * 1000L;
	nanosleep(&d, NULL);
}

static void frame_late(void)
{
	static unsigned long long rng = 88172645463325252ull;
	long us;

	static unsigned calls;
	unsigned level;

	if (overshoot_us <= 0) return;
	/* Busy in stretches, as a runner is: every 40 frames the machine is
	 * 0, a third, two thirds or all of N late, by a fixed sequence, and
	 * all of it for the first 80, as a game's start is */
	level = calls < 80 ? 3u : (unsigned)(((calls / 40u) * 2654435761u) >> 16) % 4u;
	calls++;
	rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
	us = (long)(rng % (unsigned long long)(overshoot_us + 1)) * (long)level / 3;
	spend_us(us);
}

static void frame_cost(void)
{
	struct timespec d;
	long cost;

	for (int i = 0; i < cost_at_n; i++)
		if ((long)frame >= cost_at_frame[i]) frame_cost_us = cost_at_us[i];
	cost = drawn_cost_us > 0 && !frame_unshown ? drawn_cost_us : frame_cost_us;
	if (cost <= 0) { frame_late(); return; }
	/* On the test clock, exactly the cost, precise or not */
	if (tclock_spend) {
		spend_us(cost);
		frame_late();
		return;
	}
	/* Slept, not spent: the frontend's pacing sees only the time a frame
	 * takes, and a sleep takes it however busy the machine is (make check
	 * runs many at once), where a busy wait took longer on a busy one */
	if (precise_cost) {
		/* STUBCORE_PRECISE=1: to the microsecond, for a check of a speed
		 * (test/hudspeed.py). A sleep alone overshoots by 4 ms on a Mac, which
		 * made a core of 2 frames at 33.5 ms run at 88% of its speed. Slept
		 * to just short of the end, spun for the rest. */
		struct timespec t0, t;
		long us;

		clock_gettime(CLOCK_MONOTONIC, &t0);
		if (cost > PRECISE_SPIN_US) {
			d.tv_sec = (cost - PRECISE_SPIN_US) / 1000000L;
			d.tv_nsec = ((cost - PRECISE_SPIN_US) % 1000000L) * 1000L;
			nanosleep(&d, NULL);
		}
		do {
			clock_gettime(CLOCK_MONOTONIC, &t);
			us = (t.tv_sec - t0.tv_sec) * 1000000L + (t.tv_nsec - t0.tv_nsec) / 1000L;
		} while (us < cost);
		frame_late();
		return;
	}
	spend_us(cost);
	frame_late();
}

bool retro_load_game(const struct retro_game_info *game)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
	report_load(game);                           /* content is optional here */
	crash_configure();
	{
		const char *e = getenv("STUBCORE_INIT_LOG");
		FILE *f = e ? fopen(e, "a") : NULL;

		if (f) {
			fprintf(f, "loads=%d\n", ++loads);
			fclose(f);
		}
	}
	/* Every game starts on its first disc with the lid shut. */
	tray_disc = 0;
	tray_open = false;
	/* A new game starts at frame zero. The core is resident across games,
	 * so without this the counter carries over and every fixture built on
	 * it - the geometry toggle, the crash frame, sysram - means something
	 * different on the second game than on the first. */
	frame = 0;
	{
		const char *e = getenv("STUBCORE_LATE_SRAM");
		late_at = e && *e ? strtol(e, NULL, 10) : -1;
	}
	rumble_configure();
	{
		const char *e = getenv("STUBCORE_MESSAGE"), *colon = e ? strchr(e, ':') : NULL;

		message_at = colon ? strtol(e, NULL, 10) : -1;
		snprintf(message_text, sizeof message_text, "%s", colon ? colon + 1 : "");
	}
	{
		const char *e = getenv("STUBCORE_IMAGE");
		FILE *f = e && *e ? fopen(e, "rb") : NULL;

		image_on = f && fread(image, sizeof image, 1, f) == 1;
		if (e && *e && !image_on) fprintf(stderr, "stubcore: STUBCORE_IMAGE %s: not a %dx%d RGB565 frame\n", e, BASE_W, BASE_H);
		if (f) fclose(f);
		if (image_on) cur_w = BASE_W;
	}
	/* Offered by every real core and discarded by Diatom until the state
	 * plane. The port-1 entry is here so the "port 0 only" filter has
	 * something to exclude rather than being untested. */
	if (env) env(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descriptors);
	/* Before RUNNING has gone out, so the frontend owes the launcher an ERROR
	 * and must keep its hands off the display. */
	if (!strcmp(crash_mode, "load")) *null_ptr = 1;
	tclock_find();
	{
		const char *e = getenv("STUBCORE_FRAME_US");
		frame_cost_us = e && *e ? strtol(e, NULL, 10) : 0;
		e = getenv("STUBCORE_DRAWN_US");
		drawn_cost_us = e && *e ? strtol(e, NULL, 10) : 0;
		precise_cost = getenv("STUBCORE_PRECISE") != NULL;
		e = getenv("STUBCORE_OVERSHOOT_US");
		overshoot_us = e && *e ? strtol(e, NULL, 10) : 0;
		e = getenv("STUBCORE_DUPE");
		dupe_every = e && *e ? (unsigned)strtoul(e, NULL, 10) : 1;
		if (dupe_every < 1) dupe_every = 1;
		e = getenv("STUBCORE_SPAN");
		span = e && *e ? (unsigned)strtoul(e, NULL, 10) : 1;
		if (span < 1) span = 1;
		if (span > 4) span = 4;
		e = getenv("STUBCORE_SOUND_BURST");
		burst_n = 0;
		while (e && *e && burst_n < 8) {
			char *end;
			burst_frame[burst_n] = strtol(e, &end, 10);
			if (*end != ':') break;
			burst_times[burst_n++] = strtol(end + 1, &end, 10);
			e = *end == ',' ? end + 1 : NULL;
		}
		e = getenv("STUBCORE_FRAME_US_AT");
		cost_at_n = 0;
		while (e && *e && cost_at_n < 8) {
			char *end;
			cost_at_frame[cost_at_n] = strtol(e, &end, 10);
			if (*end != ':') break;
			cost_at_us[cost_at_n++] = strtol(end + 1, &end, 10);
			e = *end == ',' ? end + 1 : NULL;
		}
	}
	{
		const char *e = getenv("STUBCORE_SHOWN_LOG");
		if (shown_log) fclose(shown_log);
		shown_log = e && *e ? fopen(e, "w") : NULL;
	}
	if (getenv("STUBCORE_SKIPPABLE") && env) {
		struct retro_audio_buffer_status_callback cb = { frame_status };
		env(RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK, &cb);
	}
	return env && env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
}
bool retro_load_game_special(unsigned t, const struct retro_game_info *i, size_t n)
{ (void)t; (void)i; (void)n; return false; }
void retro_unload_game(void)
{
	if (shown_log) fclose(shown_log);
	shown_log = NULL;
	frame_unshown = false;
}
unsigned retro_get_region(void) { return RETRO_REGION_PAL; }

/* STUBCORE_STATE_PAD=n: n bytes more in each state after the frame, laid
 * out as a core's memory often is - mostly zeros, a stretch of something
 * every 4 KB - so a state is big enough to be packed (test/states.py). A
 * state handed back with any of them changed, or fewer of them, is refused,
 * as a core refuses a broken state: what the core is given is what it gave. */
static size_t state_pad(void)
{
	const char *e = getenv("STUBCORE_STATE_PAD");
	return e && *e ? (size_t)strtoul(e, NULL, 10) : 0;
}
static uint8_t pad_byte(size_t i) { return (i % 4096) < 512 ? (uint8_t)(i * 31 >> 3) : 0; }

size_t retro_serialize_size(void) { return sizeof frame + state_pad(); }
bool retro_serialize(void *d, size_t n)
{
	size_t pad = state_pad(), i;
	uint8_t *p = d;

	if (n < sizeof frame + pad) return false;
	memcpy(d, &frame, sizeof frame);
	for (i = 0; i < pad; i++) p[sizeof frame + i] = pad_byte(i);
	return true;
}
bool retro_unserialize(const void *d, size_t n)
{
	size_t pad = state_pad(), i;
	const uint8_t *p = d;

	if (n < sizeof frame + pad) return false;
	for (i = 0; i < pad; i++)
		if (p[sizeof frame + i] != pad_byte(i)) return false;
	memcpy(&frame, d, sizeof frame);
	return true;
}

/* A block of "system RAM", so the achievement path has
 * something real to read. Deliberately predictable: an achievement condition
 * is a statement about specific bytes, and a fixture whose bytes move
 * unpredictably proves nothing when one fires.
 *
 *   [0]  the frame number, low byte
 *   [1]  counts up to 10 and stays there
 *   [2]  counts DOWN 10 to 0, so a delta condition has something to compare
 *        against - `0xH0002<d0xH0002` with `0xH0001=10` is true on exactly
 *        one frame, frame 10
 *   [3]  the LEFT button, 1 or 0, so a condition can be driven by hand
 *
 * Only SYSTEM_RAM. Exposing SAVE_RAM would make the frontend start writing
 * .srm files for a core that has nothing to save. */
static uint8_t sysram[256];

static void sysram_tick(void)
{
	sysram[0] = (uint8_t)(frame & 0xff);
	sysram[1] = (uint8_t)(frame < 10 ? frame : 10);
	sysram[2] = (uint8_t)(frame < 10 ? 10 - frame : 0);
	sysram[3] = (uint8_t)(input_cb(0, RETRO_DEVICE_JOYPAD, 0,
	                               RETRO_DEVICE_ID_JOYPAD_LEFT) ? 1 : 0);
}

static bool late_here(void) { return late_at >= 0 && (long)frame >= late_at; }

/* STUBCORE_INPUT_LOG=path: a line a frame, the frame and the pads as the core
 * read them (1 << each RETRO_DEVICE_ID_JOYPAD_*), ports 0 to 3 in order, so a
 * test can say what the game saw - SELECT given late, a chord's buttons never
 * (test/hotkeys.py), the guests' buttons on ports 1 to 3 (test/player2.py). */
static void input_log(void)
{
	static FILE *f;
	static bool tried;
	unsigned id, port, bits[4] = { 0 };

	if (!tried) {
		const char *e = getenv("STUBCORE_INPUT_LOG");

		tried = true;
		if (e && *e) f = fopen(e, "w");
	}
	if (!f) return;
	for (port = 0; port < 4; port++)
		for (id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; id++)
			if (input_cb(port, RETRO_DEVICE_JOYPAD, 0, id)) bits[port] |= 1u << id;
	fprintf(f, "%u %u %u %u %u", (unsigned)frame, bits[0], bits[1], bits[2], bits[3]);
	/* STUBCORE_INPUT_ANALOG=1 adds a sixth column, port 0's left stick x as
	 * the core read it (test/hotkeys.py: the d-pad as the stick). Off by
	 * default so the readers of the other columns stay as they were. */
	if (getenv("STUBCORE_INPUT_ANALOG"))
		fprintf(f, " %d", (int)input_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
		                                RETRO_DEVICE_ID_ANALOG_X));
	fprintf(f, "\n");
	fflush(f);
}

/* STUBCORE_RUMBLE=frame:on,frame:off,...: the motor, asked for as
 * ParallelN64's Rumble Pak asks (mupen64plus-core's libretro input plugin):
 * on is the weak motor to full and then the strong one, off the weak to 0
 * and then the strong, two calls back to back each time. The second call of
 * an off is the one ZyperOS 1.0.4's motor never heard (port/rumble_evdev.c);
 * test/rumble.py reads what the port was told. Frames count from the game's
 * start, and again from 0 after a reset, as `frame` does. */
static struct retro_rumble_interface rumble;
static struct { unsigned at; bool on; } rumble_steps[16];
static int rumble_n;

static void rumble_configure(void)
{
	const char *e = getenv("STUBCORE_RUMBLE");
	char buf[256], *step, *save = NULL;

	rumble_n = 0;
	memset(&rumble, 0, sizeof rumble);
	if (!e || !*e || !env || !env(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) ||
	    !rumble.set_rumble_state)
		return;
	snprintf(buf, sizeof buf, "%s", e);
	for (step = strtok_r(buf, ",", &save); step && rumble_n < 16;
	     step = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(step, ':');

		if (!colon) continue;
		rumble_steps[rumble_n].at = (unsigned)strtoul(step, NULL, 10);
		rumble_steps[rumble_n].on = !strcmp(colon + 1, "on");
		rumble_n++;
	}
}

static void rumble_tick(void)
{
	int i;

	for (i = 0; i < rumble_n; i++) {
		uint16_t v;

		if (rumble_steps[i].at != frame) continue;
		v = rumble_steps[i].on ? 0xffff : 0;
		rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, v);
		rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, v);
	}
}

static void late_tick(void)
{
	size_t i;

	if (late_at < 0 || (long)frame != late_at) return;
	for (i = 0; i < sizeof late_sram; i++) late_sram[i] = (uint8_t)(0x5a ^ i);
}

/* STUBCORE_RTC: a cartridge's clock kept apart from the battery RAM, as
 * Gambatte keeps an MBC3's (RETRO_MEMORY_RTC): a count of the frames run,
 * little-endian, so a frontend that reads it back in at the start and writes
 * it out on the way out carries the count across runs (test/rtc.py) */
static uint8_t rtc_mem[16];

static bool rtc_here(void)
{
	return getenv("STUBCORE_RTC") != NULL;
}

void *retro_get_memory_data(unsigned id)
{
	if (id == RETRO_MEMORY_RTC) return rtc_here() ? rtc_mem : NULL;
	if (id == RETRO_MEMORY_SAVE_RAM) return late_here() ? late_sram : NULL;
	return id == RETRO_MEMORY_SYSTEM_RAM ? sysram : NULL;
}

size_t retro_get_memory_size(unsigned id)
{
	if (id == RETRO_MEMORY_RTC) return rtc_here() ? sizeof rtc_mem : 0;
	if (id == RETRO_MEMORY_SAVE_RAM) return late_here() ? sizeof late_sram : 0;
	return id == RETRO_MEMORY_SYSTEM_RAM ? sizeof sysram : 0;
}
void retro_reset(void) { frame = 0; }
/* Cheats, written down: STUBCORE_CHEAT_LOG=<file> gets "reset" for each
 * retro_cheat_reset and "set <index> <enabled> <code>" for each
 * retro_cheat_set, a line each, with the frame it came on, so
 * test/cheats.py can see when Diatom sends them and that it sends none
 * while frames merely run. Nothing else happens to a code: the stub has no
 * memory to poke. */
static void cheat_log(const char *what, unsigned i, bool e, const char *c)
{
	const char *path = getenv("STUBCORE_CHEAT_LOG");
	FILE *f = path && *path ? fopen(path, "a") : NULL;

	if (!f) return;
	if (c) fprintf(f, "%u %s %u %d %s\n", frame, what, i, e ? 1 : 0, c);
	else   fprintf(f, "%u %s\n", frame, what);
	fclose(f);
}

void retro_cheat_reset(void) { cheat_log("reset", 0, false, NULL); }
void retro_cheat_set(unsigned i, bool e, const char *c)
{
	cheat_log("set", i, e, c ? c : "(null)");
}

static uint16_t rgb565(int r, int g, int b)
{
	return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

void retro_run(void)
{
	int x, y, xs;
	int16_t audio[AUDIO_MAX_FRAMES * 2];
	size_t n, i;
	int16_t held;

	/* Die from inside retro_run, on a frame late enough that RUNNING has long
	 * gone out and the frontend is in its steady loop. Doing it on frame 0
	 * would land during the warmup and prove less. */
	if (crash_mode[0] && frame == crash_at) die(crash_mode);
	if (message_at >= 0 && (long)frame == message_at && env) {
		struct retro_message m = { message_text, 120 };

		env(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
	}
	if (rtc_here()) {
		uint32_t t = (uint32_t)rtc_mem[0] | (uint32_t)rtc_mem[1] << 8 |
		             (uint32_t)rtc_mem[2] << 16 | (uint32_t)rtc_mem[3] << 24;

		t++;
		rtc_mem[0] = (uint8_t)t; rtc_mem[1] = (uint8_t)(t >> 8);
		rtc_mem[2] = (uint8_t)(t >> 16); rtc_mem[3] = (uint8_t)(t >> 24);
	}
	if (shown_log) {
		struct timespec now;
		long long t;

		clock_gettime(CLOCK_MONOTONIC, &now);
		t = tclock_now ? (long long)tclock_now()
		               : (long long)now.tv_sec * 1000000LL + now.tv_nsec / 1000;
		fprintf(shown_log, "%d %lld\n", frame_unshown ? 0 : 1, t);
		fflush(shown_log);
	}
	frame_cost();

	/* One frame's worth of audio, with the fraction carried.
	 *
	 * 32040/59.7275 is 536.44 frames per video frame. An earlier version of this
	 * clamped to 512 to fit a smaller buffer, which under-produced by 4.6% -
	 * nine times what rate control can correct - and starved the frontend's audio
	 * queue. The frontend was behaving correctly on a starved input; the fixture
	 * was lying. A test core that produces the wrong amount of audio tests
	 * nothing useful. */
	audio_accum += RATE / FPS * span;
	n = (size_t)audio_accum;
	audio_accum -= (double)n;
	if (n > AUDIO_MAX_FRAMES) n = AUDIO_MAX_FRAMES;   /* cannot happen; guard anyway */

	/* STUBCORE_SHUTDOWN_AT=N: the game's own Quit on frame N, as PrBoom's
	 * Quit Game is: RETRO_ENVIRONMENT_SHUTDOWN, and from then on no frame */
	{
		const char *e = getenv("STUBCORE_SHUTDOWN_AT");

		if (e && frame >= strtoul(e, NULL, 10)) {
			if (frame == strtoul(e, NULL, 10)) env(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
			frame++;
			return;
		}
	}

	poll_cb();
	sysram_tick();
	late_tick();
	input_log();
	rumble_tick();

	/* A box you can drive, so input is verifiable by looking at it. */
	held = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
	if (held) box_x -= 2;
	if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT)) box_x += 2;
	if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))    box_y -= 2;
	if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))  box_y += 2;

	/* Toggle between base and hires every 5 seconds, the way a real core does
	 * when a game opens a menu. This is the path 3 of 6 measured cores take and
	 * the one most likely to be broken by an implementation that computes the
	 * destination rect once at load. */
	if (frame && frame % 300 == 0 && !image_on) {
		struct retro_game_geometry g;
		cur_w = (cur_w == BASE_W) ? WIDE_W : BASE_W;
		memset(&g, 0, sizeof g);
		g.base_width   = cur_w;
		g.base_height  = cur_h;
		g.max_width    = MAX_W;
		g.max_height   = MAX_H;
		g.aspect_ratio = 4.0f / 3.0f;
		env(RETRO_ENVIRONMENT_SET_GEOMETRY, &g);
	}

	/* Draw in DISPLAY units, not source pixels.
	 *
	 * Hires pixels are physically half-width, so real content in a 512-wide mode
	 * uses 512-wide coordinates and looks the same size as it did at 256 - just
	 * with finer detail. A fixture that draws a 32-source-pixel box in both
	 * modes renders correctly and demonstrates nothing, because the box really
	 * is half as wide in hires. Scaling x by this factor is what makes the
	 * comparison honest. */
	xs = cur_w / BASE_W;                 /* 1 at base, 2 at hires */

	if (box_x < 0) box_x = 0;
	if (box_y < 0) box_y = 0;
	if (box_x > BASE_W - 32) box_x = BASE_W - 32;
	if (box_y > cur_h - 32) box_y = cur_h - 32;

	for (y = 0; y < cur_h; y++) {
		for (x = 0; x < cur_w; x++) {
			int v = ((x / xs + frame) / 16 + (y / 16)) & 1;
			fb[y * cur_w + x] = v ? rgb565(24, 24, 32) : rgb565(48, 52, 64);
		}
	}

	/* Detail that only resolves in hires: vertical lines one SOURCE pixel wide.
	 * At base these are 3 screen pixels apart, at hires 1.5 - which is the whole
	 * point of the mode, and the thing a correct implementation should show. */
	for (y = 24; y < 72 && y < cur_h; y++)
		for (x = 24 * xs; x < 120 * xs && x < cur_w; x += 2)
			fb[y * cur_w + x] = rgb565(200, 200, 210);
	/* Single-pixel border, so integer scaling is checkable by eye: at 3x each
	 * edge should be exactly three screen pixels, with no smearing. */
	for (x = 0; x < cur_w; x++) {
		fb[x] = rgb565(255, 255, 255);
		fb[(cur_h - 1) * cur_w + x] = rgb565(255, 255, 255);
	}
	for (y = 0; y < cur_h; y++) {
		fb[y * cur_w] = rgb565(255, 255, 255);
		fb[y * cur_w + cur_w - 1] = rgb565(255, 255, 255);
	}
	for (y = box_y; y < box_y + 32 && y < cur_h; y++)
		for (x = box_x * xs; x < (box_x + 32) * xs && x < cur_w; x++)
			fb[y * cur_w + x] = rgb565(220, 90, 40);

	if (image_on) memcpy(fb, image, sizeof image);
	/* A repeat is a NULL frame, as libretro has a core say it drew nothing new */
	if (frame % dupe_every)
		video_cb(NULL, (unsigned)cur_w, (unsigned)cur_h, (size_t)cur_w * sizeof(uint16_t));
	else
		video_cb(fb, (unsigned)cur_w, (unsigned)cur_h, (size_t)cur_w * sizeof(uint16_t));

	for (i = 0; i < n; i++) {
		int16_t s = (int16_t)(sin(phase) * 2200.0);
		audio[i * 2] = audio[i * 2 + 1] = s;
		phase += 2.0 * M_PI * 440.0 / RATE;
		if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
	}
	{
		long times = 1;
		int b;

		for (b = 0; b < burst_n; b++)
			if ((long)frame == burst_frame[b]) times = burst_times[b];
		while (times-- > 0) audio_cb(audio, n);
	}

	frame++;
}

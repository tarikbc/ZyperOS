/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PLATFORM_H
#define ZYPEROS_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>
#include <time.h>

#include "sidelights.h"
#include "startfail.h"   /* plat_start_error: why a game never started, with no SDL */
#include "device/profile.h"   /* what the handheld is, and the answers it gives */

/* Everything that knows which device it is running on sits behind this file:
 * the display, the buttons, the panel backlight, the codec, the battery, and
 * the pipe to the resident emulator. The rest of ZyperOS talks to this file and
 * to SDL, and to nothing else.
 *
 * Behind it: platform.c and resident.c, which every device shares, and one
 * device file, src/device/<name>.c, chosen by DEVICE= at build time (the
 * Brick's is brick.c). device.h is the seam between those two halves. */

#define ZYPEROS_SCREEN_W 1024
#define ZYPEROS_SCREEN_H 768

/* THE FORMAT OF EVERY TEXTURE ZYPEROS DRAWS INTO (SDL_TEXTUREACCESS_TARGET):
 * the screen itself on the Pixel 2, the menus' panel pass, the keyboard's and
 * the tour's. ABGR8888, never ARGB8888 (2026-11-11).
 *
 * On the Pixel 2 every video snap came out with red and blue swapped - FireRed's
 * Charmander blue - while covers were right. SDL's GLES2 renderer (2.32.10)
 * keeps an ARGB8888 target with red and blue changed over in the GPU's
 * texture: it swaps every draw color and picks a swapping shader for every
 * RGB texture drawn into one, and swaps back when the target is drawn out.
 * But a YUV texture's shader writes plain RGB into any target, with no swap
 * (SDL_render_gles2.c, SetCopyState), so a snap drawn into the ARGB8888 screen
 * came out swapped on the panel. In an ABGR8888 target, GLES2's own byte
 * order, nothing is swapped, so YUV and RGB textures both come out right;
 * read back with the device's SDL and Mesa, a BT.601 red drew #0000fe in an
 * ARGB8888 target and #fe0000 in an ABGR8888 one (tools/yuvtarget-check.c),
 * and in QEMU (docs/qemu.md, the same SDL) a color-bars snap on the shelf had
 * its red and blue bars, and its yellow and cyan, changed over, and has not
 * since.
 * MP4's player was right all along: its picture goes under the screen,
 * straight onto the panel (plat_video_under), in no target at all.
 *
 * The Mac's Metal renderer and the Brick's OpenGL one hold both formats as
 * they are, so there it changes nothing. tools/targets-check.py holds every
 * SDL_TEXTUREACCESS_TARGET texture in src/ to this. */
#define PLAT_TARGET_FORMAT SDL_PIXELFORMAT_ABGR8888

/* launch.sh polls for this and powers the device down when it appears. */
#define ZYPEROS_POWEROFF_FLAG "/tmp/zyperos_poweroff"
/* And restarts it with this one: System Update's Restart Now, which the next
 * start's switch (docs/updates.md) is waiting for. */
#define ZYPEROS_REBOOT_FLAG "/tmp/zyperos_reboot"
/* Where one of those flags is for this launcher: the path as it is on a card;
 * on a computer, in $ZYPEROS_FLAG_DIR when that is set. Every launcher on a
 * computer shares /tmp, a shot of the power-off writes its flag there, and a
 * desk mode window (tools/desk) took another's for its own and quit after a
 * game: tools/desk gives each window a folder of its own. */
const char *plat_flag(const char *path);

/* Overridable at runtime so the launcher can be pointed at a test tree. */
extern const char *P_ROOT;     /* /mnt/SDCARD/ZyperOS      */
extern const char *P_CARD;     /* /mnt/SDCARD             */
extern const char *P_ROMS;     /* /mnt/SDCARD/Roms        */
extern const char *P_USERDATA; /* /mnt/SDCARD/.userdata/brick or pixel2 */
extern const char *P_SHARED;   /* /mnt/SDCARD/.userdata/shared */
extern const char *P_WEB;      /* /mnt/SDCARD/ZyperOS/res/web - Hare's page */
extern const char *P_FONT;     /* the UI typeface          */
void paths_init(void);

#include "inbutton.h"   /* in_button: the buttons, with no SDL */

typedef struct {
	bool   down[IN_COUNT];
	bool   pressed[IN_COUNT];   /* this frame only */
	Uint32 down_since[IN_COUNT];
	Uint32 last_repeat[IN_COUNT];
	bool   quit_requested;
	/* POWER went down this frame, whatever MENU > Power Button says:
	 * pressed[IN_POWER] is the turn-off, a press or a hold done
	 * (src/powerhold.h), and this the key itself, for the charging screen,
	 * where a press starts the handheld as an off one's would */
	bool   power_tap;
} in_state;

bool plat_video_init(void);
void plat_video_quit(void);
SDL_Renderer *plat_renderer(void);
unsigned plat_now_ms(void);

/* The screen, as ZyperOS draws it: always ZYPEROS_SCREEN_W x ZYPEROS_SCREEN_H,
 * landscape. Every frame is shown with plat_present, and drawing goes back to
 * the screen from an offscreen texture with plat_draw_to_screen - never
 * SDL_RenderPresent or SDL_SetRenderTarget(r, NULL) directly. On the Brick
 * the screen is the window and those two are exactly the SDL calls. A device
 * whose panel is another size or turned draws into a texture of this size
 * instead, and fits it to the panel at present. */
void plat_present(void);
void plat_draw_to_screen(void);
/* ZyperOS's own screens' shader (src/screenfx.h, DESIGN.md 11.50): Diatom's
 * name for it ("crt", "crt-curved", "scanlines", "lcd"; anything else is
 * Off) and its strength, 10 to 100, on every frame plat_present shows from
 * now on, at the panel's own pixels. The launcher asks for Off while a game
 * is up: what it draws then is over the game. Off draws the frame as it was,
 * with no pass of its own; an old screen where plat_gpu_shaders says no is
 * Off. Defined in src/screenfx_draw.c, for every device. */
void plat_screen_shader(const char *name, int strength);
/* That shader on the frame drawn so far, now, for a shot that reads the
 * screen back before plat_present: where the shader goes on as the frame
 * goes to the panel (the Pixel 2), nothing. Once a frame: the present after
 * it does not put it on again. */
void plat_screen_finish(void);
/* The shader is put on the frame itself, in the window it is drawn in (the
 * host's file): a frame drawn over the last one's picture, as a focus frame
 * is (src/shelf_draw.c), would put it on twice, so none is, while this is
 * true. False where it goes on as the frame goes to the panel (the Pixel 2). */
bool plat_screen_shader_in_frame(void);
/* A video picture UNDER the screen, for the next plat_present only: MP4's
 * player, called at the start of a frame in place of clearing it. The frame
 * is `bg` everywhere, then the `src` part of `tex` (which may be NULL; `src`
 * NULL for all of it) in `dst` - in screen coordinates - and then everything
 * drawn after this call, on top, its alpha kept. Where the device scales the
 * screen to its panel, the picture is drawn at the panel's own resolution
 * rather than through the screen's 1024x768: on the Pixel 2 a 640x360 video
 * lands on the 640x480 panel pixel for pixel, instead of scaled up and back
 * down. */
void plat_video_under(SDL_Texture *tex, const SDL_Rect *src, const SDL_Rect *dst, SDL_Color bg);
/* Shows the screen as it is drawn and also writes it to path as the panel
 * shows it, raw XRGB8888 at the panel's own size and turn: a still for
 * plastron's splash --still to hold while a port loads (launch_port). False
 * where there is nothing to hold it with. */
bool plat_save_screen(const char *path);
/* The same file, with nothing shown: the panel keeps what it had */
bool plat_save_screen_hidden(const char *path);
/* plat_save_screen's opposite: a picture of the panel as the panel shows it,
 * src/portmenu.h's still (a port's frame, saved by portkeys under the in-game
 * menu), turned back into the landscape this process draws in, as a texture
 * to draw at the screen's size. NULL when there is none, it is not the
 * panel's size, or the device has no such pictures. */
SDL_Texture *plat_load_screen(const char *path);

bool plat_input_init(void);
/* The motor once, at `scale` (0 to 1, rumble_run_pct's share over 100) for
 * `ms` milliseconds, then still by itself: Rumble's step, felt as it is
 * chosen (src/rumble.h). True when it buzzed, or `scale` is 0 (Off); false
 * where the handheld has no motor it can run (plat_has_rumble). */
bool plat_rumble_buzz(float scale, int ms);
void plat_input_quit(void);
void plat_input_poll(in_state *st);
void plat_input_flush(void);
/* The shelf's rest when it has nothing to draw: until a button goes down or
 * up, or `ms` have passed, whichever is first. It slept a fixed 16 ms, so a
 * press waited up to that long to be seen, and on the Pixel 2 every press on
 * the shelf reached the panel two blanks later (33 ms). With no button
 * devices to wait on (a desktop), it sleeps `ms`. */
void plat_input_wait(unsigned ms);
/* Work that goes on behind every screen while it rests: each device's
 * plat_input_wait calls the hook with the wait it was asked for, and the
 * hook does its step and returns how long the wait may be (`ms`, or less to
 * be back sooner). The downloads' lanes (src/dlq.h, dlq_wait); none set,
 * the wait is as asked. */
void plat_on_input_wait(unsigned (*hook)(unsigned ms));
unsigned plat_input_wait_hook(unsigned ms);
/* Once, just before the shelf's first frame: throw away whatever input the
 * boot produced, so nothing pressed while the device came up is honored, and
 * leave `st` empty. How long that takes depends on the device - on one whose
 * input arrives in bursts as it starts, settling means waiting them out. */
void plat_input_settle(in_state *st);
bool in_repeat(in_state *st, in_button b);
/* A pair of opposite buttons, `neg` and `pos` (Up and Down, Left and Right),
 * each read once this frame by in_repeat: -1 for `neg`'s press or repeat,
 * +1 for `pos`'s (both: `pos`), 0 for neither. A repeat is true once: a
 * second in_repeat of the same button in a frame is false, so a held
 * direction read twice turns round */
int in_repeat_axis(in_state *st, in_button neg, in_button pos);

/* Tell the input layer the process is going away. The next plat_input_poll
 * raises quit_requested, which every loop already checks. */
void plat_terminate(void);

/* Run a child to completion, watching the power button while it runs.
 * envkv is a NULL-terminated array of "KEY=value" strings added to this
 * process's own environment, or NULL for that environment alone. */
int  plat_run(char *const argv[], const char *const envkv[], const char *workdir);
bool plat_run_power_pressed(void);
/* While plat_run waits for its child: `fn` every 100 ms (NULL none), the
 * launcher's slice of a game run the slow way; and plat_run_stop ends the
 * child as a power press would, TERM then KILL, without counting as one */
void plat_run_on_tick(void (*fn)(void));
void plat_run_stop(void);
/* Say that something equivalent to a power press has happened, for the paths
 * the evdev watchdog cannot see: a screen the launcher is drawing over a
 * paused game, where plat_resident_wait is not running and so nothing is
 * reading the power key. Auto Off expiring in the in-game menu is exactly
 * that. Cleared with the flag, at the start of the next game. */
void plat_note_power_pressed(void);

/* Start a child and forget it: it outlives this process and leaves no zombie
 * behind. This is how a resident emulator that has died gets started again.
 * The _pid form returns the program's pid once it is running, or -1 when it
 * could not be started; the other, whether it was. */
bool plat_spawn_detached(char *const argv[], const char *const envkv[],
                         const char *workdir);
long plat_spawn_detached_pid(char *const argv[], const char *const envkv[],
                             const char *workdir);

/* The resident emulator. plat_resident_send() hands over a game and returns
 * at once - the launcher draws nothing over the load, because it and the
 * emulator presenting together wedges the display (see launch() in main.c),
 * and a warm launch leaves nothing to cover anyway; plat_resident_wait()
 * blocks until the game is over - or, on the Diatom transport, until the
 * player opens the in-game menu, which the launcher draws (the emulator hands
 * the display over rather than drawing its own).
 *
 * One transport: Diatom's socket protocol, where peer death is EOF, the stop
 * signal is a message, and the reply is a line saying what actually happened.
 * ZYPEROS_DIATOM_SOCKET overrides the path for tests. */
#define RES_DEAD   0   /* emulator missing, dead, or the game never started */
#define RES_EXIT   1   /* the game ran and is over */
#define RES_PAUSED 2   /* Diatom only: menu open, the launcher owns the display */
const char *plat_resident_socket(void);
/* The resident's pid file, /tmp/diatom.pid (ZYPEROS_DIATOM_PIDFILE for a
 * check): launch.sh writes it when it starts a resident, and the launcher
 * when it starts one again after a crash (plat_resident_note_pid, written
 * whole under another name and renamed over), so launch.sh, which reads it
 * to find the running resident, never starts a second one beside it. */
const char *plat_resident_pidfile(void);
bool plat_resident_note_pid(long pid);
bool plat_resident_ready(void);
/* plat_resident_ready, waiting up to max_ms for a Diatom that is running but
 * not answering yet (still starting); false at once when none is running. */
bool plat_resident_await(unsigned max_ms, unsigned *waited_ms);
/* What one game is run with (plat_resident_send). Paths "" or NULL for none.
 * `console` is a RetroAchievements console id and `cheevos` a set file for
 * Diatom to watch; 0 and NULL mean the game has no achievements, which is the
 * ordinary case. */
typedef struct {
	const char *tag, *core, *rom;
	const char *resume;       /* the state to resume; "" starts it afresh */
	const char *exit_state;   /* where the state goes at exit */
	const char *preview;      /* where the frame goes before a pause and at exit */
	const char *card;         /* the frame its card shows (src/cardart.h): written
	                           * once, a minute into play, if not there yet */
	int         console;
	const char *cheevos;
} plat_game_run;
bool plat_resident_send(const plat_game_run *run);
int  plat_resident_wait(void);
/* Whether the game last sent has said RUNNING: its first frame is up */
bool plat_resident_started(void);

/* Why the game last sent never started, when Diatom says: its ERROR before
 * RUNNING, a core that would not load (plat_start_error, src/startfail.h).
 * Taken once: false when the last start gave no such error, or when it was
 * taken already. */
bool plat_resident_start_error(plat_start_error *out);

/* Called from inside plat_resident_wait when Diatom reports an achievement
 * unlocked. A callback rather than a queue because the wait blocks: anything
 * buffered would have to be sized against a play session, and this has nothing
 * to size. NULL to stop. */
void plat_resident_on_unlock(void (*fn)(int id));

/* Called from inside plat_resident_wait when Diatom reports its speed step,
 * in halves (2 is 1x, 3 is 1.5x, up to 6, 3x): on every SELECT+R2 and
 * SELECT+L2, and again when a game resumes fast. A callback for the unlock's
 * reason. NULL to stop. */
void plat_resident_on_speed(void (*fn)(int half));

/* Called from inside plat_resident_wait when R2 turned a PSP game's d-pad
 * into the left stick (true) or back (false): RUN stick=toggle, Diatom's
 * STICK line. Not for the answer to a SETSTICK sent by
 * plat_resident_set_stick. NULL to stop. */
void plat_resident_on_stick(void (*fn)(bool on));
/* Called from inside plat_resident_wait as L1 or R1 moves a Famicom Disk
 * System game's disk (Diatom's DISK line, diatom/src/fdsdisk.h): `state`
 * "selected" (L1 turned it), "in", "out" (R1) or "eject-first" (L1 with the
 * disk in, which turns nothing), the disk 1 to 4 and its side 'A' or 'B'
 * (0 and 0 for eject-first). NULL to stop. */
void plat_resident_on_disk(void (*fn)(const char *state, int disk, char side));
/* SETSTICK mode=..., the d-pad as the stick at once (the Controls tab's
 * row), counted so that its STICK answer reaches no on_stick callback */
bool plat_resident_set_stick(const char *mode);

/* Called from inside plat_resident_wait for the quick slot: Diatom's SELECT+R1
 * and SELECT+L1 (PLAT_QUICK_ASK_*, no path), and the answers to a SAVE or
 * LOAD sent while the game runs, with the state's path: SAVED and LOADED, or
 * their ERRORs. The menu's own SAVE and LOAD are answered the same way, so a
 * callback acts on a path it asked for. NULL to stop. */
enum {
	PLAT_QUICK_ASK_SAVE, PLAT_QUICK_ASK_LOAD,
	PLAT_QUICK_SAVED, PLAT_QUICK_LOADED,
	PLAT_QUICK_SAVE_FAILED, PLAT_QUICK_LOAD_FAILED
};
void plat_resident_on_quick(void (*fn)(int what, const char *path));
/* Called from inside plat_resident_wait for the player's hotkeys the
 * launcher carries out (src/hotkeys.h): `key` "shot" with path NULL as the
 * shot is taken (its shutter) and again with the PNG's path once Diatom
 * wrote it, or "shader" or "display" (path NULL) for the next */
void plat_resident_on_hotkey(void (*fn)(const char *key, const char *path));
/* What the PAUSED that made plat_resident_wait return RES_PAUSED was for:
 * "reset" (Reset Game's hotkey: ask first) or "quit" (Quit's: stop), ""
 * for MENU's, the menu */
const char *plat_resident_paused_for(void);

/* Called from inside plat_resident_wait when Diatom reports an achievement's
 * count moving ("CHEEVO id=.. state=progress value=.. target=.. text=.."),
 * which it holds to one every two seconds per achievement. NULL to stop. */
void plat_resident_on_progress(void (*fn)(int id, const char *text, int value,
                                          int target));

/* Called from inside plat_resident_wait roughly ten times a second, which is
 * the rate its socket poll already runs at. For work the launcher wants to do
 * WHILE a game is running and cannot do anywhere else, because this process is
 * blocked here for the whole session.
 *
 * Must not block. This loop is also the power button's watchdog, and the one
 * control that always has to work stops working for as long as anything here
 * takes. NULL to stop. */
void plat_resident_on_tick(void (*fn)(void));
/* Asked from the same loop: whether the battery is too low to go on. When it
 * says so the game is ended exactly as by the power button: STOP, so it saves
 * on its way out, and the launcher's after-the-game check powers the device
 * off. Must not block, for the same reason. NULL to stop. */
void plat_resident_on_battery_empty(bool (*fn)(void));
/* Diatom only: one protocol line (RESUME, STOP, SAVE\tpath=...), newline added. */
bool plat_resident_line(const char *fmt, ...);
/* Whether the game should be heard - Diatom's QUIET. Sent when it changes, and
 * before every RUN whether it changed or not, so it is safe to call as often
 * as the answer might have moved. */
void plat_resident_quiet(bool on);
/* Whether Diatom draws the low-battery disc over the game - its LOWBATTERY:
 * 0 none, 1 steady, 2 blinking. Sent like quiet: on a change, and before
 * every RUN. */
void plat_resident_low_battery(int level);
/* Which ears hear what - Diatom's CHANNELS: "stereo", "mono", "left" or
 * "right", already decided for where Diatom's sound goes (src/audioout.h).
 * Sent like quiet: on a change, and before every RUN. */
void plat_resident_channels(const char *mode);
/* The equalizer's bands for where Diatom plays (src/eq.h), dB each: SETEQ,
 * on a change and before every RUN, as the channels. All 0 is flat. */
void plat_resident_eq(const int8_t db[5]);
/* Where Diatom says its sound actually is, which may not be where it was asked
 * to put it - a sink that will not open, or one that died, makes the port fall
 * back and report the fallback. False until it has said. */
bool plat_resident_audio(char *out, size_t cap);
/* Forget that report: a SETAUDIO has just gone, and until Diatom answers it -
 * its audio_set always does - what it said last describes the moment before. */
void plat_resident_audio_asked(void);
/* The volume Diatom last reported while a game runs - an index on its own
 * ladder, with the ladder's length in *count - or -1 when it has said nothing
 * since the game started. It is applied to this side only when the game ends
 * (see d_apply_levels), so a headset that has to follow the keys DURING a game
 * reads it here. */
int  plat_resident_volume(int *count);
/* Increments each time the launcher connects to a resident emulator. Anything
 * the launcher pushed into the last one has to be pushed into a new one. */
unsigned plat_resident_generation(void);
/* Path from the most recent PREVIEW message, or "" - the menu's backdrop. */
const char *plat_resident_last_preview(void);
/* Core options the launcher wants applied to every game, read once from
 * the coreopt. namespace. Diatom keeps no per-core knowledge (its register
 * section 12), so the per-core opinions live here. Each entry is a whole
 * "key=value" string; a core that does not declare the key ignores it.
 * Applied BEFORE the game loads, because options marked (Restart) are read at
 * retro_load_game and setting one afterwards does nothing until next launch. */
int         plat_coreopt_count(const char *tag);
const char *plat_coreopt(const char *tag, int i);
/* The turbo map for this system, or NULL. */
const char *plat_turbo_map(const char *tag);

/* Where Diatom is actually drawing the game, from its DISPLAY message. False
 * until it has said, which is the standalone path and the first moments of a
 * launch. Cached rather than asked for: Diatom reports it from the one place
 * its mode or rect can change, settling included, so the last one heard is
 * current. */
bool plat_resident_rect(SDL_Rect *out);
/* The picture the game's core makes and the shape it asks for (width over
 * height; 0 for square pixels), as Diatom's last DISPLAY said: the Display
 * list's previews (src/dmodefit.h). False before one said it. */
bool plat_resident_source(int *w, int *h, double *aspect);
/* And the surface Diatom draws on (its DISPLAY's surface=), in pixels */
bool plat_resident_source_surface(int *w, int *h);
/* Whether the game that just ended took the resident emulator with it (its
 * EXIT said reason=crash): then there is no resident until one is started */
bool plat_resident_crashed(void);
/* Waits up to `max_ms` for no Diatom to be running (a crashed one still on
 * its way out); false when one still is */
bool plat_resident_wait_gone(unsigned max_ms);
/* Read replies for up to timeout_ms, stopping as soon as a DISPLAY arrives.
 * For changing the mode from the in-game menu: Diatom answers a SETDISPLAY with
 * the new rect, and the menu wants it now so its backdrop can redraw where the
 * game is about to be, rather than on the next wait after resuming. A missed
 * reply costs a stale backdrop, never a hang. */
bool plat_resident_sync_rect(int timeout_ms);

/* Keys for the next RUN only, tab separated - "screens=2\tframeskip=late"
 * for a DS game. Cleared by the RUN that carries them. */
void plat_resident_run_keys(const char *keys);
/* The next game's whole button map, the SETMAP form, sent after its RUN as
 * given: the launcher builds it (src/remapstore.c). NULL or "" for none.
 * Like the RUN keys, it is for the next plat_resident_send only. */
void plat_resident_map(const char *map);
/* One core option for the next RUN only, sent after the system's own (and
 * so over them): a choice the player made for one game, as the N64's Pak
 * (src/main.c, pak_*). Like the map, for the next plat_resident_send only. */
void plat_resident_opt(const char *key, const char *value);
/* The stylus of a game with two screens, as Diatom last said (STYLUS on=):
 * R2 turns it in the game, where this process cannot see. Off at every RUN.
 * _note records what the menu just asked for, so the row says it at once. */
bool plat_resident_stylus(void);
/* The game's core asked for the motor, as Diatom's last PAUSED said
 * (rumble=1): the in-game Controls tab's Rumble row is for its games alone.
 * Off at every RUN, and from a Diatom too old to say. */
bool plat_resident_rumble_asked(void);
void plat_resident_note_stylus(bool on);
/* Whether the bottom screen is in front, as Diatom last said (SCREENS swap=):
 * L2 switches it in the game. Off at every RUN, so each game starts on the
 * top screen. */
bool plat_resident_swap(void);
/* After SETSCREENS or SETSTYLUS from the in-game menu: read Diatom's answer
 * through its SCREENS or STYLUS line, taking the new rect and the new paused
 * picture on the way, so the menu's backdrop shows the layout just chosen. */
bool plat_resident_sync_screens(int timeout_ms);
/* The same for an answer that is a line of its own (SETPLAYER2's PLAYER2):
 * Diatom's lines read through to the first that starts with `prefix`, copied
 * into `out`, the rect, the picture and the levels on the way noted as the
 * others note them. False when none came in `timeout_ms`; 0 reads only the
 * lines waiting already. Only while paused, as those: a game that is running
 * can send EXIT, and this would eat it. */
bool plat_resident_sync_line(const char *prefix, char *out, size_t n, int timeout_ms);

void plat_request_poweroff(void);
void plat_request_reboot(void);

/* The two level scales, stated once. ZyperOS shares them verbatim with
 * launch.sh and with Diatom, so a level crossing the socket needs no
 * conversion - which only holds while every place that rescales a level agrees
 * on the top of the range, so they live here rather than beside the ladder
 * table that only the settings code can see. */
#define PLAT_VOL_MAX     20   /* 21 positions, 0..20 */
#define PLAT_BRIGHT_MAX  11   /* a 12-rung geometric ladder, 0..11 */

/* Levels come from the settings database: seeded from the shipped defaults on
 * first run, then overwritten by every nudge of the rocker. There is no longer
 * a saved value and a fallback to reconcile, which is why this takes nothing -
 * it used to be handed the config defaults so the precedence rule lived in one
 * place, and now the precedence has nowhere to disagree with itself. */
void plat_settings_init(void);
int  plat_volume_get(void);
int  plat_brightness_get(void);
void plat_volume_nudge(int delta);
void plat_brightness_nudge(int delta);
void plat_volume_set_pct(int pct);
void plat_brightness_set(int level);
/* The brightness boost past the top rung (src/levels.c): 0 none, up to
 * PANELGAMMA_LIFT_MAX steps of a mid-tone lift on the display's table */
void plat_brightness_lift(int lift);
/* The backlight's own value at rung `level` (its ladder), -1 where there is
 * none to hand to another program: Flash Drive's usbdrive steps the same
 * rungs, and the boot screens light at the player's (src/boottheme.h) */
int  plat_brightness_raw(int level);

/* The panel's backlight off or on, leaving the brightness level as it was -
 * MP4's pocket lock. Off is the display engine's own switch, not a black
 * frame: measured 2026-09-30, it takes a book playing from about 470 mA to
 * 300, where a dark picture would leave the light burning. On comes back at
 * the level it had. Nothing on the host. */
void plat_backlight(bool on);
/* The light at the backlight's floor for now - the ladder's lowest rung up
 * to 1.0.5, and a rung under the floor (the Pixel 2's two gamma-table rungs)
 * stays as it is - or back at the level it has (2026-11-09: "wake up in the
 * lowest brightness" from the pocket lock). Saves nothing, so the level the
 * rocker set is the one that comes back; nothing on the host. */
void plat_backlight_dim(bool dim);
/* The display's own colors from now on, with no dimming table (the Pixel 2's
 * rungs under the backlight's floor, src/panelgamma.h): for a program that
 * comes after this one and sets no table, USB Drive's usbdrive, whose steps
 * would otherwise all be darkened by the rung this one left. The rung
 * itself is kept; the launcher puts its table back when it starts again.
 * Nothing where there is no such table. */
void plat_display_plain(void);
/* The player's Screen Colors (MENU > Display > Screen Colors,
 * src/panelgamma.h), each SCREEN_COLORS_RANGE either way of 0, the frame as
 * drawn. The Pixel 2 puts Temperature, Tint, Contrast and Gamma in the
 * display's gamma table and Saturation in the display controller's BCSH
 * block (plastron's kernel patch, SCREEN_SATURATION_FILE), which keeps it for
 * every program. A table load blinks one frame, so loads come at most every
 * SCREEN_COLORS_GAP_MS while a key repeats or a list's cursor runs: the one
 * held back is made by plat_screen_colors_tick, and the last asked for is
 * always the one left on. Saturation never blinks and goes at once.
 * `keep`: the player's setting, left in PANELGAMMA_COLOR_FILE for Diatom and
 * portkeys, which write the table too; not kept, a preview on this display
 * alone (the Look list's cursor), which those two never see. The launcher
 * keeps the settings in its database ("screen_temp", "screen_tint",
 * "screen_contrast", "screen_saturation", "screen_gamma"), and
 * plat_settings_init reads them. Nothing where the profile has no table. */
#define SCREEN_COLORS_RANGE 10
#define SCREEN_COLORS_GAP_MS 120
typedef struct { int temp, tint, contrast, saturation, gamma; } screen_colors;
void plat_screen_colors(const screen_colors *c, bool keep);
/* A table load held back for the gap, made once the gap is over (or at once
 * with `now`, as the page closes): called by the Screen Colors page's every
 * frame. Nothing when none is held back. */
void plat_screen_colors_tick(bool now);
/* Whether the kernel has the Saturation knob: the Saturation row shows only
 * then (a system image without plastron's BCSH patch has none) */
bool plat_screen_saturation_here(void);
/* Night Mode (src/nightmode.h, MENU > Display): the whole screen warmer,
 * always or by the Night Hours, in the same table as the Screen Colors.
 * Only where the profile has that table (plat_night_here, as
 * plat_screen_colors_here): the Pixel 2's VOP, the Brick's TCON.
 *
 * plat_night: the kept settings (src/nightkeep.c's night_keep calls it, and
 * plat_settings_init reads them at the start): the table now, at once, and
 * the warmth and the next nights in PANELGAMMA_COLOR_FILE for Diatom and
 * portkeys, which turn it on and off at the hour themselves over a game or
 * a port.
 *
 * plat_night_preview: Night Warmth's list, `warmth` (1..10) on the whole
 * screen now and kept nowhere, as the Look list previews; 0 puts the kept
 * Night Mode back. Loads after the gap as plat_screen_colors' preview does,
 * so the list calls plat_screen_colors_tick every frame, as the Look list's
 * picture does.
 *
 * plat_night_tick: the hour's change, once a second at most, from every
 * screen's loop (tick_theme) and a game's (on_game_tick): the table loaded
 * at once when the warmth due now is not the one on (one blink, no fade),
 * and the nights worked out again each minute and the file written when they
 * change (the zone, the clock, a night that ended). True when it turned on
 * or off. Over a game it only remembers, and the table the launcher loads
 * when it has the display back is right. */
struct night_cfg;
void plat_night(const struct night_cfg *n);
void plat_night_preview(int warmth);
bool plat_night_tick(void);
bool plat_night_here(void);
/* Headphones and the speaker want different volume ladders - the jack covers
 * 61 dB where the speaker covers 45 - so the level has to be re-applied when a
 * plug goes in or comes out, not merely at the next volume press. Call from a
 * periodic path; it is one ioctl and writes nothing unless the state changed.
 * A no-op on the host. */
void plat_audio_jack_poll(void);

/* The hardware mute switch. plat_mute_poll reads it and returns true on the
 * frame it CHANGED, so a caller can tell a running game; plat_muted answers
 * the current position. Muting the launcher's own audio is handled inside. */
/* `own_volume` is false while a game is running: Diatom owns the level then,
 * so only the speaker switch may be touched and never the gain. */
bool plat_mute_poll(bool own_volume);
bool plat_muted(void);

/* Is a cable in the headphone jack? SW_HEADPHONE_INSERT on the codec's input
 * node, the same switch the volume ladder above already follows.
 *
 * Exposed because Diatom puts the choice of OUTPUT in the launcher, and a
 * cable outranks everything else (src/audioout.c). The port keeps reading the
 * switch for its own reason - which volume window to use - so both halves ask
 * the hardware rather than one telling the other something it could get wrong. */
bool plat_headphones_present(void);

/* A USB sound card is plugged in: a USB-C DAC, where the port can be a host.
 * False on a device whose port can't be one safely (the Brick). */
bool plat_usb_audio_present(void);

/* Files move over the USB cable rather than Wi-Fi: the device's USB port is a
 * network link to a computer (the GKD Pixel 2). True, with the address a
 * browser opens (addr may be NULL); false on a device that moves files over
 * Wi-Fi. */
bool plat_cable_link(char *addr, size_t n);
/* A fresh connection over that cable, as if it were plugged in again: the
 * transfer screen asks for one as it opens, since a computer can drop the
 * link while the device goes on believing it is up. Nothing on a device with
 * no cable link. */
void plat_cable_reconnect(void);
/* USB's charge-only rule: what is on the port, and the link (usbpeer.h) */
#include "usbpeer.h"
/* kind: 1 = brightness, 2 = volume, 3 = POWER's hold (OSDBAR_*) */
void plat_osd_show(int kind, int val, int max);
/* Out at once rather than after its hold: POWER let go early */
void plat_osd_fade(void);
void plat_draw_osd(SDL_Renderer *r);
/* Something the launcher draws over every screen, every frame a screen draws
 * its bar (plat_draw_osd), under the bar: the replug message (main.c,
 * DESIGN.md 11.36). NULL for none. */
void plat_set_overlay(void (*fn)(SDL_Renderer *r));
/* The overlay for a frame that skipped plat_draw_osd: each device's
 * plat_present calls it before it shows the frame */
void plat_overlay_present(SDL_Renderer *r);
/* The renderer the bar is drawn with is about to be destroyed: its texture
 * goes now, and the next plat_draw_osd makes one on whatever renderer it is
 * given. Each device's plat_video_quit calls this before SDL_DestroyRenderer;
 * a renderer made after it can come back at the same address. */
void plat_osd_forget(void);
/* When the volume or brightness bar will next look different on its own, or
 * UINT32_MAX while none is showing: the coming frame while it slides in or
 * fades out (src/osdbar.h has its timing), the start of the fade while it
 * holds. A launcher that only redraws when something changes asks this, or
 * the bar would freeze half faded until the next unrelated redraw. */
Uint32 plat_osd_until(void);

/* POWER, a press or a hold (src/powerhold.h), for every loop that reads the
 * key. plat_power_event: the key went down or up, true when that is the
 * turn-off (a press, in Press mode). plat_power_due: true once when a hold
 * is done (Hold mode). plat_power_hold: how far a hold has got, 0 to 1000, or
 * -1 for none under way. plat_power_consume: this time down was taken for
 * something else (the charging screen's hold to start), so it turns nothing
 * off. plat_power_poll: each device's plat_input_poll, last, so a hold done
 * is pressed[IN_POWER] that frame. plat_power_wait: `ms`, or less to wake for
 * the hold's next segment or its end, for each device's plat_input_wait. The
 * settings pill shows a hold while it is under way. */
bool plat_power_event(bool down);
bool plat_power_due(void);
int  plat_power_hold(void);
void plat_power_consume(void);
void plat_power_poll(in_state *st);
unsigned plat_power_wait(unsigned ms);

/* Read only the requested fields: NULL skips that sysfs read. With pct,
 * returns whether capacity was read; with charging alone, whether cable
 * state was read. A failed charger read reports false through *charging.
 * The fake battery file represents a battery off the charger on both paths. */
bool plat_battery(int *pct, bool *charging);
/* Read-only sysfs directory with charge_full and charge_full_design, or NULL
 * on a host. About treats absent capacity files as unknown. */
const char *plat_battery_info_path(void);

/* The chip's temperature in mC, the hottest of the thermal zones its profile
 * names (src/thermal.h), for "Getting hot" (src/heat.h): false with no
 * reading. A number in /tmp/zyperos_fake_temp stands in for the sensors, in C
 * or mC (90 or 90000), for testing the warning from a shell: written while a
 * game runs, and gone at power-off. A few small sysfs reads; asked every few
 * seconds. */
bool plat_chip_temp(int *mc);

/* What the charging screen shows (src/charging.h): the charge, the current and
 * voltage, the charge in mAh, the cable and what it is plugged into, and
 * whether the charge limit holds it. False with no battery to read. A line
 * in /tmp/zyperos_fake_charge stands in for the gauge, "PCT MA MV
 * [wall|computer [ONLINE [HELD]]]" ("62 1480 4120 wall 1"), for testing the
 * screen from a shell. */
struct ch_reading;
bool plat_charge_read(struct ch_reading *r);

/* MENU > Charge Limit (DESIGN.md 11.26): whether the battery's charger can
 * stop short of full (plastron's rk817 charge_control_end_threshold), and
 * that limit set, in percent, 100 for none; it charges again 2 under it
 * (charge_control_start_threshold). The fake gauge above has one, written to
 * /tmp/zyperos_fake_charge_limit. */
bool plat_charge_limit_supported(void);
void plat_charge_limit_set(int pct);

/* Whether the device has a Wi-Fi or Bluetooth radio at all - not whether it
 * is on. A radio that is off is the player's to turn on; a device without one
 * hides everything that needs it (src/sys_menu.h). */
bool plat_has_wifi(void);
/* plat_has_bluetooth, plat_gpu_shaders, plat_speaker_mono,
 * plat_brightness_on_volume, plat_battery_lights and plat_player2: facts
 * about the handheld, answered from its profile (src/device/profile.h). */
/* Whether MENU is held down this moment where the button itself cannot say:
 * on the Pixel 2 MENU is FUNCTION, reported as IN_MENU only on its release
 * (src/device/pixel2.c), so in_state never sees it held. The tutorial lights
 * its MENU glyph from this while the player holds it. False where IN_MENU's
 * own `down` says it, as on the Brick. */
bool plat_menu_held(void);
/* The same for L2 and R2 (`b` IN_L2 or IN_R2): on the Pixel 2 each is a tap
 * reported as it is let go, so that L2+R2 together can be the battery level
 * instead, and in_state never sees one held. The tutorial's handheld shows
 * them pressed from this. False for any other button, and where their own
 * `down` says it, as on the Brick. */
bool plat_tap_held(in_button b);

/* MENU > Side Lights (src/sidelights.h), where plat_battery_lights says there
 * are lights: the parts that are on, read from where the device keeps them,
 * and set, which also tells whatever drives the lights to look again. Every
 * part, and nothing, on a device without them. */
side_lights plat_side_lights(void);
void plat_side_lights_set(side_lights on);

/* One line for whatever drives the lights (SIDE_LIGHTS_GAME_ON and the rest,
 * src/sidelights.h): something happened that they may show. Never waits.
 * Nothing on a device without them. */
void plat_side_lights_say(const char *line);

/* The shelf sitting still: the memory at a slower clock while nobody presses
 * anything and no art is being decoded, and back at full with the next press,
 * before anything is drawn for it (the main loop). Called only on a change. A
 * device with no memory clock of its own to set does nothing. */
void plat_ram_idle(bool idle);

/* Date & Time (src/clock.h). Set the clock to `t`, and the hardware clock with
 * it, which keeps the time while the device is off: the Pixel 2 has nothing
 * else to set it from. False if the system clock would not take it. */
bool plat_clock_set(time_t t);

/* Times read as local time in zone `id` from now on, in this process: a zone
 * database name such as "America/New_York". The setting itself is the
 * library database's "timezone". */
void plat_clock_zone(const char *id);

/* The boot screen's theme for the next start (src/boottheme.h, DESIGN.md
 * 11.21): `text`, theme.txt's two lines, kept where the next start's boot
 * reads it. 1 when written, 0 on a device whose boot has no such screen,
 * -1 when it could not be written. */
int plat_boot_theme(const char *text);

#endif

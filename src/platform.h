/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PLATFORM_H
#define ZYPEROS_PLATFORM_H

#include <SDL.h>
#include <stdbool.h>
#include <time.h>

#include "sidelights.h"

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
/* A video picture UNDER the screen, for the next plat_present only: MP4's
 * player, called at the start of a frame in place of clearing it. The frame
 * is `bg` everywhere, then `tex` (which may be NULL) in `dst` - in screen
 * coordinates - and then everything drawn after this call, on top, its alpha
 * kept. Where the device scales the screen to its panel, the picture is drawn
 * at the panel's own resolution rather than through the screen's 1024x768:
 * on the Pixel 2 a 640x360 video lands on the 640x480 panel pixel for pixel,
 * instead of scaled up and back down. */
void plat_video_under(SDL_Texture *tex, const SDL_Rect *dst, SDL_Color bg);
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
void plat_input_quit(void);
void plat_input_poll(in_state *st);
void plat_input_flush(void);
/* The shelf's rest when it has nothing to draw: until a button goes down or
 * up, or `ms` have passed, whichever is first. It slept a fixed 16 ms, so a
 * press waited up to that long to be seen, and on the Pixel 2 every press on
 * the shelf reached the panel two blanks later (33 ms). With no button
 * devices to wait on (a desktop), it sleeps `ms`. */
void plat_input_wait(unsigned ms);
/* Once, just before the shelf's first frame: throw away whatever input the
 * boot produced, so nothing pressed while the device came up is honored, and
 * leave `st` empty. How long that takes depends on the device - on one whose
 * input arrives in bursts as it starts, settling means waiting them out. */
void plat_input_settle(in_state *st);
bool in_repeat(in_state *st, in_button b);

/* Tell the input layer the process is going away. The next plat_input_poll
 * raises quit_requested, which every loop already checks. */
void plat_terminate(void);

/* Run a child to completion, watching the power button while it runs.
 * envkv is a NULL-terminated array of "KEY=value" strings added to this
 * process's own environment, or NULL for that environment alone. */
int  plat_run(char *const argv[], const char *const envkv[], const char *workdir);
bool plat_run_power_pressed(void);
/* Say that something equivalent to a power press has happened, for the paths
 * the evdev watchdog cannot see: a screen the launcher is drawing over a
 * paused game, where plat_resident_wait is not running and so nothing is
 * reading the power key. Auto Off expiring in the in-game menu is exactly
 * that. Cleared with the flag, at the start of the next game. */
void plat_note_power_pressed(void);

/* Start a child and forget it: it outlives this process and leaves no zombie
 * behind. This is how a resident emulator that has died gets started again. */
bool plat_spawn_detached(char *const argv[], const char *const envkv[],
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
bool plat_resident_ready(void);
/* plat_resident_ready, waiting up to max_ms for a Diatom that is running but
 * not answering yet (still starting); false at once when none is running. */
bool plat_resident_await(unsigned max_ms, unsigned *waited_ms);
/* `console` is a RetroAchievements console id and `cheevos` a set file for
 * Diatom to watch; 0 and NULL mean the game has no achievements, which is the
 * ordinary case.
 *
 * Eight positional arguments, five of them paths, is one past comfortable -
 * the call already passes the same state path twice in a row. A struct is the
 * next change to this function, not a further parameter. */
bool plat_resident_send(const char *tag, const char *core, const char *rom,
                        const char *resume, const char *exit_state,
                        const char *preview,
                        int console, const char *cheevos);
int  plat_resident_wait(void);
/* Whether the game last sent has said RUNNING: its first frame is up */
bool plat_resident_started(void);

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
 * STICK line. NULL to stop. */
void plat_resident_on_stick(void (*fn)(bool on));

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
/* Read replies for up to timeout_ms, stopping as soon as a DISPLAY arrives.
 * For changing the mode from the in-game menu: Diatom answers a SETDISPLAY with
 * the new rect, and the menu wants it now so its backdrop can redraw where the
 * game is about to be, rather than on the next wait after resuming. A missed
 * reply costs a stale backdrop, never a hang. */
bool plat_resident_sync_rect(int timeout_ms);

/* Keys for the next RUN only, tab separated - "screens=2\tframeskip=late"
 * for a DS game. Cleared by the RUN that carries them. */
void plat_resident_run_keys(const char *keys);
/* The next game's button map, the SETMAP form, sent after its RUN joined
 * with the system's turbo (plat_turbo_map); NULL or "" for none. Like the
 * RUN keys, it is for the next plat_resident_send only. */
void plat_resident_map(const char *map);
/* One core option for the next RUN only, sent after the system's own (and
 * so over them): a choice the player made for one game, as the N64's Pak
 * (src/main.c, pak_*). Like the map, for the next plat_resident_send only. */
void plat_resident_opt(const char *key, const char *value);
/* The stylus of a game with two screens, as Diatom last said (STYLUS on=):
 * R2 turns it in the game, where this process cannot see. Off at every RUN.
 * _note records what the menu just asked for, so the row says it at once. */
bool plat_resident_stylus(void);
void plat_resident_note_stylus(bool on);
/* Whether the bottom screen is in front, as Diatom last said (SCREENS swap=):
 * L2 switches it in the game. Off at every RUN, so each game starts on the
 * top screen. */
bool plat_resident_swap(void);
/* After SETSCREENS or SETSTYLUS from the in-game menu: read Diatom's answer
 * through its SCREENS or STYLUS line, taking the new rect and the new paused
 * picture on the way, so the menu's backdrop shows the layout just chosen. */
bool plat_resident_sync_screens(int timeout_ms);

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
/* The backlight's own value at rung `level` (its ladder), -1 where there is
 * none to hand to another program: Flash Drive's usbdrive steps the same rungs */
int  plat_brightness_raw(int level);

/* The panel's backlight off or on, leaving the brightness level as it was -
 * Muse's pocket lock. Off is the display engine's own switch, not a black
 * frame: measured 2026-09-30, it takes a book playing from about 470 mA to
 * 300, where a dark picture would leave the light burning. On comes back at
 * the level it had. Nothing on the host. */
void plat_backlight(bool on);
/* The light at the ladder's lowest rung for now, or back at the level it
 * has (2026-11-09: "wake up in the lowest brightness" from the pocket lock).
 * Saves nothing, so the level the rocker set is the one that comes back;
 * nothing on the host. */
void plat_backlight_dim(bool dim);
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
/* kind: 1 = brightness, 2 = volume */
void plat_osd_show(int kind, int val, int max);
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

/* Read only the requested fields: NULL skips that sysfs read. With pct,
 * returns whether capacity was read; with charging alone, whether cable
 * state was read. A failed charger read reports false through *charging.
 * The fake battery file represents a battery off the charger on both paths. */
bool plat_battery(int *pct, bool *charging);
/* Read-only sysfs directory with charge_full and charge_full_design, or NULL
 * on a host. About treats absent capacity files as unknown. */
const char *plat_battery_info_path(void);

/* The chip's temperature in mC, the hotter of its thermal zones, for "Getting
 * hot" (src/heat.h): false with no reading. A number in /tmp/zyperos_fake_temp
 * stands in for the sensors, in C or mC (90 or 90000), for testing the
 * warning from a shell: written while a game runs, and gone at power-off.
 * Two small sysfs reads; asked every few seconds. */
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
bool plat_has_bluetooth(void);
/* Whether the built-in speaker is one speaker, as on the GKD Pixel 2, where
 * the Brick has two: Audio Channels' Auto is mono there and stereo on
 * everything else (src/audioout.h). */
bool plat_speaker_mono(void);

/* Whether brightness is FUNCTION with the volume keys, the device having no
 * brightness keys of its own. What MENU > Controls calls them (controls.h). */
bool plat_brightness_on_volume(void);
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

/* Whether the device shows the battery on lights of its own, L2 with R2
 * flashing the level there outside a game (the Pixel 2's four side lights,
 * which plastron's gauge drives). What MENU > Controls says. */
bool plat_battery_lights(void);

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

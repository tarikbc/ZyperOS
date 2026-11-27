/* SPDX-License-Identifier: GPL-3.0-only */
#include "launch_screen.h"

#include "aout_route.h"
#include "app.h"
#include "bgm.h"
#include "biosfile.h"
#include "gamefile.h"
#include "ctl.h"
#include "hkstore.h"
#include "ctl_channel.h"
#include "dl_screen.h"
#include "dlq.h"
#include "dscache.h"
#include "dsv.h"
#include "ingame_screen.h"
#include "menukeys_screen.h"
#include "cheats_screen.h"
#include "hudmenu_screen.h"
#include "mp4c.h"
#include "net.h"
#include "notice.h"
#include "pacing.h"
#include "sgbmode.h"
#include "border_pic.h"
#include "panel.h"
#include "pico8.h"
#include "p8fx.h"
#include "p8pad.h"
#include "p8fx_send.h"
#include "place.h"
#include "pminstall.h"
#include "portload.h"
#include "portmenu.h"
#include "mp4hand.h"
#include "rabadge.h"
#include "rafetch.h"
#include "rahashjob.h"
#include "raqueue.h"
#include "remap.h"
#include "rumble.h"
#include "remapstore.h"
#include "shelfbuild.h"
#include "shots_screen.h"
#include "shotname.h"
#include "startfail.h"
#include "stats.h"
#include "strength.h"
#include "ui.h"
#include "uisound.h"
#include "update_screen.h"
#include "zyperdraw.h"
#include "levels.h"
#include "wdog.h"

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* Bring the resident emulator back.
 *
 * With one process per game a core that segfaults takes down that game and
 * nothing else. With a resident emulator it takes down the emulator, and every
 * launch for the rest of the session would fall back to the slow path -- until
 * the launcher itself exits and launch.sh starts a new one. So the launcher
 * starts one too, once the fallback game has given the display back. Never
 * while a game is running: two GL contexts and a launcher is one more than
 * this device should be asked for. */
/* A game run the slow way: px2-open's status and quit (ctl_poll), which
 * the slow path's wait in plat_run calls ten times a second */
static void fallback_tick(void)
{
	if (g_ctl_app) ctl_poll(g_ctl_app, CTL_AT_GAME);
	/* Its play time's marker and checkpoints, as on_game_tick writes a
	 * resident game's. The monotonic clock, not SDL's: SDL is down for it */
	stats_tick((unsigned)stats_mono_ms());
}

static void respawn_resident(app *a)
{
	char elf[CFG_STR * 2], save[CFG_STR * 2], bios[CFG_STR * 2];
	char cores[CFG_STR * 2];
	char *argv[10];
	long pid;
	(void)a;

	if (plat_resident_ready()) return;

	/* The resident is Diatom. It maps every core in the directory it is given
	 * and keeps them, so the directory is the only thing handed over and
	 * nothing here changes when a system is added. */
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	if (access(elf, X_OK) != 0) return;
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
	/* --cores here as well as in launch.sh. There are two places that start
	 * the resident emulator - the boot script and this - and a flag added to
	 * only one of them means every restart after the first silently loses it.
	 * That is exactly what happened: the log showed a premapped boot, then a
	 * dlopen on every launch once this path had restarted it. */
	snprintf(cores, sizeof cores, "%s/cores", P_ROOT);
	argv[0] = elf;
	argv[1] = (char *)"--socket";
	argv[2] = (char *)plat_resident_socket();
	argv[3] = (char *)"--cores";  argv[4] = cores;
	argv[5] = (char *)"--save";   argv[6] = save;
	argv[7] = (char *)"--system"; argv[8] = bios;
	argv[9] = NULL;
	/* Its pid goes in the pid file, where launch.sh looks for the running
	 * resident before it starts one (plat_resident_note_pid says why) */
	pid = plat_spawn_detached_pid(argv, NULL, P_ROOT);
	if (pid <= 0)
		fprintf(stderr, "resident emulator is gone, and diatom would not start: %s\n",
		        strerror(errno));
	else if (!plat_resident_note_pid(pid))
		fprintf(stderr, "resident emulator is gone, started diatom (pid %ld), but could "
		        "not write %s\n", pid, plat_resident_pidfile());
	else
		fprintf(stderr, "resident emulator is gone, started diatom (pid %ld)\n", pid);
}

/* The Ports shelf. A port is a Linux program with a script of its own - a
 * PortMaster port, OpenBOR, anything that is not a libretro core - and it
 * wants the display, the buttons and most of the memory, which this process
 * and the resident Diatom are holding. So it does not run under the launcher
 * at all: the launcher names the script in PORT_REQUEST and exits, and
 * launch.sh stops Diatom, runs the script, and starts both again
 * (sd/zyperos/launch.sh). Coming back costs a cold start, which a port's own
 * start dwarfs.
 *
 * A shelf whose core is "run:<script>" is the same handoff for a standalone
 * emulator: the script, under the card, is run with the game as its one
 * argument - OpenBOR, which has no libretro core, or the Dreamcast through
 * the standalone Flycast.
 *
 * The request is one line of four fields, a tab between each: the script,
 * the game it is given (none for a port, whose script is the game), the tag
 * of the system whose in-game menu goes over it (src/portmenu.h), none for a
 * program that has no menu, and the system's Display row (its display.<TAG>
 * name, "integer" for Sharp Pixels), which launch.sh hands a "run:" script as
 * ZYPEROS_DISPLAY: your PICO-8 draws Sharp Pixels at 3x and every other mode
 * at the panel's full height (Tarik's pick, 2026-11-19). Empty fields stay
 * fields: launch.sh splits the line by hand, not with read, which would run
 * two tabs together. A launch.sh from before 1.0.7 reads three fields and
 * gives the fourth to the tag, which no menu has: the old launch.sh is never
 * beside this launcher, which ships with it. */
#define PORT_REQUEST "/tmp/zyperos_port"
bool is_port(const system_cfg *s)
{
	return !strcmp(s->core, PORT_CORE) ||
	       !strncmp(s->core, RUN_PREFIX, strlen(RUN_PREFIX));
}

static bool pm_locked(void);
/* PortMaster's config/ before it starts: a file of it cut off is removed,
 * for PortMaster to make again, and the settings with a cut-off source list
 * (src/pminstall.h, pm_json_whole). Its first and last bytes read of each,
 * the whole of the small ones: a few reads, where PortMaster's own start
 * takes seconds. */
static void pm_heal_config(const char *folder)
{
	static char buf[PM_JSON_SCAN];
	char dir[LIB_PATH], path[LIB_PATH * 2];
	bool reset = false;
	struct dirent *e;
	DIR *d;

	snprintf(dir, sizeof dir, "%s/%s/%s/%s", P_ROMS, folder, PM_FOLDER, PM_CONFIG);
	if (!(d = opendir(dir))) return;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char head[64], tail[64];
		size_t hn = 0, tn = 0;
		bool whole;
		long size;
		FILE *f;

		if (len < 6 || strcasecmp(e->d_name + len - 5, ".json") || e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (!(f = fopen(path, "rb"))) continue;
		if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) { fclose(f); continue; }
		rewind(f);
		if (size <= (long)sizeof buf) {
			size_t got = fread(buf, 1, sizeof buf, f);
			whole = got == (size_t)size && pm_json_balanced(buf, got);
		} else {
			hn = fread(head, 1, sizeof head, f);
			if (fseek(f, -(long)sizeof tail, SEEK_END) == 0) tn = fread(tail, 1, sizeof tail, f);
			whole = pm_json_whole(head, hn, tail, tn);
		}
		fclose(f);
		if (whole) continue;
		fprintf(stderr, "portmaster: %s/%s was cut off (%ld bytes): removed, PortMaster makes it again\n",
		        PM_CONFIG, e->d_name, size);
		unlink(path);
		if (pm_json_resets_settings(e->d_name)) reset = true;
	}
	closedir(d);
	if (reset) {
		snprintf(path, sizeof path, "%s/%s", dir, PM_SETTINGS);
		if (!unlink(path))
			fprintf(stderr, "portmaster: its settings removed with it, for its first start's sources\n");
	}
}

/* A file of a few lines written whole: a temporary beside it, renamed in */
static bool write_whole(const char *path, const char *text)
{
	char tmp[LIB_PATH * 2];
	FILE *f;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	f = fopen(tmp, "w");
	ok = f && fputs(text, f) >= 0;
	if (f && fclose(f) != 0) ok = false;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
		return false;
	}
	return true;
}

void p8_keys_save(const system_cfg *s, const char *file, const char *shots)
{
	char text[CFG_STR * 6], run[200], kept[CFG_STR * 4] = "";
	hk_table hk;

	/* The folder the hand-over named, kept by the menu, which comes back to
	 * a card with no shelf of its own */
	if (!shots) {
		FILE *f = fopen(plat_flag(PORT_KEYS_FILE), "r");
		char line[CFG_STR * 4 + 16];

		while (f && fgets(line, sizeof line, f))
			if (!strncmp(line, "shots\t", 6)) {
				line[strcspn(line, "\n")] = '\0';
				snprintf(kept, sizeof kept, "%s", line + 6);
			}
		if (f) fclose(f);
		shots = kept;
	}
	/* The table in force for the cart, as a Diatom game is sent it: This
	 * Game's, PICO-8's, Every Game's or ZyperOS's own */
	hks_get(HKS_GAME, s->tag, file, &hk);
	hk_run(&hk, run, sizeof run);
	snprintf(text, sizeof text, "hotkeys\t%s\nshots\t%s\n", run, shots);
	if (!write_whole(plat_flag(PORT_KEYS_FILE), text))
		fprintf(stderr, "port: couldn't write %s\n", PORT_KEYS_FILE);
	/* Next Shader's pop-ups, "Shader / CRT", where it has a key: each
	 * shader's in both themes, made as the hand-over is (`kept` NULL), and
	 * by the menu only where one is missing (a key given in it) */
	if (hk.key[HK_SHADER] != HK_NONE) {
		const char *names[16];
		int n = p8fx_shaders(names, 16), i, t;

		for (i = 0; i < n; i++)
			for (t = 0; t < DS_THEMES; t++) {
				char path[LIB_PATH];

				snprintf(path, sizeof path, PORT_SHADER_POPUP, names[i], t == DS_DARK ? "dark" : "light");
				if (shots == kept && access(plat_flag(path), F_OK) == 0) continue;
				notice_render(T(S_HOTKEYS_POPUP_SHADER), T(p8fx_shader_text(names[i])), plat_flag(path), t);
			}
	}
}

void p8_pad_save(const system_cfg *s, const char *file)
{
	char line[600], vline[600], text[1220];
	rm_table t;

	/* The table a Diatom game of the shelf would be sent (rms_sent); the
	 * PICO-8 has no stick. For the joypad, and for the pad portkeys gives
	 * PICO-8 while it holds the joypad for the hotkeys (tools/pkvpad.h) */
	rms_sent(s, file, false, &t);
	if (!p8_pad_line(&t, line, sizeof line) || !p8_vpad_line(&t, vline, sizeof vline)) return;
	snprintf(text, sizeof text, "%s\n%s\n", line, vline);
	if (!write_whole(plat_flag(P8_PAD_FILE), text))
		fprintf(stderr, "port: couldn't write %s\n", P8_PAD_FILE);
}

/* `core` in place of the shelf's own: Splore runs on your PICO-8 whichever
 * core the shelf plays its carts on */
static void launch_port_core(app *a, const system_cfg *s, const char *file, const char *core);

static void launch_port(app *a, const system_cfg *s, const char *file)
{
	launch_port_core(a, s, file, s->core);
}

static void launch_port_core(app *a, const system_cfg *s, const char *file, const char *core)
{
	char path[LIB_PATH * 2], missing[CFG_STR];
	const char *dmode = DMODES[0].name;
	FILE *f;
	bool ok;

	/* PortMaster's entry while PortMaster is not on the card, asked of the
	 * card now: its page, which installs it (src/pminstall.h). And while
	 * its installer runs, a reinstall left going on its page: its page
	 * again, not a PortMaster whose folder is about to be swapped */
	if (!strcmp(s->core, PORT_CORE) && !strcasecmp(file, PM_SCRIPT) &&
	    (pm_offer_refresh(a) || pm_locked())) {
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
		pm_install_screen(a, s, pm_on_card(s->folder), NULL, false);
		return;
	}

	/* A core whose files the player brings, chosen while they are not on the
	 * card (taken off since, by hand): said under the game, as a missing
	 * BIOS is, rather than a script that ends at once */
	if (!core_brought(s, core, missing, sizeof missing)) {
		char msg[CFG_STR + 64];
		const char *name = !strncmp(missing, "Bios/", 5) ? missing + 5 : missing;

		snprintf(msg, sizeof msg, T(S_LAUNCH_NEEDS_BIOS), (int)strlen(name), name);
		fprintf(stderr, "launch: %s refused: %s is not on the card\n", s->name, missing);
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
		result_message(a, s->name, msg);
		return;
	}
	{
		int sys = (int)(s - a->sys.systems);

		if (sys >= 0 && sys < a->sys.count) {
			int k = a->view[sys].dmode;

			dmode = DMODES[k >= 0 && k < DMODE_COUNT ? k : 0].name;
		}
	}
	snprintf(path, sizeof path, "%s/%s/%s", P_ROMS, s->folder, file);
	/* PortMaster itself: its cached lists and settings as it can start on */
	if (!strcmp(s->core, PORT_CORE) && !strcasecmp(file, PM_SCRIPT)) pm_heal_config(s->folder);
	/* Written whole, then renamed into place: launch.sh reads it the moment
	 * this process is gone, and half a path would run nothing. (In
	 * ZYPEROS_FLAG_DIR on a computer, plat_flag, where a check reads it.) */
	f = fopen(plat_flag(PORT_REQUEST ".tmp"), "w");
	/* A program of its own has the in-game menu over it: the system's tag,
	 * the request's third field, says so to launch.sh, which gives it to
	 * portkeys with --menu (src/portmenu.h). A standalone emulator's script
	 * gets the game as its argument, the second field; a port's script is
	 * the game, and its second field is empty (port_menu_for). The fifth,
	 * "mp4", is for a program MP4 plays on over (port_keeps_mp4: your
	 * PICO-8): launch.sh stops MP4 for every other */
	if (!strncmp(core, RUN_PREFIX, strlen(RUN_PREFIX)))
		ok = f && fprintf(f, "%s/%s\t%s\t%s\t%s%s%s\n", P_CARD, core + strlen(RUN_PREFIX), path,
		                  port_menu_for(core) ? s->tag : "", dmode,
		                  port_keeps_mp4(core) ? "\t" : "", port_keeps_mp4(core) ? PORT_KEEPS_MP4 : "") > 0;
	else
		ok = f && fprintf(f, "%s\t\t%s\t%s\n", path, port_menu_for(core) ? s->tag : "", dmode) > 0;
	if (f && fclose(f) != 0) ok = false;
	if (!ok || rename(plat_flag(PORT_REQUEST ".tmp"), plat_flag(PORT_REQUEST)) != 0) {
		fprintf(stderr, "port: couldn't write %s\n", PORT_REQUEST);
		unlink(plat_flag(PORT_REQUEST ".tmp"));
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
		result_message(a, s->name, T(S_LAUNCH_COULDNT_START));
		return;
	}
	/* A's answer, now that the port will start, and before remember_place
	 * waits on the card. */
	if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
	bgm_hold(BGM_HOLD_GAME, true);              /* faded under the loading screen */
	fprintf(stderr, "port: %s\n", path);
	/* What px2-open's status says while no launcher runs to say it, with
	 * launch.sh's process for its quit (src/ctl.h) */
	ctl_port_write(ctl_dir(), (int)getppid(), (long)time(NULL), path);
	/* Its play time, which launch.sh closes as it ends (stats.h, "a program
	 * of its own"): the game on Recents, its time on Played. Not PortMaster
	 * itself, which installs games and is none; Splore is where its carts
	 * are played, so it is */
	if (strcmp(s->core, PORT_CORE) || strcasecmp(file, PM_SCRIPT)) {
		if (!stats_hand_over(s->tag, file, plat_flag(STATS_PORT_NOTE), (long)time(NULL), stats_mono_ms()))
			fprintf(stderr, "port: couldn't write %s; this play is not timed\n", STATS_PORT_NOTE);
	}
	/* The shelf comes back on this game, and an audiobook keeps its place:
	 * launch.sh stops MP4 too, as the port needs the sound card and the
	 * memory, but over your PICO-8 (below). */
	remember_place(a);
	{
		FILE *r = fopen(plat_flag(PORT_RETURN), "w");
		if (r) fclose(r);
	}
	book_keep(true);
	/* MP4 kept playing over it (your PICO-8): its queue to the follower
	 * launch.sh starts beside it, and the resume row kept, for a power-off
	 * over the program (src/mp4hand.h). Any other port takes the sound
	 * (launch.sh stops MP4's daemon), and the launcher that comes back after
	 * it is no start after a power-off: nothing opens MP4 by itself then
	 * (src/mp4resume.h) */
	unlink(plat_flag(MP4HAND_PATH));
	if (port_keeps_mp4(core)) mp4_hand_save();
	else                      resume_clear();
	/* And PICO-8 quiet from its first sound while MP4 plays; never another */
	port_quiet_hand(port_keeps_mp4(core));
	anim_launch(a);
	port_loading_screen(a, s);
	/* "Getting hot" for portkeys to show over the port, which this process
	 * is not there to render then (src/portmenu.h), in either theme, as Auto
	 * can turn it while the port runs */
	notice_render_mark(HEAT_HEAD, HEAT_BODY, NOTICE_MARK_HEAT, PORT_HEAT_LIGHT, DS_LIGHT);
	notice_render_mark(HEAT_HEAD, HEAT_BODY, NOTICE_MARK_HEAT, PORT_HEAT_DARK, DS_DARK);
	/* Your PICO-8's hotkeys and buttons (src/portmenu.h, src/p8pad.h): what
	 * portkeys and PICO-8.sh are handed, and "Screenshot saved" with room
	 * for the shot, in both themes, as Diatom's pop-up has it (on_hotkey) */
	unlink(plat_flag(PORT_KEYS_FILE));
	unlink(plat_flag(P8_PAD_FILE));
	/* ...and its in-game menu opening on Game, on Continue, at its first
	 * MENU, as a game's does after launch's gm_mem_reset */
	unlink(plat_flag(PORT_MENU_MEM));
	if (!strcmp(core, P8_CORE) && port_hotkeys(s->tag)) {
		char shots[CFG_STR * 4], name[LIB_NAME] = "PICO-8", folder[CFG_STR * 2];
		const sysview *v = &a->view[a->sys_cursor];
		size_t i;

		/* The cart by the name its shelf shows, as a Diatom game's shot is;
		 * Splore and PICO-8's own start are PICO-8's */
		if (!splore_file(file) && v->list.count > 0 && !strcmp(v->list.items[v->cursor].file, file))
			snprintf(name, sizeof name, "%s", v->list.items[v->cursor].name);
		for (i = 0; name[i]; i++)
			if (name[i] == '/' || name[i] == '\t' || name[i] == '\\') name[i] = '-';
		snprintf(folder, sizeof folder, "Screenshots/%s", s->folder);
		snprintf(shots, sizeof shots, "%s/%s/%s", P_CARD, folder, name);
		p8_keys_save(s, file, shots);
		p8_pad_save(s, file);
		notice_render_ex(T(S_HOTKEYS_SHOT_SAVED), folder, NULL, NOTICE_SLOT, 0, 0, plat_flag(PORT_SHOT_LIGHT),
		                 DS_LIGHT);
		notice_render_ex(T(S_HOTKEYS_SHOT_SAVED), folder, NULL, NOTICE_SLOT, 0, 0, plat_flag(PORT_SHOT_DARK),
		                 DS_DARK);
	}
	/* Your PICO-8: the PICO-8 shelf's Shader, Strength and Border, for the
	 * library its script loads before SDL (src/p8fx.h), Off and no border
	 * written too, so what is drawn is never an earlier start's */
	if (!strcmp(core, P8_CORE)) p8fx_send(a, (int)(s - a->sys.systems));
	a->running = false;
}

/* ---------- Your PICO-8 and Splore (src/pico8.h, DESIGN.md 11.55) -------- */

/* The page A opens on Splore while your PICO-8 is not on the card: what it
 * is, whose it is, how to add it. Back is its one key: nothing here can
 * bring PICO-8, which is Lexaloffle's to sell. */
static void p8_page_screen(app *a)
{
	char notes[P8_NOTES][P8_NOTE_LEN], path[LIB_PATH * 2];
	const char *np[P8_NOTES];
	SDL_Texture *icon;
	su_page p;
	int nnotes, i;

	snprintf(path, sizeof path, "%s/%s", P_ROOT, P8_ICON);
	icon = access(path, R_OK) == 0 ? IMG_LoadTexture(a->r, path) : NULL;
	p8_page_build(&p, notes, &nnotes);
	for (i = 0; i < P8_NOTES; i++) np[i] = notes[i];
	while (!want_quit && a->running) {
		plat_input_poll(&a->in);
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); break; }
		if (a->in.pressed[IN_SELECT]) mp4_open(a, NULL, NULL);
		levels_keys(&a->in, BOOST_HIDE);
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}
		if (a->in.pressed[IN_ACCEPT] && update_page_key(&p, "A") == SUA_NONE)
			uisound_play(DS_SOUND_DENY);
		tick_tint(a);
		tick_theme(a);
		update_page_draw(a, T(S_PICO8_PAGE_TITLE), &p, np, nnotes, icon);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
	if (icon) SDL_DestroyTexture(icon);
}

bool g_p8_shot;

/* The page as a host shot draws it (src/shots.c) */
void p8_page_shot(app *a)
{
	char notes[P8_NOTES][P8_NOTE_LEN], path[LIB_PATH * 2];
	const char *np[P8_NOTES];
	SDL_Texture *icon;
	su_page p;
	int nnotes, i;

	snprintf(path, sizeof path, "%s/%s", P_ROOT, P8_ICON);
	icon = access(path, R_OK) == 0 ? IMG_LoadTexture(a->r, path) : NULL;
	p8_page_build(&p, notes, &nnotes);
	for (i = 0; i < P8_NOTES; i++) np[i] = notes[i];
	update_page_draw(a, T(S_PICO8_PAGE_TITLE), &p, np, nnotes, icon);
	if (icon) SDL_DestroyTexture(icon);
}

int g_p8_said_shot = -1;

/* What your PICO-8 says over the shelf, as a host shot draws it
 * (src/shots.c, --pico8-said K): 0 to 3 a zip copied by hand, taken (with
 * its version and without) or not (a zip of another PICO-8, one that could
 * not be taken), in p8_take_message's words under the heading main.c gives
 * them; 4 Splore with no network joined; 5 Delete Game on Splore; 6 the
 * question Splore asks with no network, as confirm_ask hands it to
 * confirm_rows_pick, Turn On Wi-Fi focused */
void p8_said_shot(app *a, int k)
{
	static const p8_take TAKES[] = { P8_TAKE_DONE, P8_TAKE_DONE, P8_TAKE_NOT_RASPI, P8_TAKE_FAILED };
	static const char *const ZIPS[] = { NULL, NULL, "pico-8_0.2.7_windows.zip", "pico-8_0.2.7_raspi.zip" };
	char said[256];

	if (k >= 0 && k < 4) {
		p8_take_message(TAKES[k], k == 0 ? "0.2.7" : NULL, ZIPS[k], said, sizeof said);
		result_preview(a, "PICO-8", said);
	} else if (k == 4) {
		result_preview(a, SPLORE_NAME, T(S_PICO8_NO_NETWORK));
	} else if (k == 5) {
		result_preview(a, T(S_GAMEMENU_DELETE), T(S_PICO8_SPLORE_STAYS));
	} else if (k == 6) {
		draw_shelf(a);
		ui_scrim(a->r, 0);
		confirm_preview(a, CQ_SPLORE_WIFI, T(S_PICO8_WIFI_BODY));
	}
}

/* Wi-Fi on and its saved network joined, for Splore: false when no network
 * was joined in 15 s, or the player pressed B */
static bool splore_join(app *a)
{
	unsigned until;

	wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_TURNING_ON));
	wdog_hold("Wi-Fi");                 /* its waits have no end of their own */
	if (!wifi_up()) {
		wdog_release();
		return false;
	}
	wdog_release();
	wifi_pref_save(true);
	until = plat_now_ms() + 15000;
	while (plat_now_ms() < until && a->running) {
		if (wifi_status(NULL, 0, NULL, 0) == WIFI_CONNECTED && net_internet(NULL)) return true;
		plat_input_poll(&a->in);
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_BACK]) { uisound_play(DS_SOUND_BACK); return false; }
		wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_CONNECTING));
		SDL_Delay(250);
	}
	return false;
}

/* A on Splore's entry: the page while your PICO-8 is not on the card; with
 * no way out to the internet and a radio to turn on, the question first
 * (Turn On Wi-Fi, Open Offline: Tarik's pick); then Splore, on your PICO-8
 * (its script's -splore, for the entry's file) */
static void splore_open(app *a, const system_cfg *s, const char *file)
{
	if (!p8_on_card(s)) {
		if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);
		fprintf(stderr, "splore: your PICO-8 is not on the card: its page\n");
		p8_page_screen(a);
		return;
	}
	if (!net_internet(NULL) && plat_has_wifi()) {
		int r = confirm_ask_ex(a, CQ_SPLORE_WIFI, T(S_PICO8_WIFI_BODY), NULL, NULL, NULL);

		if (r == 0) return;
		if (r == 1 && !splore_join(a)) {
			fprintf(stderr, "splore: no Wi-Fi network joined\n");
			result_message(a, SPLORE_NAME, T(S_PICO8_NO_NETWORK));
			return;
		}
	}
	launch_port_core(a, s, file, P8_CORE);
}

/* ---------- Install PortMaster (src/pminstall.h, DESIGN.md 11.32) --------
 *
 * The Ports shelf's PortMaster while it is not on the card: System Update's
 * page with its own words. Install starts res/portmaster/pm-install.py on
 * its own (it outlives this screen: B leaves it working, as System Update's
 * download goes on), and the page draws the status file it writes. */

#define PM_PYTHON "/usr/bin/python3"

static void pm_status_path(char *out, size_t n)
{
	const char *e = getenv("ZYPEROS_PM_STATUS");   /* a host shot's status file */

	snprintf(out, n, "%s", e && *e ? e : PM_STATUS_DIR "/status");
}

/* The installer holds its lock, beside the status file, while it runs */
static bool pm_locked(void)
{
	char p[LIB_PATH], *slash;
	bool busy;
	int fd;

	pm_status_path(p, sizeof p);
	if (!(slash = strrchr(p, '/'))) return false;
	snprintf(slash + 1, sizeof p - (size_t)(slash + 1 - p), "lock");
	if ((fd = open(p, O_RDONLY | O_CLOEXEC)) < 0) return false;
	busy = flock(fd, LOCK_SH | LOCK_NB) != 0;
	close(fd);
	return busy;
}

/* A way out to the internet: Wi-Fi, or a default route on another link
 * (a phone's tethering, QEMU's network, a wired adapter): net_internet. A
 * computer on the USB cable gives no route out, so File Transfer's link is
 * not one. */
static bool pm_network(void)
{
	return net_internet(NULL);
}

static pm_view g_pm;
static unsigned g_pm_read_at, g_pm_started;
/* The page's errand, kept beside the view it reads again: a reinstall
 * (pm_install_screen's `repair`), A pressed on it this visit, and the error
 * PortMaster closed with when that is why it opened */
static bool g_pm_repair, g_pm_asked, g_pm_crashed;
static char g_pm_failed[PM_WHY];
/* --portmaster [running,nonet,nopython,installed]: a host shot's view, the
 * status from ZYPEROS_PM_STATUS; -1 none */
int g_pm_shot = -1;
/* The version of the PortMaster in a Ports folder, "" when it has none */
static void pm_have(const char *folder, char *out, size_t n)
{
	char p[LIB_PATH];

	snprintf(p, sizeof p, "%s/%s/%s/version", P_ROMS, folder, PM_FOLDER);
	up_first_line(p, out, n);
	if (strlen(out) > 31 || strchr(out, ' ')) out[0] = 0;
}

/* Everything the page shows, read again at most four times a second: the
 * page asks every frame */
/* The installer at work, for Auto Off (long_task_busy, from every screen and
 * from a game): its lock, looked at twice a second at most, since the
 * question comes with every frame of every loop and the lock is a file */
bool pm_installing(void)
{
	static unsigned at;
	static bool busy;
	unsigned t = plat_now_ms();

	if (!at || t - at >= 500) {
		at = t ? t : 1;
		/* Python takes a moment to start and take its lock: a run just
		 * begun counts, as the page counts it (pm_view_read) */
		busy = pm_locked() || (g_pm_started && t - g_pm_started < 5000);
	}
	return busy;
}

const pm_view *pm_view_read(const system_cfg *s, bool now)
{
	unsigned t = plat_now_ms();
	char p[LIB_PATH], text[512] = "";

	if (!now && g_pm_read_at && t - g_pm_read_at < 250) return &g_pm;
	g_pm_read_at = t ? t : 1;
	memset(&g_pm, 0, sizeof g_pm);
	pm_status_path(p, sizeof p);
	up_read(p, text, sizeof text);
	pm_status_parse(text, &g_pm.st);
	g_pm.repair = g_pm_repair;
	g_pm.asked = g_pm_asked;
	g_pm.crashed = g_pm_crashed;
	snprintf(g_pm.failed, sizeof g_pm.failed, "%s", g_pm_failed);
	if (g_pm_shot >= 0) {
		const char *why = getenv("ZYPEROS_PM_WHY");

		g_pm.running = g_pm_shot & PM_SHOT_RUNNING;
		g_pm.network = !(g_pm_shot & PM_SHOT_NONET);
		g_pm.python = !(g_pm_shot & PM_SHOT_NOPYTHON);
		g_pm.installed = g_pm_shot & PM_SHOT_INSTALLED;
		g_pm.repair = g_pm_shot & (PM_SHOT_REPAIR | PM_SHOT_FAILED);
		g_pm.asked = g_pm.repair && (g_pm.running || g_pm.st.reinstall);
		g_pm.crashed = g_pm_shot & PM_SHOT_FAILED;
		if (g_pm.crashed)
			snprintf(g_pm.failed, sizeof g_pm.failed, "%s",
			         why ? why : "ModuleNotFoundError: No module named 'sdl2'");
		snprintf(g_pm.have, sizeof g_pm.have, "%s", "2026.11.12-1059");
		return &g_pm;
	}
	pm_have(s->folder, g_pm.have, sizeof g_pm.have);
	/* Python takes a moment to start and take its lock: a run just begun
	 * is running until it says it is done or failed */
	g_pm.running = pm_locked() ||
	               (g_pm_started && t - g_pm_started < 5000 &&
	                g_pm.st.state != PM_DONE && g_pm.st.state != PM_ERROR);
	g_pm.network = pm_network();
	g_pm.python = access(PM_PYTHON, X_OK) == 0;
	g_pm.installed = pm_on_card(s->folder);
	return &g_pm;
}

static void pm_spawn(const system_cfg *s, bool reinstall)
{
	char script[LIB_PATH], ports[LIB_PATH], status[LIB_PATH];
	char *argv[] = { (char *)PM_PYTHON, (char *)"-I", script, (char *)"--ports", ports,
	                 (char *)"--status", status, reinstall ? (char *)"--reinstall" : NULL, NULL };
	FILE *f;

	snprintf(script, sizeof script, "%s/%s", P_ROOT, PM_INSTALLER);
	snprintf(ports, sizeof ports, "%s/%s", P_ROMS, s->folder);
	pm_status_path(status, sizeof status);
	/* The last run's word is not this one's: the page says it is looking
	 * from the press on, until the installer writes its own */
	mkdir(PM_STATUS_DIR, 0755);
	if ((f = fopen(status, "w"))) {
		fputs(reinstall ? "state checking\nkind reinstall\n" : "state checking\n", f);
		fclose(f);
	}
	fprintf(stderr, "portmaster: %s into %s\n", reinstall ? "reinstalling" : "installing", ports);
	plat_spawn_detached(argv, NULL, P_ROOT);
	g_pm_started = plat_now_ms();
	if (!g_pm_started) g_pm_started = 1;
	g_pm_read_at = 0;
}

/* PortMaster's icon for its page, or NULL (Zyper, then) */
SDL_Texture *pm_icon_tex(app *a)
{
	char path[LIB_PATH * 2];

	return pm_icon_path(path, sizeof path) ? IMG_LoadTexture(a->r, path) : NULL;
}

static void pm_install_loop(app *a, const system_cfg *s, SDL_Texture *icon)
{
	char notes[PM_NOTES][120];
	bool had = pm_view_read(s, true)->installed;

	while (!want_quit && a->running) {
		const pm_view *v = pm_view_read(s, false);
		su_action act = SUA_NONE;
		su_page p;
		int nnotes;

		/* A reinstall seen at work is this page's to follow to its end,
		 * started here or before the page opened */
		if (g_pm_repair && v->running) g_pm_asked = true;
		pm_page_build(v, &p, notes, &nnotes);
		/* Installed while the page is up: the shelf's A says Play now */
		if (v->installed && !had) {
			had = true;
			pm_offer_refresh(a);
		}
		plat_input_poll(&a->in);
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (a->in.pressed[IN_SELECT]) mp4_open(a, NULL, NULL);
		levels_keys(&a->in, BOOST_HIDE);
		if (a->in.pressed[IN_ACCEPT]) act = update_page_key(&p, "A");
		if (a->in.pressed[IN_BACK] || a->in.pressed[IN_MENU]) {
			uisound_play(DS_SOUND_BACK);
			break;
		}
		if (a->in.pressed[IN_ACCEPT] && act == SUA_NONE) uisound_play(DS_SOUND_DENY);
		switch (act) {
		case SUA_DOWNLOAD:
			uisound_play(DS_SOUND_ACCEPT);
			pm_spawn(s, g_pm_repair);
			g_pm_asked = true;
			break;
		case SUA_OPEN:
			/* PortMaster, run as any port is: the launcher exits for it */
			pm_offer_refresh(a);
			launch_port(a, s, PM_SCRIPT);
			return;
		case SUA_BACK:
			uisound_play(DS_SOUND_BACK);
			pm_offer_refresh(a);
			return;
		default:
			break;
		}

		tick_tint(a);
		tick_theme(a);
		{
			const char *np[PM_NOTES];
			int i;

			for (i = 0; i < PM_NOTES; i++) np[i] = notes[i];
			update_page_draw(a, T(g_pm_repair ? S_PORTMASTER_REINSTALL_TITLE : S_PORTMASTER_TITLE),
			                 &p, np, nnotes, icon);
		}
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(16);
	}
	pm_offer_refresh(a);
}

/* The page: Install PortMaster, or with `repair` Reinstall PortMaster, the
 * error it closed with in `failed` when `crashed` (pm_after_run) */
void pm_install_screen(app *a, const system_cfg *s, bool repair, const char *failed,
                       bool crashed)
{
	SDL_Texture *icon = pm_icon_tex(a);

	g_pm_repair = repair;
	g_pm_asked = false;
	g_pm_crashed = repair && crashed;
	snprintf(g_pm_failed, sizeof g_pm_failed, "%s", failed ? failed : "");
	g_pm_read_at = 0;
	pm_install_loop(a, s, icon);
	g_pm_repair = g_pm_asked = g_pm_crashed = false;
	g_pm_failed[0] = 0;
	g_pm_read_at = 0;
	if (icon) SDL_DestroyTexture(icon);
}

/* The last `n` bytes of a file at most, from a line's start, into a buffer
 * of n + 1; false when it cannot be read */
static bool pm_read_tail(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	long size;
	size_t got;
	char *nl;

	buf[0] = 0;
	if (!f) return false;
	if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > (long)n)
		fseek(f, size - (long)n, SEEK_SET);
	else
		fseek(f, 0, SEEK_SET);
	got = fread(buf, 1, n, f);
	fclose(f);
	buf[got] = 0;
	/* Cut into the middle of a line: from the next one */
	if (got == n && (nl = strchr(buf, '\n'))) memmove(buf, nl + 1, strlen(nl + 1) + 1);
	return true;
}

/* How long a PortMaster that left no word in its log may have run for it
 * still to be one that never started: its launcher script truncates the log
 * as one of its first steps */
#define PM_QUIET_START_S 20

/* Back from PortMaster (launch.sh's PM_RAN): when it closed with an error,
 * its page says so, with the line of its log that says how, and offers
 * Reinstall PortMaster, before the shelf (DESIGN.md 11.33). Before this a
 * PortMaster that could not start fell back to the shelf with nothing said,
 * every time it was opened. */
void pm_after_run(app *a)
{
	static char tail[32 * 1024 + 1];
	char ran[96], log[LIB_PATH * 2], why[PM_WHY] = "";
	long long start = 0, end = 0;
	const system_cfg *s = NULL;
	struct stat st;
	bool crashed;
	int rc = 0, i;

	if (!up_read(PM_RAN, ran, sizeof ran)) return;
	unlink(PM_RAN);
	if (sscanf(ran, "%lld %lld %d", &start, &end, &rc) < 2) return;
	for (i = 0; i < sys_kept(a) && !s; i++)        /* Ports off the home too */
		if (!strcmp(a->sys.systems[i].core, PORT_CORE) && pm_on_card(a->sys.systems[i].folder))
			s = &a->sys.systems[i];
	if (!s) return;
	snprintf(log, sizeof log, "%s/%s/%s/%s", P_ROMS, s->folder, PM_FOLDER, PM_LOG);
	if (stat(log, &st) != 0 || (long long)st.st_mtime + 2 < start)
		/* Its log not written this run: the script stopped before it */
		crashed = end - start < PM_QUIET_START_S;
	else
		crashed = pm_read_tail(log, tail, sizeof tail - 1) && pm_failure(tail, why, sizeof why);
	if (!crashed) return;
	fprintf(stderr, "portmaster: closed with an error after %llds (status %d): %s\n",
	        end - start, rc, why[0] ? why : "nothing in its log");
	pm_install_screen(a, s, true, why, true);
}

void launch(app *a)
{
	/* The game has the screen, and the CPU: no snap under it */
	snap_off();
	sysview *v = &a->view[a->sys_cursor];
	/* The game's system, not the shelf's. Launching a favorite off the
	 * Favorites shelf otherwise loads whatever core the shelf claims, which
	 * is none, and looks for the ROM under a folder that does not exist. */
	int o = shelf_owner(a, a->sys_cursor, v->cursor);
	const system_cfg *s = &a->sys.systems[o];
	char core[CFG_STR * 2], elf[CFG_STR * 2], rom[LIB_PATH * 2];
	char st[LIB_PATH * 2], pv[LIB_PATH * 2], apv[LIB_PATH * 2], cf[LIB_PATH * 2];
	char save[CFG_STR * 2], bios[CFG_STR * 2];
	char active[LIB_PATH * 2] = "";
	char set[LIB_PATH * 2];
	int  console = 0;
	bool first_play = false;
	/* 22 fixed entries (the shader's two and its strength's since 1.0.5)
	 * plus NULL, seven more for a DS game's two screens (or two for a late skip,
	 * frameskip|tag|late), two for Timing's
	 * --pace (1.0.6), then two per core option. Sized off the loader's own cap so the two cannot drift apart:
	 * the previous 20 was already 18 full, and a silent bound check would
	 * have dropped every option rather than failing loudly. */
	char *argv[35 + 2 * 32];
	bool resident = false, want_menu;
	/* Diatom said its core would not load (src/startfail.h): the shelf says
	 * so, and a second Diatom that would fail the same way is not started */
	bool core_said = false;
	char core_msg[320] = "";
	/* A DS game's resume by its language (ds_resume_fresh): what it will
	 * run in, whether it starts fresh, and its auto state as it was */
	char ds_lang[16] = "";
	bool ds_fresh = false, ds_had = false;
	/* And by the core that left it (core_resume_fresh) */
	bool core_fresh = false;
	struct stat ds_before;
	unsigned waited;
	int n = 0;

	if (v->list.count == 0) return;
	/* Not beside File Transfer: the game takes the loop that moves the files
	 * (2026-11-06). Closed for it, or the player says when. */
	if (!xs_before_game(a)) return;
	/* The grid's rows ahead let go: the game gets their memory
	 * (DESIGN.md 11.47) */
	if (v->grid && v->gm.set) evict_grid(v, gg_cols(&v->gm), true);

	snprintf(core, sizeof core, "%s/cores/%s_libretro.so", P_ROOT, s->core);
	snprintf(elf, sizeof elf, "%s/diatom", P_ROOT);
	/* Captured once, beside the path it goes into: play time is keyed on the
	 * launch path the same way favorites are, and reading the cursor again
	 * further down would be reading it at a different moment. */
	const char *romfile = v->list.items[v->cursor].file;

	/* Splore's entry on the PICO-8 shelf, whichever core plays its carts */
	if (!strcmp(s->tag, P8_TAG) && splore_file(romfile)) {
		splore_open(a, s, romfile);
		return;
	}
	if (is_port(s)) {
		launch_port(a, s, romfile);
		return;
	}

	/* A new game: its in-game menu starts on the Game tab again */
	gm_mem_reset();
	snprintf(rom, sizeof rom, "%s/%s/%s", P_ROMS, s->folder, romfile);
	snprintf(save, sizeof save, "%s/Saves", P_CARD);
	snprintf(bios, sizeof bios, "%s/Bios", P_CARD);

	/* The game's own file, before anything is sent or started for it: an
	 * empty one, or one too short to be its kind, is said here (gamefile.h).
	 * A 0-byte PSP .iso crashed Diatom inside PPSSPP and came back to the
	 * shelf with nothing said (2026-11-16). */
	{
		unsigned long long have;
		gamefile_state gs = gamefile_check(rom, &have);

		if (gs != GAMEFILE_OK) {
			char msg[160];

			gamefile_message(gs, have, msg, sizeof msg);
			fprintf(stderr, "launch: %s refused: %s is %llu bytes\n", s->name, romfile, have);
			if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
			result_message(a, v->list.items[v->cursor].title, msg);
			return;
		}
	}
	/* A zip Diatom unpacks (a cartridge's: a shelf whose core takes no
	 * archive itself, the arcades' listing 7z and MAME's) whose game is
	 * compressed in a way it cannot read, bzip2 or Zstandard: said here,
	 * where a start would have come back with nothing said (1.0.7) */
	if (romfile && strlen(romfile) > 4 && !strcasecmp(romfile + strlen(romfile) - 4, ".zip") &&
	    !strstr(s->exts, "7z") && strcmp(s->core, "mame2003_plus")) {
		int m = gamefile_zip_method(rom);

		if (m >= 0 && !gamefile_zip_reads(m)) {
			fprintf(stderr, "launch: %s refused: %s is compressed by method %d\n", s->name, romfile, m);
			if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
			result_message(a, v->list.items[v->cursor].title, T(S_LAUNCH_ZIP_METHOD));
			return;
		}
	}

	/* A DS game: two screens in one frame, a DraStic save (ROGUE OS's) to
	 * take over the first time, and frames left undrawn when they run late -
	 * DSperate's 3D is the heaviest thing a Cortex-A35 is asked to draw, and
	 * a skipped drawing keeps the game at its speed where a late frame slows
	 * it. */
	g_game_dual = two_screens(s);
	if (g_game_dual) dsv_import(save, romfile);
	/* The game stopped by itself last time while cheats were on: asked
	 * first, the cursor on Without Cheats, which starts it with none this
	 * time and keeps them set for later (DESIGN.md 11.66). B: not started. */
	bool cheats_without = false;
	if (cheats_crash_asks(s, romfile)) {
		char q[480];
		int r;

		cq_text(CQ_CHEATS_CRASH, q, sizeof q);
		r = confirm_ask_ex(a, CQ_CHEATS_CRASH, q, NULL, NULL, NULL);
		if (r == 0 || !a->running) return;
		cheats_without = r == 1;
	}
	{
		/* The RUN keys this game needs beyond the usual: a DS game's two
		 * screens, the system's clock ceiling, controller type and d-pad
		 * stick, and its button map (config.h: power, controller, stick,
		 * map) */
		char keys[1536], stick[24];
		rm_table sent;
		char map[320];
		bool stick_on = rms_stick_get(s, romfile);

		/* The motor at the player's Rumble level (the game's own, its
		 * system's or MENU's) times the system's share (src/rumble.h) */
		cfg_run_keys_at(s, g_game_dual, pacing_id(pacing_load(s)),
		                rumble_run_pct(s->rumble, rumble_scope(s->tag, romfile)), keys, (int)sizeof keys);
		/* The whole table, whatever the card holds for the game or its
		 * system (rms_sent): Diatom is sent no turbo of its own */
		rms_sent(s, romfile, stick_on, &sent);
		/* The d-pad as the stick, a game's own (rms_stick_get): the PSP's
		 * stick=toggle-stick when it was left on the stick, and a stick
		 * that is off on the d-pad's side when a button turns it */
		rms_run_stick(s, stick_on, rm_has_stick(&sent), stick, sizeof stick);
		if (stick[0] && strlen(keys) + strlen(stick) + 2 <= sizeof keys)
			snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%s%s",
			         keys[0] ? "\t" : "", stick);
		/* Super Game Boy, the shelf's (src/sgbmode.h): Diatom reads the
		 * game's header for it. Off sends nothing, as before. */
		if (sgbm_shelf(s->tag, s->core) && sgbm_load(s) != SGBM_OFF &&
		    strlen(keys) + 12 <= sizeof keys)
			snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%ssgb=%s",
			         keys[0] ? "\t" : "", sgbm_id(sgbm_load(s)));
		/* The player's hotkeys for this game (src/hkstore.h), sent only
		 * when they are not ZyperOS's own, so a player who never set one
		 * sends what was always sent; and where a screenshot goes,
		 * Screenshots/<System>/<Game>, Diatom adding the time, with every
		 * game, so a key given in its Controls tab works at once */
		{
			hk_table hk, def;
			char run[200], name[LIB_NAME], shots[CFG_STR * 4];

			hks_get(HKS_GAME, s->tag, romfile, &hk);
			hk_default(&def);
			if (!hk_equal(&hk, &def)) {
				hk_run(&hk, run, sizeof run);
				if (strlen(keys) + strlen(run) + 10 <= sizeof keys)
					snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%shotkeys=%s",
					         keys[0] ? "\t" : "", run);
			}
			/* Fast Forward's speed, where it is not 2x, Diatom's own */
			if (hk.ff != def.ff && strlen(keys) + 16 <= sizeof keys)
				snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%sffspeed=%s",
				         keys[0] ? "\t" : "", hk_ff_word(hk.ff));
			{
				/* The game's part of the name as src/shotname.h has it,
				 * which the Screenshots shelf reads back */
				shot_game_part(v->list.items[v->cursor].name, name, sizeof name);
				snprintf(shots, sizeof shots, "%s/%s/%s/%s", P_CARD, SHOT_DIR, s->folder, name);
				if (strlen(keys) + strlen(shots) + 8 <= sizeof keys)
					snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%sshots=%s",
					         keys[0] ? "\t" : "", shots);
			}
		}
		/* MENU's chords (src/menukeys.h), which work in a game too: Diatom
		 * takes the screenshot's button under MENU as SELECT's hotkey does,
		 * and pauses for the lock (PAUSED for=lock, game_lock). With every
		 * game, so a key changed in MENU > System > Hotkeys holds at once */
		{
			char mk[48];

			mk_run(mk_now(), mk, sizeof mk);
			if (strlen(keys) + strlen(mk) + 12 <= sizeof keys)
				snprintf(keys + strlen(keys), sizeof keys - strlen(keys), "%smenukeys=%s",
				         keys[0] ? "\t" : "", mk);
		}
		/* The cheats that are on for this game, handed over as it loads
		 * (src/cheats_screen.h): cheats= on RUN, none for most games */
		cheats_game_begin(s, romfile, cheats_without, keys, sizeof keys);
		if (keys[0]) plat_resident_run_keys(keys);
		rm_format(&sent, map, sizeof map);
		plat_resident_map(strcmp(map, "identity") ? map : "");
		/* And an N64 game's Pak, its own over the system's; none else */
		if (pak_system(s))
			plat_resident_opt(gm_pak_option(s->core), pak_get(s, romfile));
		else
			plat_resident_opt(NULL, NULL);
	}

	/* A disc that needs firmware, before anything tries to run it.
	 *
	 * Diatom checks this too and answers bios_missing, but that answer has
	 * never been reachable: it acts on a `firmware=` key the launcher has
	 * never sent. So the core was asked to load a CD with no System Card,
	 * refused it the way it refuses a corrupt ROM, and the shelf came back
	 * with nothing said - which is a long way from "needs Bios/syscard3.pce".
	 *
	 * Disc images only. A HuCard needs no System Card, and refusing every
	 * cartridge on the shelf because a CD would have needed one is a worse
	 * failure than the one being fixed. Every game where the shelf's whole
	 * library needs it (bios_every, a `bios|tag|names` line: the
	 * ColecoVision's). And a file of a type that needs firmware of its own
	 * (a `bios|tag|names|extensions` line): an .fds image on the NES shelf
	 * needs disksys.rom, which FCEUmm refuses the game without, while a
	 * cartridge beside it needs nothing.
	 *
	 * And a file of the wrong size as well as no file: the core would start
	 * a 0-byte saturn_bios.bin to a black screen with no sound. Looked at in
	 * the core's order, so a bad first name is said even beside a good later
	 * one the core never reaches (src/biosfile.h). */
	{
		const char *gf = v->list.items[v->cursor].file;
		bios_result br;

		if (bios_check_file(s, gf, lib_is_disc(gf), bios, &br) != BIOS_OK) {
			char msg[CFG_STR + 160];

			bios_message(&br, msg, sizeof msg);
			if (br.state == BIOS_WRONG_SIZE)
				fprintf(stderr, "launch: %s refused: Bios/%s is %llu bytes, not %u\n", s->name,
				        br.name, br.have, br.want);
			if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_DENY);
			result_message(a, v->list.items[v->cursor].title, msg);
			return;
		}
	}

	/* A's answer, once nothing is left that can refuse the launch: the
	 * work below reads files and is not to be waited for. Only for A: a game
	 * resumed at boot, or started by ZYPEROS_AUTOLAUNCH, had no press. */
	if (a->in.pressed[IN_ACCEPT]) uisound_play(DS_SOUND_ACCEPT);

	/* The autosave story, by path rather than by convention: the state and the
	 * preview live beside each other, the launch hands both over, and a game
	 * always comes up where it was left. */
	state_path(a, o, &v->list.items[v->cursor], st, sizeof st);
	/* Not a resume a DS game would come back from in another language than
	 * the one Game Language says now (src/dsresume.h) */
	ds_fresh = ds_resume_fresh(s, romfile, st, ds_lang, sizeof ds_lang);
	/* Nor one the shelf's other core left (src/corestate.h) */
	core_fresh = !ds_fresh && core_resume_fresh(s, romfile, st);
	ds_had = ds_lang[0] && stat(st, &ds_before) == 0;
	quick_bind(a, o, &v->list.items[v->cursor]);
	/* The scratch, not the Auto card - see pause_preview_path. The card is
	 * written from this only when the game actually ends, below. */
	pause_preview_path(pv, sizeof pv);
	/* and the frame its card will show once played (src/cardart.h) */
	card_frame_path(a, o, &v->list.items[v->cursor], cf, sizeof cf);
	persist_dir_ensure(a, o);

	/* Achievements, if this game has any. Most of a library does not, and that
	 * is not a failure: chv_load says so by returning false and everything
	 * below carries on with console 0 and no set, which is what Diatom reads
	 * as "this game has none". */
	{
		chv_path(P_ROMS, s->folder, v->list.items[v->cursor].name,
		         set, sizeof set);
		/* Every launch, not just a first play: this is what a late fetch is
		 * checked against, and it has to name the game on screen now rather
		 * than the last one that went looking for a set. */
		snprintf(g_game_set, sizeof g_game_set, "%s", set);
		g_game_no_hash = ra_hash_support(s->tag, romfile, s->exts);

		/* The async slot, cleared of an account answer from the last game that
		 * never landed, before this launch asks anything of its own. And no
		 * watch list to trim until this game has handed one over. */
		sync_abandon();
		g_watch_ms = 0;

		/* Fetch it if this game has never been played here. Only then: a
		 * cached set costs nothing and this is the launch path, so the delay
		 * is paid once per game rather than every time. Failing is ordinary -
		 * offline, not signed in, or a game RetroAchievements has never seen -
		 * and the launch carries on without. */
		/* Never played here, so there is no set to hand over. The game starts
		 * anyway and the set is found behind it - see ra_fetch_begin. Nothing
		 * in front of the launch. */
		/* Not for a game the launcher has no hash for: nothing would be
		 * asked, and its rows already say so (ra_hash_support) */
		first_play = ra_signed_in() && access(set, R_OK) != 0 && net_online() &&
		             g_game_no_hash == RA_HASH_OK;

		if (chv_load(set)) {
			/* Reconcile with the account BEFORE deciding what to watch.
			 * Without this the launcher filters against what this device
			 * happens to have seen, which is not the same question and does
			 * not look different: measured 2026-08-29 as 3 of 40 for Contra
			 * against the site's own 13. */
			/* The account's answer, without the launch waiting for it - read
			 * while the game runs, see sync_poll. On a first play there is
			 * nothing to ask about yet; that question is asked once the set
			 * arrives. */
			if (!first_play) sync_begin(chv_game(), set);

			/* No "startsession" request, deliberately. It drives the "currently
			 * playing" indicator on the website and nothing on the device, and
			 * it is another 320ms request - which is the exact trade this whole
			 * path exists to refuse. */

			chv_active_path(active, sizeof active);
			if (chv_write_active(active)) {
				console = chv_console();
			} else {
				/* Everything in the set is already earned. Nothing to watch,
				 * and sending an empty file would have Diatom log a set with
				 * no achievements in it every launch. */
				active[0] = '\0';
			}
		}
	}

	/* The pop-ups start with this game; the first is what is earned so far,
	 * once the game is running (the tick shows it), when the set was here
	 * before the launch - a first play says it when the set arrives */
	nq_clear(&g_nq);
	/* Said as the game starts, ahead of anything else: why it is not where
	 * it was left */
	if (ds_fresh || core_fresh) {
		nq_item it;

		memset(&it, 0, sizeof it);
		it.kind = NQ_ALERT;
		it.ms = 4000;
		snprintf(it.head, sizeof it.head, "%s",
		         ds_fresh ? T(S_POPUP_DSLANG_HEAD) : T(S_POPUP_CORESTATE_HEAD));
		snprintf(it.body, sizeof it.body, "%s",
		         ds_fresh ? T(S_POPUP_DSLANG_BODY) : T(S_POPUP_CORESTATE_BODY));
		nq_push(&g_nq, &it);
	}
	if (active[0] || (chv_count() > 0 && !first_play)) {
		nq_item it;

		memset(&it, 0, sizeof it);
		it.kind = NQ_SET;
		it.ms = 3000;
		snprintf(it.head, sizeof it.head, T(S_CHEEVOLIST_UNLOCKED), chv_earned(), chv_count());
		snprintf(it.body, sizeof it.body, "%s", chv_game_title());
		nq_push(&g_nq, &it);
	}

	remember_place(a);
	/* Before the launch, not after: a device that loses power during the load
	 * was still playing this game. */
	playing_set(a);

	/* Read and cleared together. Leaving it set would have the NEXT game
	 * launched in this session open with its menu up too, which is the sort
	 * of thing that looks like a haunting rather than a bug. */
	want_menu = a->resume_menu;
	a->resume_menu = false;

	/* The output is let go the moment A's accept ends, well before the game
	 * makes a sound of its own. */
	uisound_release();
	/* Menu music fades out under the launch; the shelf lets it go again */
	bgm_hold(BGM_HOLD_GAME, true);

	/* Whether this game should be heard - quiet while MP4 plays, Diatom's
	 * QUIET. Stated before the RUN goes out, which sends it. */
	plat_resident_quiet(mp4c_playing());
	/* And whether the battery is low, so the disc is over its first frame. */
	plat_resident_low_battery(battery_level());

	/* A game resumed at boot waited 71 and 47 ms for the resident on the
	 * Pixel 2 (2026-10-05, a DAC plugged in), where it had been falling back
	 * unseen. A second is ten times that, and only ever spent on a Diatom
	 * that is running but stuck; one that isn't running falls back at once. */
	if (plat_resident_await(1000, &waited)) {
		if (waited)
			fprintf(stderr, "resident emulator answered after %u ms\n", waited);
		/* The emulator is already up, holding its context and every core,
		 * so this is ~200ms rather than ~1100. Nothing here is torn
		 * down -- this process keeps its own context through the whole game,
		 * which is why coming back is a frame rather than a second and a
		 * half. The two do overlap for the length of this animation, while
		 * the game loads behind it; after that only the emulator draws,
		 * because this process is blocked - except when the player opens
		 * the in-game menu, which is drawn HERE now, over the frame the
		 * emulator hands us on the way into its pause. */
		aout_before_launch();
		if (plat_resident_send(&(plat_game_run){
		        .tag = s->tag, .core = core, .rom = rom,
		        .resume = ds_fresh || core_fresh || g_ctl_fresh ? "" : st,
		        .exit_state = st, .preview = pv, .card = cf,
		        .console = console, .cheevos = active[0] ? active : NULL })) {
			int r;

			/* What px2-open's status says from here to the wait's end */
			memset(&g_ctl_game, 0, sizeof g_ctl_game);
			g_ctl_game.on = true;
			screen_shader_apply();   /* the screens' shader off over the game */
			snprintf(g_ctl_game.tag, sizeof g_ctl_game.tag, "%s", s->tag);
			snprintf(g_ctl_game.name, sizeof g_ctl_game.name, "%s", s->name);
			snprintf(g_ctl_game.core, sizeof g_ctl_game.core, "%s", s->core);
			snprintf(g_ctl_game.path, sizeof g_ctl_game.path, "%s", rom);
			g_ctl_game.since = (long)time(NULL);
			if (g_ctl_fresh) fprintf(stderr, "ctl: %s starts fresh, without its auto state\n", rom);

			/* The list Diatom was just handed, for an early answer to trim. */
			if (active[0]) {
				g_watch_ms = plat_now_ms();
				snprintf(g_watch_active, sizeof g_watch_active, "%s", active);
			}

			/* Play time starts here, and costs a clock read. Nothing is
			 * opened or written - the launch path does no I/O for this, on
			 * purpose, because a millisecond bought here is the first of
			 * several. See stats.h. */
			stats_begin(s->tag, romfile, plat_now_ms());

			/* Straight after RUN and the levels, and for the same reason: the
			 * mode is Diatom's own global and survives from the last game, so
			 * a system that has never been set would otherwise inherit
			 * whatever the previous one chose. Ordered on the same socket, so
			 * it lands before the first frame. The shader with it, Diatom's
			 * global as well. */
			plat_resident_line("SETDISPLAY\tmode=%s\tfilter=%s\tstrength=%d",
			                   DMODES[a->view[o].dmode].name,
			                   SHADERS[a->view[o].shader].name,
			                   strength_clamp(a->view[o].strength));
			/* Its Border (src/border.h), the system's, after the mode it
			 * fills around: Diatom drops it at each RUN, so a system with
			 * none is black around as before. Off sends nothing more. */
			if (border_load(a, o) != BORDER_OFF) border_send(a, o);
			/* On-Screen Stats, the player's, the same way: Diatom keeps
			 * it from game to game, and a new launcher's is the one kept */
			{
				hm_state hs;

				hm_load(&hs);
				hm_send(&hs);
			}
			/* The DS layout, for the same reason and in the same place: the
			 * layout is Diatom's own global too. */
			if (g_game_dual) {
				int lay;
				bool sw;

				screens_get(s->tag, &lay, &sw);
				plat_resident_line("SETSCREENS\tlayout=%s\tswap=%d",
				                   gm_layout_names[lay], sw ? 1 : 0);
			}

			/* Auto Off. Out here rather than inside the chv_load branch
			 * above, which is where the first version put it and is the same
			 * mistake the paragraph below describes: that branch is the one a
			 * game with no achievements never takes, so Auto Off only ever
			 * worked for games that happened to have a cached set.
			 *
			 * Unless on the charger - the feature exists to stop a game
			 * running unattended on battery, and plugged in there is no
			 * battery reason and no cost to leaving it. on_game_tick keeps
			 * this true if the cable changes mid-game. */
			g_idle_secs = a->auto_off;
			g_idle_charging = idle_held();
			plat_resident_line("SETIDLE\tms=%d",
			                   (g_idle_secs > 0 && !g_idle_charging)
			                       ? g_idle_secs * 1000 : 0);

			/* The first play of this game: nothing was cached, so there is a
			 * set to go and find. Out here and not inside the chv_load branch
			 * above, which is the mistake the first version made - that
			 * branch is precisely the one a first play does not take.
			 *
			 * Hashing is local work and happens after RUN, so it overlaps the
			 * game's own startup rather than delaying it - on a thread of its
			 * own since 1.0.5 (src/rahashjob.h), because a PSP disc's hash
			 * can take seconds and this loop is the power button's watchdog.
			 * The two requests then run behind the game and on_game_tick
			 * hands the set over when they land. A file the launcher cannot
			 * read gets the "none" marker there; a kind it has no hash for at
			 * all never gets here (first_play) and needs no marker: its rows
			 * say so by its name. */
			if (first_play) {
				chv_active_path(g_pending_active, sizeof g_pending_active);
				if (rhj_begin(rom, s->tag, set)) {
					snprintf(g_pending_set, sizeof g_pending_set, "%s", set);
					g_pending_hashing = true;
				}
			}
			/* Coming back from a shutdown: the game loads and the menu is
			 * already up, so nothing is handed control of a game the player
			 * may not have meant to resume. Ordered on the same socket, so it
			 * lands before the first frame the player could act on. */
			if (want_menu) plat_resident_line("PAUSE");
			g_menu_unasked = want_menu;
			/* "2 cheats on" over the first frames, so none is forgotten */
			if (!want_menu) cheats_game_started();
			/* No launch animation, and it is a display-safety rule, not a
			 * taste call: Diatom presents through fbdev, this process
			 * through GL, and the handoff spike's one invariant is that
			 * they never present concurrently - a 190ms overlap that is
			 * harmless GL-on-GL is the exact case that wedges the display
			 * engine when one side is fbdev. A warm launch is 26 to 44 ms from
			 * RUN to RUNNING, so there is nothing to animate over anyway; the
			 * shelf simply holds until the game's first frame replaces it. */
			/* The downloads behind the screens wait while the game runs
			 * (src/dlq.h): no frame of it is spent on them */
			dlq_game(true);
			for (;;) {
				r = plat_resident_wait();
				if (r == RES_PAUSED) {
					const char *why = plat_resident_paused_for();

					g_ctl_game.paused = true;
					/* The volume keys over it are the game's (src/levels.h):
					 * a press down empties a cell, as in the game */
					levels_game_paused(true);
					/* A hotkey's pause (src/hotkeys.h): Quit stops the
					 * game as MENU then X does, Reset Game asks first;
					 * MENU's lock chord locks over it (src/menukeys.h);
					 * MENU's opens the menu */
					if (!strcmp(why, "quit")) gm_quit();
					else if (!strcmp(why, "reset")) game_reset_ask(a);
					else if (!strcmp(why, "lock")) game_lock(a);
					else game_menu(a);
					levels_game_paused(false);
					g_ctl_game.paused = false;
					continue;
				}
				break;
			}
			dlq_game(false);
			g_ctl_game.on = false;
			screen_shader_apply();
			resident = (r == RES_EXIT);
			/* A game that stopped by itself with cheats on asks at its next
			 * start whether to start without them */
			cheats_game_end(resident && plat_resident_crashed());
			/* The game is over, and however it ended it ended here: Quit,
			 * a crash (EXIT reason=crash) or Diatom gone (RES_DEAD), POWER,
			 * the battery, Auto Off, px2-open's next game, a TERM. NES
			 * Player 2 goes with it: p2d, Diatom's share, and the hotspot
			 * with the Wi-Fi the player had back (a second or two, said as
			 * File Transfer says it) or the cable's link as it was. */
			p2_game_over();          /* before the panel's tick: nothing more is said */
			if (p2_link_on() == P2_HOTSPOT)
				wait_panel(a, T(S_PLAYER2_ROW), T(S_XFER_CLOSING_HOTSPOT));
			p2_stop();

			/* The Auto card's picture, written ONLY here - at the same moment
			 * Diatom wrote the state it belongs to, so the two cannot drift.
			 * Between exits the card keeps the previous session's frame, which
			 * is exactly what resuming would give you. */
			if (resident) {
				char ap[LIB_PATH * 2];

				preview_path(a, o, &v->list.items[v->cursor],
				             ap, sizeof ap);
				copy_file(plat_resident_last_preview(), ap);
			}

			/* The game is over, so this is the one write that is synced -
			 * on the path where the player is waiting for the shelf rather
			 * than for a game to start. */
			stats_end(NULL, plat_now_ms());

			/* Whatever was earned is sent now, behind the shelf rather than in
			 * front of it - see ra_flush_start. It used to be sent right here,
			 * blocking, on the reasoning that the wait loop above is the power
			 * button's watchdog and so the first safe moment was after it. True
			 * of that loop, and not the only place the watchdog matters: this
			 * spot is between that loop and the shelf's, so the button was
			 * read by neither while it waited. Whatever will not send stays
			 * queued. */
			/* After the account's answer, so anything it already held is
			 * settled before deciding what is owed - otherwise the flush would
			 * cheerfully submit a dozen duplicates. The answer has usually been
			 * in since half a second into the game. If it is still on its way,
			 * the sending waits for it rather than the shelf - see sync_poll.
			 *
			 * The game is passed rather than read later: by the time a waiting
			 * send starts, the info screen may have loaded another set. */
			g_watch_ms = 0;
			sync_poll();
			if (g_sync_game) {
				g_sync_flush = true;
				snprintf(g_sync_rom, sizeof g_sync_rom, "%s", rom);
				snprintf(g_sync_tag, sizeof g_sync_tag, "%s", s->tag);
				g_sync_cur = chv_game();
				fprintf(stderr, "ra: the account's answer is still on its way; "
				                "sending waits for it\n");
			} else {
				ra_flush_start(rom, s->tag, chv_game());
			}
			/* The set's badges, behind the shelf, for the next time it is
			 * played or opened (src/rabadge.h) */
			rb_fetch_loaded();

			/* RES_DEAD means it stopped answering -- it died, or the game
			 * never started. Say so by falling through to the path that
			 * runs it the slow way, rather than fading the shelf back up
			 * as though it had run. */
			if (!resident) {
				plat_start_error se;

				/* A core that would not load: the fallback is the same Diatom
				 * and the same core, and the game would go straight back to
				 * the shelf with nothing said (Davistoa's bsnes and MAME,
				 * Discord, 2026-11-18). The reason is on the shelf instead. */
				if (plat_resident_start_error(&se) && startfail_message(&se, core_msg, sizeof core_msg)) {
					fprintf(stderr, "launch: %s refused: its core would not load: %s (%s%s%s)\n", s->name,
					        se.core, se.why, se.name[0] ? " " : "", se.name);
					core_said = true;
				} else {
					fprintf(stderr, "resident emulator stopped answering, falling back\n");
				}
			}
		} else {
			fprintf(stderr, "resident emulator did not answer, falling back\n");
		}
	} else {
		/* Silent until 2026-10-05, which is how a resumed game running in
		 * the fallback went unnoticed. */
		fprintf(stderr, "no resident emulator to connect to (waited %u ms), falling back\n",
		        waited);
	}

	if (!resident && !core_said) {
		/* One game per process, the old way: the fallback for a resident that
		 * is missing or has died. Diatom standalone IS the one-shot mode -
		 * same binary, no socket - so the fallback stopped being a different
		 * emulator and became the same one held differently. It has to take
		 * the display, so this side tears its own down first. */
		anim_launch(a);
		free_all_textures(a);
		downloads_video_gone();     /* the Downloads page's kept pictures */
		page_fonts_quit();
		ui_quit();
		plat_input_quit();
		plat_video_quit();

		argv[n++] = elf;
		argv[n++] = (char *)"--core";            argv[n++] = core;
		argv[n++] = (char *)"--rom";             argv[n++] = rom;
		argv[n++] = (char *)"--save";            argv[n++] = save;
		argv[n++] = (char *)"--system";          argv[n++] = bios;
		argv[n++] = (char *)"--display";
		argv[n++] = (char *)DMODES[a->view[o].dmode].name;
		argv[n++] = (char *)"--filter";
		argv[n++] = (char *)SHADERS[a->view[o].shader].name;
		/* Only under 100%, which is Diatom's own default */
		if (shader_has_strength(a->view[o].shader) && strength_clamp(a->view[o].strength) < 100) {
			static char pct[8];

			snprintf(pct, sizeof pct, "%d", strength_clamp(a->view[o].strength));
			argv[n++] = (char *)"--strength";
			argv[n++] = pct;
		}
		/* The channels too: this path is rare, but a player who hears with
		 * one ear should not lose half a game to it. It plays on the codec,
		 * so the cable decides between headphones and the speaker. */
		{
			aout_state cs = aout_now();

			argv[n++] = (char *)"--channels";
			argv[n++] = (char *)aout_channels_wire(
				aout_channels_for(g_channels, aout_dest_of(&cs, ""), plat_speaker_mono()));
		}
		if (!ds_fresh && !core_fresh && !g_ctl_fresh) {
			argv[n++] = (char *)"--load-state";  argv[n++] = st;
		}
		argv[n++] = (char *)"--state-on-exit";   argv[n++] = st;
		/* A DS game's screens, as RUN and SETSCREENS give them to the
		 * resident one. */
		if (g_game_dual) {
			int lay;
			bool sw;

			screens_get(s->tag, &lay, &sw);
			argv[n++] = (char *)"--screens";   argv[n++] = (char *)"2";
			argv[n++] = (char *)"--frameskip"; argv[n++] = (char *)"late";
			argv[n++] = (char *)"--layout";    argv[n++] = (char *)gm_layout_names[lay];
			if (sw) argv[n++] = (char *)"--swap";
		} else if (s->late_skip) {
			/* And a late frame left undrawn, as RUN's frameskip=late
			 * (config/systems.cfg, frameskip|PSP|late). Left out here, the
			 * first PSP game after the resident Diatom ended drew every
			 * frame (1.1.0-rc.6, 2026-11-20). */
			argv[n++] = (char *)"--frameskip"; argv[n++] = (char *)"late";
		}
		/* Timing, as RUN's pace= gives it to the resident one (src/pacing.h) */
		if (s->pace[0]) {
			argv[n++] = (char *)"--pace";
			argv[n++] = (char *)pacing_id(pacing_load(s));
		}
		/* And the Super Game Boy, as RUN's sgb= (src/sgbmode.h) */
		if (sgbm_shelf(s->tag, s->core) && sgbm_load(s) != SGBM_OFF) {
			argv[n++] = (char *)"--sgb";
			argv[n++] = (char *)sgbm_id(sgbm_load(s));
		}
		/* The Auto card's own path here, not the scratch the resident mode
		 * uses. Standalone has no in-game menu - the launcher is torn down -
		 * so nothing can overwrite it mid-session, and it is written once at
		 * exit beside the state. The resident path needs the indirection
		 * because a pause writes the preview too; this one does not. */
		preview_path(a, o, &v->list.items[v->cursor], apv, sizeof apv);
		argv[n++] = (char *)"--preview-on-exit"; argv[n++] = apv;
		argv[n++] = (char *)"--card-frame";      argv[n++] = cf;
		/* Same opinions as the resident path gets over SETOPT, so a game plays
		 * the same whether the resident was up or the fallback ran it. */
		{
			int ci;
			for (ci = 0; ci < plat_coreopt_count(s->tag) && n < (int)(sizeof argv / sizeof argv[0]) - 3; ci++) {
				argv[n++] = (char *)"--core-option";
				argv[n++] = (char *)plat_coreopt(s->tag, ci);
			}
		}
		argv[n] = NULL;
		/* px2-open's status and quit while it runs, as over a resident's
		 * game (on_game_tick): with nothing answering, a quit asked of it
		 * waited for the game to end some other way (2026-11-15) */
		memset(&g_ctl_game, 0, sizeof g_ctl_game);
		g_ctl_game.on = true;
		g_ctl_game.fallback = true;
		screen_shader_apply();
		snprintf(g_ctl_game.tag, sizeof g_ctl_game.tag, "%s", s->tag);
		snprintf(g_ctl_game.name, sizeof g_ctl_game.name, "%s", s->name);
		snprintf(g_ctl_game.core, sizeof g_ctl_game.core, "%s", s->core);
		snprintf(g_ctl_game.path, sizeof g_ctl_game.path, "%s", rom);
		g_ctl_game.since = (long)time(NULL);
		/* Its play time too, as the resident's game has it: from Diatom's
		 * start to its end. This path kept none before, so a game run the
		 * slow way was on neither Recents nor Played */
		stats_begin(s->tag, romfile, (unsigned)stats_mono_ms());
		plat_run_on_tick(fallback_tick);
		fprintf(stderr, "diatom exited %d\n", plat_run(argv, NULL, P_ROOT));
		plat_run_on_tick(NULL);
		stats_end(NULL, (unsigned)stats_mono_ms());
		g_ctl_game.on = false;
		screen_shader_apply();

		if (!plat_video_init() || !plat_input_init()) { a->running = false; return; }
		a->r = plat_renderer();
		ui_init(a->r, P_FONT);
		page_fonts_init(P_FONT);
		a->menu_w = 0;   /* fonts reopened: remeasure the panel */
		prime_home(a);
		prime_window(a, a->sys_cursor);
		/* The display is ours again and no game is running: the safe moment
		 * to put a resident emulator back, so the NEXT launch is fast. */
		respawn_resident(a);
	}

	/* The emulator crashed as the game ended (EXIT reason=crash), and took
	 * the resident with it. Without one, the next game, of any system, ran
	 * the slow fallback, with no in-game menu, no Auto Off and nothing for
	 * px2-open to talk to: every game after a Saturn quit, whose core
	 * crashes in its unload (2026-11-15). So it is started again here, at
	 * the shelf, as after a fallback game; the next launch waits for it
	 * (plat_resident_await, while a Diatom is running). */
	if (resident && plat_resident_crashed()) {
		/* After the crashed one is gone: still on its way out, its socket
		 * would answer, and respawn_resident would start none */
		if (!plat_resident_wait_gone(2000))
			fprintf(stderr, "resident emulator crashed and is still running after 2 s\n");
		respawn_resident(a);
	}

	t_back0 = plat_now_ms();
	present_ground(a);
	/* The language its auto state was just written in, if it was */
	ds_resume_record(st, ds_lang, &ds_before, ds_had);
	/* The zipped DS games DSperate unpacked beside this one, kept under its
	 * own frontend's cap, this game's copy kept (src/dscache.h) */
	if (!strcmp(s->tag, "NDS") && !strcmp(s->core, "dsperate")) {
		char romdir[CFG_STR * 2], dir[1024], keep[1200];
		int went = 0;
		unsigned long long freed;

		if (snprintf(romdir, sizeof romdir, "%s/%s", P_ROMS, s->folder) < (int)sizeof romdir &&
		    dsc_dir(romdir, romfile, dir, sizeof dir) &&
		    dsc_image(romdir, romfile, keep, sizeof keep) &&
		    (freed = dsc_trim(dir, DSC_CAP, keep, &went)) > 0)
			fprintf(stderr, "dscache: %s: %d unpacked game%s removed, %llu MB given back\n",
			        dir, went, went == 1 ? "" : "s", freed >> 20);
	}

	/* The game has just written a fresh autosave preview. Throw the card's
	 * texture away so it picks that up - but only if the preview is what the
	 * card is drawn FROM.
	 *
	 * Box art comes first in the order a card finds its picture, so for a game
	 * that has it this used to rebuild the card from a file that had not
	 * changed, into a pixel-identical texture. That was always wasted work, and
	 * it was invisible while decoding blocked the frame: the card was back
	 * before anything drew. Since decoding moved to a worker, the card is
	 * genuinely absent for a few frames, and the one you just quit popped in
	 * while every card beside it was already there. The question was never
	 * "did the preview change", it is "is this card showing the preview". */
	{
		game_entry *g = &v->list.items[v->cursor];
		int o = shelf_owner(a, a->sys_cursor, v->cursor);

		if (v->tex[v->cursor] && !has_box_art(a->sys.systems[o].folder, g->name)) {
			SDL_DestroyTexture(v->tex[v->cursor]);
			v->tex[v->cursor] = NULL;
		}
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* The idle clock is not restarted here, though it has been stopped for
	 * the whole session. idle_due notices the gap on its own, which is what
	 * stops this from being a list of places to remember. */

	/* A power-off from outside ends the game the same way: the system's
	 * shutdown TERMs this process and Diatom together (plastron's zyperos
	 * stop), and Diatom saves the game and its state on its way out - the
	 * kernel's at 115 C (an orderly power-off, src/heat.h), `poweroff` from
	 * a shell. That is a power-off mid-game,
	 * as POWER is, so the marker stays and the next start goes back into the
	 * game; it was cleared below as a return to the shelf. Marked as asked
	 * for (playing_mark_clean), as a crash or a power cut is not - except
	 * MENU+START's TERM (tools/zyhome.c), which is the way to the home. */
	if (access(plat_flag(ZYPEROS_POWEROFF_FLAG), F_OK) == 0 || want_quit) {
		if (access(plat_flag(CTL_HOME_FLAG), F_OK) != 0) playing_mark_clean();
		a->running = false;
		return;
	}
	/* Power was pressed during the game. The emulator no longer handles that
	 * key itself, so the press arrived here. */
	if (plat_run_power_pressed()) { power_off(a); return; }

	/* Past both power checks, so this is a real return to the shelf rather
	 * than a shutdown. Everything above leaves the marker standing, which is
	 * what makes the next boot able to tell them apart. */
	playing_clear();
	if (core_said) {
		/* It never ran: not first on Recents, and the reason on the screen */
		tick_theme(a);
		render(a);
		result_message(a, v->list.items[v->cursor].title, core_msg);
	} else {
		/* The game just played first on Recents (src/homesys.h) */
		refresh_recents_shelf(a);
	}
	/* Its shots on the Screenshots shelf, the first one making it
	 * (DESIGN.md 11.68): read again only after a game that took one */
	if (shots_take_new()) refresh_shots_shelf(a);

	tick_theme(a);
	render(a);
	fprintf(stderr, "exit: back in %u ms\n", plat_now_ms() - t_back0);
}

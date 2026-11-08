/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher's command channel, for debugging over SSH (tools/px2-open.c,
 * docs/building.md): a Unix socket the running launcher listens on, and the
 * one line each command is.
 *
 * Where: CTL_DIR (or $ZYPEROS_CTL_DIR), a folder the launcher makes 0700 and
 * owned by itself, root, so only root can reach the socket inside it, which
 * is 0600 too; and the launcher answers only a peer of its own user. A Unix
 * socket has no network side, so the channel is as far away as a root shell:
 * a development image's SSH, or a release image's with Developer Mode on.
 *
 * The protocol, one line each way, fields split by tabs:
 *
 *   open<TAB>path=/mnt/SDCARD/Roms/NES/Game.nes[<TAB>fresh=1][<TAB>core=NAME]
 *   quit
 *   status
 *
 * and the answer, one line: "ok" and key=value fields, or
 * "error<TAB>code=CODE<TAB>msg=TEXT". The launcher reads the socket on its
 * shelf's loop, in every screen's (tick_theme) and ten times a second during
 * a game (on_game_tick): an accept that finds nothing is one system call.
 *
 * A port (the Ports shelf, a "run:" system) is run by launch.sh after the
 * launcher has exited, so no launcher answers while one runs: the launcher
 * leaves CTL_PORT_NAME beside the socket as it goes, "PID<TAB>SINCE<TAB>PATH",
 * PID launch.sh's, which px2-open reads then, and the launcher removes it
 * when it starts again.
 *
 * Nothing here draws or knows the launcher's app: the parser, the paths and
 * the socket, for the launcher (src/main.c) and tools/ctl-check.c. */
#ifndef ZYPEROS_CTL_H
#define ZYPEROS_CTL_H

#include <stdbool.h>
#include <stddef.h>

#define CTL_DIR        "/run/zyperos"
#define CTL_SOCK_NAME  "ctl.sock"
#define CTL_PORT_NAME  "port"
#define CTL_LINE       1400          /* one command or one answer */
#define CTL_PATH       1024
#define CTL_CORE       256

typedef enum { CTL_NONE = 0, CTL_OPEN, CTL_QUIT, CTL_STATUS } ctl_verb;

typedef struct {
	ctl_verb verb;
	bool fresh;               /* open: without the auto state, this once */
	char core[CTL_CORE];      /* open: another core the shelf offers, "" its own */
	char path[CTL_PATH];      /* open: the game */
} ctl_cmd;

/* The folder the socket is in: $ZYPEROS_CTL_DIR, or CTL_DIR */
const char *ctl_dir(void);

/* One command line, the newline taken off or not. False, with the reason in
 * `err`, for anything else: an unknown verb or field, a field twice, open
 * with no path. */
bool ctl_parse(const char *line, ctl_cmd *c, char *err, size_t en);

/* A game's path, as text: absolute, under `roms` ("/mnt/SDCARD/Roms"), a
 * system's folder and a file in it, no empty, "." or ".." part, no control
 * character. Split into that folder and the rest (a disc folder's game is
 * "Game/Game.cue"). False, with the reason, for anything else. */
bool ctl_path_split(const char *roms, const char *path, char *folder, size_t fn,
                    char *file, size_t filen, char *err, size_t en);

/* And as the card has it: the file is there, and with every link followed
 * it is still under `roms`. */
bool ctl_path_real(const char *roms, const char *path, char *err, size_t en);

/* The system of a folder, from the folders the shelves have: the same name,
 * or failing that the same name in another case, as the card's FAT has it.
 * -1 for none. A NULL entry is not a system's (Favorites). */
int ctl_find_system(const char *folder, const char *const *folders, int n);

/* The game on its shelf: the shelf's own launch path (game_entry.file), the
 * same in another case, or a disc folder named by its folder alone. -1 for
 * none. */
int ctl_find_game(const char *file, const char *const *files, int n);

/* Whether `want` names `core`: its own name ("flycast2021",
 * "run:Emulators/Flycast/Flycast.sh"), or a script's by what the Core row
 * says for it, its file's name without the extension ("Flycast"), in any
 * case. */
bool ctl_core_is(const char *want, const char *core);

/* The listening socket in `dir`, made 0700 if it is not there; -1, said in
 * the log, when the folder is not a folder of this user's alone or the
 * socket cannot be made. Non-blocking: ctl_take never waits for a peer. */
int ctl_listen(const char *dir);

/* A command waiting: its connection, the line read into `line`; -1 when
 * nobody is there (or a peer of another user, turned away). The line is read
 * with a short timeout, so a peer that connects and says nothing costs a
 * moment, not the frame. */
int ctl_take(int lfd, char *line, size_t n);

/* The answer, one line, and the connection closed */
void ctl_answer(int cfd, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* The socket closed and removed */
void ctl_shut(int lfd, const char *dir);

/* The port record: written as a port starts, read by px2-open, removed when
 * the launcher starts */
bool ctl_port_write(const char *dir, int pid, long since, const char *path);
bool ctl_port_read(const char *dir, int *pid, long *since, char *path, size_t pn);
void ctl_port_clear(const char *dir);

#endif

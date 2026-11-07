/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_EMPTYHOME_H
#define ZYPEROS_EMPTYHOME_H

/* The home when the card has no games yet (DESIGN.md 11.19; my pick in
 * the empty home lab, 2026-11-06): how to add them, with the two ways side
 * by side and one button each. A starts USB File Transfer and Y starts Wi-Fi
 * File Transfer; MENU is Settings and SELECT is MP4, as on the home.
 *
 * What a way says follows what is plugged in, live. On the Pixel 2 the USB
 * cable and the USB Wi-Fi adapter share the one port, so only one way works
 * at a time: the one that cannot is dimmed, its button plays deny, and one
 * line says why. Here are the words, from what the launcher knows of the
 * port and the radio; tools/emptyhome-check.c holds them. src/main.c reads
 * the device into an eh_state and draws through eh_draw. */

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { EH_USB, EH_WIFI, EH_WAYS } eh_way;

typedef struct {
	/* Whether the way can start now: USB File Transfer has its link (on the
	 * Pixel 2, the port is not the Wi-Fi adapter's), Wi-Fi File Transfer a
	 * radio and a network to show its address on, or a hotspot to make */
	bool ok[EH_WAYS];
	/* The USB port is shared with the Wi-Fi adapter (the Pixel 2), rather
	 * than a cable that only links when a computer is on it (the Brick) */
	bool shared_port;
	/* Wi-Fi is built in (the Brick); else it is the USB adapter */
	bool builtin_wifi;
	/* The address a computer browses to over the cable, "" when unknown */
	char cable_addr[32];
	/* MP4 has something to play, so the footer offers SELECT */
	bool music;
} eh_state;

/* One way as the screen says it */
typedef struct {
	const char *name;       /* "USB File Transfer" */
	const char *key;        /* the button that starts it: "A", "Y" */
	const char *step[3];    /* what to do, in order */
	const char *why;        /* why it cannot start now; NULL when it can */
} eh_text;

/* `s`'s words for `w`. `buf` holds the one step with an address in it. */
void eh_words(const eh_state *s, eh_way w, eh_text *out, char *buf, size_t n);

/* The way a button starts (-1 for none) */
int eh_way_of_key(char key);

/* Zyper, drawn by whoever owns him (src/main.c), centered on (cx, cy), `h`
 * tall, his shadow under him on Beard */
typedef void (*eh_zyper_fn)(SDL_Renderer *r, float cx, float cy, float h);

/* The empty home under the home's own header and over its ground: Zyper
 * and "No games yet", the two ways as cards (**raised**, **radius.card**)
 * with their marks, buttons and steps, the line about BIOS files, and the
 * footer's MENU and SELECT. Nothing on it moves by itself, so a still home
 * draws nothing until something changes. */
void eh_draw(SDL_Renderer *r, const eh_state *s, eh_zyper_fn zyper);

/* The cheer when the first games arrive (My pick): over the home as it
 * comes in, faded toward the ground, Zyper hops, a lime check pops and his
 * bubble says how many games; then all of it fades and the home is left.
 * `t` ms into it, eh_cheer_ms long. Zyper is drawn by the caller, posed by
 * the rig at its own clock, at `alpha`. */
typedef void (*eh_pose_fn)(SDL_Renderer *r, float cx, float cy, float h, float alpha,
                           void *ctx);
unsigned eh_cheer_ms(void);
void eh_cheer_draw(SDL_Renderer *r, int games, unsigned t, eh_pose_fn zyper, void *ctx);

/* The geometry, in the launcher's 1024x768 units, which the lab drew
 * (docs/design/mock/empty-home-lab.html) and tools/emptyhome-check.c holds:
 * the cards, and the top of the BIOS line under them */
#define EH_CARD_W   476
#define EH_CARD_H   368
#define EH_CARD_Y   252
#define EH_CARD_X0  24                  /* DS_INSET */
#define EH_CARD_X1  (1024 - 24 - EH_CARD_W)
#define EH_PAD_X    28
#define EH_BIOS_CY  660

#endif

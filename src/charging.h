/* SPDX-License-Identifier: GPL-3.0-only */
/* The charging screen, while the handheld is off and on a charger (DESIGN.md
 * 11.25; my picks in the charging lab, 2026-11-06): its words, the time
 * to full, and its rules.
 *
 * It comes two ways. A charger plugged into a Pixel 2 that is off wakes it,
 * and the bootloader, told by plastron to boot instead of turning it straight
 * off again, puts zycharge=1 on the kernel's line: the launcher shows
 * this first, and a held POWER goes on with the start. And POWER, or Auto
 * Off, on the charger: the game saved and the send-off played as ever, then
 * this instead of black; a held POWER starts ZyperOS afresh. Unplugged, it
 * says so for **hold-short** and the handheld turns off - unless the cable
 * is back before it has (main.c's chg_send_off, and plastron's last step of
 * a power-off, which restarts the handheld onto this screen), or POWER is
 * pressed, which starts ZyperOS as from off.
 *
 * Lit for **hold-charge-lit** from the last button, then dark (the side
 * lights go on showing the charge); any button lights it again; POWER held
 * for **hold-unlock**, with the lock screen's bar filling, starts ZyperOS.
 *
 * Pure: no SDL, no files, the caller's readings and milliseconds, so
 * tools/charging-check.c holds the words, the estimate and the rules. */
#ifndef ZYPEROS_CHARGING_H
#define ZYPEROS_CHARGING_H

#include <stdbool.h>
#include <stddef.h>

/* What the charger is, by the port's BC1.2 answer (the USB PHY's extcon
 * state): a wall charger (DCP), which the Pixel 2 draws 2 A from, or a
 * computer (SDP, or CDP, a computer's charging port), 1.5 A. CH_PENDING: not
 * named yet, and so not said yet (ch_kind_settle). */
typedef enum { CH_UNKNOWN, CH_WALL, CH_COMPUTER, CH_PENDING } ch_kind;

typedef struct ch_reading {
	int     pct;        /* 0 to 100 */
	int     ma;         /* the battery's current, into it positive, mA */
	int     mv;         /* the battery's voltage, mV */
	int     now_mah;    /* charge in it, mAh, or -1 */
	int     full_mah;   /* charge when full, mAh, or -1 */
	ch_kind kind;
	bool    online;     /* the cable in */
	bool    full;       /* the gauge says Full */
	/* Held off at the charge limit: the cable in, not charging (the
	 * kernel's "Not charging", plastron's rk817 charge limit) */
	bool    held;
	/* The charge limit in force, the caller's: 0 for none (MENU > Charge
	 * Limit Off, or a full charge asked for) */
	int     limit;
	/* `ma` was read: false where the gauge has no current file (it then
	 * reads 0, which is no answer), so the corner does not wait on one */
	bool    ma_known;
} ch_reading;

/* The fake gauge's line (plat_charge_read), "PCT MA MV [wall|computer|unknown
 * [ONLINE [HELD]]]", into `r` as a 3000 mAh battery: false when it is not
 * one. HELD 1 is held at the charge limit. */
bool ch_fake_parse(const char *line, ch_reading *r);

/* The charger's kind, once it is sure. The PHY names it a moment after the
 * cable comes, when its BC1.2 detection has run (under a second), and at boot
 * only some 6 s after its probe; until then it reads no kind, and the screen
 * said "charger" and then "computer" (a Pixel 2, 2026-11-06). So a kind not
 * read yet is CH_PENDING for up to CH_KIND_WAIT_MS from the cable's coming,
 * or from the first reading, and "charger" only after that. */
#define CH_KIND_WAIT_MS 8000u

typedef struct {
	bool     online;
	unsigned since;     /* the cable came, or the first reading */
} ch_kind_wait;

/* After each reading: `r`'s kind as it is, or CH_PENDING while it waits */
void ch_kind_settle(ch_kind_wait *w, unsigned now, ch_reading *r);

/* The current under which a charger counts as not charging, and the one at
 * which the PMIC ends a charge (the CV phase's tail) */
#define CH_MA_CHARGING  50
#define CH_MA_TERM      150
/* The share of the charge the CV phase puts in, and where it starts by the
 * percentage when the voltage cannot say */
#define CH_CV_SHARE     0.15
#define CH_CV_PCT       85
#define CH_CV_MV        4250
/* The current's smoothing (a time constant), and how long before the time to
 * full is shown at all */
#define CH_SMOOTH_MS    30000u
#define CH_SETTLE_MS    30000u

/* The current smoothed, for the time to full */
typedef struct {
	bool     primed;
	unsigned first_at, at;
	double   ma;
} ch_eta;

void ch_eta_note(ch_eta *e, unsigned now, const ch_reading *r);

/* Minutes to full, or to r->limit when there is one, rounded up to 5; 0
 * when there; -1 while it settles, when nothing is going in, or with no
 * charge numbers to go by. A battery charges at the current it takes until
 * near full (CC), then at a falling one (CV), which this models as a fall
 * to CH_MA_TERM with the charge left: the last CH_CV_SHARE takes longer than
 * the current alone says. */
int ch_eta_minutes(const ch_eta *e, unsigned now, const ch_reading *r);

/* The screen's two lines: `speed` "Charging · 7.4 W · wall charger" (or
 * "Plugged in · ..." with nothing going in, "Charged · ..." full, and ""
 * while the kind is CH_PENDING: the line comes whole), and `when`
 * "Full in about 1 h 10 min", "Almost full", "Fully charged", "Working out
 * the time to full" while it settles, or "" with nothing to say. With a
 * charge limit (My picks in the charge limit lab, 2026-11-06), the
 * time is to the limit, "85% in about 40 min", and held there `speed` is
 * "Kept at 85% · wall charger" and `when` "Charge Limit in MENU". */
void ch_words(const ch_reading *r, int eta_min, bool settling,
              char *speed, size_t sn, char *when, size_t wn);

/* The rk817-charger's usb_type line, "Unknown SDP [DCP] CDP ...": the one in
 * brackets */
ch_kind ch_kind_parse(const char *usb_type);

/* ---- the rules ----------------------------------------------------------- */

typedef enum {
	CS_LIT,         /* the screen lit */
	CS_DARK,        /* the backlight off, the side lights on */
	CS_UNPLUGGED,   /* "Unplugged", then the handheld turns off */
	CS_START,       /* POWER held: ZyperOS starts */
	CS_OFF,         /* turn the handheld off now */
} cs_step;

typedef struct {
	unsigned lit_at;        /* the last button, or the start */
	unsigned unplugged_at;
	unsigned held_at;       /* POWER went down */
	bool     holding;
	unsigned full_at;       /* A went down, for a full charge */
	bool     full_holding;
	cs_step  step;
} ch_screen;

void ch_screen_start(ch_screen *s, unsigned now);

/* One pass: any button pressed this pass (POWER's press too), POWER down,
 * the cable in. Returns the step, kept in s->step; CS_START and CS_OFF stay.
 * Unplugged, POWER down is CS_START at once. */
cs_step ch_screen_tick(ch_screen *s, unsigned now, bool any_press, bool power_down,
                       bool online);

/* How far POWER's hold has got, 0 to 1 */
float ch_screen_hold(const ch_screen *s, unsigned now);

/* "Hold A for a full charge", offered while held at the charge limit: one
 * pass, A down; true once, when it has been down for **hold-unlock** */
bool ch_screen_full(ch_screen *s, unsigned now, bool a_down);

/* How far A's hold has got, 0 to 1 */
float ch_screen_full_hold(const ch_screen *s, unsigned now);

#endif

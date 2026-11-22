/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The TrimUI Brick's rumble motor: when its pin is high, and nothing else.
 *
 * The motor is on PH3 (GPIO 227), on the AXP2202's BLDO3 rail. Two testers
 * pulsed it from kit v1.0.7-kit.1's Hardware Check and felt it (a white and
 * a red Brick, 2026-11-20 and 2026-11-21; the red one's log: "hwcheck:
 * motor: rail axp2202-bldo3 3300000 uV, enabled", "PH3 (gpio227) high for
 * 300 ms at 3300000 uV, then low"). The pin is on or off: PH3 has no PWM
 * function, and the rail's voltage is left as U-Boot leaves it (3.3 V),
 * never written. So a strength is a duty cycle: the pin high for part of
 * each BM_PERIOD_MS, from a thread of the port's own (diatom/port/brick.c)
 * and from the launcher's buzz (src/device/brick.c).
 *
 * Every decision here, no file and no clock of its own, so tools/
 * brickmotor-check.c holds them on a clock moved by hand. Times are
 * milliseconds on a clock that only goes forward; every comparison is of a
 * difference, so the clock may wrap.
 *
 * Shared by Diatom's Brick port and the launcher's Brick file, which
 * includes it from here (as src/device/desk.c includes Diatom's header). */
#ifndef DIATOM_BRICK_MOTOR_H
#define DIATOM_BRICK_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

/* One on-and-off cycle. 20 ms is 50 Hz: the motor's own inertia smooths it
 * into a weaker shake, where a slower cycle would be felt as pulses. */
#define BM_PERIOD_MS  20u
/* Per mille of the motor's full drive. Under BM_MIN_PM a wish is no shake
 * (a core easing its motor down to 0 ends sooner), and a wish between that
 * and BM_FLOOR_PM is raised to BM_FLOOR_PM: a small motor at a quarter of a
 * cycle barely starts, so a weak wish still has to be felt as one. */
#define BM_MIN_PM     50
#define BM_FLOOR_PM   250
/* Never left on: a motor still asked for this long after the core last
 * said anything stops by itself. A core says when it changes (N64 Rumble
 * Pak, a DualShock, Doom's hits), so a shake longer than this without a
 * word is a core that stopped talking, or a Diatom stuck. The next word
 * starts it again. */
#define BM_QUIET_MS   5000u
/* A buzz (the launcher's step on a Rumble row, Diatom's boost) is at most
 * this long, whatever is asked */
#define BM_BUZZ_MAX_MS 2000u
#define BM_NEVER      0xffffffffu

/* What the core asked, through the player's level: the stronger of the two
 * motors (one motor here), times `scale` (Off 0, Low 0.35, Medium 0.65,
 * Full 1, times the system's share), in per mille of a full drive: 0 (off),
 * or BM_FLOOR_PM to 1000. */
static inline int bm_duty(uint16_t strong, uint16_t weak, float scale)
{
	uint16_t m = strong > weak ? strong : weak;
	float f;
	int pm;

	if (!m || !(scale > 0.0f)) return 0;
	if (scale > 1.0f) scale = 1.0f;
	f = (float)m / 65535.0f * scale * 1000.0f + 0.5f;
	pm = f >= 1000.0f ? 1000 : (int)f;
	if (pm < BM_MIN_PM) return 0;
	return pm < BM_FLOOR_PM ? BM_FLOOR_PM : pm;
}

typedef struct {
	int      duty;      /* per mille; 0 is off */
	unsigned since;     /* the cycle's clock starts here */
	unsigned said_at;   /* the core's last word, for BM_QUIET_MS */
	unsigned buzz_ms;   /* a buzz's length from said_at */
	bool     buzz;
} bm_motor;

static inline void bm_init(bm_motor *m)
{
	m->duty = 0;
	m->since = m->said_at = 0;
	m->buzz_ms = 0;
	m->buzz = false;
}

/* The core's wish, in per mille (bm_duty), at `now`. A change of duty
 * starts a fresh cycle, high first, so a short shake is felt at once. */
static inline void bm_set(bm_motor *m, int duty, unsigned now)
{
	if (duty < 0) duty = 0;
	if (duty > 1000) duty = 1000;
	if (duty && duty != m->duty) m->since = now;
	m->duty = duty;
	m->said_at = now;
	m->buzz = false;
	m->buzz_ms = 0;
}

/* A buzz of `ms` at `duty`, which ends by itself */
static inline void bm_buzz(bm_motor *m, int duty, unsigned ms, unsigned now)
{
	bm_set(m, duty, now);
	if (!m->duty) return;
	if (!ms) { m->duty = 0; return; }
	if (ms > BM_BUZZ_MAX_MS) ms = BM_BUZZ_MAX_MS;
	m->since = now;            /* high first, a repeat of the same too */
	m->buzz = true;
	m->buzz_ms = ms;
}

/* Off, now: a hold (the in-game menu), a stop, a quit */
static inline void bm_off(bm_motor *m, unsigned now)
{
	bm_set(m, 0, now);
}

/* Whether the pin is high at `now`. A buzz past its end, or a wish quiet
 * for BM_QUIET_MS, turns the motor off here, for good until the next word. */
static inline bool bm_pin(bm_motor *m, unsigned now)
{
	unsigned phase;

	if (!m->duty) return false;
	if (now - m->said_at >= (m->buzz ? m->buzz_ms : BM_QUIET_MS)) {
		m->duty = 0;
		m->buzz = false;
		m->buzz_ms = 0;
		return false;
	}
	if (m->duty >= 1000) return true;
	phase = (now - m->since) % BM_PERIOD_MS;
	return phase < (unsigned)m->duty * BM_PERIOD_MS / 1000u;
}

/* How long from `now` until the pin may change, for the thread's wait:
 * BM_NEVER while it is off. Call after bm_pin at the same `now`. */
static inline unsigned bm_wake(const bm_motor *m, unsigned now)
{
	unsigned phase, high, edge, end;

	if (!m->duty) return BM_NEVER;
	end = (m->buzz ? m->buzz_ms : BM_QUIET_MS) - (now - m->said_at);
	if (m->duty >= 1000) return end;
	phase = (now - m->since) % BM_PERIOD_MS;
	high = (unsigned)m->duty * BM_PERIOD_MS / 1000u;
	edge = phase < high ? high - phase : BM_PERIOD_MS - phase;
	return edge < end ? edge : end;
}

#endif

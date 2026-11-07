/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The charging screen's words, time to full and rules. See src/charging.h. */
#include "charging.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "design.h"
#include "i18n.h"

void ch_eta_note(ch_eta *e, unsigned now, const ch_reading *r)
{
	double k;

	if (!r->online || r->ma < CH_MA_CHARGING) {
		/* Nothing going in: start over when something does */
		e->primed = false;
		return;
	}
	if (!e->primed) {
		e->primed = true;
		e->first_at = e->at = now;
		e->ma = r->ma;
		return;
	}
	/* An exponential average over CH_SMOOTH_MS, by the time since the last
	 * reading, so a missed pass weighs what it should */
	k = 1.0 - exp(-(double)(now - e->at) / CH_SMOOTH_MS);
	e->ma += (r->ma - e->ma) * k;
	e->at = now;
}

/* Hours to put `x` of the `rest` the CV phase has left in, the current `ma`
 * now and falling with the charge left to CH_MA_TERM at full */
static double cv_hours(double rest, double x, double ma)
{
	double at;

	if (ma <= CH_MA_TERM * 1.05) return x / CH_MA_TERM;
	/* The current when `x` is in: on the line from `ma` to CH_MA_TERM */
	at = CH_MA_TERM + (ma - CH_MA_TERM) * (rest - x) / rest;
	return rest / (ma - CH_MA_TERM) * log(ma / at);
}

int ch_eta_minutes(const ch_eta *e, unsigned now, const ch_reading *r)
{
	double need, rest, cv, hours, ma;
	int min;

	if (r->full || r->pct >= 100) return 0;
	if (!e->primed || now - e->first_at < CH_SETTLE_MS) return -1;
	if (r->now_mah < 0 || r->full_mah <= 0) return -1;
	ma = e->ma;
	if (ma < CH_MA_CHARGING) return -1;
	rest = r->full_mah - r->now_mah;
	need = rest;
	if (r->limit > 0 && r->limit < 100) need = r->full_mah * r->limit / 100.0 - r->now_mah;
	if (need <= 0 || rest <= 0) return 0;
	cv = r->full_mah * CH_CV_SHARE;
	if (r->pct >= CH_CV_PCT || r->mv >= CH_CV_MV || rest <= cv)
		/* In the CV phase already: the current falls from what it is */
		hours = cv_hours(rest, need, ma);
	else if (need <= rest - cv)
		/* There before the CV phase */
		hours = need / ma;
	else
		/* At the current it takes until the last share, then falling */
		hours = (rest - cv) / ma + cv_hours(cv, need - (rest - cv), ma);
	if (!(hours > 0.0) || hours > 24.0) return -1;
	min = (int)ceil(hours * 60.0);
	return (min + 4) / 5 * 5;
}

static const char *kind_word(ch_kind k)
{
	return k == CH_WALL ? T(S_CHARGING_WALL) : k == CH_COMPUTER ? T(S_CHARGING_COMPUTER)
	                    : T(S_CHARGING_CHARGER);
}

void ch_words(const ch_reading *r, int eta_min, bool settling,
              char *speed, size_t sn, char *when, size_t wn)
{
	bool full = r->full || r->pct >= 100;
	int lim = r->limit > 0 && r->limit < 100 ? r->limit : 0;
	bool kept = lim && r->held && r->online && !full;

	if (r->kind == CH_PENDING)
		snprintf(speed, sn, "%s", "");
	else if (kept)
		snprintf(speed, sn, T(S_CHARGING_KEPT), lim, kind_word(r->kind));
	else if (full)
		snprintf(speed, sn, T(S_CHARGING_CHARGED), kind_word(r->kind));
	else if (r->ma >= CH_MA_CHARGING && r->mv > 0) {
		/* Tenths of a watt, from mA and mV */
		long dw = ((long)r->ma * r->mv + 50000) / 100000;

		snprintf(speed, sn, T(S_CHARGING_WATTS), dw / 10, dw % 10, kind_word(r->kind));
	} else
		snprintf(speed, sn, T(S_CHARGING_PLUGGED), kind_word(r->kind));

	if (full) snprintf(when, wn, "%s", T(S_CHARGING_FULL));
	else if (kept) snprintf(when, wn, "%s", T(S_CHARGING_LIMIT_IN_MENU));
	else if (eta_min < 0 && !settling) snprintf(when, wn, "%s", "");
	else if (lim) {
		/* To the limit: "85%" where full would be */
		if (eta_min < 0) snprintf(when, wn, T(S_CHARGING_TO_LIMIT_WORKING), lim);
		else if (eta_min <= 5) snprintf(when, wn, T(S_CHARGING_TO_LIMIT_ALMOST), lim);
		else if (eta_min < 60) snprintf(when, wn, T(S_CHARGING_TO_LIMIT_MIN), lim, eta_min);
		else if (eta_min % 60 == 0) snprintf(when, wn, T(S_CHARGING_TO_LIMIT_H), lim, eta_min / 60);
		else snprintf(when, wn, T(S_CHARGING_TO_LIMIT_H_MIN), lim, eta_min / 60, eta_min % 60);
	}
	else if (eta_min < 0) snprintf(when, wn, "%s", T(S_CHARGING_TO_FULL_WORKING));
	else if (eta_min <= 5) snprintf(when, wn, "%s", T(S_CHARGING_TO_FULL_ALMOST));
	else if (eta_min < 60) snprintf(when, wn, T(S_CHARGING_TO_FULL_MIN), eta_min);
	else if (eta_min % 60 == 0) snprintf(when, wn, T(S_CHARGING_TO_FULL_H), eta_min / 60);
	else snprintf(when, wn, T(S_CHARGING_TO_FULL_H_MIN), eta_min / 60, eta_min % 60);
}

bool ch_fake_parse(const char *line, ch_reading *r)
{
	char kind[16] = "";
	int pct, ma, mv, online = 1, held = 0, n;

	if (!line) return false;
	n = sscanf(line, "%d %d %d %15s %d %d", &pct, &ma, &mv, kind, &online, &held);
	if (n < 3 || pct < 0 || pct > 100) return false;
	memset(r, 0, sizeof *r);
	r->pct = pct;
	r->ma = ma;
	r->mv = mv;
	r->full_mah = 3000;
	r->now_mah = 3000 * pct / 100;
	r->kind = !strcmp(kind, "computer") ? CH_COMPUTER : !strcmp(kind, "unknown") ? CH_UNKNOWN : CH_WALL;
	r->online = online != 0;
	r->full = pct >= 100;
	r->held = held != 0 && r->online;
	return true;
}

void ch_kind_settle(ch_kind_wait *w, unsigned now, ch_reading *r)
{
	if (!r->online) {
		w->online = false;
		return;
	}
	if (!w->online) {
		w->online = true;
		w->since = now;
	}
	if (r->kind == CH_UNKNOWN && now - w->since < CH_KIND_WAIT_MS) r->kind = CH_PENDING;
}

ch_kind ch_kind_parse(const char *s)
{
	const char *a = s ? strchr(s, '[') : NULL, *b = a ? strchr(a, ']') : NULL;
	size_t n;

	if (!a || !b) return CH_UNKNOWN;
	n = (size_t)(b - a - 1);
	if (n == 3 && !strncmp(a + 1, "DCP", 3)) return CH_WALL;
	if (n == 3 && (!strncmp(a + 1, "SDP", 3) || !strncmp(a + 1, "CDP", 3))) return CH_COMPUTER;
	return CH_UNKNOWN;
}

/* ---- the rules ----------------------------------------------------------- */

void ch_screen_start(ch_screen *s, unsigned now)
{
	memset(s, 0, sizeof *s);
	s->lit_at = now;
	s->step = CS_LIT;
}

cs_step ch_screen_tick(ch_screen *s, unsigned now, bool any_press, bool power_down,
                       bool online)
{
	if (s->step == CS_START || s->step == CS_OFF) return s->step;
	if (!online) {
		if (s->step != CS_UNPLUGGED) {
			s->step = CS_UNPLUGGED;
			s->unplugged_at = now;
			s->holding = false;
		}
		/* POWER: on after all, as an off handheld's press would start
		 * it, without the hold a charging one asks for (I pressed it
		 * here and waited for the power-off to end, 2026-11-06) */
		if (power_down) return s->step = CS_START;
		/* Said for its time, then off; another button does not keep it */
		if (now - s->unplugged_at >= DS_MS_HOLD_SHORT) s->step = CS_OFF;
		return s->step;
	}
	/* Plugged in again before it turned off: lit, as from a press */
	if (s->step == CS_UNPLUGGED) s->lit_at = now;
	if (any_press) s->lit_at = now;
	if (power_down) {
		if (!s->holding) {
			s->holding = true;
			s->held_at = now;
		}
		s->lit_at = now;
		if (now - s->held_at >= DS_MS_HOLD_UNLOCK) return s->step = CS_START;
	} else
		s->holding = false;
	return s->step = now - s->lit_at < DS_MS_HOLD_CHARGE_LIT ? CS_LIT : CS_DARK;
}

bool ch_screen_full(ch_screen *s, unsigned now, bool a_down)
{
	if (!a_down) {
		s->full_holding = false;
		return false;
	}
	if (!s->full_holding) {
		s->full_holding = true;
		s->full_at = now;
		return false;
	}
	if (now - s->full_at < DS_MS_HOLD_UNLOCK) return false;
	/* Once: it starts over only from A up and down again */
	s->full_at = now - DS_MS_HOLD_UNLOCK;
	s->full_holding = false;
	return true;
}

float ch_screen_full_hold(const ch_screen *s, unsigned now)
{
	unsigned t;

	if (!s->full_holding) return 0.0f;
	t = now - s->full_at;
	return t >= DS_MS_HOLD_UNLOCK ? 1.0f : (float)t / DS_MS_HOLD_UNLOCK;
}

float ch_screen_hold(const ch_screen *s, unsigned now)
{
	unsigned t;

	if (!s->holding) return 0.0f;
	t = now - s->held_at;
	return t >= DS_MS_HOLD_UNLOCK ? 1.0f : (float)t / DS_MS_HOLD_UNLOCK;
}

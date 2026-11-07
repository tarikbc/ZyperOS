/* The tutorial's steps (src/tour.c), held to DESIGN.md 11.10 and to the lab
 * I pressed through: what every launcher button is to it and which glyph
 * part the hand lights, every step in order and what each waits for, the
 * deaf start of a step for every key, START, the wrong presses and what
 * Zyper says to each, every button's name, POWER only in its own step, the
 * volume keys, the reward's hold, the loops' clocks, Zyper's moves (the
 * rig's, the corner's rise and the hello's pop), the little shelf's colors
 * against config/systems.cfg, and `tour.done` saved only for a tour that is
 * seen. No SDL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/config.h"
#include "../src/design.h"
#include "../src/tour.h"

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static bool near(float a, float b) { return a - b < 0.002f && b - a < 0.002f; }

#define T0 100000u

/* The right key for each lesson. */
static const tour_key RIGHT[TOUR_STEPS] = {
	[TOUR_WELCOME] = TK_A, [TOUR_MOVE] = TK_RIGHT, [TOUR_A] = TK_A, [TOUR_B] = TK_B,
	[TOUR_Y] = TK_Y, [TOUR_X] = TK_X, [TOUR_MENU] = TK_MENU, [TOUR_SELECT] = TK_SELECT,
	[TOUR_VOL] = TK_VOL, [TOUR_BRIGHT] = TK_BRIGHT, [TOUR_POWER] = TK_POWER,
	[TOUR_THEME] = TK_A, [TOUR_HOME] = TK_A, [TOUR_SOUNDS] = TK_A, [TOUR_MUSIC] = TK_A,
	[TOUR_FINISH] = TK_A,
};

/* The whole tour, pressed through: the welcome's A, two moves, a press per
 * lesson, a look and an A per choice, the finish's A; each rewarded and the
 * next on by itself. With menu music on the card (`music`), four choices,
 * else three. */
static void walk(bool fn, bool music)
{
	tour t;
	uint32_t now = T0;
	int s, rewards = 0;
	const tour_step last = music ? TOUR_MUSIC : TOUR_SOUNDS;

	printf("the whole tour, %s, %s:\n", fn ? "MENU+VOL brightness" : "F1/F2 brightness",
	       music ? "menu music" : "no menu music");
	tour_start(&t, now, fn);
	tour_set_music(&t, music);
	ck(t.step == TOUR_WELCOME && !t.over, "it starts on the welcome");
	now += 300;
	ck(tour_press(&t, TK_A, now) == DS_SOUND_ACCEPT, "A on the welcome is accept");
	ck(t.step == TOUR_MOVE, "and the move step comes on");
	for (s = TOUR_MOVE; s <= (int)last; s++) {
		int snd;

		ck(t.step == (tour_step)s, "the steps come in 11.10's order");
		if (tour_choice_of(t.step) >= 0) {
			const int c = tour_choice_of(t.step);

			now += 300;
			ck(tour_press(&t, TK_RIGHT, now) == DS_SOUND_MOVE && t.pick[c] == 1 && !t.done,
			   "a choice's RIGHT looks at the next and is a move");
			ck(t.kept[c] == 0, "and keeps nothing yet");
		}
		now += 300;
		if (s == TOUR_MOVE) {
			ck(tour_press(&t, TK_RIGHT, now) == DS_SOUND_MOVE, "a first move is a move");
			ck(!t.done, "and one move does not end the step");
			now += 200;
		}
		ck(tour_takes_power(&t) == (s == TOUR_POWER), "POWER is the tour's in its step only");
		snd = tour_press(&t, RIGHT[s], now);
		ck(snd == DS_SOUND_REWARD, "the right press is rewarded");
		ck(t.done, "and ends the step");
		if (snd == DS_SOUND_REWARD) rewards++;
		ck(!tour_takes_power(&t), "and POWER is not the tour's once it is pressed");
		ck(tour_press(&t, TK_B, now + 100) == TOUR_QUIET && !t.wrong,
		   "a press during the reward is nothing, not a wrong press");
		tour_tick(&t, now + DS_MS_TOUR_REWARD - 1);
		ck(t.step == (tour_step)s, "the reward holds tour-reward");
		if (tour_choice_of((tour_step)s) >= 0)
			ck(t.kept[tour_choice_of((tour_step)s)] == 1, "A keeps the choice looked at");
		now += DS_MS_TOUR_REWARD;
		tour_tick(&t, now);
		ck(t.step == (s == (int)last ? TOUR_FINISH : (tour_step)(s + 1)),
		   "and then the next step comes on by itself");
		ck(t.had_reward && t.last_step == (tour_step)s, "with the last reward still going");
	}
	ck(rewards == TOUR_LESSONS + TOUR_CHOICES - !music, "a reward a lesson and a choice");
	ck(t.step == TOUR_FINISH, "the finish follows the last choice");
	ck(tour_press(&t, TK_A, now + 599) == TOUR_QUIET && !t.over,
	   "the finish is deaf for 600 ms, so the A of the last reward does not end it");
	ck(tour_press(&t, TK_A, now + 600) == DS_SOUND_ACCEPT, "then A is accept");
	ck(t.over && !t.skipped, "and the tour is over, finished");
	ck(tour_press(&t, TK_A, now + 900) == TOUR_QUIET, "a tour that is over takes no press");
}

/* The choices: where they start, how LEFT and RIGHT go round, what A keeps,
 * the wrong press's answer, the strip, and START. */
static void choices(void)
{
	tour t;
	const int at[TOUR_CHOICES] = { 1, 0, 2, 7 };
	int c;

	printf("the choices:\n");
	ck(tour_choice_count(TOUR_THEME) == 2 && tour_choice_count(TOUR_HOME) == 2 &&
	   tour_choice_count(TOUR_SOUNDS) == 4 && tour_choice_count(TOUR_MUSIC) == 2 &&
	   tour_choice_count(TOUR_POWER) == 0, "Theme 2, Home Screen 2, Sounds 4, Menu Music 2");
	ck(!strcmp(tour_choice_name(TOUR_THEME, 1), "Dark") &&
	   !strcmp(tour_choice_name(TOUR_HOME, 1), "Gallery") &&
	   !strcmp(tour_choice_name(TOUR_SOUNDS, 3), "Off") &&
	   !strcmp(tour_choice_name(TOUR_MUSIC, 0), "On") &&
	   !strcmp(tour_choice_name(TOUR_SOUNDS, 4), ""), "named as MENU's rows name them");
	ck(tour_choice_of(TOUR_POWER) < 0 && tour_choice_of(TOUR_FINISH) < 0 &&
	   tour_choice_of(TOUR_THEME) == 0 && tour_choice_of(TOUR_MUSIC) == 3,
	   "the four after the power step are the choices");

	tour_pose(&t, TOUR_THEME, T0, 2000, -1, false);
	tour_choices_from(&t, at);
	ck(t.pick[0] == 1 && t.kept[0] == 1, "a choice starts on the setting as it is");
	ck(t.kept[3] == 0, "and one out of range is the first");
	tour_pose(&t, TOUR_SOUNDS, T0, 2000, -1, false);
	tour_choices_from(&t, at);
	t.pick[2] = t.kept[2];
	t.pos = 2;
	t.pos_from = 2.0f;
	ck(tour_press(&t, TK_RIGHT, T0) == DS_SOUND_MOVE && t.pick[2] == 3, "RIGHT is the next");
	ck(near(tour_focus_pos(&t, T0), 2.0f) && near(tour_focus_pos(&t, T0 + DS_MS_MOVE), 3.0f),
	   "the focus slides to it in move");
	ck(tour_press(&t, TK_RIGHT, T0 + 50) == DS_SOUND_EDGE && t.pick[2] == 0,
	   "and past the last it comes round to the first, on edge");
	ck(tour_press(&t, TK_LEFT, T0 + 100) == DS_SOUND_EDGE && t.pick[2] == 3,
	   "LEFT past the first comes round to the last");
	ck(t.kept[2] == 2 && !t.done, "looking keeps nothing");
	ck(tour_press(&t, TK_B, T0 + 150) == DS_SOUND_DENY && t.wrong &&
	   !strcmp(t.hint, "That's B. Try LEFT, RIGHT or A."), "any other button is a wrong press");
	ck(tour_press(&t, TK_UP, T0 + 200) == DS_SOUND_DENY, "UP too");
	ck(tour_press(&t, TK_A, T0 + 250) == DS_SOUND_REWARD && t.done && t.kept[2] == 3,
	   "A keeps the one looked at, rewarded");
	ck(tour_seen(&t), "a tour in its choices is seen");

	tour_pose(&t, TOUR_HOME, T0, 2000, -1, false);
	tour_choices_from(&t, at);
	tour_press(&t, TK_RIGHT, T0);
	ck(tour_press(&t, TK_START, T0 + 10) == DS_SOUND_BACK && t.over && t.skipped &&
	   t.pick[1] == 1 && t.kept[1] == 0,
	   "START skips with the one looked at not kept, for the caller to put back");

	tour_start(&t, T0, false);
	ck(tour_segments(&t) == TOUR_LESSONS + TOUR_CHOICES - 1 && tour_segment(&t) == -1,
	   "no menu music: 13 segments, and the welcome has none");
	tour_set_music(&t, true);
	ck(tour_segments(&t) == TOUR_LESSONS + TOUR_CHOICES, "with it, 14");
	for (c = TOUR_MOVE; c <= TOUR_MUSIC; c++) {
		tour_pose(&t, (tour_step)c, T0, 2000, -1, false);
		tour_set_music(&t, true);
		ck(tour_segment(&t) == c - TOUR_MOVE, "a step's segment is its place after the welcome");
	}
	tour_pose(&t, TOUR_SOUNDS, T0, 2000, 0, false);
	tour_set_music(&t, true);
	tour_tick(&t, T0 + DS_MS_TOUR_REWARD);
	ck(t.step == TOUR_MUSIC, "with menu music the Menu Music step follows Sounds");
	tour_pose(&t, TOUR_SOUNDS, T0, 2000, 0, false);
	tour_tick(&t, T0 + DS_MS_TOUR_REWARD);
	ck(t.step == TOUR_FINISH, "without, the finish does");
	tour_pose(&t, TOUR_THEME, T0, 2000, -1, false);
	tour_pose_pick(&t, 1, T0, 40);
	ck(t.pick[0] == 1 && tour_focus_pos(&t, T0) > 0.0f && tour_focus_pos(&t, T0) < 1.0f,
	   "a shot can put the focus on any choice, mid-slide");
}

static void deaf(void)
{
	tour t;
	tour_key k;

	printf("a step's first quarter second:\n");
	tour_start(&t, T0, false);
	for (k = 0; k < TK_KEYS; k++)
		ck(tour_press(&t, k, T0 + 249) == TOUR_QUIET, "every press is nothing until 250 ms");
	ck(t.step == TOUR_WELCOME && !t.wrong && !t.over, "and changes nothing");
	ck(tour_press(&t, TK_A, T0 + 250) == DS_SOUND_ACCEPT, "at 250 ms A counts");
	ck(tour_press(&t, TK_RIGHT, T0 + 250 + 249) == TOUR_QUIET && t.moves == 0,
	   "and the new step is deaf from its own start");
	ck(tour_press(&t, TK_RIGHT, T0 + 500) == DS_SOUND_MOVE && t.moves == 1, "then it hears");

	tour_pose(&t, TOUR_VOL, T0, 249, -1, false);
	ck(tour_press(&t, TK_VOL, T0) == TOUR_QUIET && !t.done,
	   "the volume step's own key is deaf with the rest");
	ck(tour_press(&t, TK_VOL, T0 + 1) == DS_SOUND_REWARD, "and heard at 250 ms");
	tour_pose(&t, TOUR_BRIGHT, T0, 249, -1, true);
	ck(tour_press(&t, TK_BRIGHT, T0) == TOUR_QUIET && !t.done, "so is the brightness step's");
	ck(tour_press(&t, TK_VOL, T0) == TOUR_QUIET && !t.wrong,
	   "and a volume key there is not wrong while it is deaf");
	ck(tour_press(&t, TK_BRIGHT, T0 + 1) == DS_SOUND_REWARD, "and the chord is heard at 250 ms");
}

static void start_skips(void)
{
	tour t;
	tour_step s;

	printf("START:\n");
	for (s = TOUR_WELCOME; s < TOUR_FINISH; s++) {
		tour_pose(&t, s, T0, 249, -1, false);
		ck(tour_press(&t, TK_START, T0) == TOUR_QUIET && !t.over, "is deaf with the rest");
		tour_pose(&t, s, T0, 2000, s == TOUR_WELCOME ? -1 : 300, false);
		ck(tour_press(&t, TK_START, T0) == DS_SOUND_BACK, "skips from any step, the reward too");
		ck(t.over && t.skipped, "and the tour is over, skipped");
		ck(t.end_at == T0, "at the press");
	}
	tour_pose(&t, TOUR_FINISH, T0, 2000, -1, false);
	tour_press(&t, TK_START, T0);
	ck(t.over && !t.skipped, "START on the finish finishes it");
}

static void wrong(void)
{
	tour t;

	printf("a wrong press:\n");
	tour_pose(&t, TOUR_WELCOME, T0, 2000, -1, false);
	ck(tour_press(&t, TK_B, T0) == DS_SOUND_DENY, "on the welcome is deny");
	ck(t.wrong && !strcmp(t.hint, "That's B. Try A."), "and says which to try");
	ck(t.step == TOUR_WELCOME, "and stays");

	tour_pose(&t, TOUR_Y, T0, 2000, -1, false);
	ck(tour_press(&t, TK_X, T0) == DS_SOUND_DENY, "in a lesson is deny");
	ck(!strcmp(t.hint, "That's X. Try Y."), "That's X. Try Y.");
	ck(!t.done && t.step == TOUR_Y, "and the step still waits");
	ck(tour_wrong_on(&t, T0 + DS_MS_HOLD_SHORT + DS_MS_FADE_OUT - 1) &&
	   !tour_wrong_on(&t, T0 + DS_MS_HOLD_SHORT + DS_MS_FADE_OUT),
	   "the answer holds hold-short and fades out");
	ck(tour_wrong_pulse(&t, T0 + DS_MS_TOUR_PULSE / 4) > 0.8f &&
	   tour_wrong_pulse(&t, T0 + DS_MS_TOUR_PULSE / 2) < 0.01f &&
	   tour_wrong_pulse(&t, T0 + DS_MS_TOUR_PULSE * 3 / 4) > 0.5f,
	   "the glyph pulses twice");
	ck(tour_wrong_pulse(&t, T0 + DS_MS_TOUR_PULSE) == 0.0f &&
	   tour_wrong_pulse(&t, T0 + DS_MS_TOUR_PULSE / 4) > 0.8f,
	   "the pulse is tour-pulse long");
	ck(tour_press(&t, TK_Y, T0 + 10) == DS_SOUND_REWARD && !t.wrong,
	   "the right press after it is rewarded and the answer goes");

	tour_pose(&t, TOUR_MOVE, T0, 2000, -1, false);
	tour_press(&t, TK_A, T0);
	ck(!strcmp(t.hint, "That's A. Try the D-pad."), "the move step names the D-pad");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	tour_press(&t, TK_L1, T0);
	ck(!strcmp(t.hint, "That's L1. Try A."), "a shoulder is named");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	tour_press(&t, TK_UP, T0);
	ck(!strcmp(t.hint, "That's Up. Try A."), "a direction is named");
	tour_pose(&t, TOUR_VOL, T0, 2000, -1, false);
	tour_press(&t, TK_SELECT, T0);
	ck(!strcmp(t.hint, "That's SELECT. Try VOL+ or VOL-."), "the rocker is both keys");
	tour_pose(&t, TOUR_BRIGHT, T0, 2000, -1, true);
	tour_press(&t, TK_A, T0);
	ck(!strcmp(t.hint, "That's A. Try MENU+VOL."),
	   "the Pixel 2's chord is MENU+VOL, as MENU > Controls names it");
	tour_pose(&t, TOUR_FINISH, T0, 2000, -1, false);
	ck(tour_press(&t, TK_B, T0) == TOUR_QUIET && !t.wrong, "the finish has no wrong press");
}

static void levels(void)
{
	tour t;

	printf("the volume and brightness keys:\n");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	ck(tour_press(&t, TK_VOL, T0) == TOUR_QUIET && !t.wrong && !t.done,
	   "VOL in another step is not the tour's and not wrong");
	ck(tour_press(&t, TK_BRIGHT, T0) == TOUR_QUIET && !t.wrong, "nor is brightness");
	tour_pose(&t, TOUR_VOL, T0, 2000, -1, false);
	ck(tour_press(&t, TK_BRIGHT, T0) == TOUR_QUIET && !t.wrong && !t.done,
	   "brightness in the volume step is neither");
	ck(tour_press(&t, TK_VOL, T0) == DS_SOUND_REWARD, "VOL answers the volume step");

	tour_pose(&t, TOUR_BRIGHT, T0, 2000, -1, true);
	ck(tour_press(&t, TK_VOL, T0) == DS_SOUND_DENY, "VOL alone in the brightness step is wrong");
	ck(!strcmp(t.hint, "That's volume. Hold MENU too."), "and says to hold MENU too");
	ck(tour_press(&t, TK_MENU, T0 + 10) == DS_SOUND_DENY, "and so is MENU alone");
	ck(!strcmp(t.hint, "Hold MENU, then press VOL+."), "which says how the chord goes");
	ck(tour_press(&t, TK_BRIGHT, T0 + 20) == DS_SOUND_REWARD, "the chord answers it");

	tour_pose(&t, TOUR_BRIGHT, T0, 2000, -1, false);
	tour_press(&t, TK_VOL, T0);
	ck(!strcmp(t.hint, "That's volume. Try F1 or F2."), "on the Brick it names F1 and F2");
	tour_press(&t, TK_MENU, T0 + 10);
	ck(!strcmp(t.hint, "That's MENU. Try F1 or F2."), "and MENU is only MENU");
	ck(!strcmp(tour_step_info(TOUR_BRIGHT)->line, "Press F1 or F2.") &&
	   !strcmp(tour_step_info(TOUR_BRIGHT)->line_fn, "Hold MENU and press VOL+ or VOL-."),
	   "each device's line names its own keys");
}

static void power(void)
{
	tour t;
	tour_step s;

	printf("POWER:\n");
	for (s = TOUR_WELCOME; s <= TOUR_FINISH; s++) {
		tour_pose(&t, s, T0, 2000, -1, false);
		ck(tour_takes_power(&t) == (s == TOUR_POWER), "is the tour's only in its own step");
		if (s != TOUR_POWER)
			ck(tour_press(&t, TK_POWER, T0) == TOUR_QUIET && !t.done && !t.wrong,
			   "and elsewhere the tour does nothing with it");
	}
	tour_pose(&t, TOUR_POWER, T0, 100, -1, false);
	ck(tour_takes_power(&t), "kept from turning the handheld off while the step is deaf");
	ck(tour_press(&t, TK_POWER, T0) == TOUR_QUIET && !t.done, "but not answered then");
	ck(tour_press(&t, TK_POWER, T0 + 150) == DS_SOUND_REWARD, "and then it is");
	ck(!tour_takes_power(&t), "a second POWER in the reward turns the handheld off");
	t.over = true;
	ck(!tour_takes_power(&t), "and so does any POWER after the tour");
}

static void moves(void)
{
	tour t;

	printf("the move step:\n");
	tour_pose(&t, TOUR_MOVE, T0, 2000, -1, false);
	ck(tour_press(&t, TK_LEFT, T0) == DS_SOUND_MOVE && t.pos == -1, "the shelf follows a move");
	ck(near(tour_shelf_pos(&t, T0), 0.0f) && near(tour_shelf_pos(&t, T0 + DS_MS_MOVE), -1.0f),
	   "in move, from where it was");
	ck(tour_press(&t, TK_UP, T0 + 40) == DS_SOUND_REWARD && t.pos == -2,
	   "any direction counts, and the second move ends it");
	ck(t.pos_from > -1.0f && t.pos_from < 0.0f, "starting from wherever the shelf had got to");
	tour_pose(&t, TOUR_MOVE, T0, 2000, -1, false);
	t.pos = 2;
	ck(tour_press(&t, TK_RIGHT, T0) == DS_SOUND_EDGE && t.pos == 2,
	   "a move past the last card plays edge and goes nowhere");

	/* L1/R1, which the step's note names, jump as on the home: 3 systems,
	 * as far as the little shelf has cards */
	tour_pose(&t, TOUR_MOVE, T0, 2000, -1, false);
	ck(tour_press(&t, TK_R1, T0) == DS_SOUND_MOVE && t.pos == 2 && !t.wrong,
	   "R1 counts as a move, and jumps to the little shelf's end");
	ck(tour_press(&t, TK_L1, T0 + 40) == DS_SOUND_REWARD && t.pos == -1,
	   "L1 jumps back 3, and ends the step as a second move");
	tour_pose(&t, TOUR_MOVE, T0, 2000, -1, false);
	t.pos = -2;
	ck(tour_press(&t, TK_L1, T0) == DS_SOUND_EDGE && t.pos == -2,
	   "a jump from the first card plays edge and goes nowhere");
	ck(!strcmp(tour_step_info(TOUR_MOVE)->note, "L1/R1 jump 3 systems."),
	   "its note names L1/R1, as MENU > Controls writes the pair");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	ck(tour_press(&t, TK_R1, T0) == DS_SOUND_DENY && t.wrong,
	   "anywhere else L1/R1 are a wrong press, as before");
}

static void loops(void)
{
	tour t;
	tour_loop_at L;
	const tour_loop *la = tour_step_info(TOUR_A)->loop;
	const tour_loop *lm = tour_step_info(TOUR_MENU)->loop;

	printf("the loops:\n");
	tour_pose(&t, TOUR_A, T0, 0, -1, false);
	ck(tour_loop_time(&t, la->period, T0 + 499) == 0, "a loop starts half a second in");
	L = tour_loop_now(&t, T0 + 500 + la->act - 1);
	ck(L.a == 0.0f, "before its press acts, nothing");
	L = tour_loop_now(&t, T0 + 500 + la->act + la->dur);
	ck(near(L.a, 1.0f), "and the press has done its whole thing in its duration");
	ck(tour_part_press(&t, TP_A, T0 + 500 + la->press_at + DS_MS_PRESS) > 0.99f,
	   "the glyph is down in press");
	ck(tour_part_press(&t, TP_A, T0 + 500 + la->release_at + DS_MS_PRESS) < 0.01f,
	   "and up in press after the release");
	ck(tour_part_press(&t, TP_B, T0 + 500 + la->press_at + DS_MS_PRESS) == 0.0f,
	   "and only its own glyph");
	L = tour_loop_now(&t, T0 + 500 + la->hold + la->back_ms / 2);
	ck(near(L.a, 1.0f) && L.back > 0.4f && L.back < 0.6f, "a faded undo fades over the start");
	L = tour_loop_now(&t, T0 + 500 + la->period + la->act - 1);
	ck(L.a == 0.0f && L.back == 0.0f, "and round it goes");

	/* A press mid-loop goes on from where the loop was. */
	tour_pose(&t, TOUR_A, T0, 0, -1, false);
	tour_press(&t, TK_A, T0 + 500 + la->act + la->dur / 2);
	ck(t.a0 > 0.0f && t.a0 < 1.0f, "a press mid-loop keeps where the loop was");
	ck(near(tour_real_a(&t, t.done_at), t.a0), "and starts from there");
	ck(near(tour_real_a(&t, t.done_at + la->dur), 1.0f), "to the end in the loop's time");

	/* A shot of a press landed mid-loop shows it from there */
	tour_pose(&t, TOUR_A, T0, 500 + la->act + la->dur / 2 + 50, 50, false);
	ck(t.a0 > 0.0f && t.a0 < 1.0f, "a posed press keeps where the loop was");

	tour_pose(&t, TOUR_MENU, T0, 0, -1, false);
	L = tour_loop_now(&t, T0 + 500 + 700);
	ck(L.a > 0.0f, "on the Brick the menu opens at MENU's press");
	tour_pose(&t, TOUR_MENU, T0, 0, -1, true);
	L = tour_loop_now(&t, T0 + 500 + 700);
	ck(L.a == 0.0f, "on the Pixel 2 at its release, as MENU comes there");
	ck(tour_loop_now(&t, T0 + 500 + lm->release_at + lm->dur).a > 0.99f, "and is open then");

	tour_pose(&t, TOUR_BRIGHT, T0, 0, -1, true);
	ck(tour_part_press(&t, TP_MENU, T0 + 500 + 800) > 0.99f &&
	   tour_part_press(&t, TP_VOLUP, T0 + 500 + 700) > 0.99f,
	   "the chord holds MENU over the volume key's presses");
	tour_pose(&t, TOUR_BRIGHT, T0, 0, -1, false);
	ck(tour_part_press(&t, TP_F2, T0 + 500 + 700) > 0.99f &&
	   tour_part_press(&t, TP_MENU, T0 + 500 + 800) == 0.0f, "the Brick presses F2");
}

static void hand(void)
{
	tour t;

	printf("the player's hand:\n");
	tour_pose(&t, TOUR_VOL, T0, 2000, 0, false);
	ck(tour_part_press(&t, TP_VOLDN, T0) == 0.0f, "nothing pressed is nothing");
	tour_hand(&t, TP_VOLDN, true, T0);
	ck(tour_part_press(&t, TP_VOLDN, T0 + DS_MS_PRESS) > 0.99f, "a held key is down in press");
	ck(tour_part_press(&t, TP_VOLDN, T0 + 1000) > 0.99f, "and stays down while held");
	tour_hand(&t, TP_VOLDN, false, T0 + 1000);
	ck(tour_part_press(&t, TP_VOLDN, T0 + 1000 + DS_MS_PRESS) < 0.01f, "and up in press");
	tour_hand(&t, TP_VOLDN, true, T0 + 2000);
	tour_hand(&t, TP_VOLDN, false, T0 + 2020);
	ck(tour_part_press(&t, TP_VOLDN, T0 + 2020) < 0.99f &&
	   tour_part_press(&t, TP_VOLDN, T0 + 2020) > 0.3f,
	   "a tap goes up from as far as it got");
}

/* What each launcher button is to the tutorial. */
static void keys(void)
{
	static const struct { in_button b; tour_key k; } want[] = {
		{ IN_LEFT, TK_LEFT }, { IN_RIGHT, TK_RIGHT }, { IN_UP, TK_UP }, { IN_DOWN, TK_DOWN },
		{ IN_ACCEPT, TK_A }, { IN_BACK, TK_B }, { IN_X, TK_X }, { IN_Y, TK_Y },
		{ IN_L1, TK_L1 }, { IN_R1, TK_R1 },
		{ IN_START, TK_START }, { IN_SELECT, TK_SELECT }, { IN_MENU, TK_MENU },
		{ IN_VOLUP, TK_VOL }, { IN_VOLDN, TK_VOL },
		{ IN_BRIGHTUP, TK_BRIGHT }, { IN_BRIGHTDN, TK_BRIGHT }, { IN_POWER, TK_POWER },
		/* The menu music's shoulders: no key of the tutorial's */
		{ IN_L2, TK_KEYS }, { IN_R2, TK_KEYS },
	};
	static const char *const names[TK_KEYS] = {
		"Up", "Down", "Left", "Right", "A", "B", "X", "Y", "L1", "R1",
		"START", "SELECT", "MENU", "volume", "brightness", "POWER" };
	size_t i;
	int k;

	printf("the launcher's buttons:\n");
	ck(sizeof want / sizeof want[0] == IN_COUNT, "every button is in the table below");
	for (i = 0; i < sizeof want / sizeof want[0]; i++) {
		char what[64];

		snprintf(what, sizeof what, "button %d is %s", (int)want[i].b,
		         tour_key_name(want[i].k));
		ck(tour_key_of(want[i].b) == want[i].k, what);
	}
	ck(tour_key_of(IN_NONE) == TK_KEYS && tour_key_of(IN_COUNT) == TK_KEYS,
	   "no button is no key");
	for (k = 0; k < TK_KEYS; k++) {
		char what[64];

		snprintf(what, sizeof what, "key %d is named %s", k, names[k]);
		ck(!strcmp(tour_key_name((tour_key)k), names[k]), what);
	}
}

/* Every button of a frame reaches the tour, each to its own step. */
static void buttons(void)
{
	static const struct { tour_step s; in_button b; int snd; } want[] = {
		{ TOUR_MOVE, IN_UP, DS_SOUND_MOVE }, { TOUR_MOVE, IN_DOWN, DS_SOUND_MOVE },
		{ TOUR_MOVE, IN_LEFT, DS_SOUND_MOVE }, { TOUR_MOVE, IN_RIGHT, DS_SOUND_MOVE },
		{ TOUR_A, IN_ACCEPT, DS_SOUND_REWARD }, { TOUR_B, IN_BACK, DS_SOUND_REWARD },
		{ TOUR_Y, IN_Y, DS_SOUND_REWARD }, { TOUR_X, IN_X, DS_SOUND_REWARD },
		{ TOUR_MENU, IN_MENU, DS_SOUND_REWARD }, { TOUR_SELECT, IN_SELECT, DS_SOUND_REWARD },
		{ TOUR_VOL, IN_VOLUP, DS_SOUND_REWARD }, { TOUR_VOL, IN_VOLDN, DS_SOUND_REWARD },
		{ TOUR_BRIGHT, IN_BRIGHTUP, DS_SOUND_REWARD },
		{ TOUR_BRIGHT, IN_BRIGHTDN, DS_SOUND_REWARD },
		{ TOUR_MOVE, IN_L1, DS_SOUND_MOVE }, { TOUR_MOVE, IN_R1, DS_SOUND_MOVE },
		{ TOUR_A, IN_L1, DS_SOUND_DENY }, { TOUR_A, IN_R1, DS_SOUND_DENY },
		{ TOUR_A, IN_START, DS_SOUND_BACK },
	};
	bool pressed[IN_COUNT];
	int snd[IN_COUNT], n;
	size_t i;
	tour t;

	printf("a frame's buttons:\n");
	for (i = 0; i < sizeof want / sizeof want[0]; i++) {
		char what[80];

		tour_pose(&t, want[i].s, T0, 2000, -1, false);
		memset(pressed, 0, sizeof pressed);
		pressed[want[i].b] = true;
		n = tour_buttons(&t, pressed, T0, snd);
		snprintf(what, sizeof what, "%s pressed reaches the %s step",
		         tour_key_name(tour_key_of(want[i].b)), tour_step_info(want[i].s)->title);
		ck(n == 1 && snd[0] == want[i].snd, what);
	}
	tour_pose(&t, TOUR_POWER, T0, 2000, -1, false);
	memset(pressed, 0, sizeof pressed);
	pressed[IN_POWER] = true;
	ck(tour_buttons(&t, pressed, T0, snd) == 0 && !t.done,
	   "POWER is the caller's, not the frame's: tour_takes_power says");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	memset(pressed, 0, sizeof pressed);
	pressed[IN_START] = true;
	pressed[IN_SELECT] = true;
	ck(tour_buttons(&t, pressed, T0, snd) == 1 && t.over,
	   "a press after START in the same frame reaches no tour that is over");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	memset(pressed, 0, sizeof pressed);
	pressed[IN_L2] = true;
	pressed[IN_R2] = true;
	ck(tour_buttons(&t, pressed, T0, snd) == 0 && !t.done && !t.over,
	   "L2 and R2, the songs', are no press of the tutorial's");
	tour_pose(&t, TOUR_SELECT, T0, 2000, -1, false);
	ck(tour_buttons(&t, pressed, T0, snd) == 0 && !t.done && !t.wrong,
	   "not even in Music, whose note names them");
	ck(!strcmp(tour_step_info(TOUR_SELECT)->note, "L2/R2 play the previous or next song."),
	   "Music's note says what L2/R2 do");
}

/* Which glyph part the player's own hand lights, on each device. */
static void hands(void)
{
	bool d[IN_COUNT];
	tour t;

	printf("the hand, by device:\n");
	tour_pose(&t, TOUR_VOL, T0, 2000, 0, true);
	memset(d, 0, sizeof d);
	d[IN_X] = true;
	tour_hands(&t, d, false, T0);
	ck(tour_part_press(&t, TP_X, T0 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_Y, T0 + DS_MS_PRESS) == 0.0f, "X lights X and not Y");
	memset(d, 0, sizeof d);
	tour_hands(&t, d, true, T0 + 200);
	ck(tour_part_press(&t, TP_MENU, T0 + 200 + DS_MS_PRESS) > 0.99f,
	   "the Pixel 2's MENU lights while FUNCTION is held, before in_state sees it");
	tour_hands(&t, d, false, T0 + 400);
	d[IN_BRIGHTUP] = true;
	tour_hands(&t, d, false, T0 + 600);
	ck(tour_part_press(&t, TP_MENU, T0 + 600 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_VOLUP, T0 + 600 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_F2, T0 + 600 + DS_MS_PRESS) == 0.0f,
	   "its brightness up is MENU and VOL+ held, not F2");
	d[IN_BRIGHTUP] = false;
	d[IN_BRIGHTDN] = true;
	tour_hands(&t, d, false, T0 + 800);
	ck(tour_part_press(&t, TP_VOLDN, T0 + 800 + DS_MS_PRESS) > 0.99f,
	   "and brightness down is VOL-");

	tour_pose(&t, TOUR_VOL, T0, 2000, 0, false);
	memset(d, 0, sizeof d);
	d[IN_BRIGHTUP] = true;
	d[IN_BRIGHTDN] = true;
	tour_hands(&t, d, false, T0);
	ck(tour_part_press(&t, TP_F2, T0 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_F1, T0 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_MENU, T0 + DS_MS_PRESS) == 0.0f &&
	   tour_part_press(&t, TP_VOLUP, T0 + DS_MS_PRESS) == 0.0f,
	   "the Brick's brightness keys are F1 and F2 alone");
	memset(d, 0, sizeof d);
	d[IN_VOLDN] = true;
	d[IN_POWER] = true;
	tour_hands(&t, d, false, T0 + 200);
	ck(tour_part_press(&t, TP_VOLDN, T0 + 200 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_POWER, T0 + 200 + DS_MS_PRESS) > 0.99f,
	   "VOL- and POWER light their own");

	/* The shoulders have no glyph; the handheld shows them pressed */
	tour_pose(&t, TOUR_SELECT, T0, 2000, -1, true);
	memset(d, 0, sizeof d);
	d[IN_L1] = true;
	d[IN_R2] = true;
	tour_hands(&t, d, false, T0);
	ck(tour_part_press(&t, TP_L1, T0 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_R2, T0 + DS_MS_PRESS) > 0.99f &&
	   tour_part_press(&t, TP_R1, T0 + DS_MS_PRESS) == 0.0f &&
	   tour_part_press(&t, TP_L2, T0 + DS_MS_PRESS) == 0.0f,
	   "L1 and R2 press their own shoulders, and no other");
}

/* Zyper: the rig's moves, the corner's rise, the hello. */
static void zyper(void)
{
	tour t;
	tour_zyper_at z;
	float before, after;

	printf("Zyper:\n");
	tour_pose(&t, TOUR_A, T0, 2000, -1, false);
	z = tour_zyper_now(&t, T0);
	ck(!z.corner && !z.hello, "a step waiting for its press has no Zyper");
	tour_pose(&t, TOUR_A, T0, 2000, 0, false);
	ck(tour_zyper_now(&t, T0).drop == 1.0f && tour_zyper_now(&t, T0 + 10).corner &&
	   tour_zyper_now(&t, T0 + 10).drop > 0.5f,
	   "the reward brings him from under the bottom edge");
	tour_tick(&t, T0 + DS_MS_MOVE);
	z = tour_zyper_now(&t, T0 + DS_MS_MOVE);
	ck(z.corner && near(z.drop, 0.0f), "up in move");
	tour_tick(&t, T0 + DS_MS_MOVE + DS_MS_ZYPER_HOP / 3);
	z = tour_zyper_now(&t, T0 + DS_MS_MOVE + DS_MS_ZYPER_HOP / 3);
	ck(z.pose.dy < -10.0f, "and hops, the rig's hop, once he is up");
	tour_tick(&t, T0 + DS_MS_TOUR_REWARD);
	z = tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD);
	ck(t.step == TOUR_B && z.corner && near(z.drop, 0.0f), "the next step comes on with him up");
	z = tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD + DS_MS_EXIT);
	ck(!z.corner, "and he is down in exit");

	/* A wrong press, then the right one while he is on his way up: the
	 * reward starts from where he is */
	tour_pose(&t, TOUR_Y, T0, 2000, -1, false);
	tour_press(&t, TK_X, T0);
	tour_tick(&t, T0 + DS_MS_ZYPER_TILT);
	z = tour_zyper_now(&t, T0 + DS_MS_ZYPER_TILT);
	ck(z.corner && z.pose.tilt > 15.0f && z.pose.tongue < 0.1f,
	   "a wrong press: up, his head cocked and his tongue in (the rig's failure)");
	before = tour_zyper_now(&t, T0 + DS_MS_MOVE / 2).drop;
	tour_pose(&t, TOUR_Y, T0, 2000, -1, false);
	tour_press(&t, TK_X, T0);
	tour_press(&t, TK_Y, T0 + DS_MS_MOVE / 2);
	after = tour_zyper_now(&t, T0 + DS_MS_MOVE / 2).drop;
	ck(before > 0.0f && near(before, after), "a reward replacing a wrong press does not snap him");
	tour_pose(&t, TOUR_Y, T0, 2000, -1, false);
	tour_press(&t, TK_X, T0);
	tour_tick(&t, T0 + DS_MS_HOLD_ZYPER_FAIL + DS_MS_ZYPER_TILT + 10);
	z = tour_zyper_now(&t, T0 + DS_MS_HOLD_ZYPER_FAIL + DS_MS_ZYPER_TILT + 10);
	ck(near(z.pose.tilt, 10.0f) && z.pose.tongue > 0.99f, "and he settles, as the rig does");
	tour_tick(&t, T0 + DS_MS_HOLD_SHORT + DS_MS_EXIT);
	ck(!tour_zyper_now(&t, T0 + DS_MS_HOLD_SHORT + DS_MS_EXIT).corner,
	   "and goes down when the answer has held hold-short");

	tour_pose(&t, TOUR_POWER, T0, 2000, -1, false);
	ck(!tour_zyper_now(&t, T0).corner, "the power step's Zyper is the stage's");
	tour_press(&t, TK_A, T0);
	ck(t.wrong && !tour_zyper_now(&t, T0 + DS_MS_MOVE).corner,
	   "and a wrong press there brings no second one");
	tour_press(&t, TK_POWER, T0 + DS_MS_MOVE);
	ck(near(tour_zyper_now(&t, T0 + DS_MS_MOVE).drop, 1.0f),
	   "and its reward after that wrong press still comes up from under the edge");
	tour_pose(&t, TOUR_POWER, T0, 2000, 300, false);
	ck(tour_zyper_now(&t, T0).corner, "until its press, when the corner's is the one");

	tour_pose(&t, TOUR_WELCOME, T0, 0, -1, false);
	ck(!tour_zyper_now(&t, T0 + DS_MS_QUICK).hello && !tour_zyper_now(&t, T0 + DS_MS_QUICK).corner,
	   "the hello waits a quick, with no corner Zyper at the welcome");
	z = tour_zyper_now(&t, T0 + DS_MS_QUICK + DS_MS_ZYPER_POP * 3 / 4);
	ck(z.scale > 1.0f, "and pops him in past his size");
	z = tour_zyper_now(&t, T0 + DS_MS_QUICK + DS_MS_ZYPER_POP);
	ck(z.scale == 1.0f && z.pose.tongue < 0.01f, "to exactly it, his tongue still in");
	z = tour_zyper_now(&t, T0 + DS_MS_QUICK + DS_MS_ZYPER_POP + DS_MS_ZYPER_EARS / 2);
	ck(z.pose.ears > 13.0f && z.pose.tongue > 0.99f, "then the tongue out and the ears up");
	/* The last choice's reward hands over to the finish: the corner Zyper
	 * goes down in exit, and only then does the big one pop in */
	tour_pose(&t, TOUR_SOUNDS, T0, 3000, 0, false);
	tour_tick(&t, T0 + DS_MS_TOUR_REWARD - 1);
	ck(t.step == TOUR_SOUNDS && tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD - 1).corner,
	   "the last choice's reward has him in the corner");
	tour_tick(&t, T0 + DS_MS_TOUR_REWARD);
	z = tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD + DS_MS_EXIT / 2);
	ck(t.step == TOUR_FINISH && z.corner && !z.hello && z.drop > 0.0f && z.drop < 1.0f,
	   "at the finish he leaves the corner in exit, alone");
	z = tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD + DS_MS_EXIT - 1);
	ck(z.corner && !z.hello, "to the last moment of it");
	z = tour_zyper_now(&t, T0 + DS_MS_TOUR_REWARD + DS_MS_EXIT + 1);
	ck(!z.corner && z.hello, "and then the hello pops in, one Zyper at a time");
	tour_pose(&t, TOUR_FINISH, T0, 0, -1, false);
	z = tour_zyper_now(&t, T0);
	ck(t.had_reward && z.corner && !z.hello && near(z.drop, 0.0f),
	   "a finish shot includes the last reward's corner Zyper at entry");
	z = tour_zyper_now(&t, T0 + DS_MS_EXIT / 2);
	ck(z.corner && !z.hello && z.drop > 0.0f && z.drop < 1.0f,
	   "a finish shot shows that Zyper leaving before the hello");
	tour_tick(&t, T0 + DS_MS_EXIT + DS_MS_ZYPER_POP + DS_MS_TOUR_REWARD + DS_MS_ZYPER_HOP / 3);
	ck(tour_zyper_now(&t, T0 + DS_MS_EXIT + DS_MS_ZYPER_POP + DS_MS_TOUR_REWARD +
	                      DS_MS_ZYPER_HOP / 3).pose.dy < -10.0f,
	   "the finish hops every tour-reward");
}

/* The little shelf's colors are the systems' own. */
static void little(void)
{
	static systems_cfg cfg;
	int i, k;

	printf("the little shelf:\n");
	if (!cfg_load_systems("config/systems.cfg", &cfg)) {
		ck(0, "config/systems.cfg reads");
		return;
	}
	for (i = 0; i < TOUR_LITTLE_N; i++) {
		char what[96];

		for (k = 0; k < cfg.count && strcmp(cfg.systems[k].name, TOUR_LITTLE[i].name); k++) ;
		snprintf(what, sizeof what, "%s is systems.cfg's %06X", TOUR_LITTLE[i].name,
		         k < cfg.count ? cfg.systems[k].accent : 0u);
		ck(k < cfg.count && cfg.systems[k].accent == TOUR_LITTLE[i].rgb, what);
	}
	ck(!strcmp(TOUR_LITTLE[TOUR_LITTLE_FOCUS].name, "SNES"), "the one that opens is the SNES");
}

static void copy(void)
{
	tour_step s;
	int i;

	printf("the words (DESIGN.md 12):\n");
	for (s = TOUR_WELCOME; s < TOUR_STEPS; s++) {
		const tour_info *st = tour_step_info(s);
		const char *lines[] = { st->line, st->line_fn, st->note, st->note_done };
		size_t k;

		ck(st->title && st->title[0], "every step has a title");
		/* "!" only in Zyper's bubble: the welcome's and the finish's titles
		 * are his words in it. */
		if (s != TOUR_WELCOME && s != TOUR_FINISH)
			ck(!strchr(st->title, '!'), "no step's title calls out");
		for (k = 0; k < sizeof lines / sizeof lines[0]; k++) {
			if (!lines[k]) continue;
			ck(lines[k][strlen(lines[k]) - 1] == '.', "a line ends with a full stop");
			ck(!strchr(lines[k], '!'), "and has no exclamation mark");
		}
	}
	ck(tour_step_info(TOUR_POWER)->note && tour_step_info(TOUR_POWER)->note_done,
	   "the power step says it stays on, and then what to do when it freezes");
	for (i = 0; i < TOUR_LESSONS; i++)
		ck(strchr(tour_cheer(i), '!') != NULL, "every cheer is his");
	ck(DS_MS_TOUR_REWARD == 1100, "a reward is 1.1 s (11.10)");
}

static void saved(void)
{
	char dir[] = "/tmp/zyperos-tour-check-XXXXXX", dev[64];
	db *d;
	tour t;

	printf("tour.done:\n");
	if (!mkdtemp(dir)) {
		ck(0, "a scratch directory for the database");
		return;
	}
	snprintf(dev, sizeof dev, "%s/zyperos.db", dir);
	d = db_open(dev, DB_DEVICE);
	ck(d != NULL, "a device database opens");
	if (!d) return;
	ck(tour_wanted(d), "a new handheld wants the tutorial");
	ck(db_has(d, TOUR_DONE_KEY) && db_get_int(d, TOUR_DONE_KEY, 1) == 0,
	   "seeded 0, so --set can change it");
	tour_pose(&t, TOUR_X, T0, 2000, -1, false);
	tour_save(d, &t);
	ck(tour_wanted(d), "a tour cut off by POWER is not remembered, and runs again");
	tour_pose(&t, TOUR_POWER, T0, 2000, -1, false);
	tour_save(d, &t);
	ck(tour_wanted(d) && !tour_seen(&t), "nor one cut off in the power step before its press");
	tour_pose(&t, TOUR_POWER, T0, 2000, 300, false);
	ck(tour_seen(&t), "one past the power step's press is seen");
	tour_save(d, &t);
	ck(!tour_wanted(d), "and remembered, so its second POWER leaves it seen");
	db_set_int(d, TOUR_DONE_KEY, 0);
	tour_pose(&t, TOUR_X, T0, 2000, -1, false);
	tour_press(&t, TK_START, T0);
	tour_save(d, &t);
	ck(!tour_wanted(d), "a skipped one is");
	db_close(d);
	d = db_open(dev, DB_DEVICE);
	ck(d && !tour_wanted(d), "and stays remembered across a start");
	if (d) {
		db_set_int(d, TOUR_DONE_KEY, 0);
		ck(tour_wanted(d), "--set tour.done=0 shows it again");
		tour_pose(&t, TOUR_FINISH, T0, 2000, -1, false);
		tour_press(&t, TK_A, T0);
		tour_save(d, &t);
		ck(!tour_wanted(d), "a finished one is remembered");
		db_close(d);
	}
	ck(!tour_wanted(NULL), "with no database, it does not run at every start");
	{
		char p[96];

		unlink(dev);
		snprintf(p, sizeof p, "%s-wal", dev);
		unlink(p);
		snprintf(p, sizeof p, "%s-shm", dev);
		unlink(p);
		rmdir(dir);
	}
}

int main(void)
{
	walk(false, false);
	walk(true, true);
	choices();
	deaf();
	start_skips();
	wrong();
	levels();
	power();
	moves();
	loops();
	hand();
	keys();
	buttons();
	hands();
	zyper();
	little();
	copy();
	saved();
	if (fails) { printf("\n%d tour check(s) FAILED\n", fails); return 1; }
	printf("ok: the tutorial's steps\n");
	return 0;
}

#ifndef VITAMEDIADECK_MEDIA_PLAYER_INPUT_LOCK_H
#define VITAMEDIADECK_MEDIA_PLAYER_INPUT_LOCK_H

#include <stdint.h>
#include <psp2/ctrl.h>

#define PLAYER_INPUT_LOCK_FEEDBACK_US (2ULL * 1000ULL * 1000ULL)
#define PLAYER_INPUT_LOCK_HOLD_US 900000ULL

typedef struct {
	int locked;
	int armed;
	int holding;
	int hold_fired;
	uint64_t hold_started_us;
} PlayerInputLock;

static inline int player_input_lock_update(PlayerInputLock *state,
	                                       unsigned int buttons,
	                                       uint64_t now_us) {
	if (!state) return 0;
	/* A player can be entered while SELECT is still physically held. Require
	 * one observed release before accepting a new gesture in that case. */
	if (!state->armed) {
		if (!(buttons & SCE_CTRL_SELECT)) state->armed = 1;
		return 0;
	}
	if (buttons & SCE_CTRL_SELECT) {
		if (!state->holding) {
			state->holding = 1;
			state->hold_fired = 0;
			state->hold_started_us = now_us;
		} else if (!state->hold_fired &&
		           now_us - state->hold_started_us >= PLAYER_INPUT_LOCK_HOLD_US) {
			state->hold_fired = 1;
			state->locked = !state->locked;
			return 1;
		}
		return 0;
	}
	state->holding = 0;
	state->hold_fired = 0;
	state->hold_started_us = 0;
	return 0;
}

#endif

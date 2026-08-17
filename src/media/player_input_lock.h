#ifndef VITATUBE_MEDIA_PLAYER_INPUT_LOCK_H
#define VITATUBE_MEDIA_PLAYER_INPUT_LOCK_H

#include <stdint.h>
#include <psp2/ctrl.h>

#define PLAYER_INPUT_LOCK_HOLD_US 900000ULL
#define PLAYER_INPUT_LOCK_FEEDBACK_US (2ULL * 1000ULL * 1000ULL)

typedef struct {
	int locked;
	int hold_fired;
	uint64_t hold_started_us;
} PlayerInputLock;

static inline int player_input_lock_update(PlayerInputLock *state,
	                                       unsigned int buttons,
	                                       uint64_t now_us) {
	if (!state) return 0;
	if (!(buttons & SCE_CTRL_SELECT)) {
		state->hold_started_us = 0;
		state->hold_fired = 0;
		return 0;
	}
	if (!state->hold_started_us) state->hold_started_us = now_us;
	if (!state->hold_fired &&
	    now_us - state->hold_started_us >= PLAYER_INPUT_LOCK_HOLD_US) {
		state->hold_fired = 1;
		state->locked = !state->locked;
		return 1;
	}
	return 0;
}

#endif

#ifndef VITAWAVE_MEDIA_PLAYER_POWER_SAVE_H
#define VITAWAVE_MEDIA_PLAYER_POWER_SAVE_H

#include <stdint.h>

#include <psp2/ctrl.h>

#define PLAYER_POWER_SAVE_HOLD_US 900000ULL
#define PLAYER_POWER_SAVE_LOCK_MOVE_US (10ULL * 1000ULL * 1000ULL)
#define PLAYER_POWER_SAVE_LOCK_POSITIONS 8

typedef struct {
	int armed;
	int holding;
	int hold_fired;
	uint64_t hold_started_us;
} PlayerPowerSaveInput;

enum {
	PLAYER_POWER_SAVE_EVENT_NONE = 0,
	PLAYER_POWER_SAVE_EVENT_SHORT_PRESS,
	PLAYER_POWER_SAVE_EVENT_TOGGLE
};

/* START has two meanings in the player. A short press is emitted only on
 * release, while a 900 ms hold is emitted as soon as the threshold is met.
 * Disabling the detector (input lock) also cancels any partial hold. */
static inline int player_power_save_update(PlayerPowerSaveInput *state,
	                                        unsigned int buttons,
	                                        uint64_t now_us,
	                                        int enabled) {
	if (!state) return PLAYER_POWER_SAVE_EVENT_NONE;
	if (!enabled) {
		state->armed = (buttons & SCE_CTRL_START) == 0;
		state->holding = 0;
		state->hold_fired = 0;
		state->hold_started_us = 0;
		return PLAYER_POWER_SAVE_EVENT_NONE;
	}
	/* A fullscreen resume is entered while the START that requested it may
	 * still be physically held. Require one observed release before accepting
	 * a new gesture, otherwise that release would immediately minimize again. */
	if (!state->armed) {
		if (!(buttons & SCE_CTRL_START)) state->armed = 1;
		return PLAYER_POWER_SAVE_EVENT_NONE;
	}
	if (buttons & SCE_CTRL_START) {
		if (!state->holding) {
			state->holding = 1;
			state->hold_fired = 0;
			state->hold_started_us = now_us;
		} else if (!state->hold_fired &&
		           now_us - state->hold_started_us >= PLAYER_POWER_SAVE_HOLD_US) {
			state->hold_fired = 1;
			return PLAYER_POWER_SAVE_EVENT_TOGGLE;
		}
		return PLAYER_POWER_SAVE_EVENT_NONE;
	}
	if (state->holding) {
		int event = state->hold_fired ? PLAYER_POWER_SAVE_EVENT_NONE
		                              : PLAYER_POWER_SAVE_EVENT_SHORT_PRESS;
		state->holding = 0;
		state->hold_fired = 0;
		state->hold_started_us = 0;
		return event;
	}
	return PLAYER_POWER_SAVE_EVENT_NONE;
}

static inline void player_power_save_lock_position(int index, int *x, int *y) {
	static const int positions[PLAYER_POWER_SAVE_LOCK_POSITIONS][2] = {
		{ 28, 24 }, { 456, 24 }, { 884, 24 }, { 884, 250 },
		{ 884, 477 }, { 456, 477 }, { 28, 477 }, { 28, 250 }
	};
	if (index < 0) index = 0;
	index %= PLAYER_POWER_SAVE_LOCK_POSITIONS;
	if (x) *x = positions[index][0];
	if (y) *y = positions[index][1];
}

#endif /* VITAWAVE_MEDIA_PLAYER_POWER_SAVE_H */

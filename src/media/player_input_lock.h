#ifndef VITAMEDIADECK_MEDIA_PLAYER_INPUT_LOCK_H
#define VITAMEDIADECK_MEDIA_PLAYER_INPUT_LOCK_H

#include <stdint.h>
#include <psp2/ctrl.h>

#define PLAYER_INPUT_LOCK_FEEDBACK_US (2ULL * 1000ULL * 1000ULL)

typedef struct {
	int locked;
	int select_down;
} PlayerInputLock;

static inline int player_input_lock_update(PlayerInputLock *state,
	                                       unsigned int buttons,
	                                       uint64_t now_us) {
	if (!state) return 0;
	if (!(buttons & SCE_CTRL_SELECT)) {
		state->select_down = 0;
		return 0;
	}
	/* SELECT is a discrete lock key. Toggle on the first sampled down frame,
	 * then require a release before accepting the next toggle. */
	if (!state->select_down) {
		state->select_down = 1;
		state->locked = !state->locked;
		return 1;
	}
	(void)now_us;
	return 0;
}

#endif

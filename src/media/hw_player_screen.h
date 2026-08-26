#ifndef VITAWAVE_MEDIA_HW_PLAYER_SCREEN_H
#define VITAWAVE_MEDIA_HW_PLAYER_SCREEN_H

#include <stdint.h>

#include "media/vita_decoder.h"

typedef struct {
	VtDecoderStreamFactory stream;
	const char *title;
	const char *location;
	int authenticated_remote;
	uint64_t start_position_ms;
	uint32_t expected_width;
	uint32_t expected_height;
	int expected_fps;
} VtHwPlayerScreenSource;

/* A player can hand navigation back to the application's shared sections
 * through its L1 sidebar without conflating that with decoder errors. */
#define VT_HW_PLAYER_ACTION_SECTION_BASE 100

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms);

#endif

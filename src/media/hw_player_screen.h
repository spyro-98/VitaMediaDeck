#ifndef VITATUBE_MEDIA_HW_PLAYER_SCREEN_H
#define VITATUBE_MEDIA_HW_PLAYER_SCREEN_H

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

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms);

#endif

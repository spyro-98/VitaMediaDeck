#include "media/audio_volume.h"

static volatile int g_volume_percent = 100;

int vt_audio_volume_percent(void) {
	return g_volume_percent;
}

void vt_audio_volume_set_percent(int percent) {
	if (percent < 0) percent = 0;
	if (percent > 300) percent = 300;
	g_volume_percent = percent;
}

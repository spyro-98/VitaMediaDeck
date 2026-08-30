#ifndef VITAMEDIADECK_UI_MUSIC_PLAYER_H
#define VITAMEDIADECK_UI_MUSIC_PLAYER_H

#include <stdint.h>

enum {
	UI_MUSIC_PLAYER_STOP = 0,
	UI_MUSIC_PLAYER_MINIMIZE,
	UI_MUSIC_PLAYER_NEXT,
	UI_MUSIC_PLAYER_REPEAT,
	UI_MUSIC_PLAYER_SECTION_BASE = 10
};

/* Runs the full-screen music presentation over an already activated
 * background-playback session. The return value tells the local-media owner
 * whether the queue should stop, advance, repeat, or remain in mini-player. */
int ui_music_player_run(const char *artwork_path, const char *album,
	                    const char *codec_name,
	                    uint32_t average_bitrate_kbps);
int ui_music_player_shuffle_enabled(void);
int ui_music_player_repeat_one_enabled(void);

#endif

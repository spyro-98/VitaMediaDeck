#ifndef VMD_AUDIO_LAYOUT_H
#define VMD_AUDIO_LAYOUT_H
#include "settings/preferences.h"
/* Native 960x544 coordinates from the approved audio-layout atlas. Drawing
 * and touch use the same transport rectangles; radio has only Play/Pause. */
enum { UI_AUDIO_SEEK_X=18, UI_AUDIO_SEEK_Y=432, UI_AUDIO_SEEK_W=924,
       UI_AUDIO_CONTROL_Y=452, UI_AUDIO_CONTROL_W=48, UI_AUDIO_CONTROL_H=44,
       UI_AUDIO_BACK_X=394, UI_AUDIO_PLAY_X=456, UI_AUDIO_NEXT_X=518 };
typedef struct { float cover_x,cover_y,cover_size,text_x,text_width,title_y; } UiAudioLayout;
static inline UiAudioLayout ui_audio_layout(int layout,int has_cover) {
	if(!has_cover) return (UiAudioLayout){372,68,216,72,816,228};
	if(layout==VT_AUDIO_LAYOUT_LEFT)return (UiAudioLayout){18,78,332,394,528,163};
	if(layout==VT_AUDIO_LAYOUT_RIGHT)return (UiAudioLayout){610,78,332,38,528,163};
	return (UiAudioLayout){372,68,216,210,540,316};
}
#endif

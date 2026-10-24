#ifndef VMD_ONLINE_AUDIO_SCREEN_H
#define VMD_ONLINE_AUDIO_SCREEN_H
#include "library/library.h"
int ui_online_audio_open_library(const VtLibraryItem *);
int ui_online_audio_screen(void);
void ui_online_audio_set_home(int favorites_only);
#endif

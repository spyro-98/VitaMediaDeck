#ifndef VMD_LIBRARY_SCREEN_H
#define VMD_LIBRARY_SCREEN_H
#include "library/library.h"
enum {
    UI_LIBRARY_BACK,
    UI_LIBRARY_PLAY,
    UI_LIBRARY_LOCAL,
    UI_LIBRARY_TAB,
    UI_LIBRARY_CONNECT,
    UI_LIBRARY_SECTION_BASE = 100
};
void ui_library_reset_home(void);
int ui_library_screen(VtLibraryItem *selected);
#endif

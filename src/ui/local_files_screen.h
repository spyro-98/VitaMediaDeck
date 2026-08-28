#ifndef VITAMEDIADECK_UI_LOCAL_FILES_SCREEN_H
#define VITAMEDIADECK_UI_LOCAL_FILES_SCREEN_H

#include "ui/local_media_screen.h"

/* Finder-style read-only browser for ux0: and uma0:. It returns the same play
 * action and item type as the indexed Library so the application playback
 * path remains shared. */
int ui_local_files_screen(VtLocalMediaItem *selected_out);

#endif

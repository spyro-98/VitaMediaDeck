#ifndef VMD_UI_FOLDER_PICKER_H
#define VMD_UI_FOLDER_PICKER_H
#include <stddef.h>
/* Select within the initial mount. NULL defaults to existing ux0:download.
 * No directory is created unless the user explicitly selects New folder. */
int ui_folder_picker(const char *initial, char *out, size_t out_size);
int ui_playlist_file_picker(const char *initial,char *out,size_t out_size);
#endif

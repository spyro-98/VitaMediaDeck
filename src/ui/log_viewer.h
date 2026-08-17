#ifndef VITATUBE_UI_LOG_VIEWER_H
#define VITATUBE_UI_LOG_VIEWER_H

#define UI_LOG_VIEWER_TO_SPECS (-100)

/* Returns UI_LOG_VIEWER_TO_SPECS when the left tab is selected, a
 * UI_SECTION_* destination selected through L1, or UI_SECTION_NONE on back. */
int ui_log_viewer_screen(void);

#endif

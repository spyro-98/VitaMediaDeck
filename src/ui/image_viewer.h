#ifndef VITAMEDIADECK_UI_IMAGE_VIEWER_H
#define VITAMEDIADECK_UI_IMAGE_VIEWER_H

/* Opens a local image in the bounded full-screen viewer. Returns 0 after a
 * normal close, UI_IMAGE_VIEWER_SECTION_BASE + section for section navigation,
 * and a negative value when the image could not be loaded. */
#define UI_IMAGE_VIEWER_SECTION_BASE 10
int ui_image_viewer_show(const char *path, const char *title);

#endif

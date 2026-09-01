#ifndef VITAMEDIADECK_UI_IMAGE_VIEWER_H
#define VITAMEDIADECK_UI_IMAGE_VIEWER_H

/* Opens a local image in the bounded full-screen viewer. Returns 0 after a
 * normal close and a negative value when the image could not be loaded. */
int ui_image_viewer_show(const char *path, const char *title);

#endif

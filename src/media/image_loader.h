#ifndef VITAMEDIADECK_MEDIA_IMAGE_LOADER_H
#define VITAMEDIADECK_MEDIA_IMAGE_LOADER_H

#include <stddef.h>

#include <vita2d.h>

typedef struct {
	unsigned int source_width;
	unsigned int source_height;
	unsigned int decoded_width;
	unsigned int decoded_height;
	int downscaled;
	char format[12];
} VtImageInfo;

typedef struct {
	unsigned char *pixels;
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	VtImageInfo info;
} VtDecodedImage;

/* Common raster formats supported by the bounded image pipeline. */
int vt_image_supported_path(const char *path);

/* CPU-only decoding, safe to run in a loading worker. */
int vt_image_decode(const char *path, unsigned int max_edge,
	                VtDecodedImage *decoded,
	                char *error, size_t error_size);

/* Upload must run on the UI/main thread, matching the thumbnail pipeline. */
vita2d_texture *vt_image_upload(const VtDecodedImage *decoded,
	                            char *error, size_t error_size);
void vt_image_decoded_free(VtDecodedImage *decoded);

/* Decode into a Vita texture whose longest edge is at most max_edge.
 * JPEG and PNG use scanline decoders, WebP uses its scaled decoder, and the
 * remaining formats use a size-guarded stb_image fallback. */
vita2d_texture *vt_image_load_texture(const char *path,
	                                  unsigned int max_edge,
	                                  VtImageInfo *info,
	                                  char *error,
	                                  size_t error_size);

#endif

#include "media/image_loader.h"

#include <ctype.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <jerror.h>
#include <png.h>
#include <psp2/io/stat.h>
#include <psp2/gxm.h>
#include <webp/decode.h>

#define STBI_NO_JPEG
#define STBI_NO_PNG
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image.h>
#pragma GCC diagnostic pop

#define IMAGE_DEFAULT_MAX_EDGE 2048U
#define IMAGE_MAX_EDGE 2048U
#define IMAGE_PNG_MAX_DIMENSION 100000U
#define IMAGE_PNG_MAX_ROW_BYTES (8U * 1024U * 1024U)
#define IMAGE_WEBP_FILE_LIMIT (32U * 1024U * 1024U)
#define IMAGE_FALLBACK_FILE_LIMIT (24U * 1024U * 1024U)
#define IMAGE_FALLBACK_PIXEL_LIMIT 3000000ULL

static void set_error(char *error, size_t size, const char *message) {
	if (error && size) snprintf(error, size, "%s", message ? message : "Image error");
}

static int ends_with_ci(const char *name, const char *suffix) {
	if (!name || !suffix) return 0;
	size_t nl = strlen(name), sl = strlen(suffix);
	if (nl < sl) return 0;
	name += nl - sl;
	for (size_t i = 0; i < sl; i++)
		if (tolower((unsigned char)name[i]) !=
		    tolower((unsigned char)suffix[i])) return 0;
	return 1;
}

int vt_image_supported_path(const char *path) {
	static const char *const extensions[] = {
		".jpg", ".jpeg", ".jpe", ".png", ".webp", ".bmp", ".dib",
		".tga", ".gif", ".psd", ".hdr", ".pic", ".pnm", ".ppm", ".pgm"
	};
	for (unsigned int i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
		if (ends_with_ci(path, extensions[i])) return 1;
	return 0;
}

static void fit_dimensions(unsigned int width, unsigned int height,
	                       unsigned int max_edge,
	                       unsigned int *out_width,
	                       unsigned int *out_height) {
	if (!max_edge || max_edge > IMAGE_MAX_EDGE) max_edge = IMAGE_MAX_EDGE;
	if (!width || !height) { *out_width = *out_height = 0; return; }
	unsigned int longest = width > height ? width : height;
	if (longest <= max_edge) {
		*out_width = width;
		*out_height = height;
		return;
	}
	*out_width = (unsigned int)(((uint64_t)width * max_edge) / longest);
	*out_height = (unsigned int)(((uint64_t)height * max_edge) / longest);
	if (!*out_width) *out_width = 1;
	if (!*out_height) *out_height = 1;
}

static int allocate_decoded(VtDecodedImage *decoded,
	                        unsigned int width, unsigned int height,
	                        char *error, size_t error_size) {
	if (!width || !height || width > IMAGE_MAX_EDGE || height > IMAGE_MAX_EDGE) {
		set_error(error, error_size, "Invalid image dimensions");
		return -1;
	}
	size_t stride = (size_t)width * 4U;
	if (stride > UINT_MAX || height > SIZE_MAX / stride) {
		set_error(error, error_size, "Image dimensions exceed safe limits");
		return -1;
	}
	decoded->pixels = malloc(stride * height);
	if (!decoded->pixels) {
		set_error(error, error_size, "Not enough memory for the image proxy");
		return -1;
	}
	decoded->width = width;
	decoded->height = height;
	decoded->stride = (unsigned int)stride;
	return 0;
}

void vt_image_decoded_free(VtDecodedImage *decoded) {
	if (!decoded) return;
	free(decoded->pixels);
	memset(decoded, 0, sizeof(*decoded));
}

vita2d_texture *vt_image_upload(const VtDecodedImage *decoded,
	                            char *error, size_t error_size) {
	if (!decoded || !decoded->pixels || !decoded->width || !decoded->height) {
		set_error(error, error_size, "No decoded image to upload");
		return NULL;
	}
	vita2d_texture *texture = vita2d_create_empty_texture_format(
	    decoded->width, decoded->height, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
	if (!texture) {
		set_error(error, error_size, "Not enough graphics memory for the image");
		return NULL;
	}
	unsigned char *destination = vita2d_texture_get_datap(texture);
	unsigned int destination_stride = vita2d_texture_get_stride(texture);
	if (!destination || destination_stride < decoded->width * 4U) {
		vita2d_free_texture(texture);
		set_error(error, error_size, "Invalid graphics texture layout");
		return NULL;
	}
	for (unsigned int y = 0; y < decoded->height; y++)
		memcpy(destination + y * destination_stride,
		       decoded->pixels + y * decoded->stride,
		       decoded->width * 4U);
	vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR,
	                           SCE_GXM_TEXTURE_FILTER_LINEAR);
	return texture;
}

static void fill_info(VtImageInfo *info, const char *format,
	                  unsigned int source_width, unsigned int source_height,
	                  unsigned int width, unsigned int height) {
	if (!info) return;
	memset(info, 0, sizeof(*info));
	info->source_width = source_width;
	info->source_height = source_height;
	info->decoded_width = width;
	info->decoded_height = height;
	info->downscaled = width != source_width || height != source_height;
	snprintf(info->format, sizeof(info->format), "%s", format);
}

typedef struct {
	struct jpeg_error_mgr base;
	jmp_buf jump;
	char message[JMSG_LENGTH_MAX];
} ImageJpegError;

static void jpeg_fail(j_common_ptr common) {
	ImageJpegError *handler = (ImageJpegError *)common->err;
	(*common->err->format_message)(common, handler->message);
	longjmp(handler->jump, 1);
}

static int load_jpeg(const char *path, unsigned int max_edge,
	                 VtDecodedImage *decoded,
	                 char *error, size_t error_size) {
	FILE *file = fopen(path, "rb");
	if (!file) { set_error(error, error_size, "Could not open JPEG file"); return -1; }
	struct jpeg_decompress_struct decoder;
	ImageJpegError handler;
	memset(&decoder, 0, sizeof(decoder));
	memset(&handler, 0, sizeof(handler));
	decoder.err = jpeg_std_error(&handler.base);
	handler.base.error_exit = jpeg_fail;
	if (setjmp(handler.jump)) {
		jpeg_destroy_decompress(&decoder);
		fclose(file);
		vt_image_decoded_free(decoded);
		set_error(error, error_size,
		          handler.message[0] ? handler.message : "JPEG decode failed");
		return -1;
	}
	jpeg_create_decompress(&decoder);
	jpeg_stdio_src(&decoder, file);
	jpeg_read_header(&decoder, TRUE);
	unsigned int source_width = decoder.image_width;
	unsigned int source_height = decoder.image_height;
	if (!source_width || !source_height) ERREXIT(&decoder, JERR_EMPTY_IMAGE);
	unsigned int longest = source_width > source_height ? source_width : source_height;
	decoder.scale_num = 1;
	decoder.scale_denom = longest > max_edge * 8U ? 8
	                    : longest > max_edge * 4U ? 4
	                    : longest > max_edge * 2U ? 2 : 1;
	decoder.out_color_space = JCS_RGB;
	jpeg_start_decompress(&decoder);
	unsigned int width, height;
	fit_dimensions(decoder.output_width, decoder.output_height, max_edge,
	               &width, &height);
	if (allocate_decoded(decoded, width, height, error, error_size) < 0)
		ERREXIT(&decoder, JERR_OUT_OF_MEMORY);
	unsigned char *pixels = decoded->pixels;
	unsigned int stride = decoded->stride;
	JSAMPARRAY row = (*decoder.mem->alloc_sarray)(
	    (j_common_ptr)&decoder, JPOOL_IMAGE,
	    decoder.output_width * decoder.output_components, 1);
	while (decoder.output_scanline < decoder.output_height) {
		unsigned int source_y = decoder.output_scanline;
		jpeg_read_scanlines(&decoder, row, 1);
		unsigned int destination_y = (unsigned int)(
		    (uint64_t)source_y * height / decoder.output_height);
		if (destination_y >= height) destination_y = height - 1;
		unsigned char *destination = pixels + destination_y * stride;
		for (unsigned int x = 0; x < width; x++) {
			unsigned int source_x = (unsigned int)(
			    (uint64_t)x * decoder.output_width / width);
			const unsigned char *sample = row[0] + source_x * 3U;
			destination[x * 4U + 0U] = sample[0];
			destination[x * 4U + 1U] = sample[1];
			destination[x * 4U + 2U] = sample[2];
			destination[x * 4U + 3U] = 255;
		}
	}
	jpeg_finish_decompress(&decoder);
	jpeg_destroy_decompress(&decoder);
	fclose(file);
	fill_info(&decoded->info, "JPEG", source_width, source_height, width, height);
	return 0;
}

typedef struct {
	FILE *file;
	png_structp png;
	png_infop png_info;
	unsigned char *row;
	VtDecodedImage *decoded;
} ImagePngCleanup;

static int load_png(const char *path, unsigned int max_edge,
	                VtDecodedImage *decoded,
	                char *error, size_t error_size) {
	ImagePngCleanup *cleanup = calloc(1, sizeof(*cleanup));
	if (!cleanup) { set_error(error, error_size, "Not enough memory for PNG"); return -1; }
	cleanup->decoded = decoded;
	cleanup->file = fopen(path, "rb");
	if (!cleanup->file) {
		free(cleanup);
		set_error(error, error_size, "Could not open PNG file");
		return -1;
	}
	unsigned char signature[8];
	if (fread(signature, 1, sizeof(signature), cleanup->file) != sizeof(signature) ||
	    png_sig_cmp(signature, 0, sizeof(signature))) {
		fclose(cleanup->file); free(cleanup);
		set_error(error, error_size, "Invalid PNG signature");
		return -1;
	}
	cleanup->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	cleanup->png_info = cleanup->png ? png_create_info_struct(cleanup->png) : NULL;
	if (!cleanup->png || !cleanup->png_info) {
		if (cleanup->png) png_destroy_read_struct(&cleanup->png, NULL, NULL);
		fclose(cleanup->file); free(cleanup);
		set_error(error, error_size, "Could not initialize PNG decoder");
		return -1;
	}
	if (setjmp(png_jmpbuf(cleanup->png))) {
		vt_image_decoded_free(cleanup->decoded);
		free(cleanup->row);
		png_destroy_read_struct(&cleanup->png, &cleanup->png_info, NULL);
		fclose(cleanup->file); free(cleanup);
		set_error(error, error_size, "PNG decode failed");
		return -1;
	}
	png_set_user_limits(cleanup->png, IMAGE_PNG_MAX_DIMENSION,
	                    IMAGE_PNG_MAX_DIMENSION);
	png_init_io(cleanup->png, cleanup->file);
	png_set_sig_bytes(cleanup->png, sizeof(signature));
	png_read_info(cleanup->png, cleanup->png_info);
	png_uint_32 source_width = png_get_image_width(cleanup->png, cleanup->png_info);
	png_uint_32 source_height = png_get_image_height(cleanup->png, cleanup->png_info);
	int color_type = png_get_color_type(cleanup->png, cleanup->png_info);
	int bit_depth = png_get_bit_depth(cleanup->png, cleanup->png_info);
	if (bit_depth == 16) png_set_strip_16(cleanup->png);
	if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(cleanup->png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(cleanup->png);
	if (png_get_valid(cleanup->png, cleanup->png_info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(cleanup->png);
	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(cleanup->png);
	if (!(color_type & PNG_COLOR_MASK_ALPHA) &&
	    !png_get_valid(cleanup->png, cleanup->png_info, PNG_INFO_tRNS))
		png_set_filler(cleanup->png, 0xff, PNG_FILLER_AFTER);
	int passes = png_set_interlace_handling(cleanup->png);
	png_read_update_info(cleanup->png, cleanup->png_info);
	png_size_t row_bytes = png_get_rowbytes(cleanup->png, cleanup->png_info);
	if (!source_width || !source_height || row_bytes > IMAGE_PNG_MAX_ROW_BYTES)
		png_error(cleanup->png, "PNG dimensions exceed safe limits");
	unsigned int width, height;
	fit_dimensions(source_width, source_height, max_edge, &width, &height);
	if (allocate_decoded(decoded, width, height, error, error_size) < 0)
		png_error(cleanup->png, "proxy allocation failed");
	cleanup->row = calloc(1, row_bytes);
	if (!cleanup->row) png_error(cleanup->png, "row allocation failed");
	unsigned char *pixels = decoded->pixels;
	unsigned int stride = decoded->stride;
	for (int pass = 0; pass < passes; pass++) {
		for (png_uint_32 y = 0; y < source_height; y++) {
			png_read_row(cleanup->png, cleanup->row, NULL);
			unsigned int destination_y = (unsigned int)((uint64_t)y * height /
			                                                  source_height);
			if (destination_y >= height) destination_y = height - 1;
			unsigned char *destination = pixels + destination_y * stride;
			for (unsigned int x = 0; x < width; x++) {
				unsigned int source_x = (unsigned int)((uint64_t)x * source_width /
				                                              width);
				memcpy(destination + x * 4U, cleanup->row + source_x * 4U, 4U);
			}
		}
	}
	png_read_end(cleanup->png, NULL);
	free(cleanup->row);
	png_destroy_read_struct(&cleanup->png, &cleanup->png_info, NULL);
	fclose(cleanup->file);
	free(cleanup);
	fill_info(&decoded->info, "PNG", source_width, source_height, width, height);
	return 0;
}

static unsigned char *read_bounded_file(const char *path, size_t limit,
	                                    size_t *size_out,
	                                    char *error, size_t error_size) {
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0) {
		set_error(error, error_size, "Could not inspect image file");
		return NULL;
	}
	uint64_t size64 = (uint64_t)stat.st_size;
	if (size64 > limit || size64 > SIZE_MAX) {
		set_error(error, error_size, "Compressed image is too large to load safely");
		return NULL;
	}
	size_t size = (size_t)size64;
	unsigned char *data = malloc(size);
	if (!data) { set_error(error, error_size, "Not enough memory for image data"); return NULL; }
	FILE *file = fopen(path, "rb");
	if (!file || fread(data, 1, size, file) != size) {
		if (file) fclose(file);
		free(data);
		set_error(error, error_size, "Could not read image file");
		return NULL;
	}
	fclose(file);
	*size_out = size;
	return data;
}

static int load_webp(const char *path, unsigned int max_edge,
	                 VtDecodedImage *decoded,
	                 char *error, size_t error_size) {
	size_t size = 0;
	unsigned char *data = read_bounded_file(path, IMAGE_WEBP_FILE_LIMIT,
	                                        &size, error, error_size);
	if (!data) return -1;
	WebPDecoderConfig config;
	if (!WebPInitDecoderConfig(&config) ||
	    WebPGetFeatures(data, size, &config.input) != VP8_STATUS_OK ||
	    config.input.width <= 0 || config.input.height <= 0) {
		free(data); set_error(error, error_size, "Invalid WebP image"); return -1;
	}
	unsigned int source_width = (unsigned int)config.input.width;
	unsigned int source_height = (unsigned int)config.input.height;
	unsigned int width, height;
	fit_dimensions(source_width, source_height, max_edge, &width, &height);
	if (allocate_decoded(decoded, width, height, error, error_size) < 0) {
		free(data); return -1;
	}
	config.options.use_scaling = width != source_width || height != source_height;
	config.options.scaled_width = (int)width;
	config.options.scaled_height = (int)height;
	config.options.use_threads = 0;
	config.output.colorspace = MODE_RGBA;
	config.output.is_external_memory = 1;
	config.output.u.RGBA.rgba = decoded->pixels;
	config.output.u.RGBA.stride = (int)decoded->stride;
	config.output.u.RGBA.size = (size_t)config.output.u.RGBA.stride * height;
	VP8StatusCode status = WebPDecode(data, size, &config);
	WebPFreeDecBuffer(&config.output);
	free(data);
	if (status != VP8_STATUS_OK) {
		vt_image_decoded_free(decoded);
		set_error(error, error_size, "WebP decode failed");
		return -1;
	}
	fill_info(&decoded->info, "WEBP", source_width, source_height, width, height);
	return 0;
}

static int load_fallback(const char *path, unsigned int max_edge,
	                     VtDecodedImage *output,
	                     char *error, size_t error_size) {
	size_t size = 0;
	unsigned char *data = read_bounded_file(path, IMAGE_FALLBACK_FILE_LIMIT,
	                                        &size, error, error_size);
	if (!data) return -1;
	int source_width = 0, source_height = 0, channels = 0;
	if (!stbi_info_from_memory(data, (int)size, &source_width, &source_height,
	                           &channels) || source_width <= 0 || source_height <= 0) {
		free(data); set_error(error, error_size, "Unsupported or invalid image"); return -1;
	}
	if ((uint64_t)source_width * (uint64_t)source_height >
	    IMAGE_FALLBACK_PIXEL_LIMIT) {
		free(data);
		set_error(error, error_size,
		          "This large format cannot be downscaled safely on PS Vita");
		return -1;
	}
	unsigned char *decoded = stbi_load_from_memory(data, (int)size,
	                                               &source_width, &source_height,
	                                               &channels, 4);
	free(data);
	if (!decoded) {
		set_error(error, error_size, stbi_failure_reason());
		return -1;
	}
	unsigned int width, height;
	fit_dimensions((unsigned int)source_width, (unsigned int)source_height,
	               max_edge, &width, &height);
	if (allocate_decoded(output, width, height, error, error_size) < 0) {
		stbi_image_free(decoded); return -1;
	}
	unsigned char *pixels = output->pixels;
	unsigned int stride = output->stride;
	for (unsigned int y = 0; y < height; y++) {
		unsigned int source_y = (unsigned int)((uint64_t)y * source_height / height);
		unsigned char *destination = pixels + y * stride;
		for (unsigned int x = 0; x < width; x++) {
			unsigned int source_x = (unsigned int)((uint64_t)x * source_width / width);
			memcpy(destination + x * 4U,
			       decoded + ((size_t)source_y * source_width + source_x) * 4U, 4U);
		}
	}
	stbi_image_free(decoded);
	const char *dot = strrchr(path, '.');
	char format[12] = "IMAGE";
	if (dot && dot[1]) {
		size_t length = strlen(dot + 1);
		if (length > sizeof(format) - 1) length = sizeof(format) - 1;
		for (size_t i = 0; i < length; i++)
			format[i] = (char)toupper((unsigned char)dot[i + 1]);
		format[length] = '\0';
	}
	fill_info(&output->info, format, (unsigned int)source_width,
	          (unsigned int)source_height, width, height);
	return 0;
}

int vt_image_decode(const char *path, unsigned int max_edge,
	                VtDecodedImage *decoded,
	                char *error, size_t error_size) {
	if (!decoded) { set_error(error, error_size, "No image output buffer"); return -1; }
	memset(decoded, 0, sizeof(*decoded));
	if (error && error_size) error[0] = '\0';
	if (!path || !path[0]) { set_error(error, error_size, "No image selected"); return -1; }
	if (!max_edge) max_edge = IMAGE_DEFAULT_MAX_EDGE;
	if (max_edge > IMAGE_MAX_EDGE) max_edge = IMAGE_MAX_EDGE;
	if (ends_with_ci(path, ".jpg") || ends_with_ci(path, ".jpeg") ||
	    ends_with_ci(path, ".jpe"))
		return load_jpeg(path, max_edge, decoded, error, error_size);
	if (ends_with_ci(path, ".png"))
		return load_png(path, max_edge, decoded, error, error_size);
	if (ends_with_ci(path, ".webp"))
		return load_webp(path, max_edge, decoded, error, error_size);
	return load_fallback(path, max_edge, decoded, error, error_size);
}

vita2d_texture *vt_image_load_texture(const char *path,
	                                  unsigned int max_edge,
	                                  VtImageInfo *info,
	                                  char *error, size_t error_size) {
	VtDecodedImage decoded;
	if (vt_image_decode(path, max_edge, &decoded, error, error_size) < 0)
		return NULL;
	vita2d_texture *texture = vt_image_upload(&decoded, error, error_size);
	if (texture && info) *info = decoded.info;
	vt_image_decoded_free(&decoded);
	return texture;
}

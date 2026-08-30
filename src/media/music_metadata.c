#include "media/music_metadata.h"

#include <stdio.h>
#include <string.h>

#include <FLAC/stream_decoder.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "app_paths.h"

#define MUSIC_ARTWORK_DIR VITAMEDIADECK_DATA_DIR "/music_art"
#define MUSIC_ARTWORK_MAX_BYTES (8U * 1024U * 1024U)

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t reserved;
	VtMusicMetadata metadata;
} MusicMetadataDisk;

typedef struct {
	char title[200];
	char artist[96];
	char album[128];
	uint64_t duration_ms;
} VtMusicMetadataV2;

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t reserved;
	VtMusicMetadataV2 metadata;
} MusicMetadataDiskV2;

typedef struct {
	char title[200];
	char artist[96];
	uint64_t duration_ms;
} VtMusicMetadataV1;

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t reserved;
	VtMusicMetadataV1 metadata;
} MusicMetadataDiskV1;

static int sidecar_path(const char *media_path, char out[512]) {
	if (!media_path || !out) return -1;
	int length = snprintf(out, 512, "%s.meta", media_path);
	return length > 0 && length < 512 ? 0 : -1;
}

static uint32_t syncsafe32(const unsigned char value[4]) {
	return ((uint32_t)(value[0] & 0x7f) << 21) |
	       ((uint32_t)(value[1] & 0x7f) << 14) |
	       ((uint32_t)(value[2] & 0x7f) << 7) |
	       (uint32_t)(value[3] & 0x7f);
}

static size_t append_utf8(char *out, size_t capacity, size_t used,
	                      uint32_t codepoint) {
	if (codepoint == 0) return used;
	if (codepoint < 0x80) {
		if (used + 1 < capacity) out[used++] = (char)codepoint;
	} else if (codepoint < 0x800) {
		if (used + 2 < capacity) {
			out[used++] = (char)(0xc0 | (codepoint >> 6));
			out[used++] = (char)(0x80 | (codepoint & 0x3f));
		}
	} else if (codepoint < 0x10000) {
		if (used + 3 < capacity) {
			out[used++] = (char)(0xe0 | (codepoint >> 12));
			out[used++] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
			out[used++] = (char)(0x80 | (codepoint & 0x3f));
		}
	}
	return used;
}

static void decode_id3_text(const unsigned char *data, size_t size,
	                        char *out, size_t out_size) {
	if (!out || out_size == 0) return;
	out[0] = '\0';
	if (!data || size < 2) return;
	unsigned encoding = data[0];
	data++; size--;
	size_t used = 0;
	if (encoding == 3) {
		while (used + 1 < out_size && used < size && data[used]) {
			out[used] = (char)data[used];
			used++;
		}
	} else if (encoding == 0) {
		for (size_t i = 0; i < size && data[i]; i++)
			used = append_utf8(out, out_size, used, data[i]);
	} else if (encoding == 1 || encoding == 2) {
		int little = 0;
		size_t offset = 0;
		if (encoding == 1 && size >= 2) {
			if (data[0] == 0xff && data[1] == 0xfe) little = 1;
			else if (data[0] == 0xfe && data[1] == 0xff) little = 0;
			offset = 2;
		}
		for (; offset + 1 < size; offset += 2) {
			uint32_t codepoint = little
			                   ? (uint32_t)data[offset] |
			                     ((uint32_t)data[offset + 1] << 8)
			                   : ((uint32_t)data[offset] << 8) |
			                     (uint32_t)data[offset + 1];
			if (!codepoint) break;
			used = append_utf8(out, out_size, used, codepoint);
		}
	}
	out[used] = '\0';
}

static int suffix_ci(const char *path, const char *suffix) {
	if (!path || !suffix) return 0;
	size_t path_length = strlen(path), suffix_length = strlen(suffix);
	if (path_length < suffix_length) return 0;
	path += path_length - suffix_length;
	for (size_t i = 0; i < suffix_length; i++) {
		char left = path[i], right = suffix[i];
		if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
		if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
		if (left != right) return 0;
	}
	return 1;
}

typedef struct {
	SceUID fd;
	FLAC__uint64 file_size;
	const char *source_path;
	VtMusicMetadata *metadata;
	int picture_rank;
	int io_error;
} FlacMetadataContext;

static FLAC__StreamDecoderReadStatus flac_metadata_read(
	const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes,
	void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	if (!context || !bytes || *bytes == 0) {
		if (bytes) *bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
	int read = sceIoRead(context->fd, buffer, *bytes);
	if (read < 0) {
		context->io_error = read;
		*bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
	*bytes = (size_t)read;
	return read == 0 ? FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM
	                 : FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderSeekStatus flac_metadata_seek(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 offset, void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	if (!context || offset > (FLAC__uint64)INT64_MAX)
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
	return sceIoLseek(context->fd, (SceOff)offset, SCE_SEEK_SET) < 0
	     ? FLAC__STREAM_DECODER_SEEK_STATUS_ERROR
	     : FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__StreamDecoderTellStatus flac_metadata_tell(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 *offset, void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	SceOff position = context ? sceIoLseek(context->fd, 0, SCE_SEEK_CUR) : -1;
	if (position < 0) return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
	*offset = (FLAC__uint64)position;
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__StreamDecoderLengthStatus flac_metadata_length(
	const FLAC__StreamDecoder *decoder, FLAC__uint64 *length, void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	if (!context || !context->file_size)
		return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
	*length = context->file_size;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool flac_metadata_eof(const FLAC__StreamDecoder *decoder,
	                                void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	SceOff position = context ? sceIoLseek(context->fd, 0, SCE_SEEK_CUR) : -1;
	return position >= 0 && (FLAC__uint64)position >= context->file_size;
}

static FLAC__StreamDecoderWriteStatus flac_metadata_write(
	const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
	const FLAC__int32 *const buffer[], void *client_data) {
	(void)decoder;
	(void)frame;
	(void)buffer;
	(void)client_data;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static int entry_key_ci(const FLAC__byte *entry, uint32_t length,
	                    const char *key) {
	size_t key_length = strlen(key);
	if (!entry || length <= key_length || entry[key_length] != '=') return 0;
	for (size_t i = 0; i < key_length; i++) {
		char left = (char)entry[i], right = key[i];
		if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
		if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
		if (left != right) return 0;
	}
	return 1;
}

static void copy_vorbis_value(const FLAC__byte *entry, uint32_t length,
	                          const char *key, char *out, size_t out_size) {
	size_t offset = strlen(key) + 1;
	if (!out || out_size == 0 || length <= offset) return;
	size_t count = length - offset;
	if (count >= out_size) count = out_size - 1;
	memcpy(out, entry + offset, count);
	out[count] = '\0';
}

static uint32_t artwork_hash(const char *path) {
	uint32_t hash = 2166136261U;
	for (const unsigned char *cursor = (const unsigned char *)path;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 16777619U;
	}
	return hash;
}

static int save_flac_picture(FlacMetadataContext *context,
	                         const FLAC__StreamMetadata_Picture *picture) {
	if (!context || !picture || !picture->data || picture->data_length == 0 ||
	    picture->data_length > MUSIC_ARTWORK_MAX_BYTES)
		return -1;
	const char *extension = NULL;
	if (picture->data_length >= 8 &&
	    !memcmp(picture->data, "\x89PNG\r\n\x1a\n", 8)) extension = "png";
	else if (picture->data_length >= 2 && picture->data[0] == 0xff &&
	         picture->data[1] == 0xd8) extension = "jpg";
	if (!extension) return -1;
	int rank = picture->type == FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER
	         ? 2 : 1;
	if (rank < context->picture_rank) return 0;
	sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
	sceIoMkdir(MUSIC_ARTWORK_DIR, 0777);
	char target[512], temporary[520];
	snprintf(target, sizeof(target), MUSIC_ARTWORK_DIR "/%08x.%s",
	         artwork_hash(context->source_path), extension);
	snprintf(temporary, sizeof(temporary), "%s.tmp", target);
	sceIoRemove(temporary);
	SceUID fd = sceIoOpen(temporary,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	uint32_t written = 0;
	while (written < picture->data_length) {
		int count = sceIoWrite(fd, picture->data + written,
		                       picture->data_length - written);
		if (count <= 0) {
			sceIoClose(fd);
			sceIoRemove(temporary);
			return count < 0 ? count : -1;
		}
		written += (uint32_t)count;
	}
	int ret = sceIoSyncByFd(fd, 0);
	sceIoClose(fd);
	if (ret < 0) {
		sceIoRemove(temporary);
		return ret;
	}
	sceIoRemove(target);
	ret = sceIoRename(temporary, target);
	if (ret < 0) {
		sceIoRemove(temporary);
		return ret;
	}
	snprintf(context->metadata->artwork_path,
	         sizeof(context->metadata->artwork_path), "%s", target);
	context->picture_rank = rank;
	return 0;
}

static void flac_metadata_block(const FLAC__StreamDecoder *decoder,
	                            const FLAC__StreamMetadata *block,
	                            void *client_data) {
	(void)decoder;
	FlacMetadataContext *context = client_data;
	if (!context || !block) return;
	if (block->type == FLAC__METADATA_TYPE_STREAMINFO) {
		if (block->data.stream_info.sample_rate &&
		    block->data.stream_info.total_samples)
			context->metadata->duration_ms =
				block->data.stream_info.total_samples * 1000ULL /
				block->data.stream_info.sample_rate;
	} else if (block->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
		for (uint32_t i = 0; i < block->data.vorbis_comment.num_comments; i++) {
			const FLAC__StreamMetadata_VorbisComment_Entry *comment =
				&block->data.vorbis_comment.comments[i];
			if (entry_key_ci(comment->entry, comment->length, "TITLE"))
				copy_vorbis_value(comment->entry, comment->length, "TITLE",
				                  context->metadata->title,
				                  sizeof(context->metadata->title));
			else if (entry_key_ci(comment->entry, comment->length, "ARTIST"))
				copy_vorbis_value(comment->entry, comment->length, "ARTIST",
				                  context->metadata->artist,
				                  sizeof(context->metadata->artist));
			else if (entry_key_ci(comment->entry, comment->length, "ALBUM"))
				copy_vorbis_value(comment->entry, comment->length, "ALBUM",
				                  context->metadata->album,
				                  sizeof(context->metadata->album));
		}
	} else if (block->type == FLAC__METADATA_TYPE_PICTURE) {
		save_flac_picture(context, &block->data.picture);
	}
}

static void flac_metadata_error(const FLAC__StreamDecoder *decoder,
	                            FLAC__StreamDecoderErrorStatus status,
	                            void *client_data) {
	(void)decoder;
	(void)status;
	(void)client_data;
}

static int load_embedded_flac(const char *path, VtMusicMetadata *metadata) {
	FlacMetadataContext context;
	memset(&context, 0, sizeof(context));
	context.fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (context.fd < 0) return context.fd;
	context.source_path = path;
	context.metadata = metadata;
	SceOff end = sceIoLseek(context.fd, 0, SCE_SEEK_END);
	if (end <= 0 || sceIoLseek(context.fd, 0, SCE_SEEK_SET) < 0) {
		sceIoClose(context.fd);
		return -1;
	}
	context.file_size = (FLAC__uint64)end;
	FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
	if (!decoder) {
		sceIoClose(context.fd);
		return -1;
	}
	FLAC__stream_decoder_set_metadata_respond(
		decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);
	FLAC__stream_decoder_set_metadata_respond(decoder, FLAC__METADATA_TYPE_PICTURE);
	FLAC__StreamDecoderInitStatus init = FLAC__stream_decoder_init_stream(
		decoder, flac_metadata_read, flac_metadata_seek, flac_metadata_tell,
		flac_metadata_length, flac_metadata_eof, flac_metadata_write,
		flac_metadata_block, flac_metadata_error, &context);
	int ret = -1;
	int decoder_initialized = init == FLAC__STREAM_DECODER_INIT_STATUS_OK;
	if (decoder_initialized &&
	    FLAC__stream_decoder_process_until_end_of_metadata(decoder) &&
	    !context.io_error)
		ret = metadata->title[0] || metadata->artist[0] || metadata->album[0] ||
		      metadata->artwork_path[0] || metadata->duration_ms ? 0 : -1;
	if (decoder_initialized) FLAC__stream_decoder_finish(decoder);
	FLAC__stream_decoder_delete(decoder);
	sceIoClose(context.fd);
	return ret;
}

static int load_embedded_id3(const char *mp3_path, VtMusicMetadata *metadata) {
	SceUID fd = sceIoOpen(mp3_path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	unsigned char header[10];
	int read = sceIoRead(fd, header, sizeof(header));
	if (read != (int)sizeof(header) || memcmp(header, "ID3", 3) ||
	    header[3] < 3 || header[3] > 4) {
		sceIoClose(fd);
		return -1;
	}
	uint32_t tag_size = syncsafe32(header + 6);
	if (tag_size > 1024 * 1024U) tag_size = 1024 * 1024U;
	uint32_t consumed = 0;
	memset(metadata, 0, sizeof(*metadata));
	while (consumed + 10 <= tag_size) {
		unsigned char frame[10];
		if (sceIoRead(fd, frame, sizeof(frame)) != (int)sizeof(frame)) break;
		consumed += 10;
		if (!frame[0]) break;
		uint32_t frame_size = header[3] == 4 ? syncsafe32(frame + 4)
		                                      : ((uint32_t)frame[4] << 24) |
		                                        ((uint32_t)frame[5] << 16) |
		                                        ((uint32_t)frame[6] << 8) |
		                                        (uint32_t)frame[7];
		if (frame_size == 0 || frame_size > tag_size - consumed) break;
		char *target = NULL;
		size_t target_size = 0;
		if (!memcmp(frame, "TIT2", 4)) {
			target = metadata->title; target_size = sizeof(metadata->title);
		} else if (!memcmp(frame, "TPE1", 4)) {
			target = metadata->artist; target_size = sizeof(metadata->artist);
		} else if (!memcmp(frame, "TALB", 4)) {
			target = metadata->album; target_size = sizeof(metadata->album);
		}
		if (target) {
			unsigned char text[512];
			uint32_t wanted = frame_size < sizeof(text) ? frame_size : sizeof(text);
			int got = sceIoRead(fd, text, wanted);
			if (got > 0) decode_id3_text(text, (size_t)got, target, target_size);
			if (frame_size > wanted)
				sceIoLseek(fd, (SceOff)(frame_size - wanted), SCE_SEEK_CUR);
		} else {
			sceIoLseek(fd, (SceOff)frame_size, SCE_SEEK_CUR);
		}
		consumed += frame_size;
		if (metadata->title[0] && metadata->artist[0] && metadata->album[0]) break;
	}
	sceIoClose(fd);
	return metadata->title[0] || metadata->artist[0] || metadata->album[0]
	     ? 0 : -1;
}

static int load_embedded_metadata(const char *media_path,
	                              VtMusicMetadata *metadata) {
	memset(metadata, 0, sizeof(*metadata));
	return suffix_ci(media_path, ".flac")
	     ? load_embedded_flac(media_path, metadata)
	     : load_embedded_id3(media_path, metadata);
}

int vt_music_metadata_save(const char *media_path,
	                       const VtMusicMetadata *metadata) {
	char path[512], temp[520];
	if (!metadata || sidecar_path(media_path, path) < 0) return -1;
	snprintf(temp, sizeof(temp), "%s.tmp", path);
	MusicMetadataDisk disk;
	memset(&disk, 0, sizeof(disk));
	memcpy(disk.magic, "VTMUSIC3", 8);
	disk.version = 3;
	disk.metadata = *metadata;
	sceIoRemove(temp);
	SceUID fd = sceIoOpen(temp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int written = sceIoWrite(fd, &disk, sizeof(disk));
	int ret = written == (int)sizeof(disk) ? sceIoSyncByFd(fd, 0) : -1;
	sceIoClose(fd);
	if (ret == 0) { sceIoRemove(path); ret = sceIoRename(temp, path); }
	if (ret < 0) sceIoRemove(temp);
	return ret;
}

int vt_music_metadata_load(const char *media_path, VtMusicMetadata *metadata) {
	char path[512];
	if (!metadata || sidecar_path(media_path, path) < 0) return -1;
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(path, &stat) < 0)
		return load_embedded_metadata(media_path, metadata);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	union {
		MusicMetadataDisk current;
		MusicMetadataDiskV2 previous;
		MusicMetadataDiskV1 legacy;
	} storage;
	memset(&storage, 0, sizeof(storage));
	int wanted = stat.st_size == (SceOff)sizeof(MusicMetadataDiskV1)
	           ? (int)sizeof(MusicMetadataDiskV1)
	           : stat.st_size == (SceOff)sizeof(MusicMetadataDiskV2)
	           ? (int)sizeof(MusicMetadataDiskV2)
	           : stat.st_size == (SceOff)sizeof(MusicMetadataDisk)
	           ? (int)sizeof(MusicMetadataDisk) : 0;
	int read = wanted ? sceIoRead(fd, &storage, (SceSize)wanted) : -1;
	sceIoClose(fd);
	if (read != wanted || wanted == 0)
		return load_embedded_metadata(media_path, metadata);
	memset(metadata, 0, sizeof(*metadata));
	if (wanted == (int)sizeof(MusicMetadataDisk)) {
		MusicMetadataDisk *disk = &storage.current;
		if (memcmp(disk->magic, "VTMUSIC3", 8) || disk->version != 3)
			return load_embedded_metadata(media_path, metadata);
		*metadata = disk->metadata;
	} else if (wanted == (int)sizeof(MusicMetadataDiskV2)) {
		MusicMetadataDiskV2 *disk = &storage.previous;
		if (memcmp(disk->magic, "VTMUSIC2", 8) || disk->version != 2)
			return load_embedded_metadata(media_path, metadata);
		memcpy(metadata->title, disk->metadata.title,
		       sizeof(disk->metadata.title));
		memcpy(metadata->artist, disk->metadata.artist,
		       sizeof(disk->metadata.artist));
		memcpy(metadata->album, disk->metadata.album,
		       sizeof(disk->metadata.album));
		metadata->duration_ms = disk->metadata.duration_ms;
	} else {
		MusicMetadataDiskV1 *disk = &storage.legacy;
		if (memcmp(disk->magic, "VTMUSIC1", 8) || disk->version != 1)
			return load_embedded_metadata(media_path, metadata);
		memcpy(metadata->title, disk->metadata.title,
		       sizeof(disk->metadata.title));
		memcpy(metadata->artist, disk->metadata.artist,
		       sizeof(disk->metadata.artist));
		metadata->duration_ms = disk->metadata.duration_ms;
	}
	metadata->title[sizeof(metadata->title) - 1] = '\0';
	metadata->artist[sizeof(metadata->artist) - 1] = '\0';
	metadata->album[sizeof(metadata->album) - 1] = '\0';
	metadata->artwork_path[sizeof(metadata->artwork_path) - 1] = '\0';
	return 0;
}

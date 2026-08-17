#include "media/music_metadata.h"

#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t reserved;
	VtMusicMetadata metadata;
} MusicMetadataDisk;

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

static int sidecar_path(const char *mp3_path, char out[512]) {
	if (!mp3_path || !out) return -1;
	int length = snprintf(out, 512, "%s.meta", mp3_path);
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

int vt_music_metadata_save(const char *mp3_path, const VtMusicMetadata *metadata) {
	char path[512], temp[520];
	if (!metadata || sidecar_path(mp3_path, path) < 0) return -1;
	snprintf(temp, sizeof(temp), "%s.tmp", path);
	MusicMetadataDisk disk;
	memset(&disk, 0, sizeof(disk));
	memcpy(disk.magic, "VTMUSIC2", 8);
	disk.version = 2;
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

int vt_music_metadata_load(const char *mp3_path, VtMusicMetadata *metadata) {
	char path[512];
	if (!metadata || sidecar_path(mp3_path, path) < 0) return -1;
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(path, &stat) < 0)
		return load_embedded_id3(mp3_path, metadata);
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	union {
		MusicMetadataDisk current;
		MusicMetadataDiskV1 legacy;
	} storage;
	memset(&storage, 0, sizeof(storage));
	int wanted = stat.st_size == (SceOff)sizeof(MusicMetadataDiskV1)
	           ? (int)sizeof(MusicMetadataDiskV1)
	           : stat.st_size == (SceOff)sizeof(MusicMetadataDisk)
	           ? (int)sizeof(MusicMetadataDisk) : 0;
	int read = wanted ? sceIoRead(fd, &storage, (SceSize)wanted) : -1;
	sceIoClose(fd);
	if (read != wanted || wanted == 0)
		return load_embedded_id3(mp3_path, metadata);
	memset(metadata, 0, sizeof(*metadata));
	if (wanted == (int)sizeof(MusicMetadataDisk)) {
		MusicMetadataDisk *disk = &storage.current;
		if (memcmp(disk->magic, "VTMUSIC2", 8) || disk->version != 2)
			return load_embedded_id3(mp3_path, metadata);
		*metadata = disk->metadata;
	} else {
		MusicMetadataDiskV1 *disk = &storage.legacy;
		if (memcmp(disk->magic, "VTMUSIC1", 8) || disk->version != 1)
			return load_embedded_id3(mp3_path, metadata);
		memcpy(metadata->title, disk->metadata.title,
		       sizeof(disk->metadata.title));
		memcpy(metadata->artist, disk->metadata.artist,
		       sizeof(disk->metadata.artist));
		metadata->duration_ms = disk->metadata.duration_ms;
	}
	metadata->title[sizeof(metadata->title) - 1] = '\0';
	metadata->artist[sizeof(metadata->artist) - 1] = '\0';
	metadata->album[sizeof(metadata->album) - 1] = '\0';
	return 0;
}

#include "history/playback_history.h"

#include <stdint.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "app_paths.h"

#define VT_PLAYBACK_HISTORY_PATH VITATUBE_DATA_DIR "/playback_history.bin"
#define VT_PLAYBACK_HISTORY_TEMP VITATUBE_DATA_DIR "/playback_history.tmp"
#define VT_PLAYBACK_HISTORY_BACKUP VITATUBE_DATA_DIR "/playback_history.bak"
#define VT_PLAYBACK_HISTORY_VERSION 1U
#define VT_PLAYBACK_HISTORY_MAX 256
#define VT_PLAYBACK_VIDEO_ID_MAX 16
#define VT_RESUME_MIN_MS 10000ULL
#define VT_RESUME_END_GUARD_MS 15000ULL

typedef struct {
	char video_id[VT_PLAYBACK_VIDEO_ID_MAX];
	uint64_t position_ms;
	uint64_t duration_ms;
	uint64_t order;
} VtPlaybackHistoryRecord;

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t count;
	uint32_t checksum;
	uint32_t reserved;
	VtPlaybackHistoryRecord records[VT_PLAYBACK_HISTORY_MAX];
} VtPlaybackHistoryDisk;

static const unsigned char g_magic[8] = { 'V','T','R','E','S','U','M','E' };
static VtPlaybackHistoryDisk g_disk;
static int g_count;
static uint64_t g_next_order = 1;

static uint32_t crc32_bytes(const unsigned char *data, size_t size) {
	uint32_t crc = 0xffffffffU;
	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
			crc = (crc >> 1) ^ (0xedb88320U & mask);
		}
	}
	return ~crc;
}

static uint32_t checksum(VtPlaybackHistoryDisk *disk) {
	uint32_t saved = disk->checksum;
	disk->checksum = 0;
	uint32_t value = crc32_bytes((const unsigned char *)disk, sizeof(*disk));
	disk->checksum = saved;
	return value;
}

static int valid_id(const char *id) {
	if (!id || !id[0]) return 0;
	for (int i = 0; i < VT_PLAYBACK_VIDEO_ID_MAX; i++) {
		unsigned char ch = (unsigned char)id[i];
		if (!ch) return 1;
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		      (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return 0;
	}
	return 0;
}

static int write_all(SceUID fd, const void *data, size_t size) {
	const unsigned char *cursor = data;
	for (size_t done = 0; done < size;) {
		int ret = sceIoWrite(fd, cursor + done, size - done);
		if (ret <= 0) return ret < 0 ? ret : -1;
		done += (size_t)ret;
	}
	return 0;
}

static int read_all(SceUID fd, void *data, size_t size) {
	unsigned char *cursor = data;
	for (size_t done = 0; done < size;) {
		int ret = sceIoRead(fd, cursor + done, size - done);
		if (ret <= 0) return ret < 0 ? ret : -1;
		done += (size_t)ret;
	}
	return 0;
}

static int save_database(void) {
	memcpy(g_disk.magic, g_magic, sizeof(g_magic));
	g_disk.version = VT_PLAYBACK_HISTORY_VERSION;
	g_disk.count = (uint32_t)g_count;
	g_disk.checksum = checksum(&g_disk);
	sceIoMkdir(VITATUBE_DATA_DIR, 0777);
	sceIoRemove(VT_PLAYBACK_HISTORY_TEMP);
	SceUID fd = sceIoOpen(VT_PLAYBACK_HISTORY_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int ret = write_all(fd, &g_disk, sizeof(g_disk));
	if (ret == 0) ret = sceIoSyncByFd(fd, 0);
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	if (ret < 0) { sceIoRemove(VT_PLAYBACK_HISTORY_TEMP); return ret; }
	sceIoRemove(VT_PLAYBACK_HISTORY_BACKUP);
	int had_previous = sceIoRename(VT_PLAYBACK_HISTORY_PATH,
	                              VT_PLAYBACK_HISTORY_BACKUP) >= 0;
	ret = sceIoRename(VT_PLAYBACK_HISTORY_TEMP, VT_PLAYBACK_HISTORY_PATH);
	if (ret < 0) {
		if (had_previous)
			sceIoRename(VT_PLAYBACK_HISTORY_BACKUP, VT_PLAYBACK_HISTORY_PATH);
		sceIoRemove(VT_PLAYBACK_HISTORY_TEMP);
	} else {
		sceIoRemove(VT_PLAYBACK_HISTORY_BACKUP);
		sceIoSync("ux0:", 0);
	}
	return ret;
}

int vt_playback_history_init(void) {
	memset(&g_disk, 0, sizeof(g_disk));
	g_count = 0;
	g_next_order = 1;
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(VT_PLAYBACK_HISTORY_PATH, &stat) < 0) {
		if (sceIoRename(VT_PLAYBACK_HISTORY_BACKUP,
		                VT_PLAYBACK_HISTORY_PATH) < 0) return 0;
	}
	if (sceIoGetstat(VT_PLAYBACK_HISTORY_PATH, &stat) < 0) return 0;
	if (stat.st_size != (SceOff)sizeof(g_disk)) return -1;
	SceUID fd = sceIoOpen(VT_PLAYBACK_HISTORY_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	int ret = read_all(fd, &g_disk, sizeof(g_disk));
	sceIoClose(fd);
	if (ret < 0 || memcmp(g_disk.magic, g_magic, sizeof(g_magic)) != 0 ||
	    g_disk.version != VT_PLAYBACK_HISTORY_VERSION ||
	    g_disk.count > VT_PLAYBACK_HISTORY_MAX ||
	    g_disk.checksum != checksum(&g_disk)) {
		memset(&g_disk, 0, sizeof(g_disk));
		return -1;
	}
	for (uint32_t i = 0; i < g_disk.count; i++) {
		VtPlaybackHistoryRecord record = g_disk.records[i];
		if (!valid_id(record.video_id)) continue;
		g_disk.records[g_count++] = record;
		if (record.order >= g_next_order) g_next_order = record.order + 1;
	}
	return 0;
}

static int find_index(const char *video_id) {
	if (!valid_id(video_id)) return -1;
	for (int i = 0; i < g_count; i++)
		if (strcmp(g_disk.records[i].video_id, video_id) == 0) return i;
	return -1;
}

uint64_t vt_playback_history_position(const char *video_id,
	                                  uint64_t duration_ms) {
	int index = find_index(video_id);
	if (index < 0) return 0;
	uint64_t position = g_disk.records[index].position_ms;
	uint64_t known_duration = duration_ms ? duration_ms
	                        : g_disk.records[index].duration_ms;
	if (position < VT_RESUME_MIN_MS ||
	    (known_duration > VT_RESUME_END_GUARD_MS &&
	     position + VT_RESUME_END_GUARD_MS >= known_duration)) return 0;
	return position;
}

int vt_playback_history_get(const char *video_id, uint64_t *position_ms,
	                        uint64_t *duration_ms) {
	int index = find_index(video_id);
	if (index < 0) return 0;
	uint64_t position = vt_playback_history_position(
	    video_id, g_disk.records[index].duration_ms);
	if (!position || !g_disk.records[index].duration_ms) return 0;
	if (position_ms) *position_ms = position;
	if (duration_ms) *duration_ms = g_disk.records[index].duration_ms;
	return 1;
}

int vt_playback_history_update(const char *video_id, uint64_t position_ms,
	                           uint64_t duration_ms) {
	if (!valid_id(video_id)) return -1;
	int index = find_index(video_id);
	int keep = position_ms >= VT_RESUME_MIN_MS &&
	           (!duration_ms || position_ms + VT_RESUME_END_GUARD_MS < duration_ms);
	if (!keep) {
		if (index < 0) return 0;
		for (int i = index + 1; i < g_count; i++)
			g_disk.records[i - 1] = g_disk.records[i];
		memset(&g_disk.records[--g_count], 0, sizeof(g_disk.records[0]));
		return save_database();
	}
	VtPlaybackHistoryRecord record;
	memset(&record, 0, sizeof(record));
	strncpy(record.video_id, video_id, sizeof(record.video_id) - 1);
	record.position_ms = position_ms;
	record.duration_ms = duration_ms;
	record.order = g_next_order++;
	if (index >= 0) {
		for (int i = index; i > 0; i--) g_disk.records[i] = g_disk.records[i - 1];
	} else {
		if (g_count < VT_PLAYBACK_HISTORY_MAX) g_count++;
		for (int i = g_count - 1; i > 0; i--)
			g_disk.records[i] = g_disk.records[i - 1];
	}
	g_disk.records[0] = record;
	return save_database();
}

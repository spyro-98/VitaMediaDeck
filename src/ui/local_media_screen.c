#include "ui/local_media_screen.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "media/music_metadata.h"
#include "media/video_thumbnail.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/mini_player.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define LOCAL_MAX_ITEMS 65536
#define LOCAL_PAGE_ITEMS 24
#define LOCAL_SCAN_THREAD_STACK 0x40000
#define LOCAL_INDEX_PATH "ux0:data/VitaMediaDeck/local_media.idx"
#define LOCAL_INDEX_TEMP "ux0:data/VitaMediaDeck/local_media.tmp"
#define LOCAL_INDEX_BACKUP "ux0:data/VitaMediaDeck/local_media.bak"
#define LOCAL_GROUP_VIDEO_TEMP "ux0:data/VitaMediaDeck/local_video.tmp"
#define LOCAL_GROUP_AUDIO_TEMP "ux0:data/VitaMediaDeck/local_audio.tmp"
#define LIST_X 52
#define LIST_Y 122
#define LIST_W 856
#define ROW_H 52
#define VISIBLE_ROWS 7
#define GRID_COLS 3
#define GRID_CARD_W 276
#define GRID_CARD_H 200
#define GRID_GAP_X 16
#define GRID_GAP_Y 8
#define GRID_THUMB_H 148

static int local_viewport_bottom(void) {
	int mini_top = ui_mini_player_top();
	return mini_top < 532 ? mini_top : 532;
}

static int local_list_visible_rows(void) {
	int rows = (local_viewport_bottom() - LIST_Y + 6) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int local_list_render_rows(void) {
	int rows = (local_viewport_bottom() - LIST_Y + ROW_H - 1) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int local_grid_visible_rows(void) {
	int step = GRID_CARD_H + GRID_GAP_Y;
	int rows = (local_viewport_bottom() - LIST_Y + GRID_GAP_Y) / step;
	if (rows < 1) rows = 1;
	if (rows > 2) rows = 2;
	return rows;
}

static int local_grid_render_rows(void) {
	int step = GRID_CARD_H + GRID_GAP_Y;
	int rows = (local_viewport_bottom() - LIST_Y + step - 1) / step;
	if (rows < 1) rows = 1;
	if (rows > 2) rows = 2;
	return rows;
}

static int grid_top_for_selection(int selected) {
	int selected_row = selected / GRID_COLS;
	int visible = local_grid_visible_rows();
	return selected_row >= visible ? selected_row - visible + 1 : 0;
}

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t record_size;
	uint32_t total_count;
	uint32_t video_count;
	uint32_t audio_count;
} LocalMediaIndexHeader;

static VtLocalMediaItem g_items[LOCAL_PAGE_ITEMS];
static LocalMediaIndexHeader g_index;
static int g_page_filter = -1;
static int g_page_start = -1;
static int g_page_count;

typedef struct {
	void *self;
	SceUID thid;
	volatile int done;
	int result;
	LocalMediaIndexHeader index;
} LocalMediaScanJob;

static LocalMediaScanJob g_scan_job = { .thid = -1 };

#define LOCAL_ART_CACHE 12
typedef struct {
	char path[VT_LOCAL_MEDIA_PATH_MAX];
	vita2d_texture *texture;
	uint64_t used;
} LocalArtworkCache;
static LocalArtworkCache g_artwork[LOCAL_ART_CACHE];

static int ends_with_ci(const char *name, const char *suffix);

static void artwork_clear(void) {
	for (int i = 0; i < LOCAL_ART_CACHE; i++) {
		if (g_artwork[i].texture) vita2d_free_texture(g_artwork[i].texture);
		memset(&g_artwork[i], 0, sizeof(g_artwork[i]));
	}
}

static int local_media_leave(int action) {
	vt_video_thumbnail_suspend();
	artwork_clear();
	return action;
}

static vita2d_texture *artwork_get(const char *path) {
	if (!path || !path[0]) return NULL;
	uint64_t now = sceKernelGetProcessTimeWide();
	for (int i = 0; i < LOCAL_ART_CACHE; i++) {
		if (g_artwork[i].texture && strcmp(g_artwork[i].path, path) == 0) {
			g_artwork[i].used = now;
			return g_artwork[i].texture;
		}
	}
	int slot = 0;
	for (int i = 1; i < LOCAL_ART_CACHE; i++)
		if (!g_artwork[i].texture || g_artwork[i].used < g_artwork[slot].used)
			slot = i;
	if (g_artwork[slot].texture) vita2d_free_texture(g_artwork[slot].texture);
	memset(&g_artwork[slot], 0, sizeof(g_artwork[slot]));
	/* A sidecar poster can be JPG/JPEG or PNG.  The old JPEG-only loader made a
	 * perfectly valid cover silently disappear for common media-library naming
	 * conventions. */
	g_artwork[slot].texture = ends_with_ci(path, ".png")
	                       ? vita2d_load_PNG_file(path)
	                       : vita2d_load_JPEG_file(path);
	if (!g_artwork[slot].texture) return NULL;
	snprintf(g_artwork[slot].path, sizeof(g_artwork[slot].path), "%s", path);
	g_artwork[slot].used = now;
	return g_artwork[slot].texture;
}

static void draw_artwork_contain(vita2d_texture *texture, float x, float y,
	                             float width, float height) {
	if (!texture) return;
	float tw = (float)vita2d_texture_get_width(texture);
	float th = (float)vita2d_texture_get_height(texture);
	if (tw <= 0 || th <= 0) return;
	float scale = width / tw < height / th ? width / tw : height / th;
	vita2d_draw_texture_scale(texture,
	                          x + (width - tw * scale) * .5f,
	                          y + (height - th * scale) * .5f,
	                          scale, scale);
}

static void draw_artwork_cover(vita2d_texture *texture, float x, float y,
	                           float width, float height) {
	if (!texture) return;
	float tw = (float)vita2d_texture_get_width(texture);
	float th = (float)vita2d_texture_get_height(texture);
	if (tw <= 0 || th <= 0) return;
	float source_x = 0.0f, source_y = 0.0f;
	float source_w = tw, source_h = th;
	if (tw / th > width / height) {
		source_w = th * width / height;
		source_x = (tw - source_w) * .5f;
	} else {
		source_h = tw * height / width;
		source_y = (th - source_h) * .5f;
	}
	vita2d_draw_texture_part_scale(texture, x, y, source_x, source_y,
	                               source_w, source_h,
	                               width / source_w, height / source_h);
}

static int ends_with_ci(const char *name, const char *suffix) {
	size_t nl = strlen(name), sl = strlen(suffix);
	if (nl < sl) return 0;
	name += nl - sl;
	for (size_t i = 0; i < sl; i++)
		if (tolower((unsigned char)name[i]) !=
		    tolower((unsigned char)suffix[i])) return 0;
	return 1;
}

static VtLocalMediaType media_type(const char *name) {
	static const char *const video[] = { ".mp4", ".m4v", ".mov", ".mkv" };
	static const char *const audio[] = { ".mp3", ".m4a", ".aac", ".wav" };
	for (unsigned i = 0; i < sizeof(video) / sizeof(video[0]); i++)
		if (ends_with_ci(name, video[i])) return VT_LOCAL_MEDIA_VIDEO;
	for (unsigned i = 0; i < sizeof(audio) / sizeof(audio[0]); i++)
		if (ends_with_ci(name, audio[i])) return VT_LOCAL_MEDIA_AUDIO;
	return 0;
}

static int write_record(SceUID fd, const VtLocalMediaItem *item) {
	const unsigned char *data = (const unsigned char *)item;
	size_t done = 0;
	while (done < sizeof(*item)) {
		int n = sceIoWrite(fd, data + done, sizeof(*item) - done);
		if (n <= 0) return n < 0 ? n : -1;
		done += (size_t)n;
	}
	return 0;
}

static void finish_external_item(VtLocalMediaItem *item) {
	if (!item) return;
	const char *slash = strrchr(item->path, '/');
	snprintf(item->name, sizeof(item->name), "%s", slash ? slash + 1 : item->path);
	/* Prefer a title-matched poster, then the conventional folder/cover image.
	 * Libraries commonly use any of these forms, and all are cheap stat calls
	 * made while the indexer is already traversing the directory. */
	static const char *const extensions[] = { ".jpg", ".jpeg", ".png" };
	static const char *const folder_names[] = {
		"poster", "cover", "folder", "thumb", "landscape"
	};
	char base[VT_LOCAL_MEDIA_PATH_MAX];
	snprintf(base, sizeof(base), "%s", item->path);
	char *dot = strrchr(base, '.');
	if (dot) *dot = '\0';
	char directory[VT_LOCAL_MEDIA_PATH_MAX];
	snprintf(directory, sizeof(directory), "%s", item->path);
	char *slash_for_dir = strrchr(directory, '/');
	if (slash_for_dir) *slash_for_dir = '\0';
	item->artwork_path[0] = '\0';
	for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]) &&
	                     !item->artwork_path[0]; i++) {
		char candidate[VT_LOCAL_MEDIA_PATH_MAX];
		SceIoStat stat;
		snprintf(candidate, sizeof(candidate), "%s%s", base, extensions[i]);
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(candidate, &stat) >= 0)
			snprintf(item->artwork_path, sizeof(item->artwork_path), "%s", candidate);
	}
	for (unsigned name = 0; name < sizeof(folder_names) / sizeof(folder_names[0]) &&
	                           !item->artwork_path[0]; name++) {
		for (unsigned ext = 0; ext < sizeof(extensions) / sizeof(extensions[0]); ext++) {
			char candidate[VT_LOCAL_MEDIA_PATH_MAX];
			SceIoStat stat;
			snprintf(candidate, sizeof(candidate), "%s/%s%s", directory,
			         folder_names[name], extensions[ext]);
			memset(&stat, 0, sizeof(stat));
			if (sceIoGetstat(candidate, &stat) >= 0) {
				snprintf(item->artwork_path, sizeof(item->artwork_path), "%s", candidate);
				break;
			}
		}
	}
	if (item->type == VT_LOCAL_MEDIA_AUDIO) {
		VtMusicMetadata metadata;
		if (vt_music_metadata_load(item->path, &metadata) == 0) {
			if (metadata.title[0])
				snprintf(item->name, sizeof(item->name), "%s", metadata.title);
			if (metadata.artist[0])
				snprintf(item->artist, sizeof(item->artist), "%s", metadata.artist);
			if (metadata.album[0])
				snprintf(item->album, sizeof(item->album), "%s", metadata.album);
			if (metadata.duration_ms) item->duration_ms = metadata.duration_ms;
		}
	}
}

static void scan_directory_to_index(const char *directory, int depth,
	                                SceUID video_fd, SceUID audio_fd,
	                                uint32_t *video_count,
	                                uint32_t *audio_count,
	                                uint32_t *total_count) {
	if (*total_count >= LOCAL_MAX_ITEMS || depth > 8) return;
	SceUID dfd = sceIoDopen(directory);
	if (dfd < 0) return;
	SceIoDirent entry;
	while (*total_count < LOCAL_MAX_ITEMS) {
		memset(&entry, 0, sizeof(entry));
		int ret = sceIoDread(dfd, &entry);
		if (ret <= 0) break;
		/* Dot-prefixed files and directories are hidden in both the indexed
		 * Library and direct filesystem browser. */
		if (entry.d_name[0] == '.') continue;
		char path[VT_LOCAL_MEDIA_PATH_MAX];
		int len = snprintf(path, sizeof(path), "%s/%s", directory, entry.d_name);
		if (len <= 0 || len >= (int)sizeof(path)) continue;
		if (SCE_S_ISDIR(entry.d_stat.st_mode)) {
			scan_directory_to_index(path, depth + 1, video_fd, audio_fd,
			                        video_count, audio_count, total_count);
			continue;
		}
		VtLocalMediaType type = media_type(entry.d_name);
		if (!type) continue;
		VtLocalMediaItem item;
		memset(&item, 0, sizeof(item));
		snprintf(item.path, sizeof(item.path), "%s", path);
		item.type = type;
		item.source = VT_LOCAL_MEDIA_SOURCE_FILE;
		item.size = entry.d_stat.st_size > 0 ? (uint64_t)entry.d_stat.st_size : 0;
		finish_external_item(&item);
		SceUID output = type == VT_LOCAL_MEDIA_AUDIO ? audio_fd : video_fd;
		if (write_record(output, &item) == 0) {
			if (type == VT_LOCAL_MEDIA_AUDIO)
				(*audio_count)++;
			else
				(*video_count)++;
			(*total_count)++;
		}
	}
	sceIoDclose(dfd);
}

static int copy_file_records(SceUID out_fd, const char *path) {
	SceUID in_fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (in_fd < 0) return in_fd;
	unsigned char buffer[8192];
	int ret = 0;
	for (;;) {
		int n = sceIoRead(in_fd, buffer, sizeof(buffer));
		if (n < 0) { ret = n; break; }
		if (n == 0) break;
		int done = 0;
		while (done < n) {
			int written = sceIoWrite(out_fd, buffer + done, n - done);
			if (written <= 0) { ret = written < 0 ? written : -1; break; }
			done += written;
		}
		if (ret < 0) break;
	}
	sceIoClose(in_fd);
	return ret;
}

static int scan_media(LocalMediaIndexHeader *result_index) {
	if (!result_index) return -1;
	LocalMediaIndexHeader index;
	memset(&index, 0, sizeof(index));
	sceIoMkdir("ux0:data/VitaMediaDeck", 0777);
	sceIoRemove(LOCAL_INDEX_TEMP);
	sceIoRemove(LOCAL_GROUP_VIDEO_TEMP);
	sceIoRemove(LOCAL_GROUP_AUDIO_TEMP);
	SceUID final_fd = sceIoOpen(LOCAL_INDEX_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	SceUID video_fd = sceIoOpen(LOCAL_GROUP_VIDEO_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	SceUID audio_fd = sceIoOpen(LOCAL_GROUP_AUDIO_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (final_fd < 0 || video_fd < 0 || audio_fd < 0) {
		if (final_fd >= 0) sceIoClose(final_fd);
		if (video_fd >= 0) sceIoClose(video_fd);
		if (audio_fd >= 0) sceIoClose(audio_fd);
		return -1;
	}
	memcpy(index.magic, "VTLOCAL4", 8);
	index.version = 4;
	index.record_size = sizeof(VtLocalMediaItem);
	/* Reserve the header, then append local video and local audio groups. */
	sceIoWrite(final_fd, &index, sizeof(index));
	scan_directory_to_index("ux0:video", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:video", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	scan_directory_to_index("ux0:movies", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:movies", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	scan_directory_to_index("ux0:music", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:music", 0, video_fd, audio_fd,
	                        &index.video_count, &index.audio_count,
	                        &index.total_count);
	sceIoSyncByFd(video_fd, 0); sceIoClose(video_fd);
	sceIoSyncByFd(audio_fd, 0); sceIoClose(audio_fd);
	int ret = copy_file_records(final_fd, LOCAL_GROUP_VIDEO_TEMP);
	if (ret == 0)
		ret = copy_file_records(final_fd, LOCAL_GROUP_AUDIO_TEMP);
	if (ret == 0) {
		sceIoPwrite(final_fd, &index, sizeof(index), 0);
		ret = sceIoSyncByFd(final_fd, 0);
	}
	sceIoClose(final_fd);
	sceIoRemove(LOCAL_GROUP_VIDEO_TEMP);
	sceIoRemove(LOCAL_GROUP_AUDIO_TEMP);
	if (ret < 0) sceIoRemove(LOCAL_INDEX_TEMP);
	log_printf("local media indexed: total=%u video=%u audio=%u",
	           index.total_count, index.video_count, index.audio_count);
	if (ret == 0) *result_index = index;
	return ret;
}

static int scan_media_task(void *ctx) {
	return scan_media((LocalMediaIndexHeader *)ctx);
}

static int local_index_load(LocalMediaIndexHeader *out) {
	if (!out) return -1;
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	if (sceIoGetstat(LOCAL_INDEX_PATH, &stat) < 0) return -1;
	SceUID fd = sceIoOpen(LOCAL_INDEX_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	LocalMediaIndexHeader header;
	int bytes = sceIoRead(fd, &header, sizeof(header));
	sceIoClose(fd);
	uint64_t grouped = (uint64_t)header.video_count + header.audio_count;
	uint64_t expected = sizeof(header) +
	                    (uint64_t)header.total_count * sizeof(VtLocalMediaItem);
	if (bytes != (int)sizeof(header) || memcmp(header.magic, "VTLOCAL4", 8) ||
	    header.version != 4 || header.record_size != sizeof(VtLocalMediaItem) ||
	    header.total_count > LOCAL_MAX_ITEMS || grouped != header.total_count ||
	    expected != (uint64_t)stat.st_size) return -1;
	*out = header;
	return 0;
}

static int local_index_publish(const LocalMediaIndexHeader *index) {
	if (!index) return -1;
	sceIoRemove(LOCAL_INDEX_BACKUP);
	SceIoStat old;
	memset(&old, 0, sizeof(old));
	int had_old = sceIoGetstat(LOCAL_INDEX_PATH, &old) >= 0;
	if (had_old && sceIoRename(LOCAL_INDEX_PATH, LOCAL_INDEX_BACKUP) < 0)
		return -1;
	int ret = sceIoRename(LOCAL_INDEX_TEMP, LOCAL_INDEX_PATH);
	if (ret < 0) {
		if (had_old) sceIoRename(LOCAL_INDEX_BACKUP, LOCAL_INDEX_PATH);
		return ret;
	}
	sceIoRemove(LOCAL_INDEX_BACKUP);
	__sync_synchronize();
	g_index = *index;
	g_page_filter = g_page_start = -1;
	g_page_count = 0;
	return 0;
}

static int local_scan_thread(SceSize args, void *argp) {
	(void)args;
	LocalMediaScanJob *job = *(LocalMediaScanJob **)argp;
	job->result = scan_media(&job->index);
	__sync_synchronize();
	job->done = 1;
	return sceKernelExitThread(0);
}

static int local_scan_start(void) {
	if (g_scan_job.thid >= 0) return 0;
	memset(&g_scan_job, 0, sizeof(g_scan_job));
	g_scan_job.self = &g_scan_job;
	g_scan_job.thid = sceKernelCreateThread(
	    "VitaMediaDeckMediaScan", local_scan_thread, 0x10000100,
	    LOCAL_SCAN_THREAD_STACK, 0, 0, NULL);
	if (g_scan_job.thid < 0) return g_scan_job.thid;
	int ret = sceKernelStartThread(g_scan_job.thid, sizeof(g_scan_job.self),
	                               &g_scan_job.self);
	if (ret < 0) {
		sceKernelDeleteThread(g_scan_job.thid);
		g_scan_job.thid = -1;
	}
	return ret;
}

static int local_scan_finish(void) {
	if (g_scan_job.thid < 0 || !g_scan_job.done) return 0;
	__sync_synchronize();
	sceKernelWaitThreadEnd(g_scan_job.thid, NULL, NULL);
	sceKernelDeleteThread(g_scan_job.thid);
	g_scan_job.thid = -1;
	int ret = g_scan_job.result == 0
	        ? local_index_publish(&g_scan_job.index) : g_scan_job.result;
	if (ret < 0) sceIoRemove(LOCAL_INDEX_TEMP);
	return ret == 0 ? 1 : -1;
}

static int filtered_count(int filter) {
	if (filter == 1) return (int)g_index.video_count;
	if (filter == 2 || filter == 3) return (int)g_index.audio_count;
	return (int)g_index.total_count;
}

static uint32_t filter_first_record(int filter) {
	if (filter == 2 || filter == 3) return g_index.video_count;
	return 0;
}

static int load_page(int filter, int start) {
	int count = filtered_count(filter);
	if (start < 0 || start >= count) return -1;
	if (start + LOCAL_PAGE_ITEMS > count) start = count > LOCAL_PAGE_ITEMS
	                                              ? count - LOCAL_PAGE_ITEMS : 0;
	SceUID fd = sceIoOpen(LOCAL_INDEX_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	int wanted = count - start;
	if (wanted > LOCAL_PAGE_ITEMS) wanted = LOCAL_PAGE_ITEMS;
	SceOff offset = sizeof(g_index) +
	                (SceOff)(filter_first_record(filter) + (uint32_t)start) *
	                (SceOff)sizeof(VtLocalMediaItem);
	int bytes = sceIoPread(fd, g_items, wanted * sizeof(g_items[0]), offset);
	sceIoClose(fd);
	if (bytes != wanted * (int)sizeof(g_items[0])) return -1;
	g_page_filter = filter;
	g_page_start = start;
	g_page_count = wanted;
	return 0;
}

static int filtered_index(int filter, int position) {
	if (position < 0 || position >= filtered_count(filter)) return -1;
	if (g_page_filter != filter || position < g_page_start ||
	    position >= g_page_start + g_page_count) {
		/* Recycler window: retain two grid rows behind the requested record and
		 * use the rest for forward prefetch. Unlike fixed pages this also keeps a
		 * viewport that straddles an old boundary inside one disk read. */
		int start = position - 6;
		if (start < 0) start = 0;
		if (load_page(filter, start) < 0) return -1;
	}
	return position - g_page_start;
}

static int screen_count(int filter) {
	return filtered_count(filter);
}

static int screen_local_index(int filter, int position) {
	return filtered_index(filter, position);
}

int ui_local_media_next_audio(const char *current_path, int direction,
	                          int random, VtLocalMediaItem *out) {
	if (!out) return -1;
	/* Internal filter 3 aliases the complete local audio group. */
	int count = filtered_count(3), current = -1;
	if (count <= 0) return -1;
	for (int i = 0; current_path && i < count; i++) {
		int slot = filtered_index(3, i);
		if (slot >= 0 && strcmp(g_items[slot].path, current_path) == 0) {
			current = i;
			break;
		}
	}
	int next;
	if (random && count > 1) {
		next = (int)((sceKernelGetProcessTimeWide() >> 10) % (uint64_t)count);
		if (next == current) next = (next + 1) % count;
	} else {
		if (current < 0) current = direction < 0 ? 0 : count - 1;
		next = (current + (direction < 0 ? -1 : 1) + count) % count;
	}
	int slot = filtered_index(3, next);
	if (slot < 0) return -1;
	*out = g_items[slot];
	return 0;
}

static void clip_text(vita2d_font *font, unsigned int size, const char *text,
	                  char out[128], int width) {
	ui_font_fit_text(font, size, text ? text : "", out, 128, width);
}

static void media_format_label(const VtLocalMediaItem *item, char out[8]) {
	out[0] = '\0';
	if (!item) return;
	const char *slash = strrchr(item->path, '/');
	const char *dot = strrchr(slash ? slash + 1 : item->path, '.');
	if (dot && dot[1]) {
		size_t length = strlen(dot + 1);
		if (length > 6) length = 6;
		for (size_t i = 0; i < length; i++)
			out[i] = (char)toupper((unsigned char)dot[i + 1]);
		out[length] = '\0';
	}
	if (!out[0]) snprintf(out, 8, "%s",
	                      item->type == VT_LOCAL_MEDIA_AUDIO ? "AUDIO" : "VIDEO");
}

static const char *media_origin_label(const VtLocalMediaItem *item) {
	(void)item;
	return vt_i18n_str(VT_STR_LOCAL_MEDIA_ORIGIN_LOCAL);
}

static int draw_media_badge(vita2d_font *font, const char *label,
	                        int x, int y, unsigned int accent) {
	if (!font || !label || !label[0]) return 0;
	int width = ui_font_text_width(font, UI_FONT_SMALL, label) + 17;
	vita2d_draw_rectangle(x, y, width, 24, RGBA8(9, 8, 7, 226));
	vita2d_draw_rectangle(x, y, 3, 24, accent);
	ui_font_draw_text(font, x + 9, y + 18, VT_THEME_TEXT,
	                  UI_FONT_SMALL, label);
	return width;
}

static void format_card_duration(uint64_t milliseconds, char out[24]) {
	uint64_t seconds = milliseconds / 1000ULL;
	if (seconds >= 3600ULL)
		snprintf(out, 24, "%llu:%02llu:%02llu",
		         (unsigned long long)(seconds / 3600ULL),
		         (unsigned long long)((seconds / 60ULL) % 60ULL),
		         (unsigned long long)(seconds % 60ULL));
	else
		snprintf(out, 24, "%llu:%02llu",
		         (unsigned long long)(seconds / 60ULL),
		         (unsigned long long)(seconds % 60ULL));
}

static void tick_right_drawer(int open, int cursor, float *animation,
	                          float *focus, uint64_t *last_tick_us) {
	if (!animation || !focus || !last_tick_us) return;
	float animation_target = open ? 1.0f : 0.0f;
	float focus_target = (float)cursor;
	uint64_t now = sceKernelGetProcessTimeWide();
	uint64_t elapsed = *last_tick_us && now > *last_tick_us
	                 ? now - *last_tick_us : 16667ULL;
	if (elapsed > 50000ULL) elapsed = 50000ULL;
	*last_tick_us = now;
	if (vt_preferences_reduce_motion()) {
		*animation = animation_target;
		*focus = focus_target;
	} else {
		float animation_alpha = (float)elapsed /
		                        (60000.0f + (float)elapsed);
		float focus_alpha = (float)elapsed / (50000.0f + (float)elapsed);
		*animation += (animation_target - *animation) * animation_alpha;
		*focus += (focus_target - *focus) * focus_alpha;
	}
	if (!open && *animation < 0.025f) *animation = 0.0f;
}

static void draw_screen(int filter, int selected, int top, int grid_mode,
	                    int right_open, int right_cursor, float right_animation,
	                    float right_focus, int focus_tabs,
	                    int delete_confirm,
	                    const UiFocusMotion *focus_motion,
	                    const UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	ui_mini_player_pump();
	vt_video_thumbnail_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header(NULL);
	int page_has_focus = !ui_mini_player_input_locked() && !delete_confirm &&
	                     right_animation <= 0.01f &&
	                     (!sidebar || (!sidebar->open &&
	                                  sidebar->animation <= 0.01f));
	int content_has_focus = page_has_focus && !focus_tabs;
	char summary[64];
	snprintf(summary, sizeof(summary), vt_i18n_str(VT_STR_LOCAL_MEDIA_SUMMARY),
	         g_index.video_count, g_index.audio_count);
	ui_scene_identity(42, 68, 338, "LIB/01",
	                  vt_i18n_str(VT_STR_HOME_TITLE), summary);
	const char *tabs[3] = {
		vt_i18n_str(VT_STR_LOCAL_MEDIA_ALL),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_VIDEO),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_AUDIO)
	};
	if (page_has_focus && focus_tabs && focus_motion)
		ui_focus_glow_draw(focus_motion->x, focus_motion->y,
		                   focus_motion->width, focus_motion->height,
		                   sceKernelGetProcessTimeWide(), 64, 112);
	for (int i = 0; i < 3; i++) {
		int x = 416 + i * 166;
		ui_panel(x, 68, 154, 38,
		         filter == i ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE,
		         filter == i ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER_DIM, 0);
		if (filter == i)
			vita2d_draw_rectangle(x + 12, 103, 130, 2, VT_THEME_SIGNAL);
		if (small) {
			char tab_text[128];
			clip_text(small, UI_FONT_SMALL, tabs[i], tab_text, 138);
			ui_font_draw_text(small, x + 8, 94,
			                  filter == i ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, tab_text);
		}
	}
	int count = screen_count(filter);
	int viewport_bottom = local_viewport_bottom();
	if (count == 0) {
		ui_panel(208, 202, 544, 140, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 244, 254, VT_THEME_TEXT,
		                               UI_FONT_BODY,
		                               vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY));
		if (small) ui_font_draw_text(small, 244, 288, VT_THEME_TEXT_MUTED,
		                                UI_FONT_SMALL,
		                                vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY_DETAIL));
	} else if (!grid_mode) {
		if (content_has_focus)
			ui_focus_glow_draw(focus_motion ? focus_motion->x : LIST_X,
			                   focus_motion ? focus_motion->y : LIST_Y + (selected - top) * ROW_H,
			                   focus_motion ? focus_motion->width : LIST_W,
			                   focus_motion ? focus_motion->height : ROW_H - 6,
			                   sceKernelGetProcessTimeWide(), LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		for (int pos = top; pos < count &&
		                      pos < top + local_list_render_rows(); pos++) {
			int row_y = LIST_Y + (pos - top) * ROW_H;
			int index = screen_local_index(filter, pos);
			if (index < 0) continue;
			const VtLocalMediaItem *item = &g_items[index];
			ui_panel(LIST_X, row_y, LIST_W, ROW_H - 6,
			         VT_THEME_SURFACE,
			         item->type == VT_LOCAL_MEDIA_VIDEO
			             ? VT_THEME_BLUE_BRIGHT : VT_THEME_BLUE_LIGHT,
			         0);
			vita2d_texture *art = artwork_get(item->artwork_path);
			if (!art && item->type == VT_LOCAL_MEDIA_VIDEO)
				art = vt_video_thumbnail_get(item->path, item->size);
			if (art) draw_artwork_contain(art, LIST_X + 10, row_y + 4, 38, 38);
			if (body) {
				char clipped[128];
				clip_text(body, UI_FONT_BODY, item->name, clipped, 520);
				ui_font_draw_text(body, LIST_X + (art ? 58 : 24), row_y + 31, VT_THEME_TEXT,
				                  UI_FONT_BODY, clipped);
			}
			if (small) {
				char format[8];
				media_format_label(item, format);
				int badge_x = 650;
				badge_x += draw_media_badge(small, media_origin_label(item),
				                            badge_x, row_y + 11,
				                            item->source == VT_LOCAL_MEDIA_SOURCE_FILE
				                                ? VT_THEME_TEXT_FAINT : VT_THEME_BLUE_BRIGHT) + 6;
				draw_media_badge(small, format, badge_x, row_y + 11,
				                 item->type == VT_LOCAL_MEDIA_AUDIO
				                     ? VT_THEME_BLUE_LIGHT : VT_THEME_BLUE_BRIGHT);
				unsigned long long mb10 = item->size * 10ULL / (1024ULL * 1024ULL);
				char size_text[32];
				snprintf(size_text, sizeof(size_text), "%llu.%llu MB",
				         mb10 / 10ULL, mb10 % 10ULL);
				int size_width = ui_font_text_width(small, UI_FONT_SMALL, size_text);
				ui_font_draw_text(small, LIST_X + LIST_W - 12 - size_width,
				                  row_y + 30, VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, size_text);
			}
		}
		vita2d_disable_clipping();
	} else {
		const int cols = GRID_COLS;
		int first_row = top;
		int viewport_bottom = local_viewport_bottom();
		if (content_has_focus)
			ui_focus_glow_draw(focus_motion ? focus_motion->x : LIST_X - 2,
			                   focus_motion ? focus_motion->y : LIST_Y - 2,
			                   focus_motion ? focus_motion->width : GRID_CARD_W + 4,
			                   focus_motion ? focus_motion->height : GRID_CARD_H + 4,
			                   sceKernelGetProcessTimeWide(), LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		int visible_grid_rows = local_grid_render_rows();
		for (int pos = first_row * cols; pos < count &&
		                      pos < (first_row + visible_grid_rows) * cols; pos++) {
			int index = filtered_index(filter, pos);
			if (index < 0) continue;
			int col = pos % cols, row = pos / cols - first_row;
			int x = LIST_X + col * (GRID_CARD_W + GRID_GAP_X);
			int y = LIST_Y + row * (GRID_CARD_H + GRID_GAP_Y);
			ui_panel(x, y, GRID_CARD_W, GRID_CARD_H,
			         VT_THEME_SURFACE,
			         g_items[index].type == VT_LOCAL_MEDIA_AUDIO
			             ? VT_THEME_BLUE_LIGHT : VT_THEME_BLUE_BRIGHT,
			         0);
			const VtLocalMediaItem *item = &g_items[index];
			vita2d_texture *art = artwork_get(item->artwork_path);
			if (!art && item->type == VT_LOCAL_MEDIA_VIDEO)
				art = vt_video_thumbnail_get(item->path, item->size);
			vita2d_draw_rectangle(x + 6, y + 6, GRID_CARD_W - 12, GRID_THUMB_H,
			                      VT_THEME_MEDIA_BACKDROP);
			if (art && item->type == VT_LOCAL_MEDIA_VIDEO)
				draw_artwork_cover(art, x + 6, y + 6, GRID_CARD_W - 12, GRID_THUMB_H);
			else if (art)
				draw_artwork_contain(art, x + 6, y + 6, GRID_CARD_W - 12, GRID_THUMB_H);
			else if (item->type == VT_LOCAL_MEDIA_AUDIO) {
				vita2d_draw_fill_circle(x + GRID_CARD_W * .5f, y + 68.0f, 38.0f,
				                        VT_THEME_SURFACE_RAISED);
				vita2d_draw_fill_circle(x + GRID_CARD_W * .5f, y + 68.0f, 12.0f,
				                        VT_THEME_BLUE_LIGHT);
			} else {
				for (int stripe = 0; stripe < 6; stripe++)
					vita2d_draw_rectangle(x + 6 + stripe * 48, y + 6, 24,
					                      GRID_THUMB_H, stripe & 1
						                          ? RGBA8(47, 34, 22, 255)
						                          : RGBA8(18, 16, 14, 255));
				vita2d_draw_fill_circle(x + GRID_CARD_W * .5f, y + 68.0f, 31.0f,
				                        RGBA8(5, 5, 6, 220));
				for (int line = 0; line < 26; line++)
					vita2d_draw_rectangle(x + GRID_CARD_W * .5f - 8,
					                      y + 55 + line, 10 + line / 2, 1,
					                      VT_THEME_TEXT);
			}
			if (small) {
				char format[8];
				media_format_label(item, format);
				int badge_x = x + 12;
				badge_x += draw_media_badge(small, media_origin_label(item),
				                            badge_x, y + 12,
				                            item->source == VT_LOCAL_MEDIA_SOURCE_FILE
				                                ? VT_THEME_TEXT_FAINT : VT_THEME_BLUE_BRIGHT) + 6;
				draw_media_badge(small, format, badge_x, y + 12,
				                 item->type == VT_LOCAL_MEDIA_AUDIO
				                     ? VT_THEME_BLUE_LIGHT : VT_THEME_BLUE_BRIGHT);
				if (item->duration_ms) {
					char duration[24];
					format_card_duration(item->duration_ms, duration);
					int duration_w = ui_font_text_width(small, UI_FONT_SMALL, duration) + 12;
					vita2d_draw_rectangle(x + GRID_CARD_W - duration_w - 12,
					                      y + GRID_THUMB_H - 18, duration_w, 24,
					                      RGBA8(9, 8, 7, 226));
					ui_font_draw_text(small, x + GRID_CARD_W - duration_w - 6,
					                  y + GRID_THUMB_H, VT_THEME_TEXT,
					                  UI_FONT_SMALL, duration);
				}
				char title[128];
				clip_text(small, UI_FONT_SMALL, item->name, title, GRID_CARD_W - 26);
				ui_font_draw_text(small, x + 13, y + 174, VT_THEME_TEXT,
				                  UI_FONT_SMALL, title);
				char detail[128];
				unsigned long long mb10 = item->size * 10ULL / (1024ULL * 1024ULL);
				char size_text[32];
				snprintf(size_text, sizeof(size_text), "%llu.%llu MB",
				         mb10 / 10ULL, mb10 % 10ULL);
				const char *metadata = item->type == VT_LOCAL_MEDIA_AUDIO && item->artist[0]
				                     ? item->artist : format;
				snprintf(detail, sizeof(detail),
				         vt_i18n_str(VT_STR_LOCAL_MEDIA_CARD_DETAILS), metadata, size_text);
				char clipped_detail[128];
				clip_text(small, UI_FONT_SMALL, detail, clipped_detail, GRID_CARD_W - 26);
				ui_font_draw_text(small, x + 13, y + 195, VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, clipped_detail);
			}
		}
		vita2d_disable_clipping();
	}
	/* Draw persistent playback first so modal drawers and confirmations remain
	 * true full-height layers above it. */
	ui_mini_player_draw();
	if (right_animation > 0.01f) {
		/* Modal drawers cover the complete display edge, including the brand bar.
		 * This prevents the panel from reading as a detached card. */
		float panel_x = 960.0f - 340.0f * right_animation;
		vita2d_draw_rectangle(panel_x, 0, 340, 544, RGBA8(7, 6, 5, 250));
		vita2d_draw_rectangle(panel_x, 0, 4, 544, VT_THEME_BLUE_BRIGHT);
		if (body) ui_font_draw_text(body, (int)panel_x + 30, 92, VT_THEME_TEXT,
		                            UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_LOCAL_MEDIA_VIEW_TITLE));
		const char *actions[4] = {
			vt_i18n_str(grid_mode ? VT_STR_LOCAL_MEDIA_VIEW_LIST
			                             : VT_STR_LOCAL_MEDIA_VIEW_GRID),
			vt_i18n_str(VT_STR_LOCAL_MEDIA_BROWSE_FILES),
			vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_RENAME),
			vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_DELETE)
		};
		if (right_open && !delete_confirm) {
			float marker_y = 124.0f + right_focus * 62.0f;
			ui_panel(panel_x + 18, marker_y, 298, 52,
			         VT_THEME_SURFACE_FOCUS, VT_THEME_BLUE_LIGHT, 0);
		}
		for (int i = 0; i < 4; i++) {
			int ay = 124 + i * 62;
			ui_action_button((int)panel_x + 22, ay, 294, 52,
			                 i == 3 ? VT_THEME_DANGER : VT_THEME_SURFACE,
			                 i == 0 ? "Left/Right" : "Cross", actions[i],
			                 0);
		}
	}
	if (delete_confirm) {
		vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
		                      544 - UI_BRAND_HEADER_HEIGHT, RGBA8(0, 3, 7, 186));
		ui_panel(208, 174, 544, 190, RGBA8(9, 8, 7, 255),
		         VT_THEME_DANGER, 0);
		if (body) ui_font_draw_text(body, 244, 224, VT_THEME_TEXT, UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_LOCAL_MEDIA_DELETE_TITLE));
		if (small) ui_font_draw_text(small, 244, 258, VT_THEME_TEXT_MUTED,
		                              UI_FONT_SMALL,
		                              vt_i18n_str(VT_STR_LOCAL_MEDIA_DELETE_DETAIL));
		ui_action_button(236, 294, 226, 48, VT_THEME_SURFACE,
		                 "Circle", vt_i18n_str(VT_STR_LOCAL_MEDIA_CANCEL), 0);
		ui_action_button(480, 294, 244, 48, VT_THEME_DANGER,
		                 "Cross", vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_DELETE), 1);
	}
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->open ? sidebar->focus_cursor : -1.0f);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_local_media_screen(VtLocalMediaItem *selected_out) {
	vt_video_thumbnail_resume();
	int refreshed = local_scan_finish();
	LocalMediaIndexHeader cached;
	if (local_index_load(&cached) == 0) {
		g_index = cached;
		g_page_filter = g_page_start = -1;
		g_page_count = 0;
		/* The previous catalog is immediately usable. Refresh a temporary index
		 * in the background and publish it only when complete. */
		if (refreshed == 0) local_scan_start();
	} else if (g_scan_job.thid < 0) {
		LocalMediaIndexHeader initial;
		memset(&initial, 0, sizeof(initial));
		int ret = ui_loading_run(vt_i18n_str(VT_STR_LOCAL_MEDIA_TITLE),
		                         scan_media_task, &initial, NULL, NULL, NULL);
		if (ret == 0) local_index_publish(&initial);
	}
	int filter = 0, selected = 0, top = 0;
	int library_grid = 1;
	int right_open = 0, right_cursor = 0, focus_tabs = 0, delete_confirm = 0;
	float right_animation = 0.0f, right_focus = 0.0f;
	uint64_t right_motion_tick_us = 0;
	UiFocusMotion focus_motion;
	ui_focus_motion_reset(&focus_motion);
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat page_repeat, right_repeat;
	ui_nav_repeat_reset(&page_repeat);
	ui_nav_repeat_reset(&right_repeat);
	int right_horizontal_latch = 0;
	for (;;) {
		if (local_scan_finish() > 0) {
			int refreshed_count = screen_count(filter);
			if (selected >= refreshed_count)
				selected = refreshed_count > 0 ? refreshed_count - 1 : 0;
			if (top > selected) top = selected;
		}
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch; unsigned flags = ui_touch_poll(&touch);
		int grid_mode = filter == 0 ? library_grid
		              : (filter == 2 ? vt_preferences_local_music_grid()
		                             : vt_preferences_local_video_grid());
		if (delete_confirm) {
			if ((flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 236, 294, 226, 48))
				pressed |= SCE_CTRL_CIRCLE;
			if ((flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 480, 294, 244, 48))
				pressed |= SCE_CTRL_CROSS;
			if (pressed & SCE_CTRL_CROSS) {
				int index = screen_local_index(filter, selected);
				if (index >= 0 && selected_out) *selected_out = g_items[index];
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_DELETE);
			}
			if (pressed & SCE_CTRL_CIRCLE) delete_confirm = 0;
			ui_sections_sidebar_tick(&sidebar);
			tick_right_drawer(right_open, right_cursor, &right_animation,
			                  &right_focus, &right_motion_tick_us);
			draw_screen(filter, selected, top, grid_mode, right_open, right_cursor,
			            right_animation, right_focus, focus_tabs, delete_confirm,
			            &focus_motion, &sidebar);
			continue;
		}
		ui_mini_player_handle_buttons(&pressed);
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			flags = UI_TOUCH_EVENT_NONE;
			ctrl.buttons &= SCE_CTRL_SELECT;
			ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
		}
		int right_layer_was_visible = right_open || right_animation > 0.01f;
		int was_open = sidebar.open;
		int section = UI_SECTION_NONE;
		if (!right_layer_was_visible)
			section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
			                                             ctrl.buttons, ctrl.ly);
		else
			pressed &= ~SCE_CTRL_LTRIGGER;
		int sidebar_owned_frame = sidebar.open || was_open;
		if (sidebar_owned_frame) {
			int touched = ui_sections_sidebar_handle_touch(&sidebar, flags,
			                                                touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			flags = 0;
		} else if (!right_layer_was_visible && sidebar.animation <= 0.01f &&
		           ui_mini_player_handle_touch(flags, &touch)) flags = 0;
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			return local_media_leave(UI_LOCAL_MEDIA_ACTION_SECTION_BASE + section);
		}
		if (!sidebar_owned_frame && sidebar.animation <= 0.01f &&
		    (pressed & SCE_CTRL_RTRIGGER)) {
			right_open = !right_open;
			if (right_open) right_cursor = 0;
			ui_nav_repeat_reset(&right_repeat);
			right_horizontal_latch = 0;
			pressed &= ~SCE_CTRL_RTRIGGER;
		}
		if (right_open) {
			float panel_x = 960.0f - 340.0f * right_animation;
			if (flags & UI_TOUCH_EVENT_TAP) {
				if (ui_touch_hit_rect(touch.x, touch.y, (int)panel_x + 22,
				                      124, 294, 52)) {
					right_cursor = 0;
					pressed |= SCE_CTRL_CROSS;
				} else if (ui_touch_hit_rect(touch.x, touch.y, (int)panel_x + 22,
				                             186, 294, 52)) {
					right_cursor = 1;
					pressed |= SCE_CTRL_CROSS;
				} else if (ui_touch_hit_rect(touch.x, touch.y, (int)panel_x + 22,
			                             248, 294, 52)) {
					right_cursor = 2;
					pressed |= SCE_CTRL_CROSS;
				} else if (ui_touch_hit_rect(touch.x, touch.y, (int)panel_x + 22,
				                             310, 294, 52)) {
					right_cursor = 3;
					pressed |= SCE_CTRL_CROSS;
				} else if (touch.x < panel_x) {
					pressed |= SCE_CTRL_CIRCLE;
				}
			}
			unsigned int drawer_nav = ui_nav_repeat_update(
			    &right_repeat, pressed, ctrl.buttons, ctrl.lx, ctrl.ly,
			    SCE_CTRL_UP | SCE_CTRL_DOWN);
			unsigned int horizontal_step =
			    pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
			int analog_horizontal = ctrl.lx < 64 ? -1 : ctrl.lx > 191 ? 1 : 0;
			if (!analog_horizontal) right_horizontal_latch = 0;
			else if (analog_horizontal != right_horizontal_latch) {
				right_horizontal_latch = analog_horizontal;
				horizontal_step = analog_horizontal < 0
				                ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT;
			}
			if ((drawer_nav & SCE_CTRL_UP) && right_cursor > 0) right_cursor--;
			if ((drawer_nav & SCE_CTRL_DOWN) && right_cursor < 3) right_cursor++;
			if (right_cursor == 0 &&
			    (horizontal_step ||
			     (pressed & SCE_CTRL_CROSS))) {
				grid_mode = !grid_mode;
				if (filter == 0) library_grid = grid_mode;
				else if (filter == 2) vt_preferences_set_local_music_grid(grid_mode);
				else vt_preferences_set_local_video_grid(grid_mode);
				selected = top = 0;
			}
			if ((pressed & SCE_CTRL_CROSS) && right_cursor == 1) {
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_BROWSE_FILES);
			}
			if ((pressed & SCE_CTRL_CROSS) && right_cursor == 2) {
				int index = screen_local_index(filter, selected);
				if (index >= 0) {
					if (selected_out) *selected_out = g_items[index];
					return local_media_leave(UI_LOCAL_MEDIA_ACTION_RENAME);
				}
			}
			if ((pressed & SCE_CTRL_CROSS) && right_cursor == 3)
				delete_confirm = 1;
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
			/* The drawer is modal: never let a tap fall through to a poster. */
			flags = 0;
		}
		if (!right_open) right_horizontal_latch = 0;
		tick_right_drawer(right_open, right_cursor, &right_animation,
		                  &right_focus, &right_motion_tick_us);
		if ((!sidebar.open && sidebar.animation > 0.01f) ||
		    (!right_open && right_animation > 0.01f)) {
			ui_touch_reset();
			flags = UI_TOUCH_EVENT_NONE;
		}
		int overlay_owns_input = sidebar_owned_frame || sidebar.open ||
		                         sidebar.animation > 0.01f || right_open ||
		                         right_animation > 0.01f;
		if (overlay_owns_input) {
			pressed = 0;
			flags = UI_TOUCH_EVENT_NONE;
			ui_nav_repeat_reset(&page_repeat);
		}
		int old_filter = filter;
		unsigned int nav = ui_nav_repeat_update(
		    &page_repeat, pressed, overlay_owns_input ? 0 : ctrl.buttons,
		    overlay_owns_input ? 128 : ctrl.lx,
		    overlay_owns_input ? 128 : ctrl.ly,
		    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
		if (focus_tabs && (nav & SCE_CTRL_LEFT))
			filter = filter > 0 ? filter - 1 : 2;
		if (focus_tabs && (nav & SCE_CTRL_RIGHT))
			filter = filter < 2 ? filter + 1 : 0;
		if (old_filter != filter) {
			selected = top = 0;
			/* The destination tab may remember a different layout. Recompute it
			 * in the same frame to avoid drawing or hit-testing one stale list/grid
			 * frame after a tab switch. */
			grid_mode = filter == 0 ? library_grid
			          : (filter == 2 ? vt_preferences_local_music_grid()
			                         : vt_preferences_local_video_grid());
		}
		int count = screen_count(filter);
		/* An empty result set has no page item that can own focus. Keep the
		 * selector on the filters instead of leaving an invisible focus target. */
		if (count == 0) focus_tabs = 1;
		if (!focus_tabs && (nav & SCE_CTRL_UP) &&
		    (selected < (grid_mode ? 3 : 1))) {
			focus_tabs = 1;
			nav &= ~SCE_CTRL_UP;
		}
		if (count > 0 && focus_tabs && (nav & SCE_CTRL_DOWN)) {
			focus_tabs = 0;
			nav &= ~SCE_CTRL_DOWN;
		}
		if (!focus_tabs && grid_mode) {
			if ((nav & SCE_CTRL_LEFT) && selected > 0) selected--;
			if ((nav & SCE_CTRL_RIGHT) && selected + 1 < count) selected++;
			if ((nav & SCE_CTRL_UP) && selected >= 3) selected -= 3;
			if ((nav & SCE_CTRL_DOWN) && selected + 3 < count) selected += 3;
		} else if (!focus_tabs) {
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
		}
		if (!grid_mode) {
			int visible = local_list_visible_rows();
			if (selected < top) top = selected;
			if (selected >= top + visible) top = selected - visible + 1;
		} else {
			top = grid_top_for_selection(selected);
		}
		/* Animate the selector itself instead of delaying input: the new item is
		 * immediately actionable while the signal rim glides across the grid. */
		if (focus_tabs) {
			int tab_x = 416 + filter * 166;
			ui_focus_motion_tick(&focus_motion, tab_x - 3, 65, 160, 44);
		} else if (grid_mode) {
			int row = selected / GRID_COLS - top;
			int column = selected % GRID_COLS;
			ui_focus_motion_tick(&focus_motion,
			                     LIST_X + column * (GRID_CARD_W + GRID_GAP_X) - 2,
			                     LIST_Y + row * (GRID_CARD_H + GRID_GAP_Y) - 2,
			                     GRID_CARD_W + 4, GRID_CARD_H + 4);
		} else {
			ui_focus_motion_tick(&focus_motion, LIST_X,
			                     LIST_Y + (selected - top) * ROW_H,
			                     LIST_W, ROW_H - 6);
		}
		if (!focus_tabs && (pressed & SCE_CTRL_CROSS) && count > 0) {
			int index = screen_local_index(filter, selected);
			if (index >= 0) {
				if (selected_out) *selected_out = g_items[index];
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_PLAY);
			}
		}
		if (!focus_tabs && (pressed & SCE_CTRL_TRIANGLE) && count > 0) {
			int index = screen_local_index(filter, selected);
			if (index >= 0) {
				if (selected_out) *selected_out = g_items[index];
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_RENAME);
			}
		}
		if (!focus_tabs && (pressed & SCE_CTRL_SQUARE) && count > 0)
			delete_confirm = 1;
		if (pressed & SCE_CTRL_CIRCLE)
			return local_media_leave(UI_LOCAL_MEDIA_ACTION_BACK);
		if ((flags & UI_TOUCH_EVENT_UP) && !(flags & UI_TOUCH_EVENT_TAP) &&
		    !focus_tabs && count > 0) {
			int dy = touch.y - touch.down_y;
			int step = grid_mode ? GRID_COLS : local_list_visible_rows();
			if (dy < -36) selected += step;
			else if (dy > 36) selected -= step;
			if (selected < 0) selected = 0;
			if (selected >= count) selected = count - 1;
			if (!grid_mode) {
				int visible = local_list_visible_rows();
				if (selected < top) top = selected;
				if (selected >= top + visible)
					top = selected - visible + 1;
			} else top = grid_top_for_selection(selected);
		}
		if (flags & UI_TOUCH_EVENT_TAP) {
			for (int i = 0; i < 3; i++) if (ui_touch_hit_rect(touch.x, touch.y,
			    416 + i * 166, 68, 154, 38)) {
					filter = i; selected = top = 0; focus_tabs = 1;
					grid_mode = filter == 0 ? library_grid
					          : (filter == 2 ? vt_preferences_local_music_grid()
					                         : vt_preferences_local_video_grid());
					count = screen_count(filter);
					break;
				}
			int touch_slots = grid_mode ? local_grid_visible_rows() * GRID_COLS
			                            : local_list_visible_rows();
			for (int slot = 0; slot < touch_slots; slot++) {
				int pos = grid_mode ? top * 3 + slot : top + slot;
				if (pos >= count) break;
				int hit = grid_mode
					        ? ui_touch_hit_rect(touch.x, touch.y,
					              LIST_X + (slot % GRID_COLS) * (GRID_CARD_W + GRID_GAP_X),
					              LIST_Y + (slot / GRID_COLS) * (GRID_CARD_H + GRID_GAP_Y),
					              GRID_CARD_W, GRID_CARD_H)
				        : ui_touch_hit_rect(touch.x, touch.y, LIST_X,
				              LIST_Y + slot * ROW_H, LIST_W, ROW_H - 6);
				if (hit) {
					selected = pos;
					int index = screen_local_index(filter, selected);
					if (index >= 0) {
						if (selected_out) *selected_out = g_items[index];
						return local_media_leave(UI_LOCAL_MEDIA_ACTION_PLAY);
					}
				}
			}
		}
		draw_screen(filter, selected, top, grid_mode, right_open, right_cursor,
		            right_animation, right_focus, focus_tabs, delete_confirm,
		            &focus_motion, &sidebar);
	}
}

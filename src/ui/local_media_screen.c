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
#define LOCAL_INDEX_PATH "ux0:data/VitaTube/local_media.idx"
#define LOCAL_INDEX_TEMP "ux0:data/VitaTube/local_media.tmp"
#define LOCAL_INDEX_BACKUP "ux0:data/VitaTube/local_media.bak"
#define LOCAL_GROUP_VIDEO_TEMP "ux0:data/VitaTube/local_video.tmp"
#define LOCAL_GROUP_AUDIO_TEMP "ux0:data/VitaTube/local_audio.tmp"
#define LIST_X 52
#define LIST_Y 132
#define LIST_W 856
#define ROW_H 52
#define VISIBLE_ROWS 7

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

static void artwork_clear(void) {
	for (int i = 0; i < LOCAL_ART_CACHE; i++) {
		if (g_artwork[i].texture) vita2d_free_texture(g_artwork[i].texture);
		memset(&g_artwork[i], 0, sizeof(g_artwork[i]));
	}
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
	g_artwork[slot].texture = vita2d_load_JPEG_file(path);
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
	static const char *const video[] = { ".mp4", ".m4v", ".mov" };
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
	snprintf(item->artwork_path, sizeof(item->artwork_path), "%s", item->path);
	char *dot = strrchr(item->artwork_path, '.');
	if (dot) snprintf(dot, (size_t)(item->artwork_path +
	                               sizeof(item->artwork_path) - dot), ".jpg");
	SceIoStat art_stat;
	memset(&art_stat, 0, sizeof(art_stat));
	if (!dot || sceIoGetstat(item->artwork_path, &art_stat) < 0)
		item->artwork_path[0] = '\0';
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
		if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, "..")) continue;
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
	sceIoMkdir("ux0:data/VitaTube", 0777);
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
	    "VitaTubeMediaScan", local_scan_thread, 0x10000100,
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

static void clip_text(vita2d_font *font, const char *text, char out[128], int width) {
	size_t len = strlen(text);
	if (len >= 128) len = 127;
	while (len > 0 && (((unsigned char)text[len] & 0xC0U) == 0x80U)) len--;
	memcpy(out, text, len); out[len] = '\0';
	while (len > 0 && ui_font_text_width(font, UI_FONT_BODY, out) > width) {
		len--;
		while (len > 0 && (((unsigned char)out[len] & 0xC0U) == 0x80U)) len--;
		out[len] = '\0';
	}
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
	vita2d_draw_rectangle(x, y, width, 24, RGBA8(2, 7, 14, 226));
	vita2d_draw_rectangle(x, y, 3, 24, accent);
	ui_font_draw_text(font, x + 9, y + 18, VT_THEME_TEXT,
	                  UI_FONT_SMALL, label);
	return width;
}

static void draw_screen(int filter, int selected, int top, int grid_mode,
	                    int right_open, int right_cursor, int focus_tabs,
	                    int delete_confirm,
	                    const UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	ui_mini_player_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header(NULL);
	if (body)
		ui_font_draw_text(body, 42, 94, VT_THEME_TEXT, UI_FONT_BODY,
		                  vt_i18n_str(VT_STR_HOME_TITLE));
	if (small) {
		char summary[64];
		snprintf(summary, sizeof(summary), "%u video  /  %u audio",
		         g_index.video_count, g_index.audio_count);
		ui_font_draw_text(small, 42, 116, VT_THEME_TEXT,
		                  UI_FONT_SMALL, summary);
	}
	const char *tabs[3] = {
		vt_i18n_str(VT_STR_LOCAL_MEDIA_ALL),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_VIDEO),
		vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_AUDIO)
	};
	for (int i = 0; i < 3; i++) {
		int x = 416 + i * 166;
		vita2d_draw_rectangle(x, 68, 154, 38,
		                      filter == i ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE);
		if (filter == i) vita2d_draw_rectangle(x, 103, 154, 3, VT_THEME_BLUE_LIGHT);
		if (focus_tabs && filter == i)
			ui_focus_glow_draw(x - 3, 65, 160, 44,
			                   sceKernelGetProcessTimeWide(), 64, 112);
		if (small) {
			char tab_text[128];
			clip_text(small, tabs[i], tab_text, 138);
			ui_font_draw_text(small, x + 8, 94,
			                  filter == i ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, tab_text);
		}
	}
	int count = screen_count(filter);
	int viewport_bottom = ui_mini_player_visible() ? UI_MINI_PLAYER_Y : 532;
	if (count == 0) {
		if (body) ui_font_draw_text(body, 250, 285, VT_THEME_TEXT_MUTED,
		                               UI_FONT_BODY,
		                               vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY));
	} else if (!grid_mode) {
		int y = LIST_Y + (selected - top) * ROW_H;
		ui_focus_glow_draw(LIST_X, y, LIST_W, ROW_H - 6,
		                   sceKernelGetProcessTimeWide(), LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		for (int pos = top; pos < count && pos < top + VISIBLE_ROWS; pos++) {
			int row_y = LIST_Y + (pos - top) * ROW_H;
			int index = screen_local_index(filter, pos);
			if (index < 0) continue;
			const VtLocalMediaItem *item = &g_items[index];
			ui_panel(LIST_X, row_y, LIST_W, ROW_H - 6,
			         pos == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         item->type == VT_LOCAL_MEDIA_VIDEO
			             ? VT_THEME_BLUE_BRIGHT : VT_THEME_BLUE_LIGHT,
			         pos == selected);
			vita2d_texture *art = artwork_get(item->artwork_path);
			if (art) draw_artwork_contain(art, LIST_X + 10, row_y + 4, 38, 38);
			if (body) {
				char clipped[128]; clip_text(body, item->name, clipped, 520);
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
		const int cols = 3, card_w = 276, card_h = 154, gap = 16;
		int first_row = top;
		int viewport_bottom = ui_mini_player_visible() ? UI_MINI_PLAYER_Y : 532;
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		for (int pos = first_row * cols; pos < count && pos < (first_row + 2) * cols; pos++) {
			int index = filtered_index(filter, pos);
			int col = pos % cols, row = pos / cols - first_row;
			int x = LIST_X + col * (card_w + gap), y = LIST_Y + row * (card_h + 14);
			ui_panel(x, y, card_w, card_h,
			         pos == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         g_items[index].type == VT_LOCAL_MEDIA_AUDIO
			             ? VT_THEME_BLUE_LIGHT : VT_THEME_BLUE_BRIGHT,
			         pos == selected);
			const VtLocalMediaItem *item = &g_items[index];
			vita2d_texture *art = artwork_get(item->artwork_path);
			if (art) draw_artwork_contain(art, x + 7, y + 7, card_w - 14, 94);
			else if (item->type == VT_LOCAL_MEDIA_AUDIO) {
				if (small) {
					char centered[128];
					clip_text(small, item->name, centered, card_w - 32);
					int width = ui_font_text_width(small, UI_FONT_SMALL, centered);
					ui_font_draw_text(small, x + (card_w - width) / 2, y + 65,
					                  VT_THEME_TEXT, UI_FONT_SMALL, centered);
				}
			} else {
				vita2d_draw_fill_circle(x + 46.0f, y + 54.0f, 30.0f,
				                        VT_THEME_SURFACE_RAISED);
				vita2d_draw_fill_circle(x + 46.0f, y + 54.0f, 8.0f,
				                        VT_THEME_BLUE_LIGHT);
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
				if (art || item->type != VT_LOCAL_MEDIA_AUDIO) {
					char title[128]; clip_text(small, item->name, title, card_w - 26);
					ui_font_draw_text(small, x + 13, y + 118, VT_THEME_TEXT,
					                  UI_FONT_SMALL, title);
				}
			}
		}
		vita2d_disable_clipping();
	}
	if (right_open) {
		vita2d_draw_rectangle(620, UI_BRAND_HEADER_HEIGHT, 340,
		                      544 - UI_BRAND_HEADER_HEIGHT, RGBA8(3, 8, 15, 250));
		vita2d_draw_rectangle(620, UI_BRAND_HEADER_HEIGHT, 4,
		                      544 - UI_BRAND_HEADER_HEIGHT, VT_THEME_BLUE_BRIGHT);
		if (body) ui_font_draw_text(body, 650, 112, VT_THEME_TEXT,
		                            UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_LOCAL_MEDIA_VIEW_TITLE));
		const char *actions[3] = {
			vt_i18n_str(grid_mode ? VT_STR_LOCAL_MEDIA_VIEW_GRID
			                             : VT_STR_LOCAL_MEDIA_VIEW_LIST),
			vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_RENAME),
			vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTION_DELETE)
		};
		for (int i = 0; i < 3; i++) {
			int ay = 148 + i * 62;
			vita2d_draw_rectangle(642, ay, 294, 54,
			                      right_cursor == i ? VT_THEME_SURFACE_FOCUS
			                                        : VT_THEME_SURFACE);
			if (body) ui_font_draw_text(body, 662, ay + 35,
			                            i == 2 ? RGBA8(255, 150, 165, 255)
			                                   : VT_THEME_TEXT,
			                            UI_FONT_BODY, actions[i]);
		}
	}
	if (delete_confirm) {
		vita2d_draw_rectangle(208, 190, 544, 152, RGBA8(3, 8, 15, 248));
		vita2d_draw_rectangle(208, 190, 5, 152, RGBA8(225, 62, 88, 255));
		if (body) ui_font_draw_text(body, 244, 240, VT_THEME_TEXT, UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_LOCAL_MEDIA_DELETE_TITLE));
		if (small) ui_font_draw_text(small, 244, 280, VT_THEME_TEXT_MUTED,
		                              UI_FONT_SMALL,
		                              vt_i18n_str(VT_STR_LOCAL_MEDIA_DELETE_DETAIL));
	}
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->focus_cursor);
	ui_mini_player_draw();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_local_media_screen(VtLocalMediaItem *selected_out) {
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
	int right_open = 0, right_cursor = 0, focus_tabs = 0, delete_confirm = 0;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	unsigned int repeat = 0; uint64_t repeat_at = 0;
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
		ui_mini_player_handle_buttons(&pressed);
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			ctrl.buttons &= SCE_CTRL_SELECT;
			ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
		}
		UiTouchEvent touch; unsigned flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(flags, &touch)) flags = 0;
		int was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                 ctrl.buttons, ctrl.ly);
		if (sidebar.open || was_open) {
			int touched = ui_sections_sidebar_handle_touch(&sidebar, flags,
			                                                touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			flags = 0;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			artwork_clear();
			return UI_LOCAL_MEDIA_ACTION_SECTION_BASE + section;
		}
		int grid_mode = filter == 0 ? 0
		              : (filter == 2 ? vt_preferences_local_music_grid()
		                             : vt_preferences_local_video_grid());
		if (pressed & SCE_CTRL_RTRIGGER) {
			right_open = !right_open;
			right_cursor = 0;
			pressed &= ~SCE_CTRL_RTRIGGER;
		}
		if (delete_confirm) {
			if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE)) {
				int index = screen_local_index(filter, selected);
				if (index >= 0 && selected_out) *selected_out = g_items[index];
				artwork_clear();
				return UI_LOCAL_MEDIA_ACTION_DELETE;
			}
			if (pressed & SCE_CTRL_CIRCLE) delete_confirm = 0;
			pressed = 0;
		}
		if (right_open) {
			if ((pressed & SCE_CTRL_UP) && right_cursor > 0) right_cursor--;
			if ((pressed & SCE_CTRL_DOWN) && right_cursor < 2) right_cursor++;
			if (filter != 0 && ((pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) ||
			    ((pressed & SCE_CTRL_CROSS) && right_cursor == 0))) {
				grid_mode = !grid_mode;
				if (filter == 2) vt_preferences_set_local_music_grid(grid_mode);
				else vt_preferences_set_local_video_grid(grid_mode);
				selected = top = 0;
			}
			if ((pressed & SCE_CTRL_CROSS) && right_cursor == 1) {
				int index = screen_local_index(filter, selected);
				if (index >= 0 && selected_out) *selected_out = g_items[index];
				artwork_clear();
				return UI_LOCAL_MEDIA_ACTION_RENAME;
			}
			if ((pressed & SCE_CTRL_CROSS) && right_cursor == 2)
				delete_confirm = 1;
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
		}
		int old_filter = filter;
		if (focus_tabs && (pressed & SCE_CTRL_LEFT))
			filter = filter > 0 ? filter - 1 : 2;
		if (focus_tabs && (pressed & SCE_CTRL_RIGHT))
			filter = filter < 2 ? filter + 1 : 0;
		if (old_filter != filter) selected = top = 0;
		unsigned int held = 0;
		if (ctrl.buttons & SCE_CTRL_UP || ctrl.ly < 48) held = SCE_CTRL_UP;
		else if (ctrl.buttons & SCE_CTRL_DOWN || ctrl.ly > 207) held = SCE_CTRL_DOWN;
		uint64_t now = sceKernelGetProcessTimeWide();
		unsigned int nav = pressed;
		if (!held) repeat = 0;
		else if (held != repeat) { repeat = held; repeat_at = now + 280000; nav |= held; }
		else if (now >= repeat_at) { repeat_at = now + 95000; nav |= held; }
		int count = screen_count(filter);
		if (!focus_tabs && (nav & SCE_CTRL_UP) &&
		    (selected < (grid_mode ? 3 : 1))) {
			focus_tabs = 1;
			nav &= ~SCE_CTRL_UP;
		}
		if (focus_tabs && (nav & SCE_CTRL_DOWN)) {
			focus_tabs = 0;
			nav &= ~SCE_CTRL_DOWN;
		}
		if (!focus_tabs && grid_mode) {
			if ((pressed & SCE_CTRL_LEFT) && selected > 0) selected--;
			if ((pressed & SCE_CTRL_RIGHT) && selected + 1 < count) selected++;
			if ((nav & SCE_CTRL_UP) && selected >= 3) selected -= 3;
			if ((nav & SCE_CTRL_DOWN) && selected + 3 < count) selected += 3;
		} else if (!focus_tabs) {
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
		}
		if (!grid_mode) {
			if (selected < top) top = selected;
			if (selected >= top + VISIBLE_ROWS) top = selected - VISIBLE_ROWS + 1;
		} else {
			top = selected / 3 > 1 ? selected / 3 - 1 : 0;
		}
		if (!focus_tabs && (pressed & SCE_CTRL_CROSS) && count > 0) {
			int index = screen_local_index(filter, selected);
			if (selected_out) *selected_out = g_items[index];
			artwork_clear();
			return UI_LOCAL_MEDIA_ACTION_PLAY;
		}
		if (!focus_tabs && (pressed & SCE_CTRL_TRIANGLE) && count > 0) {
			int index = screen_local_index(filter, selected);
			if (index >= 0 && selected_out) *selected_out = g_items[index];
			artwork_clear();
			return UI_LOCAL_MEDIA_ACTION_RENAME;
		}
		if (!focus_tabs && (pressed & SCE_CTRL_SQUARE) && count > 0)
			delete_confirm = 1;
		if (pressed & SCE_CTRL_CIRCLE) { artwork_clear(); return UI_LOCAL_MEDIA_ACTION_BACK; }
		if ((flags & UI_TOUCH_EVENT_UP) && !(flags & UI_TOUCH_EVENT_TAP) &&
		    !focus_tabs && count > 0) {
			int dy = touch.y - touch.down_y;
			int step = grid_mode ? 3 : 3;
			if (dy < -36) selected += step;
			else if (dy > 36) selected -= step;
			if (selected < 0) selected = 0;
			if (selected >= count) selected = count - 1;
			if (!grid_mode) {
				if (selected < top) top = selected;
				if (selected >= top + VISIBLE_ROWS)
					top = selected - VISIBLE_ROWS + 1;
			} else top = selected / 3 > 1 ? selected / 3 - 1 : 0;
		}
		if (flags & UI_TOUCH_EVENT_TAP) {
			for (int i = 0; i < 3; i++) if (ui_touch_hit_rect(touch.x, touch.y,
			    416 + i * 166, 68, 154, 38)) {
					filter = i; selected = top = 0; focus_tabs = 1;
				}
			int touch_slots = grid_mode ? 6 : VISIBLE_ROWS;
			for (int slot = 0; slot < touch_slots; slot++) {
				int pos = grid_mode ? top * 3 + slot : top + slot;
				if (pos >= count) break;
				int hit = grid_mode
				        ? ui_touch_hit_rect(touch.x, touch.y,
				              LIST_X + (slot % 3) * 292,
				              LIST_Y + (slot / 3) * 168, 276, 154)
				        : ui_touch_hit_rect(touch.x, touch.y, LIST_X,
				              LIST_Y + slot * ROW_H, LIST_W, ROW_H - 6);
				if (hit) {
					selected = pos;
					int index = screen_local_index(filter, selected);
					if (selected_out) *selected_out = g_items[index];
					artwork_clear();
					return UI_LOCAL_MEDIA_ACTION_PLAY;
				}
			}
		}
		draw_screen(filter, selected, top, grid_mode, right_open, right_cursor,
		            focus_tabs, delete_confirm, &sidebar);
	}
}

#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "library/catalog.h"
#include "ui/theme_scene.h"
#include "ui/local_media_screen.h"
#include "ui/home_services.h"
#include "ui/local_catalog_summary.h"

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
#include "history/playback_history.h"
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "media/image_loader.h"
#include "media/music_metadata.h"
#include "media/video_thumbnail.h"
#include "ui/brand.h"
#include "ui/layout.h"
#include "ui/media_grid_layout.h"
#include "ui/home_tabs.h"
#include "ui/online_audio_screen.h"
#include "ui/components.h"
#include "ui/navigation.h"
#include "ui/focus_glow.h"
#include "ui/format.h"
#include "ui/mini_player.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/touch.h"

#define LOCAL_MAX_ITEMS 65536
#define LOCAL_PAGE_ITEMS 24
#define LOCAL_SCAN_THREAD_STACK 0x40000
#define LOCAL_INDEX_PATH "ux0:data/VitaMediaDeck/local_media.idx"
#define LOCAL_INDEX_TEMP "ux0:data/VitaMediaDeck/local_media.tmp"
#define LOCAL_INDEX_BACKUP "ux0:data/VitaMediaDeck/local_media.bak"
#define LOCAL_ROOT_CACHE_PATH "ux0:data/VitaMediaDeck/local_roots.cache"
#define LOCAL_ROOT_CACHE_TEMP "ux0:data/VitaMediaDeck/local_roots.tmp"
#define LOCAL_GROUP_VIDEO_TEMP "ux0:data/VitaMediaDeck/local_video.tmp"
#define LOCAL_GROUP_AUDIO_TEMP "ux0:data/VitaMediaDeck/local_audio.tmp"
#define LOCAL_GROUP_IMAGE_TEMP "ux0:data/VitaMediaDeck/local_image.tmp"
#define LOCAL_SCAN_VIDEO_AUDIO 3U
#define LOCAL_SCAN_IMAGE 4U
#define HOME_LAYOUT (ui_theme_home_layout(ui_theme_id(),local_viewport_bottom()))
#define LIST_X (HOME_LAYOUT.x)
#define LIST_Y (HOME_LAYOUT.y)
#define LIST_W (HOME_LAYOUT.w)
#define ROW_H (HOME_LAYOUT.step)
#define VISIBLE_ROWS 8
#define GRID_COLS 3
#define GRID_CARD_W UI_GRID_CARD_W
#define GRID_CARD_H (ui_media_grid_layout(LIST_Y, local_viewport_bottom()).card_h)
#define GRID_GAP_X 9
#define GRID_GAP_Y 8
#define GRID_THUMB_H (ui_media_grid_layout(LIST_Y, local_viewport_bottom()).thumb_h)
#define TAB_X 392
#define TAB_W 126
#define TAB_STEP 132

static float local_option_y(float row,int flattened){
 int views=1+!!vt_ui_theme_legacy_layout(ui_theme_id())+(!flattened?2:0);
 unsigned starts=1u|(1u<<views)|(1u<<(views+1))|(1u<<(views+2));
 return ui_menu_group_focus_y(row,82,52,starts);
}
static int local_viewport_bottom(void) {
	int mini_top = ui_mini_player_top();
	return mini_top < UI_CONTENT_BOTTOM ? mini_top : UI_CONTENT_BOTTOM;
}

static int local_list_visible_rows(void) {
	if (!vt_ui_theme_legacy_layout(ui_theme_id()))
		return HOME_LAYOUT.rows;
	int rows = (local_viewport_bottom() - LIST_Y + 6) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int local_list_render_rows(void) {
	if (!vt_ui_theme_legacy_layout(ui_theme_id()))
		return HOME_LAYOUT.rows;
	int rows = (local_viewport_bottom() - LIST_Y + ROW_H - 1) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int local_grid_visible_rows(void) {
 return ui_media_grid_layout(LIST_Y, local_viewport_bottom()).rows;
}

static int local_grid_render_rows(void) {
 return ui_media_grid_layout(LIST_Y, local_viewport_bottom()).rows;
}

static int grid_top_for_selection(int selected,int top) {
	int selected_row = selected / GRID_COLS;
	int visible = local_grid_visible_rows();
	if(selected_row<top)return selected_row;
    if(selected_row>=top+visible)return selected_row-visible+1;
    return top;
}

static int thumbnail_viewport_priority(int index, int selected,
	                                   int visible_first,
	                                   int visible_limit) {
	if (index == selected) return 100;
	if (index >= visible_first && index < visible_limit) {
		int distance = index > selected ? index - selected : selected - index;
		int priority = 78 - distance * 4;
		return priority > 48 ? priority : 48;
	}
	return 24;
}

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t record_size;
	uint32_t total_count;
	uint32_t video_count;
	uint32_t audio_count;
	uint32_t image_count;
	uint32_t podcast_count;
} LocalMediaIndexHeader;

static VtLocalMediaItem g_items[LOCAL_PAGE_ITEMS];
static LocalMediaIndexHeader g_index;
static int g_page_filter = -1;
static int g_page_start = -1;
static int g_page_count;
static int g_catalog_dirty, g_index_ready;
static unsigned g_catalog_generation;
static int g_resume_actions;
static int g_saved_filter, g_saved_selected, g_saved_top;
static int g_saved_storage, g_saved_sort;

void ui_local_media_cache_invalidate(void) {
	g_catalog_dirty = 1;g_catalog_generation++;
}

#define LOCAL_ROOT_COUNT 16
typedef struct {
	const char *path;
	int external;
	int kind;
	uint32_t counts[3];
	uint64_t sizes[3];
	char preview_path[3][VT_LOCAL_MEDIA_PATH_MAX];
	uint64_t preview_size[3];
	VtLocalMediaItem item;
	uint32_t visible_count;
} LocalMediaRoot;

typedef UiLocalRootSummary LocalRootCacheEntry;

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t index_total_count;
	LocalRootCacheEntry roots[LOCAL_ROOT_COUNT];
} LocalRootCache;

static LocalMediaRoot g_roots[LOCAL_ROOT_COUNT];
static int g_root_view[LOCAL_ROOT_COUNT];
static int g_root_view_count;
static int g_root_sort_mode;
static int g_roots_ready;
/* Scanner-owned until its done flag is published; never read it while scanning. */
static LocalRootCache g_scanned_roots;

static const struct {
	const char *path;
	int external;
	int kind;
} g_root_specs[LOCAL_ROOT_COUNT] = {
	{ "ux0:video", 0, 0 }, { "ux0:movies", 0, 1 },
	{ "ux0:music", 0, 2 }, { "ux0:picture", 0, 3 },
	{ "ux0:photo", 0, 4 }, { "ux0:download", 0, 5 },
	{ "uma0:video", 1, 0 }, { "uma0:movies", 1, 1 },
	{ "uma0:music", 1, 2 }, { "uma0:picture", 1, 3 },
	{ "uma0:photo", 1, 4 }, { "uma0:download", 1, 5 },
	{ "ux0:podcast", 0, 6 }, { "uma0:podcast", 1, 6 },
	{ "ux0:podcasts", 0, 6 }, { "uma0:podcasts", 1, 6 }
};

static const char *root_kind_name(int kind) {
	switch (kind) {
		case 0: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_VIDEOS);
		case 1: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_MOVIES);
		case 2: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_MUSIC);
		case 3: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_PICTURES);
		case 4: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_PHOTOS);
		case 6: return "Podcasts";
		default: return vt_i18n_str(VT_STR_LOCAL_MEDIA_ROOT_DOWNLOADS);
	}
}

static int item_root_index(const char *path) {
	if (!path) return -1;
	for (int i = 0; i < LOCAL_ROOT_COUNT; i++) {
		size_t length = strlen(g_root_specs[i].path);
		if (!strncmp(path, g_root_specs[i].path, length) &&
		    (path[length] == '/' || path[length] == '\0')) return i;
	}
	return -1;
}

static void root_catalog_reset(void) {
    g_roots_ready=0;
	memset(g_roots, 0, sizeof(g_roots));
	for (int i = 0; i < LOCAL_ROOT_COUNT; i++) {
		g_roots[i].path = g_root_specs[i].path;
		g_roots[i].external = g_root_specs[i].external;
		g_roots[i].kind = g_root_specs[i].kind;
	}
}

static int root_cache_load(void) {
	SceUID fd = sceIoOpen(LOCAL_ROOT_CACHE_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	LocalRootCache cache;
	unsigned char *destination = (unsigned char *)&cache;
	size_t received = 0;
	while (received < sizeof(cache)) {
		int bytes = sceIoRead(fd, destination + received,
		                      sizeof(cache) - received);
		if (bytes <= 0) {
			sceIoClose(fd);
			return bytes < 0 ? bytes : -1;
		}
		received += (size_t)bytes;
	}
	sceIoClose(fd);
	if (memcmp(cache.magic, "VTROOT01", 8) || cache.version != 1U ||
	    cache.index_total_count != g_index.total_count) return -1;
	for (int i = 0; i < LOCAL_ROOT_COUNT; i++) {
		memcpy(g_roots[i].counts, cache.roots[i].counts,
		       sizeof(g_roots[i].counts));
		memcpy(g_roots[i].sizes, cache.roots[i].sizes,
		       sizeof(g_roots[i].sizes));
		memcpy(g_roots[i].preview_path, cache.roots[i].preview_path,
		       sizeof(g_roots[i].preview_path));
		memcpy(g_roots[i].preview_size, cache.roots[i].preview_size,
		       sizeof(g_roots[i].preview_size));
	}
	return 0;
}

static void root_cache_save(void) {
	LocalRootCache cache;
	memset(&cache, 0, sizeof(cache));
	memcpy(cache.magic, "VTROOT01", 8);
	cache.version = 1U;
	cache.index_total_count = g_index.total_count;
	for (int i = 0; i < LOCAL_ROOT_COUNT; i++) {
		memcpy(cache.roots[i].counts, g_roots[i].counts,
		       sizeof(g_roots[i].counts));
		memcpy(cache.roots[i].sizes, g_roots[i].sizes,
		       sizeof(g_roots[i].sizes));
		memcpy(cache.roots[i].preview_path, g_roots[i].preview_path,
		       sizeof(g_roots[i].preview_path));
		memcpy(cache.roots[i].preview_size, g_roots[i].preview_size,
		       sizeof(g_roots[i].preview_size));
	}
	SceUID fd = sceIoOpen(LOCAL_ROOT_CACHE_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return;
	const unsigned char *source = (const unsigned char *)&cache;
	size_t written = 0;
	while (written < sizeof(cache)) {
		int bytes = sceIoWrite(fd, source + written, sizeof(cache) - written);
		if (bytes <= 0) break;
		written += (size_t)bytes;
	}
	if (written == sizeof(cache)) sceIoSyncByFd(fd, 0);
	sceIoClose(fd);
	if (written != sizeof(cache)) {
		sceIoRemove(LOCAL_ROOT_CACHE_TEMP);
		return;
	}
	sceIoRemove(LOCAL_ROOT_CACHE_PATH);
	if (sceIoRename(LOCAL_ROOT_CACHE_TEMP, LOCAL_ROOT_CACHE_PATH) < 0)
		sceIoRemove(LOCAL_ROOT_CACHE_TEMP);
}

static int build_root_catalog(void) {
    if(g_roots_ready)return 0;
    root_catalog_reset();
    if(root_cache_load()==0){g_roots_ready=1;return 0;}
    /* Missing summaries are rebuilt by the scanner, never by blocking entry
     * into Images on a full linear walk through the cached media index. */
    return -1;
}

static int root_view_compare(const void *left, const void *right) {
	const LocalMediaRoot *a = &g_roots[*(const int *)left];
	const LocalMediaRoot *b = &g_roots[*(const int *)right];
	if (g_root_sort_mode == 2 || g_root_sort_mode == 3) {
		if (a->item.size != b->item.size) {
			int order = a->item.size < b->item.size ? -1 : 1;
			return g_root_sort_mode == 2 ? -order : order;
		}
	}
	int order = strcasecmp(a->item.name, b->item.name);
	return g_root_sort_mode == 1 ? -order : order;
}

static void rebuild_root_view(int type_filter, int storage_filter,
	                          int sort_mode) {
	int podcasts = type_filter == 4;
	if (podcasts) type_filter = 2;
	g_root_view_count = 0;
	g_root_sort_mode = sort_mode;
	for (int i = 0; i < LOCAL_ROOT_COUNT; i++) {
		LocalMediaRoot *root = &g_roots[i];
		if (podcasts && root->kind != 6) continue;
		if (!podcasts && type_filter == 2 && root->kind == 6) continue;
		if (storage_filter == 1 && root->external) continue;
		if (storage_filter == 2 && !root->external) continue;
		uint32_t count = type_filter
		               ? root->counts[type_filter - 1]
		               : root->counts[0] + root->counts[1] + root->counts[2];
		if (!count) continue;
		memset(&root->item, 0, sizeof(root->item));
		root->visible_count = count;
		snprintf(root->item.path, sizeof(root->item.path), "%.*s",
		         (int)sizeof(root->item.path) - 1, root->path);
		snprintf(root->item.name, sizeof(root->item.name), "%s / %s",
		         vt_i18n_str(root->external
		             ? VT_STR_LOCAL_MEDIA_STORAGE_EXTERNAL
		             : VT_STR_LOCAL_MEDIA_STORAGE_CARD),
		         root_kind_name(root->kind));
		root->item.type = type_filter ? (VtLocalMediaType)type_filter : 0;
		root->item.source = VT_LOCAL_MEDIA_SOURCE_FILE;
		root->item.is_directory = 1;
		if (type_filter) root->item.size = root->sizes[type_filter - 1];
		else root->item.size = root->sizes[0] + root->sizes[1] + root->sizes[2];
		int preview_type = type_filter ? type_filter - 1
		                 : root->preview_path[2][0] ? 2
		                 : root->preview_path[0][0] ? 0 : 1;
		if (preview_type >= 0 && preview_type < 3 &&
		    root->preview_path[preview_type][0]) {
			snprintf(root->item.artwork_path, sizeof(root->item.artwork_path), "%.*s",
			         (int)sizeof(root->item.artwork_path) - 1,
			         root->preview_path[preview_type]);
			root->item.artwork_size = root->preview_size[preview_type];
		}
		g_root_view[g_root_view_count++] = i;
	}
	qsort(g_root_view, (size_t)g_root_view_count, sizeof(g_root_view[0]),
	      root_view_compare);
}

static LocalMediaRoot *screen_root(int position) {
	if (position < 0 || position >= g_root_view_count) return NULL;
	return &g_roots[g_root_view[position]];
}

typedef struct {
	void *self;
	SceUID thid;
	volatile int done;
	int result;
	LocalMediaIndexHeader index;
    unsigned generation;
} LocalMediaScanJob;

static LocalMediaScanJob g_scan_job = { .thid = -1 };

static int ends_with_ci(const char *name, const char *suffix);

static int local_media_leave(int action) {
 ui_home_tabs_set_expanded(0);
	vt_video_thumbnail_suspend();
	return action;
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
	static const char *const audio[] = {
		".mp3", ".m4a", ".aac", ".wav", ".flac"
	};
	for (unsigned i = 0; i < sizeof(video) / sizeof(video[0]); i++)
		if (ends_with_ci(name, video[i])) return VT_LOCAL_MEDIA_VIDEO;
	for (unsigned i = 0; i < sizeof(audio) / sizeof(audio[0]); i++)
		if (ends_with_ci(name, audio[i])) return VT_LOCAL_MEDIA_AUDIO;
	if (vt_image_supported_path(name)) return VT_LOCAL_MEDIA_IMAGE;
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
    int root_index=item_root_index(item->path);
    if(root_index>=0)ui_local_summary_add(&g_scanned_roots.roots[root_index],item);
    return 0;
}

static void finish_external_item(VtLocalMediaItem *item) {
	if (!item) return;
	const char *slash = strrchr(item->path, '/');
	snprintf(item->name, sizeof(item->name), "%s", slash ? slash + 1 : item->path);
	if (item->type == VT_LOCAL_MEDIA_IMAGE) {
		snprintf(item->artwork_path, sizeof(item->artwork_path), "%s", item->path);
		item->artwork_size = item->size;
		return;
	}
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
		if (sceIoGetstat(candidate, &stat) >= 0) {
			snprintf(item->artwork_path, sizeof(item->artwork_path), "%s", candidate);
			item->artwork_size = stat.st_size > 0 ? (uint64_t)stat.st_size : 0;
		}
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
				item->artwork_size = stat.st_size > 0 ? (uint64_t)stat.st_size : 0;
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
			if (metadata.artwork_path[0] && (!item->artwork_path[0] ||
			    strstr(metadata.artwork_path,".podcast."))) {
				snprintf(item->artwork_path, sizeof(item->artwork_path), "%s",
				         metadata.artwork_path);
				SceIoStat artwork_stat;
				memset(&artwork_stat, 0, sizeof(artwork_stat));
				if (sceIoGetstat(metadata.artwork_path, &artwork_stat) >= 0 &&
				    artwork_stat.st_size > 0)
					item->artwork_size = (uint64_t)artwork_stat.st_size;
			}
			if (metadata.duration_ms) item->duration_ms = metadata.duration_ms;
		}
	}
}

static void scan_directory_to_index(const char *directory, int depth,
	                                unsigned int type_mask,
	                                SceUID video_fd, SceUID audio_fd,
	                                SceUID image_fd,
	                                uint32_t *video_count,
	                                uint32_t *audio_count,
	                                uint32_t *image_count,
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
			scan_directory_to_index(path, depth + 1, type_mask,
			                        video_fd, audio_fd, image_fd,
			                        video_count, audio_count, image_count,
			                        total_count);
			continue;
		}
		VtLocalMediaType type = media_type(entry.d_name);
		if (!type || !(type_mask & (1U << (type - 1)))) continue;
		VtLocalMediaItem item;
		memset(&item, 0, sizeof(item));
		snprintf(item.path, sizeof(item.path), "%s", path);
		item.type = type;
		item.source = VT_LOCAL_MEDIA_SOURCE_FILE;
		item.size = entry.d_stat.st_size > 0 ? (uint64_t)entry.d_stat.st_size : 0;
		finish_external_item(&item);
		SceUID output = type == VT_LOCAL_MEDIA_AUDIO ? audio_fd
		              : type == VT_LOCAL_MEDIA_IMAGE ? image_fd : video_fd;
		if (write_record(output, &item) == 0) {
			if (type == VT_LOCAL_MEDIA_AUDIO)
				(*audio_count)++;
			else if (type == VT_LOCAL_MEDIA_IMAGE)
				(*image_count)++;
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
    memset(&g_scanned_roots,0,sizeof(g_scanned_roots));
	LocalMediaIndexHeader index;
	memset(&index, 0, sizeof(index));
	sceIoMkdir("ux0:data/VitaMediaDeck", 0777);
	sceIoRemove(LOCAL_INDEX_TEMP);
	sceIoRemove(LOCAL_GROUP_VIDEO_TEMP);
	sceIoRemove(LOCAL_GROUP_AUDIO_TEMP);
	sceIoRemove(LOCAL_GROUP_IMAGE_TEMP);
	SceUID final_fd = sceIoOpen(LOCAL_INDEX_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	SceUID video_fd = sceIoOpen(LOCAL_GROUP_VIDEO_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	SceUID audio_fd = sceIoOpen(LOCAL_GROUP_AUDIO_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	SceUID image_fd = sceIoOpen(LOCAL_GROUP_IMAGE_TEMP,
	                            SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (final_fd < 0 || video_fd < 0 || audio_fd < 0 || image_fd < 0) {
		if (final_fd >= 0) sceIoClose(final_fd);
		if (video_fd >= 0) sceIoClose(video_fd);
		if (audio_fd >= 0) sceIoClose(audio_fd);
		if (image_fd >= 0) sceIoClose(image_fd);
		return -1;
	}
	memcpy(index.magic, "VTLOCAL6", 8);
	index.version = 8;
	index.record_size = sizeof(VtLocalMediaItem);
	/* Reserve the header, then append video, audio, and image groups. */
	sceIoWrite(final_fd, &index, sizeof(index));
	scan_directory_to_index("ux0:video", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:video", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	scan_directory_to_index("ux0:movies", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:movies", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	scan_directory_to_index("ux0:music", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	scan_directory_to_index("uma0:music", 0, LOCAL_SCAN_VIDEO_AUDIO,
	                        video_fd, audio_fd, image_fd,
	                        &index.video_count, &index.audio_count, &index.image_count,
	                        &index.total_count);
	static const char *const image_roots[] = {
		"ux0:picture", "uma0:picture", "ux0:photo", "uma0:photo"
	};
	for (unsigned int i = 0; i < sizeof(image_roots) / sizeof(image_roots[0]); i++)
		scan_directory_to_index(image_roots[i], 0, LOCAL_SCAN_IMAGE,
		                        video_fd, audio_fd, image_fd,
		                        &index.video_count, &index.audio_count,
		                        &index.image_count, &index.total_count);
	static const char *const download_roots[] = { "ux0:download", "uma0:download" };
	for (unsigned int i = 0; i < sizeof(download_roots) / sizeof(download_roots[0]); i++)
		scan_directory_to_index(download_roots[i], 0,
		                        LOCAL_SCAN_VIDEO_AUDIO | LOCAL_SCAN_IMAGE,
		                        video_fd, audio_fd, image_fd,
		                        &index.video_count, &index.audio_count,
		                        &index.image_count,
	                        &index.total_count);
	uint32_t music_count = index.audio_count;
	for (int i = 12; i < LOCAL_ROOT_COUNT; i++)
		scan_directory_to_index(g_root_specs[i].path, 0, 1U << (VT_LOCAL_MEDIA_AUDIO - 1),
		    video_fd, audio_fd, image_fd, &index.video_count, &index.audio_count,
		    &index.image_count, &index.total_count);
	index.podcast_count = index.audio_count - music_count;
	sceIoSyncByFd(video_fd, 0); sceIoClose(video_fd);
	sceIoSyncByFd(audio_fd, 0); sceIoClose(audio_fd);
	sceIoSyncByFd(image_fd, 0); sceIoClose(image_fd);
	int ret = copy_file_records(final_fd, LOCAL_GROUP_VIDEO_TEMP);
	if (ret == 0)
		ret = copy_file_records(final_fd, LOCAL_GROUP_AUDIO_TEMP);
	if (ret == 0)
		ret = copy_file_records(final_fd, LOCAL_GROUP_IMAGE_TEMP);
	if (ret == 0) {
		sceIoPwrite(final_fd, &index, sizeof(index), 0);
		ret = sceIoSyncByFd(final_fd, 0);
	}
	sceIoClose(final_fd);
	sceIoRemove(LOCAL_GROUP_VIDEO_TEMP);
	sceIoRemove(LOCAL_GROUP_AUDIO_TEMP);
	sceIoRemove(LOCAL_GROUP_IMAGE_TEMP);
	if (ret < 0) sceIoRemove(LOCAL_INDEX_TEMP);
	log_printf("local media indexed: total=%u video=%u audio=%u images=%u",
	           index.total_count, index.video_count, index.audio_count,
	           index.image_count);
	if (ret == 0) *result_index = index;
	return ret;
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
	uint64_t grouped = (uint64_t)header.video_count + header.audio_count +
	                   header.image_count;
	uint64_t expected = sizeof(header) +
	                    (uint64_t)header.total_count * sizeof(VtLocalMediaItem);
	if (bytes != (int)sizeof(header) || memcmp(header.magic, "VTLOCAL6", 8) ||
	    header.version != 8 || header.podcast_count > header.audio_count || header.record_size != sizeof(VtLocalMediaItem) ||
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
	g_index = *index;g_index_ready=1;
    root_catalog_reset();
    for(int i=0;i<LOCAL_ROOT_COUNT;i++) {
        memcpy(g_roots[i].counts,g_scanned_roots.roots[i].counts,sizeof(g_roots[i].counts));
        memcpy(g_roots[i].sizes,g_scanned_roots.roots[i].sizes,sizeof(g_roots[i].sizes));
        memcpy(g_roots[i].preview_path,g_scanned_roots.roots[i].preview_path,sizeof(g_roots[i].preview_path));
        memcpy(g_roots[i].preview_size,g_scanned_roots.roots[i].preview_size,sizeof(g_roots[i].preview_size));
    }
    g_roots_ready=1;root_cache_save();
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

static uint64_t g_scan_started_us;
static int local_scan_start(int force) {
	if (g_scan_job.thid >= 0) return 0;
    uint64_t now=sceKernelGetProcessTimeWide();
    if(!force&&g_scan_started_us&&now-g_scan_started_us<60000000ULL)return 0;
    g_scan_started_us=now;
	memset(&g_scan_job, 0, sizeof(g_scan_job));
	g_scan_job.self = &g_scan_job;g_scan_job.generation=g_catalog_generation;
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
	int stale=g_scan_job.generation!=g_catalog_generation;
    int ret=!stale&&g_scan_job.result==0?local_index_publish(&g_scan_job.index):-1;
    if(ret==0)g_catalog_dirty=0;
	if (ret < 0) sceIoRemove(LOCAL_INDEX_TEMP);
    if(stale)local_scan_start(1);
	return ret == 0 ? 1 : -1;
}

static int filtered_count(int filter) {
	if (filter == 1) return (int)g_index.video_count;
	if (filter == 2) return (int)(g_index.audio_count - g_index.podcast_count);
	if (filter == 4) return (int)g_index.podcast_count;
	if (filter == 3) return (int)g_index.image_count;
	return (int)g_index.total_count;
}

static uint32_t filter_first_record(int filter) {
	if (filter == 2) return g_index.video_count;
	if (filter == 4) return g_index.video_count + g_index.audio_count - g_index.podcast_count;
	if (filter == 3) return g_index.video_count + g_index.audio_count;
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

/* This device entry must not depend on scanner summaries: an empty/stale
 * index must never leave Images with only its remote provider. Empty path is
 * the filesystem browser's device picker, with the image filter retained. */
static int local_images_shortcut(const VtLocalMediaItem *item) {
    return item && item->source == VT_LOCAL_MEDIA_SOURCE_FILE &&
           item->is_directory && item->type == VT_LOCAL_MEDIA_IMAGE &&
           !item->path[0];
}

static int screen_local_count(int filter, int flattened) {
    return (flattened ? filtered_count(filter) : g_root_view_count) +
           (filter == VT_LOCAL_MEDIA_IMAGE);
}

static int screen_count(int filter, int flattened) {
    return home_provider_count(filter) + screen_local_count(filter, flattened);
}

static const VtLocalMediaItem *screen_item(int filter, int position,
	                                       int flattened,
	                                       LocalMediaRoot **root_out) {
	if (root_out) *root_out = NULL;
    int locals = screen_local_count(filter, flattened);
    int provider=home_provider_position(filter,position,locals);
    if(provider>=0)return home_provider_item(filter,provider);
    position=home_local_position(filter,position,locals);
    if(position<0)return NULL;
    if (filter == VT_LOCAL_MEDIA_IMAGE) {
        if (position == 0) {
            static VtLocalMediaItem local_images;
            local_images.type = VT_LOCAL_MEDIA_IMAGE;
            local_images.source = VT_LOCAL_MEDIA_SOURCE_FILE;
            local_images.is_directory = 1;
            snprintf(local_images.name, sizeof(local_images.name), "%s",
                     vt_i18n_str(VT_STR_LOCAL_MEDIA_BROWSE_IMAGES));
            return &local_images;
        }
        position--;
    }
	if (flattened) {
		int slot = filtered_index(filter, position);
		return slot >= 0 ? &g_items[slot] : NULL;
	}
	LocalMediaRoot *root = screen_root(position);
	if (root_out) *root_out = root;
	return root ? &root->item : NULL;
}

static int screen_open_action(const VtLocalMediaItem *item) {
    return !item ? UI_LOCAL_MEDIA_ACTION_BACK
         : item->is_directory ? UI_LOCAL_MEDIA_ACTION_BROWSE_FOLDER
                              : UI_LOCAL_MEDIA_ACTION_PLAY;
}

static const char *storage_filter_name(int filter) {
	if (filter == 1) return vt_i18n_str(VT_STR_LOCAL_MEDIA_STORAGE_CARD);
	if (filter == 2) return vt_i18n_str(VT_STR_LOCAL_MEDIA_STORAGE_EXTERNAL);
	return vt_i18n_str(VT_STR_LOCAL_MEDIA_STORAGE_ALL);
}

static const char *sort_mode_name(int mode) {
	switch (mode) {
		case 1: return vt_i18n_str(VT_STR_LOCAL_MEDIA_SORT_NAME_DESC);
		case 2: return vt_i18n_str(VT_STR_LOCAL_MEDIA_SORT_SIZE_DESC);
		case 3: return vt_i18n_str(VT_STR_LOCAL_MEDIA_SORT_SIZE_ASC);
		default: return vt_i18n_str(VT_STR_LOCAL_MEDIA_SORT_NAME_ASC);
	}
}

int ui_local_media_next_audio(const char *current_path, int direction,
	                          int random, VtLocalMediaItem *out) {
	if (!out) return -1;
	int filter=current_path && (strstr(current_path,":podcast/")||strstr(current_path,":podcasts/"))?4:2;
	int count = filtered_count(filter), current = -1;
	if (count <= 0) return -1;
	for (int i = 0; current_path && i < count; i++) {
		int slot = filtered_index(filter, i);
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
	int slot = filtered_index(filter, next);
	if (slot < 0) return -1;
	*out = g_items[slot];
	return 0;
}

static void clip_text(vita2d_font *font, unsigned int size, const char *text,
	                  char out[128], int width) {
	ui_font_fit_text(font, size, text ? text : "", out, 128, width);
}

static const char *media_origin_label(const VtLocalMediaItem *item) {
    if(item && item->source==VT_LOCAL_MEDIA_SOURCE_NAVIDROME)return "Navidrome";
    if(item && item->source==VT_LOCAL_MEDIA_SOURCE_IMMICH)return "Immich";
	return vt_i18n_str(VT_STR_LOCAL_MEDIA_ORIGIN_LOCAL);
}

static unsigned int media_accent(const VtLocalMediaItem *item) {
	if (item && item->type == VT_LOCAL_MEDIA_AUDIO) return VT_THEME_BLUE_LIGHT;
	if (item && item->type == VT_LOCAL_MEDIA_IMAGE) return VT_THEME_SPECTRAL;
	return VT_THEME_BLUE_BRIGHT;
}

static const char *media_preview_path(const VtLocalMediaItem *item) {
	if (!item) return NULL;
	if (item->artwork_path[0]) return item->artwork_path;
	/* Folder catalogue entries intentionally use their selected representative.
	 * Flat video entries without sidecar artwork still have a valid thumbnail
	 * source: the movie itself. */
	if (!item->is_directory &&
	    (item->type == VT_LOCAL_MEDIA_VIDEO ||
	     item->type == VT_LOCAL_MEDIA_IMAGE))
		return item->path;
	return NULL;
}

static uint64_t media_preview_size(const VtLocalMediaItem *item,
	                               const char *preview_path) {
	if (!item || !preview_path) return 0;
	return item->artwork_path[0] ? item->artwork_size : item->size;
}

static const char *media_type_name(const VtLocalMediaItem *item) {
    if(item && item->source!=VT_LOCAL_MEDIA_SOURCE_FILE)return "Server";
	if (!item || item->is_directory)
		return vt_i18n_str(VT_STR_LOCAL_MEDIA_FILES_FOLDER);
	if (item->type == VT_LOCAL_MEDIA_VIDEO)
		return vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_VIDEO);
	if (item->type == VT_LOCAL_MEDIA_AUDIO)
		return vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_AUDIO);
	return vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_IMAGES);
}

static const char *media_location_label(const VtLocalMediaItem *item) {
    if (!item) return "";
    if (local_images_shortcut(item))
        return vt_i18n_str(VT_STR_LOCAL_MEDIA_BROWSE_IMAGES_DETAIL);
    if (item->source == VT_LOCAL_MEDIA_SOURCE_NAVIDROME)
        return vt_i18n_str(VT_STR_HOME_NAVIDROME_DETAIL);
    if (item->source == VT_LOCAL_MEDIA_SOURCE_IMMICH)
        return vt_i18n_str(VT_STR_HOME_IMMICH_DETAIL);
    return item->path;
}

static int draw_media_badge(vita2d_font *font, const char *label,
	                        int x, int y, unsigned int accent) {
	if (!font || !label || !label[0]) return 0;
	int width = ui_font_text_width(font, UI_FONT_SMALL, label) + 17;
	vita2d_draw_rectangle(x, y, width, 24, VT_THEME_GLASS_A(226));
	vita2d_draw_rectangle(x, y, 3, 24, accent);
	ui_font_draw_text(font, x + 9, y + 18, VT_THEME_TEXT,
	                  UI_FONT_SMALL, label);
	return width;
}

static void tick_right_drawer(int open, int cursor, float *animation,
	                          float *focus, uint64_t *last_tick_us, UiFocusMotion *motion) {
	if (!animation || !focus || !last_tick_us) return;
	float animation_target = open ? 1.0f : 0.0f;

	uint64_t now = sceKernelGetProcessTimeWide();
	uint64_t elapsed = *last_tick_us && now > *last_tick_us
	                 ? now - *last_tick_us : 16667ULL;
	if (elapsed > 50000ULL) elapsed = 50000ULL;
	*last_tick_us = now;
	if (vt_preferences_reduce_motion()) {
		*animation = animation_target;

	} else {
		float animation_alpha = (float)elapsed /
		                        (60000.0f + (float)elapsed);

		*animation += (animation_target - *animation) * animation_alpha;

	}
	ui_focus_motion_tick(motion,0,(float)cursor,1,1);
	*focus=motion->y;
	if (!open && *animation < 0.025f) *animation = 0.0f;
}

static UiPageView home_page(int filter,int top,int count,int grid,int storage,int sort,int flat) {
    char key[96];snprintf(key,sizeof(key),"%d:%d:%d:%d:%d",ui_home_tab(),filter,storage,sort,flat);
	int themed = !vt_ui_theme_legacy_layout(ui_theme_id());
	int columns = themed ? HOME_LAYOUT.columns : grid ? GRID_COLS : 1;
	int slots = themed ? HOME_LAYOUT.rows
	            : grid ? local_grid_render_rows() * columns
	                   : local_list_render_rows();
	float step = themed ? HOME_LAYOUT.step : grid ? GRID_CARD_H + GRID_GAP_Y : ROW_H;
	return ui_theme_page(UI_SCENE_HOME, "local-home", key, !themed && grid ? top * columns : top,
	                     count, columns, slots, step);
}
static UiThemeRect home_item_rect(int index,int top,int grid) {
	if (!vt_ui_theme_legacy_layout(ui_theme_id()))
		return ui_theme_home_item_rect(ui_theme_id(), index - top, local_viewport_bottom());
	UiThemeRect r =
	    grid ? (UiThemeRect){LIST_X + index % GRID_COLS * (GRID_CARD_W + GRID_GAP_X),
		                     LIST_Y + (index / GRID_COLS - top) * (GRID_CARD_H + GRID_GAP_Y),
		                     GRID_CARD_W, GRID_CARD_H}
		     : (UiThemeRect){LIST_X, LIST_Y + (index - top) * ROW_H, LIST_W, ROW_H - 6};
	return ui_theme_page_rect(UI_SCENE_HOME, r);
}

static void draw_screen(int filter, int selected, int top, int grid_mode,
	                    int right_open, int right_cursor, float right_animation,
	                    float right_focus, int focus_tabs,
	                    int storage_filter, int sort_mode, int flattened,
	                    UiFocusMotion *focus_motion,
	                    const UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	ui_mini_player_pump();
	vt_video_thumbnail_resume();
	vt_video_thumbnail_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_set_loading(g_scan_job.thid >= 0 && !g_scan_job.done);
    const VtLocalMediaItem *header_item=screen_count(filter,flattened)?screen_item(filter,selected,flattened,NULL):NULL;
    if(focus_tabs)ui_brand_draw_home_header();
    else ui_brand_draw_path(ui_home_tab_label(),header_item&&header_item->path[0]?header_item->path:media_location_label(header_item));
	int page_has_focus = !ui_mini_player_input_locked() &&
	                     right_animation <= 0.01f &&
	                     (!sidebar || (!sidebar->open &&
	                                  sidebar->animation <= 0.01f));
	int content_has_focus = page_has_focus && !focus_tabs;
	ui_home_tabs_set_expanded(1);
	if(focus_tabs||!vt_ui_theme_legacy_layout(ui_theme_id()))
        ui_home_tabs_draw_with_focus(page_has_focus && focus_tabs, focus_motion);
	int count = screen_count(filter, flattened);
	int viewport_bottom = local_viewport_bottom();
    UiPageView page=home_page(filter,top,count,grid_mode,storage_filter,sort_mode,flattened);
	if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
		const VtLocalMediaItem *current =
		    count ? screen_item(filter, selected, flattened, NULL) : NULL;
		ui_theme_home_heading(ui_home_tab_label(), media_location_label(current), viewport_bottom);
		if (ui_theme_id() == VT_UI_THEME_APERTURE) {
			const char *preview = current ? media_preview_path(current) : NULL;
			uint64_t size = current ? media_preview_size(current, preview) : 0;
			vita2d_texture *art = preview
			                          ? (vt_image_supported_path(preview)
			                                 ? vt_image_thumbnail_get_priority(preview, size, 100)
			                                 : vt_video_thumbnail_get_priority(preview, size, 100))
			                          : NULL;
			ui_theme_home_preview(art, current ? current->name : NULL, viewport_bottom,
			                      current ? current->source : 0);
		}
		if (!count)
			ui_text_line(LIST_X, LIST_Y + 26, LIST_W, vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY),
			             VT_THEME_TEXT_MUTED, UI_FONT_BODY);
		vita2d_set_clip_rectangle(0, HOME_LAYOUT.y - 4, 960, ui_theme_home_content_bottom(ui_theme_id(),viewport_bottom));
		vita2d_enable_clipping();
		for (int pos = page.first; pos < page.end; pos++) {
			LocalMediaRoot *root = NULL;
			const VtLocalMediaItem *item = screen_item(filter, pos, flattened, &root);
			if (!item)
				continue;
			char detail[160], size[32];
			ui_format_file_size(item->size, size, sizeof(size));
			if (item->source != VT_LOCAL_MEDIA_SOURCE_FILE)
				snprintf(detail, sizeof(detail), "%s",
				         vt_i18n_str(item->source == VT_LOCAL_MEDIA_SOURCE_NAVIDROME
				                         ? VT_STR_HOME_NAVIDROME_DETAIL
										 : VT_STR_HOME_IMMICH_DETAIL));
            else if (local_images_shortcut(item))
                snprintf(detail, sizeof(detail), "%s", media_location_label(item));
			else if (root)
				snprintf(detail, sizeof(detail), vt_i18n_str(VT_STR_LOCAL_MEDIA_FOLDER_DETAILS),
				         root->visible_count, size);
			else
				snprintf(detail, sizeof(detail), "%s - %s", media_type_name(item), size);
			const char *preview = media_preview_path(item);
			uint64_t preview_size = media_preview_size(item, preview);
			vita2d_texture *art =
			    preview ? (vt_image_supported_path(preview)
			                   ? vt_image_thumbnail_get_priority(preview, preview_size,
			                                                     pos == selected ? 100 : 60)
			                   : vt_video_thumbnail_get_priority(preview, preview_size,
			                                                     pos == selected ? 100 : 60))
			            : NULL;
			ui_theme_home_row_art(pos - top, pos, item->name, detail, pos == selected,
			                      content_has_focus, viewport_bottom, art,
			                      item->is_directory ? "DIR" : "...");
			if (!art && item->source != VT_LOCAL_MEDIA_SOURCE_FILE)
				ui_theme_home_service(pos - top, viewport_bottom, item->source);
		}
		vita2d_disable_clipping();
		ui_theme_home_footer_with_focus(media_location_label(current), viewport_bottom, focus_tabs);
		ui_theme_home_position(selected, count, viewport_bottom);
	} else if (count == 0) {
		ui_panel(208, 202, 544, 140, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 244, 254, VT_THEME_TEXT,
		                               UI_FONT_BODY,
		                               vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY));
		if (small) ui_font_draw_text(small, 244, 288, VT_THEME_TEXT_MUTED,
		                                UI_FONT_SMALL,
		                                vt_i18n_str(VT_STR_LOCAL_MEDIA_EMPTY_DETAIL));
	} else if (!grid_mode) {
		int list_x = LIST_X + (int)roundf(page.dx);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		int visible_limit = top + local_list_visible_rows();
		for (int pos = page.first; pos < page.end; pos++) {
			int row_y = LIST_Y + (pos - top) * ROW_H + (int)roundf(page.dy);
			LocalMediaRoot *root = NULL;
			const VtLocalMediaItem *item = screen_item(filter, pos, flattened, &root);
			if (!item) continue;
			ui_panel(list_x, row_y, LIST_W, ROW_H - 6,
			         VT_THEME_SURFACE, media_accent(item),
			         0);
			const char *preview_path = media_preview_path(item);
			uint64_t preview_size = media_preview_size(item, preview_path);
			vita2d_texture *art = preview_path
			    ? (vt_image_supported_path(preview_path)
			          ? vt_image_thumbnail_get_priority(preview_path,
			                preview_size,
			                thumbnail_viewport_priority(pos, selected, top,
			                                            visible_limit))
			          : vt_video_thumbnail_get_priority(preview_path,
			                preview_size,
			                thumbnail_viewport_priority(pos, selected, top,
			                                            visible_limit)))
			    : NULL;
			if (art) draw_artwork_contain(art, list_x + 10, row_y + 4, 38, 38);
			if (body) {
				char clipped[128];
				clip_text(body, UI_FONT_BODY, item->name, clipped, 520);
				ui_font_draw_text(body, list_x + (art ? 58 : 24), row_y + 31, VT_THEME_TEXT,
				                  UI_FONT_BODY, clipped);
			}
			if (small) {
				const char *format = media_type_name(item);
				int badge_x = 650+(int)roundf(page.dx);
				badge_x += draw_media_badge(small, media_origin_label(item),
				                            badge_x, row_y + 11,
				                            item->source == VT_LOCAL_MEDIA_SOURCE_FILE
				                                ? VT_THEME_TEXT_FAINT : VT_THEME_BLUE_BRIGHT) + 6;
				draw_media_badge(small, format, badge_x, row_y + 11,
				                 media_accent(item));
				char size_text[32];
				if(item->source==VT_LOCAL_MEDIA_SOURCE_FILE && !local_images_shortcut(item))ui_format_file_size(item->size, size_text, sizeof(size_text));
                else size_text[0]=0;
				int size_width = ui_font_text_width(small, UI_FONT_SMALL, size_text);
				ui_font_draw_text(small, list_x + LIST_W - 12 - size_width,
				                  row_y + 30, VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, size_text);
			}
		}
		vita2d_disable_clipping();
	} else {
		const int cols = GRID_COLS;
		int first_row = top;
		int viewport_bottom = local_viewport_bottom();
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		int visible_limit = (top + local_grid_visible_rows()) * GRID_COLS;
		for (int pos = page.first; pos < page.end; pos++) {
			LocalMediaRoot *root = NULL;
			const VtLocalMediaItem *item = screen_item(filter, pos, flattened, &root);
			if (!item) continue;
			int col = pos % cols, row = pos / cols - first_row;
			int x = LIST_X + col * (GRID_CARD_W + GRID_GAP_X) + (int)roundf(page.dx);
			int y = LIST_Y + row * (GRID_CARD_H + GRID_GAP_Y) + (int)roundf(page.dy);
			ui_panel(x, y, GRID_CARD_W, GRID_CARD_H,
			         VT_THEME_SURFACE, media_accent(item), 0);
			const char *preview_path = media_preview_path(item);
			uint64_t preview_size = media_preview_size(item, preview_path);
			vita2d_texture *art = preview_path
			    ? (vt_image_supported_path(preview_path)
			          ? vt_image_thumbnail_get_priority(preview_path,
			                preview_size,
			                thumbnail_viewport_priority(pos, selected,
			                    top * GRID_COLS, visible_limit))
			          : vt_video_thumbnail_get_priority(preview_path,
			                preview_size,
			                thumbnail_viewport_priority(pos, selected,
			                    top * GRID_COLS, visible_limit)))
			    : NULL;
			vita2d_draw_rectangle(x + 6, y + 6, GRID_CARD_W - 12, GRID_THUMB_H,
			                      VT_THEME_MEDIA_BACKDROP);
			if (art)
				draw_artwork_cover(art, x + 6, y + 6, GRID_CARD_W - 12, GRID_THUMB_H);
            else if(item->source!=VT_LOCAL_MEDIA_SOURCE_FILE)
                ui_theme_service_icon(x+(GRID_CARD_W-136)*.5f,y+6+(GRID_THUMB_H-108)*.5f,136,108,item->source);
            else ui_folder_icon(x+(GRID_CARD_W-136)*.5f,y+6+(GRID_THUMB_H-108)*.5f);
			if (!root && item->type == VT_LOCAL_MEDIA_VIDEO) {
				char history_id[16];
				vt_playback_history_local_id(item->path, history_id);
				ui_watched_progress(history_id, x + 6,
				                    y + 6 + GRID_THUMB_H - 6,
				                    GRID_CARD_W - 12);
			}
			if (small) {
				const char *format = media_type_name(item);
				int badge_x = x + 12;
				badge_x += draw_media_badge(small, media_origin_label(item),
				                            badge_x, y + 12,
				                            item->source == VT_LOCAL_MEDIA_SOURCE_FILE
				                                ? VT_THEME_TEXT_FAINT : VT_THEME_BLUE_BRIGHT) + 6;
				draw_media_badge(small, format, badge_x, y + 12,
				                 media_accent(item));
				char title[128];
				clip_text(small, UI_FONT_SMALL, item->name, title, GRID_CARD_W - 26);
				ui_font_draw_text(small, x + 13, y + (ui_media_grid_layout(LIST_Y, local_viewport_bottom()).title_y), VT_THEME_TEXT,
				                  UI_FONT_SMALL, title);
				char detail[128];
				char size_text[32];
				if(item->source==VT_LOCAL_MEDIA_SOURCE_FILE && !local_images_shortcut(item))ui_format_file_size(item->size, size_text, sizeof(size_text));
                else size_text[0]=0;
                if (local_images_shortcut(item))
                    snprintf(detail, sizeof(detail), "%s", media_location_label(item));
				else if (root)
					snprintf(detail, sizeof(detail),
					         vt_i18n_str(VT_STR_LOCAL_MEDIA_FOLDER_DETAILS),
					         root->visible_count, size_text);
				else
					snprintf(detail, sizeof(detail), "%s  -  %s",
					         media_type_name(item), size_text);
				char clipped_detail[128];
				clip_text(small, UI_FONT_SMALL, detail, clipped_detail, GRID_CARD_W - 26);
				ui_font_draw_text(small, x + 13, y + (ui_media_grid_layout(LIST_Y, local_viewport_bottom()).detail_y), VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, clipped_detail);
			}
		}
		vita2d_disable_clipping();
	}
	/* Draw persistent playback first so modal drawers and confirmations remain
	 * true full-height layers above it. */
    ui_theme_page_finish(UI_SCENE_HOME,LIST_X,LIST_Y,LIST_W,viewport_bottom);
    if(content_has_focus && count && focus_motion) {
        UiThemeRect target=home_item_rect(selected,top,grid_mode);
        ui_focus_motion_tick(focus_motion,target.x,target.y,target.w,target.h);
    }
	if(content_has_focus && count && focus_motion)
		ui_focus_glow_draw(focus_motion->x,focus_motion->y,focus_motion->width,focus_motion->height,
		    sceKernelGetProcessTimeWide(),LIST_Y,viewport_bottom);
	ui_mini_player_draw();
	if (right_animation > 0.01f) {
#ifdef VMD_ENABLE_PAF_NATIVE
        int native_scope = ui_native_vita_scope(UI_NATIVE_SCOPE_ACTIONS);
#endif
		/* Modal drawers cover the complete display edge, including the brand bar.
		 * This prevents the panel from reading as a detached card. */
		float panel_x = 960.0f - 430.0f * right_animation;
		ui_menu_surface(panel_x,0,430,544,1);
		vita2d_draw_rectangle(panel_x, 0, 1, 544, VT_THEME_BORDER);
		if (body) ui_font_draw_text_exact(body, (int)panel_x + 30, 47, UI_MENU_TEXT,
		                            UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_LOCAL_MEDIA_VIEW_TITLE));
		char storage_action[160], sort_action[160], navigation_action[160];
		snprintf(storage_action, sizeof(storage_action), "%s: %s",
		         vt_i18n_str(VT_STR_LOCAL_MEDIA_STORAGE_FILTER),
		         storage_filter_name(storage_filter));
		snprintf(sort_action, sizeof(sort_action), "%s: %s",
		         vt_i18n_str(VT_STR_LOCAL_MEDIA_SORT), sort_mode_name(sort_mode));
		snprintf(navigation_action, sizeof(navigation_action), "%s: %s",
		         vt_i18n_str(VT_STR_LOCAL_MEDIA_FOLDER_NAVIGATION),
		         vt_i18n_str(flattened ? VT_STR_LOCAL_MEDIA_FOLDER_FLATTENED
		                                : VT_STR_LOCAL_MEDIA_FOLDER_HIERARCHY));
		const char *actions[6];
		int action_count = 0;
		if (vt_ui_theme_legacy_layout(ui_theme_id()))
			actions[action_count++] = vt_i18n_str(grid_mode ? VT_STR_LOCAL_MEDIA_VIEW_LIST
			                                                : VT_STR_LOCAL_MEDIA_VIEW_GRID);
		/* Storage and sort operate on the folder catalogue. In flattened mode the
		 * indexed collection intentionally stays paged in on-disk order, so do not
		 * present controls that would imply an in-memory global sort. */
		if (!flattened) {
			actions[action_count++] = storage_action;
			actions[action_count++] = sort_action;
		}
		actions[action_count++] = navigation_action;
		actions[action_count++] = vt_i18n_str(VT_STR_LOCAL_MEDIA_BROWSE_FILES);
		actions[action_count++] = vt_i18n_str(VT_STR_LOCAL_MEDIA_REFRESH);
		if (flattened && screen_item(filter,selected,flattened,NULL) &&
            screen_item(filter,selected,flattened,NULL)->source==VT_LOCAL_MEDIA_SOURCE_FILE &&
            !screen_item(filter,selected,flattened,NULL)->is_directory)
			actions[action_count++] = vt_i18n_str(VT_STR_LOCAL_MEDIA_ACTIONS);
		for (int i = 0; i < action_count; i++) {
            int ay = local_option_y(i,flattened);
            int view_count=1+!!vt_ui_theme_legacy_layout(ui_theme_id())+(!flattened?2:0);
            int group=i<view_count?UI_MENU_GROUP_VIEW:i==view_count?UI_MENU_GROUP_LIBRARY:
                i==view_count+1?UI_MENU_GROUP_MAINTENANCE:UI_MENU_GROUP_LIBRARY;
            if(!i||i==view_count||i==view_count+1||i==view_count+2)
                ui_menu_category(panel_x+36,ay-UI_MENU_HEADING_HEIGHT,356,group);
            ui_action_button((int)panel_x + 22, ay+4, 384, 48, UI_MENU_BG,
			                 i + (!vt_ui_theme_legacy_layout(ui_theme_id())) < (flattened ? 2 : 4)
			                     ? "Left/Right"
								 : UI_PAD_CROSS,
			                 actions[i], 0);
		}
		if(right_open){UiFocusMotion focus={.x=panel_x+22,.y=local_option_y(right_focus,flattened),.width=384,.height=52,.initialized=1};ui_menu_focus_draw(&focus);}
#ifdef VMD_ENABLE_PAF_NATIVE
        ui_native_vita_scope(native_scope);
#endif
	}
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_local_media_screen(VtLocalMediaItem *selected_out) {
	/* This screen owns the media tabs, never the retired aggregate Home. */
	if (ui_home_tab() == HOME_LIBRARY)
		return UI_LOCAL_MEDIA_ACTION_SECTION_BASE + UI_SECTION_LOCAL_MEDIA;
	uint64_t navigation_started_us = sceKernelGetProcessTimeWide();
	vt_video_thumbnail_resume();
    if(!g_index_ready) {
        LocalMediaIndexHeader cached;
        if(local_index_load(&cached)==0){g_index=cached;g_index_ready=1;}
    }
    /* The first frame uses the in-memory/disk snapshot. A cold install shows
     * its Home while indexing; returning from podcast never opens a loader. */
    local_scan_start(g_catalog_dirty);
	build_root_catalog();
	int filter = ui_home_tab_filter(), selected = 0, top = 0;
	int storage_filter = 0, sort_mode = 0;
	int flattened = vt_preferences_folder_navigation_flattened();
	if (g_resume_actions) {
		filter = ui_home_tab_filter();
		selected = g_saved_selected;
		top = g_saved_top;
		storage_filter = g_saved_storage;
		sort_mode = g_saved_sort;
		g_resume_actions = 0;
	}
	rebuild_root_view(filter, storage_filter, sort_mode);
	int initial_count = screen_count(filter, flattened);
	if (selected >= initial_count) selected = initial_count > 0 ? initial_count - 1 : 0;
	if (top > selected) top = selected;
	log_printf("local media ready: entries=%d cache-first=%llu ms",
	           screen_count(filter, flattened),
	           (unsigned long long)((sceKernelGetProcessTimeWide() -
	                                navigation_started_us) / 1000ULL));
	int library_grid = 1;
	int image_grid = 1;
	int right_open = 0, right_cursor = 0, focus_tabs = 1;
	float right_animation = 0.0f, right_focus = 0.0f;
	UiFocusMotion right_motion={0};
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
			build_root_catalog();
			rebuild_root_view(filter, storage_filter, sort_mode);
			int refreshed_count = screen_count(filter, flattened);
			if (selected >= refreshed_count)
				selected = refreshed_count > 0 ? refreshed_count - 1 : 0;
			if (top > selected) top = selected;
		}
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch; unsigned flags = ui_touch_poll(&touch);
		if (!ui_mini_player_input_locked() &&
		    ui_brand_sections_hit(flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER;
			flags = UI_TOUCH_EVENT_NONE;
		}
		ui_home_tabs_set_expanded(1);
		int grid_mode = filter == 0 ? library_grid
		              : filter == 2 ? vt_preferences_local_music_grid()
		              : filter == 3 ? image_grid
		                            : vt_preferences_local_video_grid();
		if (!vt_ui_theme_legacy_layout(ui_theme_id()))
			grid_mode = 0;
		ui_mini_player_handle_buttons(&pressed);
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			flags = UI_TOUCH_EVENT_NONE;
			ctrl.buttons &= SCE_CTRL_SELECT;
			ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
		}
		if ((pressed & SCE_CTRL_LTRIGGER) &&
		    (right_open || right_animation > 0.01f)) {
			right_open = 0;
			right_animation = 0.0f;
		}
		if ((pressed & SCE_CTRL_RTRIGGER) &&
		    (sidebar.open || sidebar.animation > 0.01f)) {
			sidebar.open = 0;
			sidebar.animation = 0.0f;
		}
		int right_layer_was_visible = right_open || right_animation > 0.01f;
		int was_open = sidebar.open;
		int section = UI_SECTION_NONE;
		if (!right_layer_was_visible)
			section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
			                                             ctrl.buttons, ctrl.ly);
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
			float panel_x = 960.0f - 430.0f * right_animation;
			const VtLocalMediaItem *focused_item=screen_item(filter,selected,flattened,NULL);
            int file_selected=flattened && focused_item && focused_item->source==VT_LOCAL_MEDIA_SOURCE_FILE && !focused_item->is_directory;
			int themed_home = !vt_ui_theme_legacy_layout(ui_theme_id());
			int drawer_row_count = (flattened ? (file_selected ? 5 : 4) : 6)-themed_home;
			if (right_cursor >= drawer_row_count) right_cursor = drawer_row_count - 1;
			if (flags & UI_TOUCH_EVENT_TAP) {
				int touched_row = -1;
				for (int i = 0; i < drawer_row_count; i++) {
					if (ui_touch_hit_rect(touch.x, touch.y, (int)panel_x + 22,
					                      local_option_y(i,flattened), 384, 52)) {
						touched_row = i;
						break;
					}
				}
				if (touched_row >= 0) {
					right_cursor = touched_row;
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
			if ((drawer_nav & SCE_CTRL_DOWN) &&
			    right_cursor + 1 < drawer_row_count) right_cursor++;
			int action_cursor=right_cursor+themed_home;
			int drawer_action = flattened
			                  ? (action_cursor == 0 ? 0
			                     : action_cursor == 1 ? 3
			                     : action_cursor + 2)
			                  : action_cursor;
			if (drawer_action == 0 &&
			    (horizontal_step ||
			     (pressed & SCE_CTRL_CROSS))) {
				grid_mode = !grid_mode;
				if (filter == 0) library_grid = grid_mode;
				else if (filter == 2) vt_preferences_set_local_music_grid(grid_mode);
				else if (filter == 3) image_grid = grid_mode;
				else vt_preferences_set_local_video_grid(grid_mode);
				selected = top = 0;
			}
			if (drawer_action == 1 &&
			    (horizontal_step || (pressed & SCE_CTRL_CROSS))) {
				int direction = horizontal_step & SCE_CTRL_LEFT ? -1 : 1;
				storage_filter = (storage_filter + direction + 3) % 3;
				rebuild_root_view(filter, storage_filter, sort_mode);
				selected = top = 0;
			}
			if (drawer_action == 2 &&
			    (horizontal_step || (pressed & SCE_CTRL_CROSS))) {
				int direction = horizontal_step & SCE_CTRL_LEFT ? -1 : 1;
				sort_mode = (sort_mode + direction + 4) % 4;
				rebuild_root_view(filter, storage_filter, sort_mode);
				selected = top = 0;
			}
			if (drawer_action == 3 &&
			    (horizontal_step || (pressed & SCE_CTRL_CROSS))) {
				flattened = !flattened;
				vt_preferences_set_folder_navigation_flattened(flattened);
				/* Keep focus on the same semantic row after the drawer contracts or
				 * expands around the navigation choice. */
				right_cursor = flattened ? 1 : 3;
				selected = top = 0;
			}
			if ((pressed & SCE_CTRL_CROSS) && drawer_action == 4)
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_BROWSE_FILES);
			if ((pressed & SCE_CTRL_CROSS) && drawer_action == 5) {
				local_scan_start(1);
				right_open = 0;
			}
			if (file_selected && drawer_action == 6 && (pressed & SCE_CTRL_CROSS)) {
				const VtLocalMediaItem *item = screen_item(filter, selected, 1, NULL);
				if (item && selected_out) {
					*selected_out = *item;
					g_resume_actions = 1;
					g_saved_filter = filter;
					g_saved_selected = selected;
					g_saved_top = top;
					g_saved_storage = storage_filter;
					g_saved_sort = sort_mode;
					return local_media_leave(UI_LOCAL_MEDIA_ACTION_MANAGE);
				}
			}
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
			/* The drawer is modal: never let a tap fall through to a poster. */
			flags = 0;
		}
		if (!right_open) right_horizontal_latch = 0;
		tick_right_drawer(right_open, right_cursor, &right_animation,
		                  &right_focus, &right_motion_tick_us, &right_motion);
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
		unsigned int nav=0;int changed_tab=0;
  int tab_touch=flags&&ui_home_tabs_touch_hit(&touch);
  if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
	  UiHomeControlSample input = {.pressed = pressed,
			                       .held = ctrl.buttons,
			                       .touch_flags = flags,
			                       .lx = ctrl.lx,
			                       .ly = focus_tabs?ctrl.ly:128,
			                       .rx = ctrl.rx,
			                       .ry = ctrl.ry,
			                       .selected = selected,
			                       .count = screen_count(filter, flattened),
			                       .bottom = local_viewport_bottom(),
			                       .focus_sections = focus_tabs,
			                       .blocked = overlay_owns_input,
			                       .touch = &touch};
	  UiHomeControlResult result = ui_home_controls_update(&input);
	  pressed = result.pressed;
	  nav = result.content_nav;
	  if(input.focus_sections && !result.focus_sections)selected=0;
      focus_tabs = result.focus_sections;
	  changed_tab = result.sections_changed;
  } else {
	  nav = ui_nav_repeat_update(&page_repeat, pressed, overlay_owns_input ? 0 : ctrl.buttons,
			                     overlay_owns_input ? 128 : ctrl.lx,
			                     overlay_owns_input || !focus_tabs ? 128 : ctrl.ly,
			                     SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
	  unsigned tab_nav =
		  ui_home_tabs_navigation(pressed, focus_tabs && !overlay_owns_input ? ctrl.buttons : 0,
			                      focus_tabs && !overlay_owns_input ? ctrl.lx : 128, 128);
	  changed_tab = ui_home_tabs_input(tab_nav, flags, &touch, focus_tabs);
	  if (changed_tab)
		  focus_tabs = 1;
  }
  if(changed_tab){
   if(ui_home_tab()==HOME_LIBRARY)
    return local_media_leave(UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_LOCAL_MEDIA);
   if(ui_home_tab_online()){
    ui_online_audio_set_home(1);
    return local_media_leave(UI_LOCAL_MEDIA_ACTION_SECTION_BASE+UI_SECTION_ONLINE_AUDIO);
   }
   filter=ui_home_tab_filter();
  }
  if(tab_touch)flags=0;
		if (old_filter != filter) {
			rebuild_root_view(filter, storage_filter, sort_mode);
			selected = top = 0;
			/* The destination tab may remember a different layout. Recompute it
			 * in the same frame to avoid drawing or hit-testing one stale list/grid
			 * frame after a tab switch. */
			grid_mode = filter == 0 ? library_grid
			          : filter == 2 ? vt_preferences_local_music_grid()
			          : filter == 3 ? image_grid
			                        : vt_preferences_local_video_grid();
			if (!vt_ui_theme_legacy_layout(ui_theme_id()))
				grid_mode = 0;
		}
		int count = screen_count(filter, flattened);
		/* An empty result set has no page item that can own focus. Keep the
		 * selector on the filters instead of leaving an invisible focus target. */
		if (count == 0 && ui_theme_id()!=VT_UI_THEME_GHOST_MESH) focus_tabs = 1;
		if (vt_ui_theme_legacy_layout(ui_theme_id()) && !focus_tabs && (nav & SCE_CTRL_UP) &&
		    (selected < (grid_mode ? 3 : 1))) {
			focus_tabs = 1;
			nav &= ~SCE_CTRL_UP;
		}

		if (vt_ui_theme_legacy_layout(ui_theme_id()) && count > 0 && focus_tabs &&
		    (nav & SCE_CTRL_DOWN)) {
			focus_tabs = 0;selected=0;
			nav &= ~SCE_CTRL_DOWN;
		}
		if (!focus_tabs && grid_mode) {
			selected=ui_grid_navigation(selected,count,3,nav);
		} else if (!focus_tabs) {
			int columns = vt_ui_theme_legacy_layout(ui_theme_id()) ? 1 : HOME_LAYOUT.columns;
			selected=ui_grid_navigation(selected,count,columns,nav);
		}
		if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
			top = ui_theme_home_top(ui_theme_id(), selected, top, local_viewport_bottom());
		} else if (!grid_mode) {
			int visible = local_list_visible_rows();
			if (selected < top) top = selected;
			if (selected >= top + visible) top = selected - visible + 1;
		} else {
			top = grid_top_for_selection(selected,top);
		}

        if(!overlay_owns_input&&(flags&UI_TOUCH_EVENT_DOWN)&&touch.down_y>=LIST_Y&&
            touch.down_y<ui_theme_home_content_bottom(ui_theme_id(),local_viewport_bottom()))focus_tabs=0;
        if(!focus_tabs && !overlay_owns_input){
            home_page(filter,top,count,grid_mode,storage_filter,sort_mode,flattened);
            int width=vt_ui_theme_legacy_layout(ui_theme_id())?UI_CONTENT_W:
                ui_theme_id()==VT_UI_THEME_GHOST_MESH?HOME_LAYOUT.w:942-LIST_X;
            ui_theme_page_input(UI_SCENE_HOME,"local-home",LIST_X,LIST_Y,width,
                (vt_ui_theme_legacy_layout(ui_theme_id())?local_viewport_bottom():ui_theme_home_content_bottom(ui_theme_id(),local_viewport_bottom()))-LIST_Y,count,
                vt_ui_theme_legacy_layout(ui_theme_id())&&grid_mode,&top,&selected,
                ctrl.buttons,ctrl.lx,ctrl.ly,&flags,&touch);
        }

		if (!focus_tabs && (pressed & SCE_CTRL_CROSS) && count > 0) {
			const VtLocalMediaItem *item = screen_item(filter, selected, flattened, NULL);
			if(home_provider_section(item)!=UI_SECTION_NONE)
                return local_media_leave(UI_LOCAL_MEDIA_ACTION_SECTION_BASE+home_provider_section(item));
            if (item && selected_out) *selected_out = *item;
			if (item)
				return local_media_leave(screen_open_action(item));
		}
		if(pressed&SCE_CTRL_CIRCLE){
			if (!vt_ui_theme_legacy_layout(ui_theme_id()) && !focus_tabs) {
				focus_tabs = 1;
				pressed &= ~SCE_CTRL_CIRCLE;
			} else
				return local_media_leave(UI_LOCAL_MEDIA_ACTION_BACK);
  }
		if (flags & UI_TOUCH_EVENT_TAP) {
            UiPageView visible=home_page(filter,top,count,grid_mode,storage_filter,sort_mode,flattened);
            for(int pos=visible.first;pos<visible.end;pos++) {
                UiThemeRect hit_rect=home_item_rect(pos,top,grid_mode);
                int hit=touch.y>=LIST_Y && touch.y<local_viewport_bottom() &&
                    ui_touch_hit_rect(touch.x,touch.y,hit_rect.x,hit_rect.y,hit_rect.w,hit_rect.h);
				if (hit) {
					selected = pos;
					const VtLocalMediaItem *item = screen_item(filter, selected, flattened, NULL);
					if(home_provider_section(item)!=UI_SECTION_NONE)
                return local_media_leave(UI_LOCAL_MEDIA_ACTION_SECTION_BASE+home_provider_section(item));
            if (item && selected_out) *selected_out = *item;
					if (item)
						return local_media_leave(screen_open_action(item));
				}
			}
		}
		draw_screen(filter, selected, top, grid_mode, right_open, right_cursor,
		            right_animation, right_focus, focus_tabs,
		            storage_filter, sort_mode, flattened,
		            &focus_motion, &sidebar);
	}
}

int ui_local_media_library_tick(void) {
	if (!g_index_ready) {
		LocalMediaIndexHeader cached;
		if (!local_index_load(&cached)) {
			g_index = cached;
			g_index_ready = 1;
		}
	}
	if (g_catalog_dirty || !g_scan_started_us)
		local_scan_start(g_catalog_dirty);
	return local_scan_finish() > 0;
}
int ui_local_media_search(const char *query, UiLocalMediaVisitor visitor, void *ctx,
                          volatile int *cancel) {
	LocalMediaIndexHeader header = {0};
	int fd = sceIoOpen(LOCAL_INDEX_PATH, SCE_O_RDONLY, 0);
	if (fd < 0)
		return -1;
	int read = sceIoRead(fd, &header, sizeof(header));
	SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
	uint64_t grouped = (uint64_t)header.video_count + header.audio_count + header.image_count;
	uint64_t expected = sizeof(header) + (uint64_t)header.total_count * sizeof(VtLocalMediaItem);
	VtLocalMediaItem item;
	int result = 0;
	if (read != sizeof(header) || memcmp(header.magic, "VTLOCAL6", 8) || header.version != 8 ||
	    header.record_size != sizeof(item) || header.total_count > LOCAL_MAX_ITEMS ||
	    grouped != header.total_count || header.podcast_count > header.audio_count || size < 0 ||
	    (uint64_t)size != expected || sceIoLseek(fd, sizeof(header), SCE_SEEK_SET) < 0) {
		sceIoClose(fd);
		return -1;
	}
	VtLocalMediaItem batch[8];
	for (unsigned i = 0; i < header.total_count;) {
		if (cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE))
			break;
		unsigned count = header.total_count - i;
		if (count > 8)
			count = 8;
		unsigned bytes = count * sizeof(*batch);
		if (sceIoRead(fd, batch, bytes) != (int)bytes) {
			result = -1;
			break;
		}
		int stop = 0;
		for (unsigned k = 0; k < count; k++) {
			VtLocalMediaItem *record = &batch[k];
			if (cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE)) {
				stop = 1;
				break;
			}
			if (!memchr(record->path, 0, sizeof(record->path)) ||
			    !memchr(record->name, 0, sizeof(record->name)) ||
			    !memchr(record->artist, 0, sizeof(record->artist)) ||
			    !memchr(record->album, 0, sizeof(record->album)) ||
			    !memchr(record->artwork_path, 0, sizeof(record->artwork_path)))
				continue;
			VtLibraryItem v;
			vt_library_local(record, &v);
			if (vt_library_matches(&v, query) && !visitor(record, ctx)) {
				stop = 1;
				break;
			}
		}
		if (stop)
			break;
		i += count;
		/* Yield after 32 records (~50 KiB), including zero-match searches. */
		if (!(i % 32))
			sceKernelDelayThread(1000);
	}
	sceIoClose(fd);
	return result;
}

#include "settings/preferences.h"

#include <stdint.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include "app_paths.h"

#define VT_PREFERENCES_VERSION 2U
#define VT_PREFERENCES_PATH VITAWAVE_DATA_DIR "/settings.bin"
#define VT_PREFERENCES_TEMP VITAWAVE_DATA_DIR "/settings.tmp"
#define VT_PREFERENCES_BACKUP VITAWAVE_DATA_DIR "/settings.bak"

static const unsigned char g_preferences_magic[8] = {
	'V', 'T', 'S', 'E', 'T', '0', '0', '1'
};

typedef struct {
	unsigned char magic[8];
	uint32_t version;
	uint32_t record_size;
	uint32_t checksum;
} VtPreferencesHeader;

/* Format shipped before the default frame-rate field was added. Never change
 * this layout: existing settings.bin files depend on it for migration. */
typedef struct {
	VtPreferencesHeader header;
	int32_t default_quality;
	/* reserved[0] stores VT_LANGUAGE_* and reserved[1] stores the playback
	 * flags. Records written before those uses contain zero (AUTO, all flags
	 * disabled), so existing settings.bin files remain valid without a
	 * separate migration. */
	uint32_t reserved[2];
} VtPreferencesDiskV1;

typedef struct {
	VtPreferencesHeader header;
	int32_t default_quality;
	uint32_t reserved[2];
	int32_t default_frame_rate;
} VtPreferencesDiskV2;

_Static_assert(sizeof(VtPreferencesHeader) == 20,
	           "preferences header layout must remain fixed");
_Static_assert(sizeof(VtPreferencesDiskV1) == 32,
	           "preferences v1 layout must remain fixed");
_Static_assert(sizeof(VtPreferencesDiskV2) == 36,
	           "preferences v2 layout must remain fixed");

#define VT_PLAYBACK_FLAG_LOOP        (1u << 0)
#define VT_PLAYBACK_FLAG_FILL_SCREEN (1u << 1)
#define VT_NETWORK_FLAG_REMEMBER_PASSWORDS (1u << 2)
#define VT_UI_FLAG_REDUCE_MOTION      (1u << 3)
#define VT_PERFORMANCE_FLAG_PSVSHELL_CLOCKS   (1u << 5)
#define VT_UI_FLAG_PLAYER_DEBUG        (1u << 6)
/* Stored inverted so every settings.bin written before this option existed
 * preserves the historical, enabled watchdog/fallback behaviour. */
#define VT_PLAYBACK_FLAG_FALLBACK_DISABLED    (1u << 7)
#define VT_UI_FLAG_PLAYER_STATUS_ALWAYS        (1u << 8)
#define VT_DIAGNOSTICS_FLAG_DISK_LOGS           (1u << 9)
/* Inverted so existing zero-filled records get the new animated mini-player
 * by default, while users can explicitly request the old artwork-only mode. */
#define VT_UI_FLAG_STATIC_MINI_PLAYER            (1u << 10)

/* Six two-bit subtitle fields fit in the previously unused upper part of the
 * existing flags word, preserving the V2 record and every migration path. */
#define VT_SUBTITLE_OUTLINE_SHIFT 11u
#define VT_SUBTITLE_BORDER_SHIFT  13u
#define VT_SUBTITLE_TEXT_SHIFT    15u
#define VT_SUBTITLE_SIZE_SHIFT    17u
#define VT_SUBTITLE_WIDTH_SHIFT   19u
#define VT_SUBTITLE_POSITION_SHIFT 21u
#define VT_SUBTITLE_FIELD_MASK(shift) (3u << (shift))
#define VT_UI_FLAG_STARTUP_CONTROLS_SEEN (1u << 23)
#define VT_PLAYBACK_FLAG_SWAP_SHOULDERS    (1u << 24)
/* Stored inverted so upgraded installations open filesystem browsers in the
 * new grid view while still allowing users to select a compact list. */
#define VT_FILE_BROWSER_FLAG_LIST            (1u << 25)
#define VT_LOCAL_VIDEO_FLAG_GRID             (1u << 26)
#define VT_LOCAL_MUSIC_FLAG_GRID             (1u << 27)
/* Enabled by default (stored inverted) so music playback behaves like video
 * playback after upgrading from 1.0.0: the display stays awake unless the
 * user explicitly opts into the normal system timeout. */
#define VT_MUSIC_FLAG_ALLOW_DISPLAY_SLEEP     (1u << 28)
#define VT_UI_FLAG_MINI_PLAYER_EXPANDED        (1u << 29)
#define VT_VIDEO_DECODER_SHIFT                   30u
#define VT_VIDEO_DECODER_MASK                    (3u << VT_VIDEO_DECODER_SHIFT)

static int g_loaded;
static int g_default_quality = VT_DEFAULT_QUALITY_720;
static int g_default_frame_rate = VT_DEFAULT_FRAME_RATE_30;
static int g_language = VT_LANGUAGE_AUTO;
static uint32_t g_playback_flags = 0;

static int set_playback_flag(uint32_t bit, int enabled);

static int valid_quality(int height) {
	return height == VT_DEFAULT_QUALITY_360 ||
	       height == VT_DEFAULT_QUALITY_480 ||
	       height == VT_DEFAULT_QUALITY_720;
}

static int valid_frame_rate(int frame_rate) {
	return frame_rate == VT_DEFAULT_FRAME_RATE_24 ||
	       frame_rate == VT_DEFAULT_FRAME_RATE_25 ||
	       frame_rate == VT_DEFAULT_FRAME_RATE_27 ||
	       frame_rate == VT_DEFAULT_FRAME_RATE_30 ||
	       frame_rate == VT_DEFAULT_FRAME_RATE_50 ||
	       frame_rate == VT_DEFAULT_FRAME_RATE_60;
}

static int valid_language(int language) {
	return language >= VT_LANGUAGE_AUTO && language <= VT_LANGUAGE_RU;
}

static uint32_t crc32_bytes(const unsigned char *data, size_t len) {
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
			crc = (crc >> 1) ^ (0xEDB88320U & mask);
		}
	}
	return ~crc;
}

static uint32_t disk_v1_checksum(const VtPreferencesDiskV1 *disk) {
	VtPreferencesDiskV1 copy = *disk;
	copy.header.checksum = 0;
	return crc32_bytes((const unsigned char *)&copy, sizeof(copy));
}

static uint32_t disk_v2_checksum(const VtPreferencesDiskV2 *disk) {
	VtPreferencesDiskV2 copy = *disk;
	copy.header.checksum = 0;
	return crc32_bytes((const unsigned char *)&copy, sizeof(copy));
}

static int path_exists(const char *path) {
	SceIoStat stat;
	memset(&stat, 0, sizeof(stat));
	return sceIoGetstat(path, &stat) >= 0;
}

static int read_all(SceUID fd, void *data, size_t size) {
	unsigned char *cursor = (unsigned char *)data;
	size_t done = 0;
	while (done < size) {
		int ret = sceIoRead(fd, cursor + done, size - done);
		if (ret <= 0) return ret < 0 ? ret : -1;
		done += (size_t)ret;
	}
	return 0;
}

static int write_all(SceUID fd, const void *data, size_t size) {
	const unsigned char *cursor = (const unsigned char *)data;
	size_t done = 0;
	while (done < size) {
		int ret = sceIoWrite(fd, cursor + done, size - done);
		if (ret <= 0) return ret < 0 ? ret : -1;
		done += (size_t)ret;
	}
	return 0;
}

static int load_path(const char *path, int *quality, int *frame_rate,
	                 int *language, uint32_t *flags) {
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	VtPreferencesHeader header;
	int ret = read_all(fd, &header, sizeof(header));
	if (ret == 0 &&
	    memcmp(header.magic, g_preferences_magic, sizeof(header.magic)) != 0)
		ret = -2;
	if (ret == 0 && header.version == 1U &&
	    header.record_size == sizeof(VtPreferencesDiskV1)) {
		VtPreferencesDiskV1 disk;
		memset(&disk, 0, sizeof(disk));
		disk.header = header;
		ret = read_all(fd, (unsigned char *)&disk + sizeof(header),
		               sizeof(disk) - sizeof(header));
		if (ret == 0 &&
		    (disk.header.checksum != disk_v1_checksum(&disk) ||
		     !valid_quality(disk.default_quality))) ret = -2;
		if (ret == 0) {
			*quality = disk.default_quality;
			*frame_rate = VT_DEFAULT_FRAME_RATE_30;
			*language = valid_language((int)disk.reserved[0])
			          ? (int)disk.reserved[0] : VT_LANGUAGE_AUTO;
			*flags = disk.reserved[1];
		}
	} else if (ret == 0 && header.version == VT_PREFERENCES_VERSION &&
	           header.record_size == sizeof(VtPreferencesDiskV2)) {
		VtPreferencesDiskV2 disk;
		memset(&disk, 0, sizeof(disk));
		disk.header = header;
		ret = read_all(fd, (unsigned char *)&disk + sizeof(header),
		               sizeof(disk) - sizeof(header));
		if (ret == 0 &&
		    (disk.header.checksum != disk_v2_checksum(&disk) ||
		     !valid_quality(disk.default_quality) ||
		     !valid_frame_rate(disk.default_frame_rate))) ret = -2;
		if (ret == 0) {
			*quality = disk.default_quality;
			*frame_rate = disk.default_frame_rate;
			*language = valid_language((int)disk.reserved[0])
			          ? (int)disk.reserved[0] : VT_LANGUAGE_AUTO;
			*flags = disk.reserved[1];
		}
	} else if (ret == 0) {
		ret = -2;
	}
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	return ret;
}

static int persist_record(int height, int frame_rate, int language,
	                      uint32_t flags) {
	VtPreferencesDiskV2 disk;
	memset(&disk, 0, sizeof(disk));
	memcpy(disk.header.magic, g_preferences_magic, sizeof(disk.header.magic));
	disk.header.version = VT_PREFERENCES_VERSION;
	disk.header.record_size = sizeof(disk);
	disk.default_quality = height;
	disk.reserved[0] = (uint32_t)language;
	disk.reserved[1] = flags;
	disk.default_frame_rate = frame_rate;
	disk.header.checksum = disk_v2_checksum(&disk);

	sceIoMkdir(VITAWAVE_DATA_DIR, 0777);
	sceIoRemove(VT_PREFERENCES_TEMP);
	SceUID fd = sceIoOpen(VT_PREFERENCES_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int ret = write_all(fd, &disk, sizeof(disk));
	if (ret == 0) ret = sceIoSyncByFd(fd, 0);
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	if (ret < 0) {
		sceIoRemove(VT_PREFERENCES_TEMP);
		return ret;
	}

	/* Keep a recoverable copy until the new record is in place. Some Vita
	 * filesystems do not replace an existing target with sceIoRename(), so the
	 * POSIX rename-overwrite guarantee cannot be assumed. */
	sceIoRemove(VT_PREFERENCES_BACKUP);
	int had_previous = path_exists(VT_PREFERENCES_PATH);
	if (had_previous) {
		ret = sceIoRename(VT_PREFERENCES_PATH, VT_PREFERENCES_BACKUP);
		if (ret < 0) {
			sceIoRemove(VT_PREFERENCES_TEMP);
			return ret;
		}
	}
	ret = sceIoRename(VT_PREFERENCES_TEMP, VT_PREFERENCES_PATH);
	if (ret < 0) {
		if (had_previous) sceIoRename(VT_PREFERENCES_BACKUP, VT_PREFERENCES_PATH);
		sceIoRemove(VT_PREFERENCES_TEMP);
		return ret;
	}
	sceIoSync("ux0:", 0);
	if (had_previous) sceIoRemove(VT_PREFERENCES_BACKUP);
	return 0;
}

int vt_preferences_init(void) {
	g_default_quality = VT_DEFAULT_QUALITY_720;
	g_default_frame_rate = VT_DEFAULT_FRAME_RATE_30;
	g_language = VT_LANGUAGE_AUTO;
	g_playback_flags = 0;
	g_loaded = 1;
	int quality = 0, frame_rate = VT_DEFAULT_FRAME_RATE_30;
	int language = VT_LANGUAGE_AUTO;
	uint32_t flags = 0;
	int ret = load_path(VT_PREFERENCES_PATH, &quality, &frame_rate,
	                    &language, &flags);
	if (ret == 0) {
		g_default_quality = quality;
		g_default_frame_rate = frame_rate;
		g_language = language;
		g_playback_flags = flags;
		return 0;
	}
	/* Recover a commit interrupted between the two rename operations. */
	if (load_path(VT_PREFERENCES_BACKUP, &quality, &frame_rate,
	              &language, &flags) == 0) {
		g_default_quality = quality;
		g_default_frame_rate = frame_rate;
		g_language = language;
		g_playback_flags = flags;
		return 0;
	}
	return path_exists(VT_PREFERENCES_PATH) ? ret : 0;
}

int vt_preferences_default_quality(void) {
	if (!g_loaded) vt_preferences_init();
	return g_default_quality;
}

int vt_preferences_set_default_quality(int height) {
	if (!valid_quality(height)) return -1;
	if (!g_loaded) vt_preferences_init();
	if (height == g_default_quality) return 0;
	int ret = persist_record(height, g_default_frame_rate, g_language,
	                         g_playback_flags);
	if (ret == 0) g_default_quality = height;
	return ret;
}

int vt_preferences_default_frame_rate(void) {
	if (!g_loaded) vt_preferences_init();
	return g_default_frame_rate;
}

int vt_preferences_set_default_frame_rate(int frame_rate) {
	if (!valid_frame_rate(frame_rate)) return -1;
	if (!g_loaded) vt_preferences_init();
	if (frame_rate == g_default_frame_rate) return 0;
	int ret = persist_record(g_default_quality, frame_rate, g_language,
	                         g_playback_flags);
	if (ret == 0) g_default_frame_rate = frame_rate;
	return ret;
}

int vt_preferences_language(void) {
	if (!g_loaded) vt_preferences_init();
	return g_language;
}

int vt_preferences_set_language(int language) {
	if (!valid_language(language)) return -1;
	if (!g_loaded) vt_preferences_init();
	if (language == g_language) return 0;
	int ret = persist_record(g_default_quality, g_default_frame_rate, language,
	                         g_playback_flags);
	if (ret == 0) g_language = language;
	return ret;
}

int vt_preferences_startup_controls_seen(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_STARTUP_CONTROLS_SEEN) != 0;
}

int vt_preferences_set_startup_controls_seen(int seen) {
	return set_playback_flag(VT_UI_FLAG_STARTUP_CONTROLS_SEEN, seen);
}

int vt_preferences_player_swap_shoulders(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_PLAYBACK_FLAG_SWAP_SHOULDERS) != 0;
}

int vt_preferences_set_player_swap_shoulders(int enabled) {
	return set_playback_flag(VT_PLAYBACK_FLAG_SWAP_SHOULDERS, enabled);
}

int vt_preferences_file_browser_grid(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_FILE_BROWSER_FLAG_LIST) == 0;
}

int vt_preferences_set_file_browser_grid(int enabled) {
	return set_playback_flag(VT_FILE_BROWSER_FLAG_LIST, !enabled);
}

int vt_preferences_local_video_grid(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_LOCAL_VIDEO_FLAG_GRID) != 0;
}

int vt_preferences_set_local_video_grid(int enabled) {
	return set_playback_flag(VT_LOCAL_VIDEO_FLAG_GRID, enabled);
}

int vt_preferences_local_music_grid(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_LOCAL_MUSIC_FLAG_GRID) != 0;
}

int vt_preferences_set_local_music_grid(int enabled) {
	return set_playback_flag(VT_LOCAL_MUSIC_FLAG_GRID, enabled);
}

int vt_preferences_music_keep_display_awake(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_MUSIC_FLAG_ALLOW_DISPLAY_SLEEP) == 0;
}

int vt_preferences_set_music_keep_display_awake(int enabled) {
	return set_playback_flag(VT_MUSIC_FLAG_ALLOW_DISPLAY_SLEEP, !enabled);
}

int vt_preferences_video_decoder(void) {
	if (!g_loaded) vt_preferences_init();
	unsigned int decoder =
	    (g_playback_flags & VT_VIDEO_DECODER_MASK) >> VT_VIDEO_DECODER_SHIFT;
	return decoder <= VT_VIDEO_DECODER_SW_FFMPEG
	     ? (int)decoder : VT_VIDEO_DECODER_AUTO;
}

int vt_preferences_set_video_decoder(int decoder) {
	if (decoder < VT_VIDEO_DECODER_AUTO ||
	    decoder > VT_VIDEO_DECODER_SW_FFMPEG) return -1;
	if (!g_loaded) vt_preferences_init();
	uint32_t next = (g_playback_flags & ~VT_VIDEO_DECODER_MASK) |
	                ((uint32_t)decoder << VT_VIDEO_DECODER_SHIFT);
	if (next == g_playback_flags) return 0;
	int ret = persist_record(g_default_quality, g_default_frame_rate,
	                         g_language, next);
	if (ret == 0) g_playback_flags = next;
	return ret;
}

static int set_playback_flag(uint32_t bit, int enabled) {
	if (!g_loaded) vt_preferences_init();
	uint32_t next = enabled ? (g_playback_flags | bit) : (g_playback_flags & ~bit);
	if (next == g_playback_flags) return 0;
	int ret = persist_record(g_default_quality, g_default_frame_rate,
	                         g_language, next);
	if (ret == 0) g_playback_flags = next;
	return ret;
}

int vt_preferences_loop_enabled(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_PLAYBACK_FLAG_LOOP) != 0;
}

int vt_preferences_set_loop_enabled(int enabled) {
	return set_playback_flag(VT_PLAYBACK_FLAG_LOOP, enabled);
}

int vt_preferences_fill_screen(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_PLAYBACK_FLAG_FILL_SCREEN) != 0;
}

int vt_preferences_set_fill_screen(int enabled) {
	return set_playback_flag(VT_PLAYBACK_FLAG_FILL_SCREEN, enabled);
}

int vt_preferences_reduce_motion(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_REDUCE_MOTION) != 0;
}

int vt_preferences_set_reduce_motion(int enabled) {
	return set_playback_flag(VT_UI_FLAG_REDUCE_MOTION, enabled);
}

int vt_preferences_player_debug_enabled(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_PLAYER_DEBUG) != 0;
}

int vt_preferences_set_player_debug_enabled(int enabled) {
	return set_playback_flag(VT_UI_FLAG_PLAYER_DEBUG, enabled);
}

int vt_preferences_player_status_always_visible(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_PLAYER_STATUS_ALWAYS) != 0;
}

int vt_preferences_set_player_status_always_visible(int enabled) {
	return set_playback_flag(VT_UI_FLAG_PLAYER_STATUS_ALWAYS, enabled);
}

int vt_preferences_mini_player_animated(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_STATIC_MINI_PLAYER) == 0;
}

int vt_preferences_set_mini_player_animated(int enabled) {
	return set_playback_flag(VT_UI_FLAG_STATIC_MINI_PLAYER, !enabled);
}

int vt_preferences_mini_player_expanded_default(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_UI_FLAG_MINI_PLAYER_EXPANDED) != 0;
}

int vt_preferences_set_mini_player_expanded_default(int enabled) {
	return set_playback_flag(VT_UI_FLAG_MINI_PLAYER_EXPANDED, enabled);
}

int vt_preferences_disk_logs_enabled(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_DIAGNOSTICS_FLAG_DISK_LOGS) != 0;
}

int vt_preferences_set_disk_logs_enabled(int enabled) {
	return set_playback_flag(VT_DIAGNOSTICS_FLAG_DISK_LOGS, enabled);
}

int vt_preferences_remember_network_passwords(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_NETWORK_FLAG_REMEMBER_PASSWORDS) != 0;
}

int vt_preferences_set_remember_network_passwords(int enabled) {
	return set_playback_flag(VT_NETWORK_FLAG_REMEMBER_PASSWORDS, enabled);
}

int vt_preferences_stream_fallback_enabled(void) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags & VT_PLAYBACK_FLAG_FALLBACK_DISABLED) == 0;
}

int vt_preferences_set_stream_fallback_enabled(int enabled) {
	return set_playback_flag(VT_PLAYBACK_FLAG_FALLBACK_DISABLED, !enabled);
}

int vt_preferences_clock_source(void) {
	if (!g_loaded) vt_preferences_init();
	/* VitaWave's documented profile is the deterministic default.  PSVshell
	 * is deliberately an opt-in: when selected, the application performs no
	 * clock writes at all.  Bit 4 was used by one development build for the
	 * inverse representation; leaving it untouched preserves the rest of the
	 * record while bit 5 gives the released setting unambiguous semantics. */
	return (g_playback_flags & VT_PERFORMANCE_FLAG_PSVSHELL_CLOCKS)
	     ? VT_CLOCK_SOURCE_PSVSHELL : VT_CLOCK_SOURCE_APP;
}

int vt_preferences_set_clock_source(int source) {
	if (source != VT_CLOCK_SOURCE_APP &&
	    source != VT_CLOCK_SOURCE_PSVSHELL) return -1;
	return set_playback_flag(VT_PERFORMANCE_FLAG_PSVSHELL_CLOCKS,
	                         source == VT_CLOCK_SOURCE_PSVSHELL);
}

static unsigned int subtitle_field(unsigned int shift) {
	if (!g_loaded) vt_preferences_init();
	return (g_playback_flags >> shift) & 3u;
}

static int set_subtitle_field(unsigned int shift, unsigned int encoded) {
	if (!g_loaded) vt_preferences_init();
	uint32_t mask = VT_SUBTITLE_FIELD_MASK(shift);
	uint32_t next = (g_playback_flags & ~mask) | ((encoded & 3u) << shift);
	if (next == g_playback_flags) return 0;
	int ret = persist_record(g_default_quality, g_default_frame_rate,
	                         g_language, next);
	if (ret == 0) g_playback_flags = next;
	return ret;
}

int vt_preferences_subtitle_outline_thickness(void) {
	unsigned int encoded = subtitle_field(VT_SUBTITLE_OUTLINE_SHIFT);
	return encoded < 3u ? (int)encoded + 1 : VT_SUBTITLE_OUTLINE_1;
}

int vt_preferences_set_subtitle_outline_thickness(int thickness) {
	if (thickness < VT_SUBTITLE_OUTLINE_1 ||
	    thickness > VT_SUBTITLE_OUTLINE_3) return -1;
	return set_subtitle_field(VT_SUBTITLE_OUTLINE_SHIFT,
	                          (unsigned int)(thickness - 1));
}

int vt_preferences_subtitle_border_color(void) {
	return (int)subtitle_field(VT_SUBTITLE_BORDER_SHIFT);
}

int vt_preferences_set_subtitle_border_color(int color) {
	if (color < VT_SUBTITLE_BORDER_BLACK ||
	    color > VT_SUBTITLE_BORDER_YELLOW) return -1;
	return set_subtitle_field(VT_SUBTITLE_BORDER_SHIFT, (unsigned int)color);
}

int vt_preferences_subtitle_text_color(void) {
	return (int)subtitle_field(VT_SUBTITLE_TEXT_SHIFT);
}

int vt_preferences_set_subtitle_text_color(int color) {
	if (color < VT_SUBTITLE_TEXT_WHITE || color > VT_SUBTITLE_TEXT_GREEN)
		return -1;
	return set_subtitle_field(VT_SUBTITLE_TEXT_SHIFT, (unsigned int)color);
}

int vt_preferences_subtitle_size(void) {
	/* zero-filled V1/V2 records retain the historical medium 20 px font. */
	unsigned int encoded = subtitle_field(VT_SUBTITLE_SIZE_SHIFT);
	return encoded == 1u ? VT_SUBTITLE_SIZE_SMALL
	     : encoded == 2u ? VT_SUBTITLE_SIZE_LARGE
	                     : VT_SUBTITLE_SIZE_MEDIUM;
}

int vt_preferences_set_subtitle_size(int size) {
	unsigned int encoded;
	if (size == VT_SUBTITLE_SIZE_MEDIUM) encoded = 0;
	else if (size == VT_SUBTITLE_SIZE_SMALL) encoded = 1;
	else if (size == VT_SUBTITLE_SIZE_LARGE) encoded = 2;
	else return -1;
	return set_subtitle_field(VT_SUBTITLE_SIZE_SHIFT, encoded);
}

int vt_preferences_subtitle_max_width(void) {
	/* The previous hard-coded 840/960 width is the zero-bit default. */
	unsigned int encoded = subtitle_field(VT_SUBTITLE_WIDTH_SHIFT);
	return encoded == 1u ? VT_SUBTITLE_WIDTH_60
	     : encoded == 2u ? VT_SUBTITLE_WIDTH_75
	     : encoded == 3u ? VT_SUBTITLE_WIDTH_96
	                     : VT_SUBTITLE_WIDTH_88;
}

int vt_preferences_set_subtitle_max_width(int width) {
	unsigned int encoded;
	if (width == VT_SUBTITLE_WIDTH_88) encoded = 0;
	else if (width == VT_SUBTITLE_WIDTH_60) encoded = 1;
	else if (width == VT_SUBTITLE_WIDTH_75) encoded = 2;
	else if (width == VT_SUBTITLE_WIDTH_96) encoded = 3;
	else return -1;
	return set_subtitle_field(VT_SUBTITLE_WIDTH_SHIFT, encoded);
}

int vt_preferences_subtitle_position(void) {
	return (int)subtitle_field(VT_SUBTITLE_POSITION_SHIFT);
}

int vt_preferences_set_subtitle_position(int position) {
	if (position < VT_SUBTITLE_POSITION_BOTTOM ||
	    position > VT_SUBTITLE_POSITION_HIGH) return -1;
	return set_subtitle_field(VT_SUBTITLE_POSITION_SHIFT,
	                          (unsigned int)position);
}

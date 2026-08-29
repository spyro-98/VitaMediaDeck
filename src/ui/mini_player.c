#include "ui/mini_player.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/player_input_lock.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"

#define SCREEN_WIDTH 960
#define MINI_TITLE_X 72
#define MINI_TITLE_W 440
#define MINI_TITLE_MARQUEE_DELAY_US 1200000ULL
#define MINI_TITLE_MARQUEE_SPEED 34.0f
#define MINI_TITLE_MARQUEE_GAP 56.0f
#define MINI_SLIDE_DURATION_US 220000ULL
#define MINI_VIDEO_COMPACT_X 8.0f
#define MINI_VIDEO_COMPACT_W 56.0f
#define MINI_VIDEO_COMPACT_H 43.0f
#define MINI_VIDEO_EXPANDED_W ((float)SCREEN_WIDTH * 0.25f)
#define MINI_VIDEO_EXPANDED_MARGIN 8.0f
#define MINI_CONTROL_FEEDBACK_US 170000ULL
#define MINI_TRANSPORT_BACK_X 732.0f
#define MINI_TRANSPORT_PLAY_X 800.0f
#define MINI_TRANSPORT_FORWARD_X 868.0f
#define MINI_TRANSPORT_CLOSE_X 922.0f

enum {
	MINI_CONTROL_NONE = 0,
	MINI_CONTROL_BACK,
	MINI_CONTROL_PLAY,
	MINI_CONTROL_FORWARD,
	MINI_CONTROL_CLOSE
};

static vita2d_texture *g_artwork;
static char g_artwork_video_id[VT_BACKGROUND_MEDIA_ID_MAX];
static int g_artwork_attempted;
static PlayerInputLock g_mini_input_lock;
static uint64_t g_mini_lock_visible_until_us;
static float g_mini_lock_animation;
static uint64_t g_mini_lock_animation_last_us;
static char g_marquee_title[128];
static uint64_t g_marquee_started_us;
static VtBackgroundPlaybackSnapshot g_cached_snapshot;
static int g_cached_snapshot_valid;
static int g_mini_target_visible;
static float g_mini_animation;
static uint64_t g_mini_animation_last_us;
static int g_mini_video_expanded;
static float g_mini_video_expansion;
static char g_mini_layout_video_id[VT_BACKGROUND_MEDIA_ID_MAX];
static uint32_t g_mini_layout_activation_serial;
static int g_mini_control_feedback;
static uint64_t g_mini_control_feedback_until_us;

typedef struct {
	float x, y, width, height;
} MiniVideoRect;

static float mini_slide_eased(void) {
	float progress = g_mini_animation;
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	/* Smoothstep keeps both ends quiet while preserving the fixed 220 ms
	 * duration.  This is also the value used by hit-testing and page clipping. */
	return progress * progress * (3.0f - 2.0f * progress);
}

static MiniVideoRect mini_video_rect(const VtBackgroundPlaybackSnapshot *snapshot,
	                                  float bar_y, float expansion) {
	float aspect = 16.0f / 9.0f;
	if (snapshot && snapshot->video_width > 0 && snapshot->video_height > 0)
		aspect = (float)snapshot->video_width / (float)snapshot->video_height;
	else if (g_artwork) {
		int tw = vita2d_texture_get_width(g_artwork);
		int th = vita2d_texture_get_height(g_artwork);
		if (tw > 0 && th > 0) aspect = (float)tw / (float)th;
	}
	if (aspect < 0.2f) aspect = 0.2f;
	if (aspect > 5.0f) aspect = 5.0f;
	float expanded_h = MINI_VIDEO_EXPANDED_W / aspect;
	float available_h = bar_y - MINI_VIDEO_EXPANDED_MARGIN * 2.0f;
	if (available_h < MINI_VIDEO_COMPACT_H) available_h = MINI_VIDEO_COMPACT_H;
	if (expanded_h > available_h) expanded_h = available_h;
	MiniVideoRect compact = {
		MINI_VIDEO_COMPACT_X, bar_y + 8.0f,
		MINI_VIDEO_COMPACT_W, MINI_VIDEO_COMPACT_H
	};
	MiniVideoRect expanded = {
		MINI_VIDEO_EXPANDED_MARGIN,
		bar_y - expanded_h - MINI_VIDEO_EXPANDED_MARGIN,
		MINI_VIDEO_EXPANDED_W, expanded_h
	};
	MiniVideoRect result = {
		compact.x + (expanded.x - compact.x) * expansion,
		compact.y + (expanded.y - compact.y) * expansion,
		compact.width + (expanded.width - compact.width) * expansion,
		compact.height + (expanded.height - compact.height) * expansion
	};
	return result;
}

static int mini_video_hit(const MiniVideoRect *rect, int x, int y) {
	return rect && (float)x >= rect->x && (float)x <= rect->x + rect->width &&
	       (float)y >= rect->y && (float)y <= rect->y + rect->height;
}

static int ends_with_ci(const char *text, const char *suffix) {
	if (!text || !suffix) return 0;
	size_t text_len = strlen(text), suffix_len = strlen(suffix);
	if (suffix_len > text_len) return 0;
	const char *tail = text + text_len - suffix_len;
	for (size_t i = 0; i < suffix_len; i++)
		if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i]))
			return 0;
	return 1;
}

static void artwork_start(const VtBackgroundPlaybackSnapshot *snapshot) {
	if (!snapshot || !snapshot->thumbnail_url[0]) return;
	/* Artwork is intentionally file-only. The local mini-player must never
	 * create an implicit network request behind the current screen. */
	g_artwork = ends_with_ci(snapshot->thumbnail_url, ".png")
	          ? vita2d_load_PNG_file(snapshot->thumbnail_url)
	          : vita2d_load_JPEG_file(snapshot->thumbnail_url);
}

static void artwork_pump(const VtBackgroundPlaybackSnapshot *snapshot) {
	if (!snapshot) return;
	if (strcmp(g_artwork_video_id, snapshot->video_id) != 0) {
		if (g_artwork) vita2d_free_texture(g_artwork);
		g_artwork = NULL;
		g_artwork_attempted = 0;
		snprintf(g_artwork_video_id, sizeof(g_artwork_video_id), "%s",
		         snapshot->video_id);
	}
	if (!g_artwork && !g_artwork_attempted && snapshot->thumbnail_url[0]) {
		g_artwork_attempted = 1;
		artwork_start(snapshot);
	}
}

static void draw_artwork_cover(const vita2d_texture *texture,
	                           float x, float y, float width, float height) {
	if (!texture) return;
	float tw = (float)vita2d_texture_get_width(texture);
	float th = (float)vita2d_texture_get_height(texture);
	if (tw <= 0.0f || th <= 0.0f) return;
	float scale = width / tw;
	if (th * scale < height) scale = height / th;
	float draw_w = tw * scale, draw_h = th * scale;
	vita2d_set_clip_rectangle((int)x, (int)y, (int)(x + width), (int)(y + height));
	vita2d_enable_clipping();
	vita2d_draw_texture_scale(texture, x + (width - draw_w) * 0.5f,
	                          y + (height - draw_h) * 0.5f, scale, scale);
	vita2d_disable_clipping();
}

static void clipped(vita2d_font *font, unsigned size, const char *source,
	                char out[128], int max_width) {
	if (!font || !source) { out[0] = '\0'; return; }
	ui_font_fit_text(font, size, source, out, 128, max_width);
}

static void draw_solid_triangle(float center_x, float center_y, float height,
	                            int points_right, unsigned int color) {
	int rows = (int)height;
	float top = center_y - height * 0.5f;
	float max_width = 3.0f + (float)((rows + 1) / 2) * 1.38f;
	for (int row = 0; row < rows; row++) {
		float half = row < rows / 2 ? (float)row + 1.0f
		                              : (float)(rows - row);
		float width = 3.0f + half * 1.38f;
		float left = points_right ? center_x - max_width * 0.5f
		                          : center_x + max_width * 0.5f - width;
		vita2d_draw_rectangle(left, top + (float)row, width, 1.25f, color);
	}
}

static void draw_title_marquee(vita2d_font *font, const char *title,
	                           int bar_y, uint64_t now_us) {
	if (!font || !title || !title[0]) return;
	if (strncmp(g_marquee_title, title, sizeof(g_marquee_title) - 1) != 0) {
		snprintf(g_marquee_title, sizeof(g_marquee_title), "%s", title);
		g_marquee_started_us = now_us;
	}
	float title_w = ui_font_text_width(font, UI_FONT_BODY, title);
	vita2d_set_clip_rectangle(MINI_TITLE_X, bar_y + 5,
	                          MINI_TITLE_X + MINI_TITLE_W, bar_y + 34);
	vita2d_enable_clipping();
	if (title_w <= (float)MINI_TITLE_W) {
		ui_font_draw_text(font, MINI_TITLE_X, bar_y + 27,
		                      VT_THEME_TEXT, UI_FONT_BODY, title);
	} else if (vt_preferences_reduce_motion()) {
		char fitted[128];
		ui_font_fit_text(font, UI_FONT_BODY, title, fitted, sizeof(fitted),
		                 MINI_TITLE_W);
		ui_font_draw_text(font, MINI_TITLE_X, bar_y + 27,
		                  VT_THEME_TEXT, UI_FONT_BODY, fitted);
	} else {
		uint64_t elapsed_us = now_us - g_marquee_started_us;
		float offset = 0.0f;
		if (elapsed_us > MINI_TITLE_MARQUEE_DELAY_US) {
			float moving_seconds =
			    (float)(elapsed_us - MINI_TITLE_MARQUEE_DELAY_US) / 1000000.0f;
			offset = fmodf(moving_seconds * MINI_TITLE_MARQUEE_SPEED,
			               title_w + MINI_TITLE_MARQUEE_GAP);
		}
		float first_x = (float)MINI_TITLE_X - offset;
		ui_font_draw_text(font, first_x, bar_y + 27,
		                      VT_THEME_TEXT, UI_FONT_BODY, title);
		ui_font_draw_text(font,
		                      first_x + title_w + MINI_TITLE_MARQUEE_GAP,
		                      bar_y + 27, VT_THEME_TEXT, UI_FONT_BODY, title);
	}
	vita2d_disable_clipping();
}

static void format_time(uint64_t ms, char out[24]) {
	uint64_t total = ms / 1000ULL;
	uint64_t hours = total / 3600ULL;
	uint64_t minutes = (total / 60ULL) % 60ULL;
	uint64_t seconds = total % 60ULL;
	if (hours > 0)
		snprintf(out, 24, vt_i18n_str(VT_STR_MINI_TIME_HMS),
		         (unsigned long long)hours, (unsigned long long)minutes,
		         (unsigned long long)seconds);
	else
		snprintf(out, 24, vt_i18n_str(VT_STR_MINI_TIME_HM), (unsigned long long)minutes,
		         (unsigned long long)seconds);
}

void ui_mini_player_draw(void) {
	VtBackgroundPlaybackSnapshot snapshot;
	int active = vt_background_playback_snapshot(&snapshot) != 0;
	if (active) {
		g_cached_snapshot = snapshot;
		g_cached_snapshot_valid = 1;
		vt_display_keep_awake_tick();
	} else if (g_cached_snapshot_valid && g_mini_animation > 0.001f) {
		snapshot = g_cached_snapshot;
	} else {
		return;
	}
	uint64_t now_us = sceKernelGetProcessTimeWide();
	uint64_t lock_delta_us = now_us >= g_mini_lock_animation_last_us
	                       ? now_us - g_mini_lock_animation_last_us : 0;
	if (!g_mini_lock_animation_last_us) lock_delta_us = 0;
	if (lock_delta_us > 50000ULL) lock_delta_us = 50000ULL;
	g_mini_lock_animation_last_us = now_us;
	int lock_visible = g_mini_input_lock.locked &&
	                   now_us < g_mini_lock_visible_until_us;
	if (vt_preferences_reduce_motion())
		g_mini_lock_animation = lock_visible ? 1.0f : 0.0f;
	else {
		float blend = 1.0f - expf(-18.0f *
		                            ((float)lock_delta_us / 1000000.0f));
		g_mini_lock_animation += ((lock_visible ? 1.0f : 0.0f) -
		                          g_mini_lock_animation) * blend;
	}
	if (!lock_visible && g_mini_lock_animation < 0.01f)
		g_mini_lock_animation = 0.0f;
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	const int y = ui_mini_player_top();

	/* A docked signal strip: black glass, warm acquisition rail, sparse packet
	 * marks. It remains the same height so every page keeps its viewport math. */
	vita2d_draw_rectangle(0, y - 4, SCREEN_WIDTH, 4, RGBA8(0, 0, 0, 112));
	vita2d_draw_rectangle(0, y, SCREEN_WIDTH, UI_MINI_PLAYER_HEIGHT,
	                      RGBA8(8, 8, 8, 250));
	vita2d_draw_rectangle(0, y, SCREEN_WIDTH, 2, VT_THEME_SIGNAL_BRIGHT);
	vita2d_draw_rectangle(0, y + 2, SCREEN_WIDTH, 1,
	                      VT_THEME_PARTICLE_A(120));
	for (int packet = 0; packet < 10; packet++)
		vita2d_draw_rectangle(506.0f + packet * 13.0f, y + 8.0f,
		                      packet == 4 ? 7.0f : 2.0f, 2.0f,
		                      packet == 4 ? VT_THEME_SIGNAL_LIGHT
		                                  : VT_THEME_SIGNAL_DIM);

	MiniVideoRect video_rect = mini_video_rect(
	    &snapshot, (float)y, g_mini_video_expansion);
	if (g_mini_video_expansion > 0.01f) {
		vita2d_draw_rectangle(video_rect.x - 5.0f, video_rect.y - 5.0f,
		                      video_rect.width + 10.0f, video_rect.height + 10.0f,
		                      RGBA8(0, 0, 0, 118));
		vita2d_draw_rectangle(video_rect.x - 2.0f, video_rect.y - 2.0f,
		                      video_rect.width + 4.0f, video_rect.height + 4.0f,
		                      VT_THEME_SIGNAL_BRIGHT);
	}
	int expects_visual = (snapshot.video_width && snapshot.video_height) || g_artwork;
	if (expects_visual)
		vita2d_draw_rectangle(video_rect.x, video_rect.y,
		                      video_rect.width, video_rect.height,
		                      RGBA8(0, 0, 0, 255));
	int drew_live_video = snapshot.video_width && snapshot.video_height
	                    ? vt_background_playback_draw_video(
	                          video_rect.x, video_rect.y,
	                          video_rect.width, video_rect.height)
	                    : 0;
	if (drew_live_video) {
		vita2d_draw_rectangle(video_rect.x,
		                      video_rect.y + video_rect.height - 2.0f,
		                      video_rect.width, 2.0f, VT_THEME_SIGNAL_LIGHT);
	} else if (g_artwork) {
		draw_artwork_cover(g_artwork, video_rect.x, video_rect.y,
		                   video_rect.width, video_rect.height);
		vita2d_draw_rectangle(video_rect.x,
		                      video_rect.y + video_rect.height - 2.0f,
		                      video_rect.width, 2.0f, VT_THEME_SIGNAL_LIGHT);
	}

	const char *title = snapshot.title[0] ? snapshot.title
	                                      : vt_i18n_str(VT_STR_MINI_DEFAULT_TITLE);
	char channel[128];
	clipped(small, UI_FONT_SMALL, snapshot.channel, channel, 390);
	if (expects_visual) {
		draw_title_marquee(body, title, y, now_us);
		if (small) ui_font_draw_text(small, 72, y + 51,
		                                VT_THEME_TEXT_MUTED, UI_FONT_SMALL,
		                                channel);
	} else if (body) {
		char centered[192];
		char fitted[128];
		clipped(body, UI_FONT_BODY, title, fitted, 490);
		snprintf(centered, sizeof(centered), "%s", fitted);
		int width = ui_font_text_width(body, UI_FONT_BODY, centered);
		ui_font_draw_text(body, 24 + (504 - width) / 2, y + 31,
		                  VT_THEME_TEXT, UI_FONT_BODY, centered);
		if (small && channel[0]) {
			int channel_width = ui_font_text_width(small, UI_FONT_SMALL, channel);
			ui_font_draw_text(small, 24 + (504 - channel_width) / 2, y + 53,
			                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, channel);
		}
	}

	const char *state = snapshot.state == VT_BACKGROUND_PREPARING
	                  ? vt_i18n_str(VT_STR_MINI_STATE_PREPARING)
	                  : snapshot.state == VT_BACKGROUND_BUFFERING
	                      ? vt_i18n_str(VT_STR_MINI_STATE_BUFFERING)
	                      : snapshot.state == VT_BACKGROUND_READY
	                          ? vt_i18n_str(VT_STR_MINI_STATE_READY)
	                          : snapshot.state == VT_BACKGROUND_PAUSED
	                              ? vt_i18n_str(VT_STR_MINI_STATE_PAUSED)
	                              : snapshot.state == VT_BACKGROUND_ERROR
	                                  ? vt_i18n_str(VT_STR_MINI_STATE_ERROR)
	                                  : vt_i18n_str(VT_STR_MINI_STATE_PLAYING);
	if (small) ui_font_draw_text(small, 552, y + 26,
	                                snapshot.state == VT_BACKGROUND_ERROR
	                                    ? VT_THEME_DANGER
		                                : VT_THEME_COLD_LIGHT,
	                                UI_FONT_SMALL, state);

	char elapsed[24], duration[24], timing[56];
	format_time(snapshot.position_ms, elapsed);
	format_time(snapshot.duration_ms, duration);
	snprintf(timing, sizeof(timing), vt_i18n_str(VT_STR_MINI_TIMING_FORMAT), elapsed, duration);
	if (small) ui_font_draw_text(small, 552, y + 50,
	                                VT_THEME_TEXT_MUTED, UI_FONT_SMALL,
	                                timing);

	/* Compact transport uses square acquisition cells instead of floating
	 * circles; hit targets stay unchanged for touch and controller input. */
	const float transport_x[4] = {
		MINI_TRANSPORT_BACK_X, MINI_TRANSPORT_PLAY_X,
		MINI_TRANSPORT_FORWARD_X, MINI_TRANSPORT_CLOSE_X
	};
	unsigned transport_fill[3];
	unsigned transport_foreground[3];
	for (int i = 0; i < 3; i++) {
		transport_fill[i] = g_mini_control_feedback == i + 1 &&
		                    now_us < g_mini_control_feedback_until_us
		                  ? VT_THEME_SIGNAL_LIGHT : VT_THEME_SIGNAL;
		transport_foreground[i] = ui_contrast_bw(transport_fill[i]);
		vita2d_draw_rectangle(transport_x[i] - 21.0f, y + 12.0f,
		                      42.0f, 42.0f, transport_fill[i]);
		vita2d_draw_rectangle(transport_x[i] - 17.0f, y + 16.0f,
		                      34.0f, 1.0f, RGBA8(255, 236, 198, 92));
	}
	if (small) {
		const char *back = "<", *forward = ">";
		int back_w = ui_font_text_width(small, UI_FONT_SMALL, back);
		int forward_w = ui_font_text_width(small, UI_FONT_SMALL, forward);
		ui_font_draw_text(small, (int)transport_x[0] - back_w / 2, y + 40,
		                  transport_foreground[0], UI_FONT_SMALL, back);
		ui_font_draw_text(small, (int)transport_x[2] - forward_w / 2, y + 40,
		                  transport_foreground[2], UI_FONT_SMALL, forward);
	}
	if (snapshot.state == VT_BACKGROUND_PREPARING ||
	    snapshot.state == VT_BACKGROUND_BUFFERING) {
		ui_draw_spinner_compact(MINI_TRANSPORT_PLAY_X, y + 33.0f, now_us);
	} else if (snapshot.state == VT_BACKGROUND_ERROR) {
		vita2d_draw_rectangle(797, y + 22, 6, 16, transport_foreground[1]);
		vita2d_draw_fill_circle(800, y + 43, 3, transport_foreground[1]);
	} else if (snapshot.state == VT_BACKGROUND_PAUSED) {
		draw_solid_triangle(MINI_TRANSPORT_PLAY_X, y + 33.0f, 21.0f, 1,
		                    transport_foreground[1]);
	} else {
		vita2d_draw_rectangle(792, y + 22, 5, 22, transport_foreground[1]);
		vita2d_draw_rectangle(803, y + 22, 5, 22, transport_foreground[1]);
	}
	unsigned close_fill = g_mini_control_feedback == MINI_CONTROL_CLOSE &&
	                      now_us < g_mini_control_feedback_until_us
	                    ? VT_THEME_DANGER : RGBA8(54, 31, 48, 255);
	vita2d_draw_rectangle(MINI_TRANSPORT_CLOSE_X - 21.0f, y + 12.0f,
	                      42.0f, 42.0f, close_fill);
	vita2d_draw_line(MINI_TRANSPORT_CLOSE_X - 7, y + 26,
	                 MINI_TRANSPORT_CLOSE_X + 7, y + 40,
	                 RGBA8(255, 165, 177, 255));
	vita2d_draw_line(MINI_TRANSPORT_CLOSE_X + 7, y + 26,
	                 MINI_TRANSPORT_CLOSE_X - 7, y + 40,
	                 RGBA8(255, 165, 177, 255));
	if (g_mini_lock_animation > 0.01f) {
		unsigned int lock_color = VT_THEME_COLD_A(
		    (unsigned int)(255.0f * g_mini_lock_animation));
		vita2d_draw_rectangle(682, y + 26, 16, 14, lock_color);
		vita2d_draw_line(686, y + 26, 686, y + 20, lock_color);
		vita2d_draw_line(694, y + 26, 694, y + 20, lock_color);
		vita2d_draw_line(686, y + 20, 694, y + 20, lock_color);
	}

	float progress = snapshot.duration_ms > 0
	               ? (float)snapshot.position_ms / (float)snapshot.duration_ms
	               : 0.0f;
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	vita2d_draw_rectangle(0, y + UI_MINI_PLAYER_HEIGHT - 3, SCREEN_WIDTH, 3,
	                      VT_THEME_BORDER);
	vita2d_draw_rectangle(0, y + UI_MINI_PLAYER_HEIGHT - 3,
	                      SCREEN_WIDTH * progress, 3,
	                      VT_THEME_SIGNAL_LIGHT);
}

void ui_mini_player_pump(void) {
	/* Every page calls pump before beginning the next vita2d frame, therefore
	 * the preceding wait_rendering_done has already retired the AvPlayer surface
	 * submitted by draw(). */
	vt_background_playback_video_render_complete();
	VtBackgroundPlaybackSnapshot snapshot;
	int active = vt_background_playback_snapshot(&snapshot) != 0;
	uint64_t now_us = sceKernelGetProcessTimeWide();
	if (active) {
		if (strcmp(g_mini_layout_video_id, snapshot.video_id) != 0 ||
		    g_mini_layout_activation_serial != snapshot.activation_serial) {
			snprintf(g_mini_layout_video_id, sizeof(g_mini_layout_video_id), "%s",
			         snapshot.video_id);
			g_mini_layout_activation_serial = snapshot.activation_serial;
			g_mini_video_expanded =
			    vt_preferences_mini_player_expanded_default();
			g_mini_video_expansion = g_mini_video_expanded ? 1.0f : 0.0f;
			g_mini_control_feedback = MINI_CONTROL_NONE;
			g_mini_control_feedback_until_us = 0;
		}
		g_cached_snapshot = snapshot;
		g_cached_snapshot_valid = 1;
		artwork_pump(&snapshot);
	} else {
		g_mini_lock_visible_until_us = 0;
		g_mini_lock_animation = 0.0f;
	}
	g_mini_target_visible = active;
	if (!g_mini_animation_last_us) g_mini_animation_last_us = now_us;
	uint64_t delta_us = now_us - g_mini_animation_last_us;
	if (delta_us > 50000ULL) delta_us = 50000ULL;
	g_mini_animation_last_us = now_us;
	float target = active ? 1.0f : 0.0f;
	float video_target = active && g_mini_video_expanded ? 1.0f : 0.0f;
	if (vt_preferences_reduce_motion()) {
		g_mini_animation = target;
		g_mini_video_expansion = video_target;
	} else {
		float step = (float)delta_us / (float)MINI_SLIDE_DURATION_US;
		if (g_mini_animation < target) {
			g_mini_animation += step;
			if (g_mini_animation > target) g_mini_animation = target;
		} else if (g_mini_animation > target) {
			g_mini_animation -= step;
			if (g_mini_animation < target) g_mini_animation = target;
		}
		float video_step = (float)delta_us / 180000.0f;
		if (g_mini_video_expansion < video_target) {
			g_mini_video_expansion += video_step;
			if (g_mini_video_expansion > video_target)
				g_mini_video_expansion = video_target;
		} else if (g_mini_video_expansion > video_target) {
			g_mini_video_expansion -= video_step;
			if (g_mini_video_expansion < video_target)
				g_mini_video_expansion = video_target;
		}
	}
	if (!active && g_mini_animation <= 0.001f) {
		g_mini_animation = 0.0f;
		g_cached_snapshot_valid = 0;
		g_marquee_title[0] = '\0';
		g_marquee_started_us = 0;
		if (g_artwork) {
			vita2d_free_texture(g_artwork);
			g_artwork = NULL;
		}
		g_artwork_video_id[0] = '\0';
		g_artwork_attempted = 0;
	}
}

int ui_mini_player_visible(void) {
	VtBackgroundPlaybackSnapshot snapshot;
	return vt_background_playback_snapshot(&snapshot) != 0 ||
	       g_mini_target_visible || g_mini_animation > 0.001f;
}

int ui_mini_player_top(void) {
	return 544 - (int)((float)UI_MINI_PLAYER_HEIGHT * mini_slide_eased() + 0.5f);
}

int ui_mini_player_input_locked(void) {
	VtBackgroundPlaybackSnapshot snapshot;
	return vt_background_playback_snapshot(&snapshot) && g_mini_input_lock.locked;
}

int ui_mini_player_handle_buttons(unsigned int *pressed) {
	VtBackgroundPlaybackSnapshot snapshot;
	if (!pressed || !vt_background_playback_snapshot(&snapshot)) {
		memset(&g_mini_input_lock, 0, sizeof(g_mini_input_lock));
		g_mini_lock_visible_until_us = 0;
		g_mini_lock_animation = 0.0f;
		/* The last 220 ms still cover the page visually. Consume controller
		 * input until the bar has fully left, matching drawer modal ownership. */
		if (pressed && g_mini_animation > 0.001f) {
			*pressed = 0;
			return 1;
		}
		return 0;
	}
	SceCtrlData current;
	memset(&current, 0, sizeof(current));
	sceCtrlPeekBufferPositive(0, &current, 1);
	uint64_t now = sceKernelGetProcessTimeWide();
	unsigned int attempted_press = *pressed;
	int lock_changed = player_input_lock_update(&g_mini_input_lock,
	                                            current.buttons, now);
	if (lock_changed)
		g_mini_lock_visible_until_us = g_mini_input_lock.locked
		                                 ? now + PLAYER_INPUT_LOCK_FEEDBACK_US : 0;
	if (g_mini_input_lock.locked && attempted_press)
		g_mini_lock_visible_until_us = now + PLAYER_INPUT_LOCK_FEEDBACK_US;
	/* SELECT belongs exclusively to the mini-player lock while it is visible.
	 * Once locked, no command reaches the underlying page. */
	*pressed &= ~SCE_CTRL_SELECT;
	if (g_mini_input_lock.locked) {
		*pressed = 0;
		return 1;
	}
	if (!(*pressed & SCE_CTRL_START)) return 0;
	*pressed &= ~SCE_CTRL_START;
	/* Never turn START into stop: if no fullscreen callback was registered,
	 * the local background session remains alive. */
	vt_background_playback_video_render_complete();
	vt_background_playback_resume_fullscreen();
	return 1;
}

int ui_mini_player_handle_touch(unsigned int touch_flags,
	                            const UiTouchEvent *touch) {
	VtBackgroundPlaybackSnapshot snapshot;
	if (!touch) return 0;
	int active = vt_background_playback_snapshot(&snapshot) != 0;
	if (!active) {
		if (!g_cached_snapshot_valid || g_mini_animation <= 0.001f) return 0;
		snapshot = g_cached_snapshot;
	}
	int bar_y = ui_mini_player_top();
	MiniVideoRect video_rect = mini_video_rect(
	    &snapshot, (float)bar_y, g_mini_video_expansion);
	int began_in_bar = touch->down_y >= bar_y;
	int now_in_bar = touch->y >= bar_y;
	int began_in_video = mini_video_hit(&video_rect, touch->down_x, touch->down_y);
	int now_in_video = mini_video_hit(&video_rect, touch->x, touch->y);
	if (g_mini_input_lock.locked) return 1;
	if (!began_in_bar && !now_in_bar && !began_in_video && !now_in_video) return 0;
	if (!active) return 1;
	if ((touch_flags & UI_TOUCH_EVENT_TAP) && began_in_video && now_in_video) {
		g_mini_video_expanded = !g_mini_video_expanded;
		return 1;
	}
	if ((touch_flags & UI_TOUCH_EVENT_TAP) && began_in_bar && now_in_bar) {
		int expects_visual = (snapshot.video_width && snapshot.video_height) ||
		                     g_artwork;
		int title_x = expects_visual ? MINI_TITLE_X : 24;
		int title_w = expects_visual ? MINI_TITLE_W : 504;
		if (touch->down_x >= title_x &&
		    touch->down_x < title_x + title_w &&
		    touch->down_y < bar_y + 56) {
			vt_background_playback_video_render_complete();
			vt_background_playback_resume_fullscreen();
		}
		else if (ui_touch_hit_rect(
		             touch->x, touch->y, (int)MINI_TRANSPORT_CLOSE_X - 26,
		             bar_y + 7, 52, 52)) {
			g_mini_control_feedback = MINI_CONTROL_CLOSE;
			g_mini_control_feedback_until_us =
			    sceKernelGetProcessTimeWide() + MINI_CONTROL_FEEDBACK_US;
			vt_background_playback_video_render_complete();
			vt_background_playback_request_stop();
		}
		else if (ui_touch_hit_rect(
		             touch->x, touch->y, (int)MINI_TRANSPORT_FORWARD_X - 26,
		             bar_y + 7, 52, 52) && snapshot.duration_ms &&
		         snapshot.state != VT_BACKGROUND_ERROR) {
			g_mini_control_feedback = MINI_CONTROL_FORWARD;
			g_mini_control_feedback_until_us =
			    sceKernelGetProcessTimeWide() + MINI_CONTROL_FEEDBACK_US;
			vt_background_playback_seek_relative(10000);
		}
		else if (ui_touch_hit_rect(
		             touch->x, touch->y, (int)MINI_TRANSPORT_PLAY_X - 26,
		             bar_y + 7, 52, 52) &&
		         snapshot.state != VT_BACKGROUND_ERROR) {
			g_mini_control_feedback = MINI_CONTROL_PLAY;
			g_mini_control_feedback_until_us =
			    sceKernelGetProcessTimeWide() + MINI_CONTROL_FEEDBACK_US;
			vt_background_playback_toggle_pause();
		}
		else if (ui_touch_hit_rect(
		             touch->x, touch->y, (int)MINI_TRANSPORT_BACK_X - 26,
		             bar_y + 7, 52, 52) && snapshot.duration_ms &&
		         snapshot.state != VT_BACKGROUND_ERROR) {
			g_mini_control_feedback = MINI_CONTROL_BACK;
			g_mini_control_feedback_until_us =
			    sceKernelGetProcessTimeWide() + MINI_CONTROL_FEEDBACK_US;
			vt_background_playback_seek_relative(-10000);
		}
	}
	return 1;
}

void ui_mini_player_shutdown(void) {
	vt_background_playback_video_render_complete();
	if (g_artwork) vita2d_free_texture(g_artwork);
	g_artwork = NULL;
	g_artwork_video_id[0] = '\0';
	g_artwork_attempted = 0;
	g_marquee_title[0] = '\0';
	g_marquee_started_us = 0;
	g_cached_snapshot_valid = 0;
	g_mini_target_visible = 0;
	g_mini_animation = 0.0f;
	g_mini_animation_last_us = 0;
	g_mini_video_expanded = 0;
	g_mini_video_expansion = 0.0f;
	g_mini_layout_video_id[0] = '\0';
	g_mini_layout_activation_serial = 0;
	g_mini_lock_visible_until_us = 0;
	g_mini_lock_animation = 0.0f;
	g_mini_lock_animation_last_us = 0;
	g_mini_control_feedback = MINI_CONTROL_NONE;
	g_mini_control_feedback_until_us = 0;
}

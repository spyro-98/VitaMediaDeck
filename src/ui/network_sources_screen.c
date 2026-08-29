#include "ui/network_sources_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "media/video_thumbnail.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define LIST_X 66
#define LIST_Y 112
#define LIST_W 828
#define ROW_H 58
#define VISIBLE_ROWS 7
#define SOURCE_LIST_X 46
#define SOURCE_LIST_Y 126
#define SOURCE_LIST_W 568
#define SOURCE_ROW_H 62
#define BROWSER_GRID_COLS 4
#define BROWSER_GRID_CARD_W 198
#define BROWSER_GRID_CARD_H 154
#define BROWSER_GRID_GAP_X 12
#define BROWSER_GRID_GAP_Y 10

static int network_viewport_bottom(void) {
	int mini_top = ui_mini_player_top();
	return mini_top < 532 ? mini_top : 532;
}

static int visible_rows(void) {
	int rows = (network_viewport_bottom() - LIST_Y + 6) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int render_rows(void) {
	int rows = (network_viewport_bottom() - LIST_Y + ROW_H - 1) / ROW_H;
	if (rows < 1) rows = 1;
	if (rows > VISIBLE_ROWS) rows = VISIBLE_ROWS;
	return rows;
}

static int browser_grid_visible_rows(void) {
	int step = BROWSER_GRID_CARD_H + BROWSER_GRID_GAP_Y;
	int rows = (network_viewport_bottom() - LIST_Y + BROWSER_GRID_GAP_Y) / step;
	if (rows < 1) rows = 1;
	if (rows > 2) rows = 2;
	return rows;
}

static int browser_grid_render_rows(void) {
	int step = BROWSER_GRID_CARD_H + BROWSER_GRID_GAP_Y;
	int rows = (network_viewport_bottom() - LIST_Y + step - 1) / step;
	if (rows < 1) rows = 1;
	if (rows > 3) rows = 3;
	return rows;
}

static int source_visible_rows(void) {
	int rows = (network_viewport_bottom() - SOURCE_LIST_Y + 8) / SOURCE_ROW_H;
	if (rows < 1) rows = 1;
	if (rows > 6) rows = 6;
	return rows;
}

static int source_render_rows(void) {
	int rows = (network_viewport_bottom() - SOURCE_LIST_Y + SOURCE_ROW_H - 1) /
	           SOURCE_ROW_H;
	if (rows < 1) rows = 1;
	if (rows > 6) rows = 6;
	return rows;
}

typedef struct {
	const VtNetworkSource *source;
	const VtNetworkCredential *credential;
	const char *path;
	VtNetworkEntry *entries;
	int result;
	char detail[192];
} ListTask;

static int list_task(void *opaque) {
	ListTask *task = opaque;
	task->result = vt_network_list(task->source, task->credential, task->path,
	                               task->entries, VT_NETWORK_MAX_ENTRIES,
	                               task->detail, sizeof(task->detail));
	return task->result < 0 ? task->result : 0;
}

typedef struct {
	const VtNetworkSource *source;
	char fingerprint[VT_NETWORK_FINGERPRINT_MAX];
	char detail[192];
} FingerprintTask;

static int fingerprint_task(void *opaque) {
	FingerprintTask *task = opaque;
	return vt_network_sftp_probe_fingerprint(task->source, task->fingerprint,
	                                         sizeof(task->fingerprint),
	                                         task->detail, sizeof(task->detail));
}

typedef struct {
	const VtNetworkSource *source;
	const VtNetworkCredential *credential;
	char pin[VT_NETWORK_TLS_PIN_MAX];
	char detail[192];
} WebDavPinTask;

static int webdav_pin_task(void *opaque) {
	WebDavPinTask *task = opaque;
	return vt_network_webdav_probe_public_key(
		task->source, task->credential, task->pin, sizeof(task->pin),
		task->detail, sizeof(task->detail));
}

static void clip(vita2d_font *font, unsigned size, const char *source,
	             char *out, size_t out_size, int width) {
	ui_font_fit_text(font, size, source ? source : "", out, out_size, width);
}

static void draw_wrapped(vita2d_font *font, unsigned size, const char *text,
	                     int x, int baseline, int width, int line_step,
	                     int max_lines, unsigned color) {
	if (!font || !text || !text[0]) return;
	const char *cursor = text;
	for (int line_index = 0; line_index < max_lines && *cursor; line_index++) {
		while (*cursor == ' ') cursor++;
		const char *scan = cursor;
		const char *last_space = NULL;
		const char *fit = cursor;
		char line[192];
		while (*scan) {
			const char *next = scan + 1;
			while ((*next & 0xc0) == 0x80) next++;
			size_t length = (size_t)(next - cursor);
			if (length >= sizeof(line)) break;
			memcpy(line, cursor, length);
			line[length] = '\0';
			if (ui_font_text_width(font, size, line) > width) break;
			fit = next;
			if (*scan == ' ') last_space = scan;
			scan = next;
		}
		const char *end = *fit && last_space && last_space > cursor
		                ? last_space : fit;
		if (end == cursor) end = scan > cursor ? scan : cursor + 1;
		size_t length = (size_t)(end - cursor);
		if (length >= sizeof(line)) length = sizeof(line) - 1;
		memcpy(line, cursor, length);
		line[length] = '\0';
		ui_font_draw_text(font, x, baseline + line_index * line_step,
		                  color, size, line);
		cursor = end;
	}
}

static void draw_summary_row(vita2d_font *font, const char *label,
	                         const char *value, int baseline) {
	if (!font) return;
	char fitted[128];
	clip(font, UI_FONT_SMALL, value, fitted, sizeof(fitted), 128);
	ui_font_draw_text(font, 666, baseline, VT_THEME_TEXT_MUTED,
	                  UI_FONT_SMALL, label);
	int width = ui_font_text_width(font, UI_FONT_SMALL, fitted);
	ui_font_draw_text(font, 892 - width, baseline, VT_THEME_TEXT,
	                  UI_FONT_SMALL, fitted);
}

static void draw_sources(const VtNetworkSource *sources, int count,
	                     int selected, int top, int remove_confirm,
	                     const UiFocusMotion *focus_motion,
	                     UiSectionsSidebar *sidebar) {
	ui_mini_player_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_header_placeholder(NULL, vt_i18n_str(VT_STR_NETWORK_TITLE));
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int page_has_focus = !ui_mini_player_input_locked() && !remove_confirm &&
	                     (!sidebar || (!sidebar->open &&
	                                  sidebar->animation <= 0.01f));
	ui_scene_identity(SOURCE_LIST_X, 68, 604, "NET/03",
	                  vt_i18n_str(VT_STR_NETWORK_SERVERS_TITLE),
	                  vt_i18n_str(VT_STR_NETWORK_PRIVACY));
	ui_action_button(720, 70, 194, 43, VT_THEME_BLUE_BRIGHT,
		                 "Square", vt_i18n_str(VT_STR_NETWORK_ADD_SOURCE), 0);
	if (!count) {
		ui_panel(SOURCE_LIST_X, SOURCE_LIST_Y, SOURCE_LIST_W, 220,
		         VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, SOURCE_LIST_X + 30, 210,
		                             VT_THEME_TEXT, UI_FONT_BODY,
			                             vt_i18n_str(VT_STR_NETWORK_EMPTY_TITLE));
		if (small) ui_font_draw_text(small, SOURCE_LIST_X + 30, 244,
		                              VT_THEME_TEXT, UI_FONT_SMALL,
			                              vt_i18n_str(VT_STR_NETWORK_EMPTY_DETAIL));
	} else {
		int viewport_bottom = network_viewport_bottom();
		if (page_has_focus)
			ui_focus_glow_draw(focus_motion ? focus_motion->x : SOURCE_LIST_X,
			                   focus_motion ? focus_motion->y
			                                : SOURCE_LIST_Y + (selected - top) * SOURCE_ROW_H,
			                   focus_motion ? focus_motion->width : SOURCE_LIST_W,
			                   focus_motion ? focus_motion->height : SOURCE_ROW_H - 8,
			                   sceKernelGetProcessTimeWide(), SOURCE_LIST_Y,
			                   viewport_bottom);
		vita2d_set_clip_rectangle(0, SOURCE_LIST_Y, 628, viewport_bottom);
		vita2d_enable_clipping();
		for (int i = top; i < count && i < top + source_render_rows(); i++) {
			int y = SOURCE_LIST_Y + (i - top) * SOURCE_ROW_H;
			ui_panel(SOURCE_LIST_X, y, SOURCE_LIST_W, SOURCE_ROW_H - 8,
			         VT_THEME_SURFACE,
			         sources[i].protocol == VT_NETWORK_SFTP ? VT_THEME_BLUE_LIGHT
			                                              : VT_THEME_BLUE_BRIGHT,
			         0);
			if (body) {
				char name[128];
				clip(body, UI_FONT_BODY, sources[i].name, name, sizeof(name), 410);
				ui_font_draw_text(body, SOURCE_LIST_X + 22, y + 29,
				                  VT_THEME_TEXT, UI_FONT_BODY, name);
			}
			if (small) {
				char endpoint[320];
				snprintf(endpoint, sizeof(endpoint), "%s:%u",
				         sources[i].host, sources[i].port);
				const char *protocol = vt_network_protocol_name(sources[i].protocol);
				int width = ui_font_text_width(small, UI_FONT_SMALL, protocol);
				char fitted_endpoint[192];
				clip(small, UI_FONT_SMALL, endpoint, fitted_endpoint,
				     sizeof(fitted_endpoint), SOURCE_LIST_W - width - 76);
				ui_font_draw_text(small, SOURCE_LIST_X + 22, y + 49,
				                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted_endpoint);
				ui_font_draw_text(small, SOURCE_LIST_X + SOURCE_LIST_W - width - 18,
				                  y + 32, VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, protocol);
			}
		}
		vita2d_disable_clipping();
		const VtNetworkSource *source = &sources[selected];
		ui_panel(642, SOURCE_LIST_Y, 272, 174, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_LIGHT, 0);
		vita2d_draw_rectangle(654, SOURCE_LIST_Y + 18, 2, 138,
		                      VT_THEME_SIGNAL_DIM);
		for (int node = 0; node < 4; node++)
			vita2d_draw_rectangle(651, SOURCE_LIST_Y + 29 + node * 28, 8, 8,
			                      node == 0 ? VT_THEME_SIGNAL_LIGHT
			                                : VT_THEME_SIGNAL_DIM);
		if (small) {
			ui_font_draw_text(small, 666, 154, VT_THEME_BLUE_LIGHT, UI_FONT_SMALL,
			                  vt_i18n_str(VT_STR_NETWORK_CONNECTION));
			char endpoint[320];
			snprintf(endpoint, sizeof(endpoint), "%s:%u", source->host, source->port);
			draw_summary_row(small, vt_i18n_str(VT_STR_NETWORK_ENDPOINT), endpoint, 184);
			draw_summary_row(small, vt_i18n_str(VT_STR_NETWORK_FIELD_PROTOCOL),
			                 vt_network_protocol_name(source->protocol), 208);
			draw_summary_row(small, vt_i18n_str(VT_STR_NETWORK_FIELD_USERNAME),
			                 source->username, 232);
			draw_summary_row(small, vt_i18n_str(VT_STR_NETWORK_SECURITY),
			                 source->protocol == VT_NETWORK_SFTP
			                     ? vt_i18n_str(source->host_key_sha256[0]
			                           ? VT_STR_NETWORK_HOST_KEY_PINNED
			                           : VT_STR_NETWORK_HOST_KEY_UNVERIFIED)
			                     : source->protocol == VT_NETWORK_WEBDAV
			                           ? vt_i18n_str(source->tls_public_key_sha256[0]
			                                 ? VT_STR_NETWORK_TLS_PINNED
			                                 : VT_STR_NETWORK_TLS_CA_VERIFIED)
			                     : vt_i18n_str(VT_STR_NETWORK_AUTH_SESSION), 256);
		}
		ui_action_button(642, 314, 272, 42, VT_THEME_BLUE_BRIGHT,
		                 "Cross", vt_i18n_str(VT_STR_NETWORK_BROWSE), 0);
		ui_action_button(642, 366, 272, 42, VT_THEME_SURFACE_RAISED,
		                 "Triangle", vt_i18n_str(VT_STR_NETWORK_EDIT), 0);
		ui_action_button(642, 418, 272, 42, VT_THEME_SURFACE,
		                 "Select", vt_i18n_str(VT_STR_NETWORK_REMOVE), 0);
	}
	/* Persistent playback stays below every modal layer. */
	ui_mini_player_draw();
	if (remove_confirm) {
		vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
		                      544 - UI_BRAND_HEADER_HEIGHT, RGBA8(0, 3, 7, 186));
		ui_panel(208, 174, 544, 190, RGBA8(9, 8, 7, 255),
		         VT_THEME_DANGER, 0);
		if (body) ui_font_draw_text(body, 244, 224, VT_THEME_TEXT, UI_FONT_BODY,
		                            vt_i18n_str(VT_STR_NETWORK_REMOVE_TITLE));
		if (small) ui_font_draw_text(small, 244, 258, VT_THEME_TEXT_MUTED,
		                              UI_FONT_SMALL,
		                              vt_i18n_str(VT_STR_NETWORK_REMOVE_DETAIL));
		ui_action_button(236, 294, 226, 48, VT_THEME_SURFACE,
		                 "Circle", vt_i18n_str(VT_STR_NETWORK_CANCEL), 0);
		ui_action_button(480, 294, 244, 48, VT_THEME_DANGER,
		                 "Cross", vt_i18n_str(VT_STR_NETWORK_REMOVE), 1);
	}
	if (sidebar && sidebar->animation > .01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->open ? sidebar->focus_cursor : -1.0f);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static void draw_browser_icon(const VtNetworkEntry *entry, int x, int y) {
	if (entry->is_directory) {
		vita2d_draw_rectangle(x + 20, y + 24, 62, 15, VT_THEME_BLUE_LIGHT);
		vita2d_draw_rectangle(x + 12, y + 35, 112, 65, VT_THEME_BLUE);
		vita2d_draw_rectangle(x + 12, y + 35, 112, 4, VT_THEME_BLUE_BRIGHT);
		return;
	}
	vita2d_draw_rectangle(x + 18, y + 14, 100, 88, VT_THEME_MEDIA_BACKDROP);
	if (!entry->is_video && !entry->is_audio) {
		vita2d_draw_rectangle(x + 34, y + 15, 68, 88, VT_THEME_SURFACE_RAISED);
		vita2d_draw_rectangle(x + 34, y + 15, 4, 88, VT_THEME_TEXT_FAINT);
		for (int line = 0; line < 4; line++)
			vita2d_draw_rectangle(x + 48, y + 40 + line * 13, 40, 2,
			                      VT_THEME_TEXT_MUTED);
		return;
	}
	if (entry->is_audio) {
		vita2d_draw_fill_circle(x + 68, y + 58, 30, VT_THEME_SURFACE_RAISED);
		vita2d_draw_fill_circle(x + 68, y + 58, 10, VT_THEME_BLUE_LIGHT);
		return;
	}
	for (int stripe = 0; stripe < 5; stripe++)
		vita2d_draw_rectangle(x + 18 + stripe * 20, y + 14, 10, 88,
		                      stripe & 1 ? RGBA8(47, 34, 22, 255)
		                                 : RGBA8(18, 16, 14, 255));
	vita2d_draw_fill_circle(x + 68, y + 58, 21, RGBA8(5, 5, 6, 220));
	for (int line = 0; line < 18; line++)
		vita2d_draw_rectangle(x + 62, y + 49 + line, 7 + line / 2, 1,
		                      VT_THEME_TEXT);
}

static void draw_video_preview(vita2d_texture *texture, float x, float y,
	                           float width, float height) {
	if (!texture) return;
	float tw = (float)vita2d_texture_get_width(texture);
	float th = (float)vita2d_texture_get_height(texture);
	if (tw <= 0.0f || th <= 0.0f) return;
	float source_x = 0.0f, source_y = 0.0f;
	float source_w = tw, source_h = th;
	if (tw / th > width / height) {
		source_w = th * width / height;
		source_x = (tw - source_w) * 0.5f;
	} else {
		source_h = tw * height / width;
		source_y = (th - source_h) * 0.5f;
	}
	vita2d_draw_texture_part_scale(texture, x, y, source_x, source_y,
	                               source_w, source_h,
	                               width / source_w, height / source_h);
}

static void draw_browser(const VtNetworkSource *source, const char *path,
	                     const VtNetworkCredential *credential,
	                     const VtNetworkEntry *entries, int count,
	                     int selected, int top, int grid_mode,
	                     const UiFocusMotion *focus_motion) {
	ui_mini_player_pump();
	vt_video_thumbnail_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header_placeholder(NULL, source->name);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	char breadcrumb[192];
	snprintf(breadcrumb, sizeof(breadcrumb), "%s / %s", source->name,
	         path && path[0] ? path : "");
	ui_panel(LIST_X, 68, LIST_W, 34, VT_THEME_SURFACE_RAISED,
	         VT_THEME_BLUE_LIGHT, 0);
	vita2d_draw_rectangle(LIST_X + 12, 78, 3, 14, VT_THEME_SIGNAL);
	if (small) {
		char fitted_breadcrumb[192];
		clip(small, UI_FONT_SMALL, breadcrumb, fitted_breadcrumb,
		     sizeof(fitted_breadcrumb), LIST_W - 190);
		ui_font_draw_text(small, LIST_X + 26, 91, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, fitted_breadcrumb);
		const char *view_hint = grid_mode
		                        ? vt_i18n_str(VT_STR_NETWORK_VIEW_GRID)
		                        : vt_i18n_str(VT_STR_NETWORK_VIEW_LIST);
		char hint[64];
		snprintf(hint, sizeof(hint), "R1  %s", view_hint);
		int hint_width = ui_font_text_width(small, UI_FONT_SMALL, hint);
		vita2d_draw_rectangle(LIST_X + LIST_W - hint_width - 28, 73,
		                      hint_width + 20, 24, VT_THEME_SURFACE_RAISED);
		ui_font_draw_text(small, LIST_X + LIST_W - hint_width - 18, 91,
		                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, hint);
	}
	if (!count) {
		ui_panel(220, 202, 520, 132, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 280, 258, VT_THEME_TEXT,
			                             UI_FONT_BODY, vt_i18n_str(VT_STR_NETWORK_EMPTY_FOLDER));
		if (small) ui_font_draw_text(small, 280, 290, VT_THEME_TEXT,
			                              UI_FONT_SMALL, vt_i18n_str(VT_STR_NETWORK_EMPTY_FOLDER_DETAIL));
	} else if (!grid_mode) {
		int viewport_bottom = network_viewport_bottom();
		if (!ui_mini_player_input_locked())
			ui_focus_glow_draw(focus_motion ? focus_motion->x : LIST_X,
		                   focus_motion ? focus_motion->y
		                                : LIST_Y + (selected - top) * ROW_H,
		                   focus_motion ? focus_motion->width : LIST_W,
		                   focus_motion ? focus_motion->height : ROW_H - 6,
		                   sceKernelGetProcessTimeWide(),
		                   LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		for (int i = top; i < count && i < top + render_rows(); i++) {
			int y = LIST_Y + (i - top) * ROW_H;
			const VtNetworkEntry *entry = &entries[i];
			vita2d_texture *preview = entry->is_video
			                        ? vt_video_thumbnail_get_remote(
			                              source, credential, entry->path, entry->size)
			                        : NULL;
			ui_panel(LIST_X, y, LIST_W, ROW_H - 6,
			         VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : (entry->is_video || entry->is_audio)
			             ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER,
			         0);
			if (preview) {
				vita2d_draw_rectangle(LIST_X + 9, y + 6, 70, 40,
				                      VT_THEME_MEDIA_BACKDROP);
				draw_video_preview(preview, LIST_X + 9, y + 6, 70, 40);
				vita2d_draw_rectangle(LIST_X + 9, y + 44, 70, 2,
				                      VT_THEME_COLD);
			}
			if (body) {
				char title[192];
				clip(body, UI_FONT_BODY, entry->name, title, sizeof(title),
				     preview ? 520 : 600);
				ui_font_draw_text(body, LIST_X + (preview ? 94 : 22), y + 33,
				                  VT_THEME_TEXT,
				                  UI_FONT_BODY, title);
			}
			if (small) {
				char detail[64];
				if (entry->is_directory) snprintf(detail, sizeof(detail), "%s",
				                                      vt_i18n_str(VT_STR_NETWORK_FOLDER));
				else if (entry->size >= 1024ULL * 1024ULL * 1024ULL)
					snprintf(detail, sizeof(detail), "%.2f GB",
					         (double)entry->size / (1024.0 * 1024.0 * 1024.0));
				else snprintf(detail, sizeof(detail), "%.1f MB",
				              (double)entry->size / (1024.0 * 1024.0));
				int width = ui_font_text_width(small, UI_FONT_SMALL, detail);
				ui_font_draw_text(small, LIST_X + LIST_W - width - 18, y + 31,
				                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL, detail);
			}
		}
		vita2d_disable_clipping();
	} else {
		int viewport_bottom = network_viewport_bottom();
		if (!ui_mini_player_input_locked())
			ui_focus_glow_draw(focus_motion ? focus_motion->x : LIST_X,
			                   focus_motion ? focus_motion->y : LIST_Y,
			                   focus_motion ? focus_motion->width : BROWSER_GRID_CARD_W,
			                   focus_motion ? focus_motion->height : BROWSER_GRID_CARD_H,
			                   sceKernelGetProcessTimeWide(), LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		int first = top * BROWSER_GRID_COLS;
		int limit = (top + browser_grid_render_rows()) * BROWSER_GRID_COLS;
		for (int i = first; i < count && i < limit; i++) {
			int col = i % BROWSER_GRID_COLS;
			int row = i / BROWSER_GRID_COLS - top;
			int x = LIST_X + col * (BROWSER_GRID_CARD_W + BROWSER_GRID_GAP_X);
			int y = LIST_Y + row * (BROWSER_GRID_CARD_H + BROWSER_GRID_GAP_Y);
			const VtNetworkEntry *entry = &entries[i];
			vita2d_texture *preview = entry->is_video
			                        ? vt_video_thumbnail_get_remote(
			                              source, credential, entry->path, entry->size)
			                        : NULL;
			ui_panel(x, y, BROWSER_GRID_CARD_W, BROWSER_GRID_CARD_H,
			         VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT
			         : (entry->is_video || entry->is_audio)
			             ? VT_THEME_BLUE_BRIGHT : VT_THEME_BORDER,
			         0);
			if (preview) {
				vita2d_draw_rectangle(x + 6, y + 6,
				                      BROWSER_GRID_CARD_W - 12, 96,
				                      VT_THEME_MEDIA_BACKDROP);
				draw_video_preview(preview, x + 6, y + 6,
				                   BROWSER_GRID_CARD_W - 12, 96);
				vita2d_draw_rectangle(x + 6, y + 100,
				                      BROWSER_GRID_CARD_W - 12, 2,
				                      VT_THEME_COLD);
			} else {
				draw_browser_icon(entry, x + 30, y + 2);
			}
			if (small) {
				char title[128];
				clip(small, UI_FONT_SMALL, entry->name, title, sizeof(title),
				     BROWSER_GRID_CARD_W - 24);
				ui_font_draw_text(small, x + 12, y + 126, VT_THEME_TEXT,
				                  UI_FONT_SMALL, title);
				char detail[64];
				if (entry->is_directory)
					snprintf(detail, sizeof(detail), "%s",
					         vt_i18n_str(VT_STR_NETWORK_FOLDER));
				else if (entry->size >= 1024ULL * 1024ULL * 1024ULL)
					snprintf(detail, sizeof(detail), "%.2f GB",
					         (double)entry->size / (1024.0 * 1024.0 * 1024.0));
				else snprintf(detail, sizeof(detail), "%.1f MB",
				              (double)entry->size / (1024.0 * 1024.0));
				ui_font_draw_text(small, x + 12, y + 148, VT_THEME_TEXT_MUTED,
				                  UI_FONT_SMALL, detail);
			}
		}
		vita2d_disable_clipping();
	}
	ui_mini_player_draw();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static int edit_text(const char *title, char *value, size_t size) {
	char next[VT_NETWORK_PATH_MAX];
	int result = ui_text_input(title, value, next,
	                           size < sizeof(next) ? size : sizeof(next));
	if (result > 0) snprintf(value, size, "%s", next);
	return result;
}

static int network_entry_compare(const void *left, const void *right) {
	const VtNetworkEntry *a = left, *b = right;
	if (a->is_directory != b->is_directory)
		return b->is_directory - a->is_directory;
	return strcasecmp(a->name, b->name);
}

static void network_resync_input(SceCtrlData *previous,
	                             UiNavRepeat *repeat) {
	ui_touch_reset();
	if (previous) sceCtrlPeekBufferPositive(0, previous, 1);
	if (repeat) ui_nav_repeat_reset(repeat);
}

static int confirm_trust_value(const char *title, const char *value) {
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	for (;;) {
		ui_mini_player_pump();
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
		ui_brand_draw_header_placeholder(NULL, title);
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		ui_panel(54, 104, 852, 286, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_BRIGHT, 0);
		if (small)
			ui_font_draw_text(small, 82, 144, VT_THEME_BLUE_LIGHT,
			                  UI_FONT_SMALL,
			                  vt_i18n_str(VT_STR_NETWORK_TRUST_VALUE_LABEL));
		if (body)
			draw_wrapped(body, UI_FONT_BODY, value, 82, 190, 796, 34, 4,
			             VT_THEME_TEXT);
		vita2d_draw_rectangle(82, 302, 796, 1, VT_THEME_BORDER);
		if (small)
			draw_wrapped(small, UI_FONT_SMALL,
			             vt_i18n_str(VT_STR_NETWORK_TRUST_INSTRUCTION),
			             82, 334, 796, 22, 2, VT_THEME_WARNING);
		ui_action_button(54, 420, 250, 46, VT_THEME_SURFACE,
		                 "Circle", vt_i18n_str(VT_STR_NETWORK_CANCEL), 0);
		ui_action_button(618, 420, 288, 46, VT_THEME_BLUE_BRIGHT,
		                 "Cross", vt_i18n_str(VT_STR_NETWORK_TRUST_ACCEPT), 1);
		ui_mini_player_draw();
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();

		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch)) touch_flags = 0;
		if (ui_mini_player_input_locked()) pressed = 0;
		if (touch_flags & UI_TOUCH_EVENT_TAP) {
			if (ui_touch_hit_rect(touch.x, touch.y, 54, 420, 250, 46))
				pressed |= SCE_CTRL_CIRCLE;
			else if (ui_touch_hit_rect(touch.x, touch.y, 618, 420, 288, 46))
				pressed |= SCE_CTRL_CROSS;
		}
		if (pressed & SCE_CTRL_CROSS) return 1;
		if (pressed & SCE_CTRL_CIRCLE) return 0;
		sceKernelDelayThread(1000);
	}
}

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = (volatile unsigned char *)memory;
	while (size--) *bytes++ = 0;
}

static int finish_network_screen(VtNetworkCredential *credentials, int result) {
	secure_zero(credentials,
	            sizeof(*credentials) * (size_t)VT_NETWORK_MAX_SOURCES);
	return result;
}

static void show_save_error(int error) {
	char detail[192];
	snprintf(detail, sizeof(detail), "%s (0x%08X)",
	         vt_i18n_str(VT_STR_NETWORK_SAVE_FAILED_DETAIL),
	         (unsigned)error);
	ui_message_show(vt_i18n_str(VT_STR_NETWORK_SAVE_FAILED_TITLE), detail, 3200);
}

static void persist_remembered_credentials(
	const VtNetworkCredential *credentials, int count) {
	if (!vt_preferences_remember_network_passwords()) return;
	int result = vt_network_credentials_save(credentials, count);
	if (result < 0) {
		/* Never leave an older index-aligned file capable of assigning a password
		 * to the wrong source after an insert, edit, or removal. */
		vt_network_credentials_clear();
		show_save_error(result);
	}
}

static int source_valid(const VtNetworkSource *source) {
	return source && source->name[0] && source->host[0] &&
	       source->username[0] &&
	       (source->protocol != VT_NETWORK_SMB || source->share[0]);
}

typedef enum {
	EDITOR_PROTOCOL,
	EDITOR_NAME,
	EDITOR_HOST,
	EDITOR_PORT,
	EDITOR_ROOT,
	EDITOR_SHARE,
	EDITOR_USERNAME,
	EDITOR_PASSWORD,
	EDITOR_DOMAIN
} EditorField;

static int editor_fields(VtNetworkProtocol protocol, EditorField fields[9]) {
	int count = 0;
	fields[count++] = EDITOR_PROTOCOL;
	fields[count++] = EDITOR_NAME;
	fields[count++] = EDITOR_HOST;
	fields[count++] = EDITOR_PORT;
	if (protocol == VT_NETWORK_SMB) fields[count++] = EDITOR_SHARE;
	fields[count++] = EDITOR_ROOT;
	fields[count++] = EDITOR_USERNAME;
	fields[count++] = EDITOR_PASSWORD;
	if (protocol == VT_NETWORK_SMB) fields[count++] = EDITOR_DOMAIN;
	return count;
}

static int editor_field_step(int field_count) {
	if (field_count >= 9) return 37;
	return field_count >= 8 ? 42 : 46;
}

static int editor_field_height(int field_count) {
	if (field_count >= 9) return 32;
	return field_count >= 8 ? 36 : 40;
}

static int editor_field_y(int field_count, int row) {
	return 82 + row * editor_field_step(field_count);
}

static void clear_transport_trust(VtNetworkSource *source) {
	if (!source) return;
	source->host_key_sha256[0] = '\0';
	source->tls_public_key_sha256[0] = '\0';
}

static const char *editor_field_label(EditorField field) {
	switch (field) {
		case EDITOR_PROTOCOL: return vt_i18n_str(VT_STR_NETWORK_FIELD_PROTOCOL);
		case EDITOR_NAME: return vt_i18n_str(VT_STR_NETWORK_FIELD_NAME);
		case EDITOR_HOST: return vt_i18n_str(VT_STR_NETWORK_FIELD_HOST);
		case EDITOR_PORT: return vt_i18n_str(VT_STR_NETWORK_FIELD_PORT);
		case EDITOR_ROOT: return vt_i18n_str(VT_STR_NETWORK_FIELD_ROOT);
		case EDITOR_SHARE: return vt_i18n_str(VT_STR_NETWORK_FIELD_SHARE);
		case EDITOR_USERNAME: return vt_i18n_str(VT_STR_NETWORK_FIELD_USERNAME);
		case EDITOR_PASSWORD: return vt_i18n_str(VT_STR_NETWORK_FIELD_PASSWORD);
		case EDITOR_DOMAIN: return vt_i18n_str(VT_STR_NETWORK_FIELD_DOMAIN);
	}
	return "";
}

static const char *editor_field_value(const VtNetworkSource *source,
	                                  const VtNetworkCredential *credential,
	                                  EditorField field, char port[16]) {
	switch (field) {
		case EDITOR_PROTOCOL: return vt_network_protocol_name(source->protocol);
		case EDITOR_NAME: return source->name;
		case EDITOR_HOST: return source->host;
		case EDITOR_PORT:
			snprintf(port, 16, "%u", source->port);
			return port;
		case EDITOR_ROOT: return source->root_path;
		case EDITOR_SHARE: return source->share;
		case EDITOR_USERNAME: return source->username;
		case EDITOR_PASSWORD:
			return credential && credential->password[0] ? "********" : "";
		case EDITOR_DOMAIN: return source->domain;
	}
	return "";
}

static void editor_change_protocol(VtNetworkSource *draft, int direction) {
	int protocol = (int)draft->protocol + direction;
	if (protocol < VT_NETWORK_WEBDAV) protocol = VT_NETWORK_SMB;
	if (protocol > VT_NETWORK_SMB) protocol = VT_NETWORK_WEBDAV;
	draft->protocol = (VtNetworkProtocol)protocol;
	clear_transport_trust(draft);
	if (draft->protocol == VT_NETWORK_WEBDAV) draft->port = 443;
	else if (draft->protocol == VT_NETWORK_SFTP) draft->port = 22;
	else draft->port = 445;
}

static void editor_edit_field(VtNetworkSource *draft,
	                          VtNetworkCredential *credential,
	                          EditorField field) {
	if (field == EDITOR_NAME)
		edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_NAME), draft->name, sizeof(draft->name));
	else if (field == EDITOR_HOST) {
		if (edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_HOST), draft->host,
		              sizeof(draft->host)) > 0)
			clear_transport_trust(draft);
	}
	else if (field == EDITOR_PORT) {
		char value[16];
		snprintf(value, sizeof(value), "%u", draft->port);
		if (edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_PORT), value, sizeof(value)) > 0) {
			unsigned long port = strtoul(value, NULL, 10);
			if (port > 0 && port <= 65535) {
				draft->port = (uint16_t)port;
				clear_transport_trust(draft);
			}
		}
	} else if (field == EDITOR_ROOT)
		edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_ROOT), draft->root_path, sizeof(draft->root_path));
	else if (field == EDITOR_SHARE)
		edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_SHARE), draft->share, sizeof(draft->share));
	else if (field == EDITOR_USERNAME)
		edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_USERNAME), draft->username, sizeof(draft->username));
	else if (field == EDITOR_PASSWORD && credential) {
		char next[VT_NETWORK_SECRET_MAX];
		int result = ui_text_input_secure(
		    vt_i18n_str(VT_STR_NETWORK_PASSWORD_PROMPT), credential->password,
		    next, sizeof(next));
		if (result > 0) {
			secure_zero(credential->password, sizeof(credential->password));
			snprintf(credential->password, sizeof(credential->password), "%s", next);
		}
		secure_zero(next, sizeof(next));
	}
	else if (field == EDITOR_DOMAIN)
		edit_text(vt_i18n_str(VT_STR_NETWORK_INPUT_DOMAIN), draft->domain, sizeof(draft->domain));
}

static int source_editor(VtNetworkSource *source,
	                     VtNetworkCredential *credential, int is_new) {
	VtNetworkSource draft;
	memset(&draft, 0, sizeof(draft));
	if (!is_new) draft = *source;
	else {
		draft.protocol = VT_NETWORK_WEBDAV;
		draft.port = 443;
		snprintf(draft.name, sizeof(draft.name), "%s",
		         vt_i18n_str(VT_STR_NETWORK_DEFAULT_NAME));
	}
	int cursor = 0;
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	UiFocusMotion focus_motion;
	ui_focus_motion_reset(&focus_motion);
	for (;;) {
		EditorField fields[9];
		int field_count = editor_fields(draft.protocol, fields);
		if (cursor > field_count) cursor = field_count;
		if (cursor < field_count)
			ui_focus_motion_tick(&focus_motion, 304,
			                     editor_field_y(field_count, cursor), 622,
			                     editor_field_height(field_count));
		else
			ui_focus_motion_tick(&focus_motion, 304, 420, 622, 46);
		ui_mini_player_pump();
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
		ui_brand_draw_header_placeholder(NULL, vt_i18n_str(is_new
		    ? VT_STR_NETWORK_ADD_TITLE : VT_STR_NETWORK_EDIT_TITLE));
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		ui_panel(34, 82, 244, 382, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 58, 126, VT_THEME_TEXT, UI_FONT_BODY,
		                            vt_network_protocol_name(draft.protocol));
		if (small) {
			const char *detail = vt_i18n_str(draft.protocol == VT_NETWORK_WEBDAV
			    ? VT_STR_NETWORK_PROTOCOL_WEBDAV_DETAIL
			    : draft.protocol == VT_NETWORK_SFTP
			          ? VT_STR_NETWORK_PROTOCOL_SFTP_DETAIL
			          : VT_STR_NETWORK_PROTOCOL_SMB_DETAIL);
			const char *security = vt_i18n_str(draft.protocol == VT_NETWORK_WEBDAV
			    ? VT_STR_NETWORK_WEBDAV_SECURITY
			    : draft.protocol == VT_NETWORK_SFTP
			          ? VT_STR_NETWORK_SFTP_SECURITY
			          : VT_STR_NETWORK_SMB_SECURITY);
			draw_wrapped(small, UI_FONT_SMALL, detail, 58, 158, 188, 20, 2,
			             VT_THEME_TEXT);
			vita2d_draw_rectangle(58, 194, 188, 1, VT_THEME_BORDER);
			draw_wrapped(small, UI_FONT_SMALL, security, 58, 222, 188, 20, 2,
			             VT_THEME_TEXT);
			draw_wrapped(small, UI_FONT_SMALL,
			             vt_i18n_str(VT_STR_NETWORK_SESSION_PASSWORD),
			             58, 274, 188, 20, 2, VT_THEME_TEXT);
			draw_wrapped(small, UI_FONT_SMALL,
			             vt_i18n_str(VT_STR_NETWORK_PASSWORD_NOT_SAVED),
			             58, 322, 188, 20, 2, VT_THEME_TEXT);
			vita2d_draw_rectangle(58, 354, 188, 1, VT_THEME_BORDER);
			draw_wrapped(small, UI_FONT_SMALL, vt_i18n_str(source_valid(&draft)
			                 ? VT_STR_NETWORK_READY_SAVE
			                 : draft.protocol == VT_NETWORK_SMB
			                       ? VT_STR_NETWORK_REQUIRED_SMB
			                       : VT_STR_NETWORK_REQUIRED_WEBDAV),
			             58, 378, 188, 20, 2,
			             source_valid(&draft) ? VT_THEME_SUCCESS : VT_THEME_WARNING);
		}
		ui_action_button(54, 406, 204, 42, VT_THEME_SURFACE,
		                 "Circle", vt_i18n_str(VT_STR_NETWORK_CANCEL), 0);
		if (!ui_mini_player_input_locked())
			ui_focus_glow_draw(focus_motion.x, focus_motion.y,
			                   focus_motion.width, focus_motion.height,
			                   sceKernelGetProcessTimeWide(), 64, 476);
		for (int i = 0; i < field_count; i++) {
			int y = editor_field_y(field_count, i);
			int height = editor_field_height(field_count);
			ui_panel(304, y, 622, height, VT_THEME_SURFACE,
			         VT_THEME_BLUE_BRIGHT, 0);
			if (small) ui_font_draw_text(small, 322, y + height - 14, VT_THEME_TEXT,
			                             UI_FONT_SMALL, editor_field_label(fields[i]));
			char port[16];
			const char *raw_value = editor_field_value(&draft, credential,
			                                                fields[i], port);
			if (body) {
				char value[192];
				const char *shown = raw_value[0]
				                  ? raw_value : vt_i18n_str(VT_STR_NETWORK_NOT_SET);
				clip(body, UI_FONT_BODY, shown, value, sizeof(value), 350);
				int width = ui_font_text_width(body, UI_FONT_BODY, value);
				ui_font_draw_text(body, 904 - width, y + height - 11,
				                  raw_value[0] ? VT_THEME_TEXT : VT_THEME_TEXT_FAINT,
				                  UI_FONT_BODY, value);
			}
		}
		ui_action_button(304, 420, 622, 46,
			                 source_valid(&draft) ? VT_THEME_BLUE_BRIGHT
			                                      : VT_THEME_SURFACE,
		                 "Cross", source_valid(&draft)
		                     ? vt_i18n_str(is_new ? VT_STR_NETWORK_SAVE_SOURCE
		                                               : VT_STR_NETWORK_SAVE_CHANGES)
		                     : vt_i18n_str(VT_STR_NETWORK_COMPLETE_REQUIRED),
			                 0);
		ui_mini_player_draw();
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch))
			touch_flags = UI_TOUCH_EVENT_NONE;
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			controls.buttons = 0;
			controls.lx = controls.ly = 128;
		}
		if (touch_flags & UI_TOUCH_EVENT_TAP) {
			int touched_field = -1;
			for (int row = 0; row < field_count; row++) {
				if (ui_touch_hit_rect(touch.x, touch.y, 304,
				                      editor_field_y(field_count, row), 622,
				                      editor_field_height(field_count))) {
					touched_field = row;
					break;
				}
			}
			if (touched_field >= 0) {
				cursor = touched_field;
				pressed |= fields[touched_field] == EDITOR_PROTOCOL
				         ? SCE_CTRL_RIGHT : SCE_CTRL_CROSS;
			} else if (ui_touch_hit_rect(touch.x, touch.y, 304, 420, 622, 46)) {
				cursor = field_count;
				pressed |= SCE_CTRL_CROSS;
			} else if (ui_touch_hit_rect(touch.x, touch.y, 54, 406, 204, 42)) {
				pressed |= SCE_CTRL_CIRCLE;
			}
		}
		unsigned int nav = ui_nav_repeat_update(
		    &nav_repeat, pressed, controls.buttons, controls.lx, controls.ly,
		    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
		if ((nav & SCE_CTRL_UP) && cursor > 0) cursor--;
		if ((nav & SCE_CTRL_DOWN) && cursor < field_count) cursor++;
		if (cursor < field_count && fields[cursor] == EDITOR_PROTOCOL &&
		    (nav & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
			editor_change_protocol(&draft, (nav & SCE_CTRL_RIGHT) ? 1 : -1);
			cursor = 0;
		}
		if (pressed & SCE_CTRL_CROSS) {
			if (cursor < field_count && fields[cursor] != EDITOR_PROTOCOL) {
				editor_edit_field(&draft, credential, fields[cursor]);
				network_resync_input(&previous, &nav_repeat);
				continue;
			} else if (cursor == field_count && source_valid(&draft)) {
				*source = draft;
				return 1;
			}
		}
		if (pressed & SCE_CTRL_CIRCLE) return 0;
		sceKernelDelayThread(1000);
	}
}

static int load_directory(const VtNetworkSource *source,
	                      const VtNetworkCredential *credential,
	                      const char *path, VtNetworkEntry *entries,
	                      int *count) {
	ListTask task = { source, credential, path, entries, -1, { 0 } };
	int ret = ui_loading_run(vt_i18n_str(VT_STR_NETWORK_READING_FOLDER), list_task, &task,
	                         NULL, NULL, NULL);
	if (ret < 0 || task.result < 0) {
		if (task.result != VT_NETWORK_TLS_TRUST_REQUIRED)
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_CONNECTION_FAILED),
			                task.detail[0] ? task.detail
			                               : vt_i18n_str(VT_STR_NETWORK_READ_FAILED), 2800);
		return task.result < 0 ? task.result : ret;
	}
	*count = task.result;
	qsort(entries, (size_t)*count, sizeof(*entries), network_entry_compare);
	return 0;
}

static int prepare_webdav_trust(VtNetworkSource *source,
	                            const VtNetworkCredential *credential) {
	if (source->protocol != VT_NETWORK_WEBDAV ||
	    source->tls_public_key_sha256[0]) return 0;
	WebDavPinTask task = { source, credential, { 0 }, { 0 } };
	int ret = ui_loading_run(vt_i18n_str(VT_STR_NETWORK_CHECKING_TLS),
	                         webdav_pin_task, &task, NULL, NULL, NULL);
	if (ret < 0 || !task.pin[0]) {
		ui_message_show(vt_i18n_str(VT_STR_NETWORK_TLS_UNAVAILABLE),
		                task.detail[0] ? task.detail
		                               : vt_i18n_str(VT_STR_NETWORK_TLS_FAILED), 2800);
		return -1;
	}
	if (!confirm_trust_value(vt_i18n_str(VT_STR_NETWORK_VERIFY_TLS_PIN),
	                         task.pin)) return -1;
	snprintf(source->tls_public_key_sha256,
	         sizeof(source->tls_public_key_sha256), "%s", task.pin);
	return 1;
}

static int prepare_sftp_trust(VtNetworkSource *source) {
	if (source->protocol != VT_NETWORK_SFTP || source->host_key_sha256[0]) return 0;
	FingerprintTask task = { source, { 0 }, { 0 } };
	int ret = ui_loading_run(vt_i18n_str(VT_STR_NETWORK_CHECKING_HOST), fingerprint_task,
	                         &task, NULL, NULL, NULL);
	if (ret < 0 || !task.fingerprint[0]) {
		ui_message_show(vt_i18n_str(VT_STR_NETWORK_HOST_UNAVAILABLE),
		                task.detail[0] ? task.detail
		                               : vt_i18n_str(VT_STR_NETWORK_SSH_FAILED), 2800);
		return -1;
	}
	if (!confirm_trust_value(vt_i18n_str(VT_STR_NETWORK_VERIFY_FINGERPRINT),
	                         task.fingerprint)) return -1;
	snprintf(source->host_key_sha256, sizeof(source->host_key_sha256), "%s",
	         task.fingerprint);
	return 1;
}

static int browse_source(const VtNetworkSource *source,
	                     const VtNetworkCredential *credential,
	                     UiNetworkSelection *selection) {
	VtNetworkEntry *entries = calloc(VT_NETWORK_MAX_ENTRIES, sizeof(*entries));
	if (!entries) {
		ui_message_show(vt_i18n_str(VT_STR_NETWORK_MEMORY_TITLE),
		                vt_i18n_str(VT_STR_NETWORK_MEMORY_DETAIL), 2600);
		return 0;
	}
	char path[VT_NETWORK_PATH_MAX] = "";
	int count = 0;
	int load_result = load_directory(source, credential, path, entries, &count);
	if (load_result < 0) {
		free(entries);
		return load_result;
	}
	vt_video_thumbnail_resume();
	int selected = 0, top = 0;
	int grid_mode = vt_preferences_file_browser_grid();
	UiFocusMotion focus_motion;
	ui_focus_motion_reset(&focus_motion);
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch)) touch_flags = 0;
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			controls.buttons = 0;
			controls.lx = controls.ly = 128;
		}
		if ((touch_flags & UI_TOUCH_EVENT_UP) &&
		    !(touch_flags & UI_TOUCH_EVENT_TAP) && count > 0 &&
		    touch.down_x >= LIST_X && touch.down_x < LIST_X + LIST_W &&
		    touch.down_y >= LIST_Y) {
			int dy = touch.y - touch.down_y;
			int page_step = grid_mode ? BROWSER_GRID_COLS : 3;
			if (dy < -36) selected += page_step;
			else if (dy > 36) selected -= page_step;
			if (selected < 0) selected = 0;
			if (selected >= count) selected = count - 1;
		}
		if ((touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
			int slots = grid_mode ? browser_grid_render_rows() * BROWSER_GRID_COLS
			                      : render_rows();
			int first = grid_mode ? top * BROWSER_GRID_COLS : top;
			for (int slot = 0; slot < slots && first + slot < count; slot++) {
				int hit = grid_mode
				        ? ui_touch_hit_rect(touch.x, touch.y,
				              LIST_X + (slot % BROWSER_GRID_COLS) *
				                       (BROWSER_GRID_CARD_W + BROWSER_GRID_GAP_X),
				              LIST_Y + (slot / BROWSER_GRID_COLS) *
				                       (BROWSER_GRID_CARD_H + BROWSER_GRID_GAP_Y),
				              BROWSER_GRID_CARD_W, BROWSER_GRID_CARD_H)
				        : ui_touch_hit_rect(touch.x, touch.y, LIST_X,
				              LIST_Y + slot * ROW_H, LIST_W, ROW_H - 6);
				if (!hit)
					continue;
				selected = first + slot;
				pressed |= SCE_CTRL_CROSS;
				break;
			}
		}
		if (pressed & SCE_CTRL_RTRIGGER) {
			grid_mode = !grid_mode;
			vt_preferences_set_file_browser_grid(grid_mode);
			top = 0;
			ui_focus_motion_reset(&focus_motion);
		}
		unsigned int nav = ui_nav_repeat_update(
		    &nav_repeat, pressed, controls.buttons, controls.lx, controls.ly,
		    SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
		if (grid_mode) {
			if ((nav & SCE_CTRL_LEFT) && selected > 0) selected--;
			if ((nav & SCE_CTRL_RIGHT) && selected + 1 < count) selected++;
			if ((nav & SCE_CTRL_UP) && selected >= BROWSER_GRID_COLS)
				selected -= BROWSER_GRID_COLS;
			if ((nav & SCE_CTRL_DOWN) && selected + BROWSER_GRID_COLS < count)
				selected += BROWSER_GRID_COLS;
			int row = selected / BROWSER_GRID_COLS;
			if (row < top) top = row;
			if (row >= top + browser_grid_visible_rows())
				top = row - browser_grid_visible_rows() + 1;
			if (count > 0) {
				int column = selected % BROWSER_GRID_COLS;
				ui_focus_motion_tick(
				    &focus_motion,
				    LIST_X + column * (BROWSER_GRID_CARD_W + BROWSER_GRID_GAP_X),
				    LIST_Y + (row - top) * (BROWSER_GRID_CARD_H + BROWSER_GRID_GAP_Y),
				    BROWSER_GRID_CARD_W, BROWSER_GRID_CARD_H);
			}
		} else {
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
			if (selected < top) top = selected;
			if (selected >= top + visible_rows()) top = selected - visible_rows() + 1;
			if (count > 0)
				ui_focus_motion_tick(&focus_motion, LIST_X,
				                     LIST_Y + (selected - top) * ROW_H,
				                     LIST_W, ROW_H - 6);
		}
		if ((pressed & SCE_CTRL_CROSS) && count > 0) {
			VtNetworkEntry *entry = &entries[selected];
			if (entry->is_directory) {
				snprintf(path, sizeof(path), "%s", entry->path);
				if (load_directory(source, credential, path, entries, &count) == 0) {
					selected = top = 0;
					vt_video_thumbnail_suspend();
					vt_video_thumbnail_resume();
				}
				network_resync_input(&previous, &nav_repeat);
				continue;
			} else if (entry->is_video) {
				if (selection) {
					memset(selection, 0, sizeof(*selection));
					selection->source = *source;
					selection->credential = *credential;
					snprintf(selection->path, sizeof(selection->path), "%s", entry->path);
					snprintf(selection->title, sizeof(selection->title), "%s", entry->name);
				}
				vt_video_thumbnail_suspend();
				free(entries);
				return UI_NETWORK_ACTION_PLAY;
			}
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			char *slash = strrchr(path, '/');
			if (!path[0]) {
				vt_video_thumbnail_suspend();
				free(entries);
				return 0;
			}
			if (slash) *slash = '\0'; else path[0] = '\0';
			if (load_directory(source, credential, path, entries, &count) == 0) {
				selected = top = 0;
				vt_video_thumbnail_suspend();
				vt_video_thumbnail_resume();
			}
			network_resync_input(&previous, &nav_repeat);
			continue;
		}
		draw_browser(source, path, credential, entries, count, selected, top, grid_mode,
		             &focus_motion);
		sceKernelDelayThread(1000);
	}
}

int ui_network_sources_screen(UiNetworkSelection *selection) {
	VtNetworkSource sources[VT_NETWORK_MAX_SOURCES];
	VtNetworkCredential credentials[VT_NETWORK_MAX_SOURCES];
	memset(credentials, 0, sizeof(credentials));
	int count = vt_network_sources_load(sources, VT_NETWORK_MAX_SOURCES);
	if (count < 0) count = 0;
	if (vt_preferences_remember_network_passwords() &&
	    vt_network_credentials_load(credentials, VT_NETWORK_MAX_SOURCES) < 0)
		vt_network_credentials_clear();
	int selected = 0, top = 0, remove_confirm = 0;
	UiFocusMotion focus_motion;
	ui_focus_motion_reset(&focus_motion);
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_NETWORK);
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		int section = UI_SECTION_NONE;
		int confirmation_owned_frame = remove_confirm;
		int sidebar_owned_frame = 0;
		if (remove_confirm) {
			if ((touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 236, 294, 226, 48))
				pressed |= SCE_CTRL_CIRCLE;
			if ((touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 480, 294, 244, 48))
				pressed |= SCE_CTRL_CROSS;
			if ((pressed & SCE_CTRL_CROSS) && count > 0) {
				int removed_index = selected;
				VtNetworkSource removed = sources[removed_index];
				VtNetworkCredential removed_credential = credentials[removed_index];
				for (int i = removed_index; i + 1 < count; i++) {
					sources[i] = sources[i + 1];
					credentials[i] = credentials[i + 1];
				}
				secure_zero(&credentials[count - 1], sizeof(credentials[count - 1]));
				count--;
				int save_result = vt_network_sources_save(sources, count);
				if (save_result < 0) {
					for (int i = count; i > removed_index; i--) {
						sources[i] = sources[i - 1];
						credentials[i] = credentials[i - 1];
					}
					sources[removed_index] = removed;
					credentials[removed_index] = removed_credential;
					count++;
					show_save_error(save_result);
				} else {
					persist_remembered_credentials(credentials, count);
					if (selected >= count && selected > 0) selected--;
					if (top > selected) top = selected;
				}
				secure_zero(&removed_credential, sizeof(removed_credential));
				remove_confirm = 0;
			}
			if (pressed & SCE_CTRL_CIRCLE) remove_confirm = 0;
			pressed = 0;
			touch_flags = 0;
		} else {
			/* The confirmation layer owns all input. Only when it is absent may
			 * the global mini-player or section drawer consume this frame. */
			ui_mini_player_handle_buttons(&pressed);
			if (ui_mini_player_input_locked()) {
				pressed = 0;
				controls.buttons &= SCE_CTRL_SELECT;
				controls.lx = controls.ly = 128;
				touch_flags = UI_TOUCH_EVENT_NONE;
			}
			int was_open = sidebar.open;
			section = ui_sections_sidebar_handle_buttons(
			    &sidebar, &pressed, controls.buttons, controls.ly);
			sidebar_owned_frame = sidebar.open || was_open;
			if (sidebar_owned_frame) {
				int touched = ui_sections_sidebar_handle_touch(
				    &sidebar, touch_flags, touch.x, touch.y);
				if (touched != UI_SECTION_NONE) section = touched;
				touch_flags = UI_TOUCH_EVENT_NONE;
			} else if (sidebar.animation <= 0.01f &&
			           ui_mini_player_handle_touch(touch_flags, &touch)) {
				touch_flags = UI_TOUCH_EVENT_NONE;
			}
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE)
			return finish_network_screen(
			    credentials, UI_NETWORK_ACTION_SECTION_BASE + section);
		if (!sidebar.open && sidebar.animation > 0.01f) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int page_owns_input = !confirmation_owned_frame &&
		                      !sidebar_owned_frame && !sidebar.open &&
		                      sidebar.animation <= 0.01f;
		if (page_owns_input) {
			if ((touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 720, 70, 194, 43))
				pressed |= SCE_CTRL_SQUARE;
			if ((touch_flags & UI_TOUCH_EVENT_UP) &&
			    !(touch_flags & UI_TOUCH_EVENT_TAP) && count > 0 &&
			    touch.down_x >= SOURCE_LIST_X &&
			    touch.down_x < SOURCE_LIST_X + SOURCE_LIST_W &&
			    touch.down_y >= SOURCE_LIST_Y) {
				int dy = touch.y - touch.down_y;
				if (dy < -36) selected += 3;
				else if (dy > 36) selected -= 3;
				if (selected < 0) selected = 0;
				if (selected >= count) selected = count - 1;
			}
			if ((touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
				for (int slot = 0; slot < source_render_rows() &&
				                       top + slot < count; slot++) {
					if (!ui_touch_hit_rect(touch.x, touch.y, SOURCE_LIST_X,
					                       SOURCE_LIST_Y + slot * SOURCE_ROW_H,
					                       SOURCE_LIST_W, SOURCE_ROW_H - 8))
						continue;
					selected = top + slot;
					pressed |= SCE_CTRL_CROSS;
					break;
				}
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 314, 272, 42))
					pressed |= SCE_CTRL_CROSS;
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 366, 272, 42))
					pressed |= SCE_CTRL_TRIANGLE;
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 418, 272, 42))
					pressed |= SCE_CTRL_SELECT;
			}
			unsigned int nav = ui_nav_repeat_update(
			    &nav_repeat, pressed, controls.buttons, controls.lx, controls.ly,
			    SCE_CTRL_UP | SCE_CTRL_DOWN);
			if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
			if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
			if (selected < top) top = selected;
			if (selected >= top + source_visible_rows())
				top = selected - source_visible_rows() + 1;
			if (count > 0)
				ui_focus_motion_tick(&focus_motion, SOURCE_LIST_X,
				                     SOURCE_LIST_Y + (selected - top) * SOURCE_ROW_H,
				                     SOURCE_LIST_W, SOURCE_ROW_H - 8);
			if ((pressed & SCE_CTRL_SQUARE) && count < VT_NETWORK_MAX_SOURCES) {
				VtNetworkSource source;
				VtNetworkCredential credential;
				memset(&credential, 0, sizeof(credential));
				if (source_editor(&source, &credential, 1)) {
					sources[count] = source;
					credentials[count] = credential;
					count++;
					selected = count - 1;
					int save_result = vt_network_sources_save(sources, count);
					if (save_result < 0) {
						count--;
						secure_zero(&credentials[count], sizeof(credentials[count]));
						selected = count > 0 ? count - 1 : 0;
						show_save_error(save_result);
					} else persist_remembered_credentials(credentials, count);
				}
				secure_zero(&credential, sizeof(credential));
				ui_touch_reset();
				sceCtrlPeekBufferPositive(0, &previous, 1);
				ui_nav_repeat_reset(&nav_repeat);
			}
			if ((pressed & SCE_CTRL_TRIANGLE) && count > 0) {
				VtNetworkSource original = sources[selected];
				VtNetworkCredential original_credential = credentials[selected];
				int edited = source_editor(&sources[selected], &credentials[selected], 0);
				if (edited) {
					int save_result = vt_network_sources_save(sources, count);
					if (save_result < 0) {
						sources[selected] = original;
						credentials[selected] = original_credential;
						show_save_error(save_result);
					} else persist_remembered_credentials(credentials, count);
				} else credentials[selected] = original_credential;
				secure_zero(&original_credential, sizeof(original_credential));
				ui_touch_reset();
				sceCtrlPeekBufferPositive(0, &previous, 1);
				ui_nav_repeat_reset(&nav_repeat);
			}
			if ((pressed & SCE_CTRL_SELECT) && count > 0) {
				remove_confirm = 1;
			}
			if ((pressed & SCE_CTRL_CROSS) && count > 0) {
				int trust = prepare_sftp_trust(&sources[selected]);
				if (trust > 0) {
					int save_result = vt_network_sources_save(sources, count);
					if (save_result < 0) show_save_error(save_result);
					else persist_remembered_credentials(credentials, count);
				}
				network_resync_input(&previous, &nav_repeat);
				if (trust < 0) continue;
				VtNetworkCredential *credential = &credentials[selected];
				if (!credential->password[0]) {
					int password_result = ui_text_input_secure(
					    vt_i18n_str(VT_STR_NETWORK_PASSWORD_PROMPT), "",
					    credential->password, sizeof(credential->password));
					network_resync_input(&previous, &nav_repeat);
					if (password_result <= 0) {
						secure_zero(credential, sizeof(*credential));
						continue;
					}
				}
				int browse_result = browse_source(&sources[selected], credential,
				                                  selection);
				if (browse_result == VT_NETWORK_TLS_TRUST_REQUIRED &&
				    sources[selected].protocol == VT_NETWORK_WEBDAV &&
				    !sources[selected].tls_public_key_sha256[0]) {
					int tls_trust = prepare_webdav_trust(&sources[selected], credential);
					if (tls_trust > 0) {
						int save_result = vt_network_sources_save(sources, count);
						if (save_result < 0) show_save_error(save_result);
						else persist_remembered_credentials(credentials, count);
						browse_result = browse_source(&sources[selected], credential,
						                              selection);
					}
				}
				if (browse_result == VT_NETWORK_AUTH_FAILED)
					secure_zero(credential, sizeof(*credential));
				else if (browse_result >= 0)
					persist_remembered_credentials(credentials, count);
				network_resync_input(&previous, &nav_repeat);
				if (browse_result == UI_NETWORK_ACTION_PLAY)
					return finish_network_screen(credentials,
					                             UI_NETWORK_ACTION_PLAY);
			}
			if (pressed & SCE_CTRL_CIRCLE)
				return finish_network_screen(credentials, UI_NETWORK_ACTION_BACK);
		} else ui_nav_repeat_reset(&nav_repeat);
		draw_sources(sources, count, selected, top, remove_confirm,
		             &focus_motion, &sidebar);
		sceKernelDelayThread(1000);
	}
}

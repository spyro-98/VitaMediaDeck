#include "ui/network_sources_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

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

static int visible_rows(void) {
	return ui_mini_player_visible() ? 6 : VISIBLE_ROWS;
}

static int source_visible_rows(void) {
	return ui_mini_player_visible() ? 5 : 6;
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

static void clip(vita2d_font *font, unsigned size, const char *source,
	             char *out, size_t out_size, int width) {
	snprintf(out, out_size, "%s", source ? source : "");
	while (out[0] && ui_font_text_width(font, size, out) > width) {
		size_t length = strlen(out);
		do { out[--length] = '\0'; }
		while (length && ((unsigned char)out[length] & 0xc0) == 0x80);
	}
}

static void draw_sources(const VtNetworkSource *sources, int count,
	                     int selected, int top, UiSectionsSidebar *sidebar) {
	ui_mini_player_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_header_placeholder(NULL, "Network sources");
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body) ui_font_draw_text(body, SOURCE_LIST_X, 94, VT_THEME_TEXT,
	                            UI_FONT_BODY, "Your media servers");
	if (small) ui_font_draw_text(small, SOURCE_LIST_X, 116, VT_THEME_TEXT,
	                             UI_FONT_SMALL,
	                             "Private connections. Passwords stay in this session.");
	ui_action_button(720, 70, 194, 43, VT_THEME_BLUE_BRIGHT,
	                 "Square", "Add source", 0);
	if (!count) {
		ui_panel(SOURCE_LIST_X, SOURCE_LIST_Y, SOURCE_LIST_W, 220,
		         VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, SOURCE_LIST_X + 30, 210,
		                             VT_THEME_TEXT, UI_FONT_BODY,
		                             "No network source yet");
		if (small) ui_font_draw_text(small, SOURCE_LIST_X + 30, 244,
		                              VT_THEME_TEXT, UI_FONT_SMALL,
		                              "Add WebDAV, SFTP or SMB to browse it here.");
	} else {
		int viewport_bottom = ui_mini_player_visible() ? UI_MINI_PLAYER_Y : 532;
		ui_focus_glow_draw(SOURCE_LIST_X,
		                   SOURCE_LIST_Y + (selected - top) * SOURCE_ROW_H,
		                   SOURCE_LIST_W, SOURCE_ROW_H - 8,
		                   sceKernelGetProcessTimeWide(), SOURCE_LIST_Y,
		                   viewport_bottom);
		vita2d_set_clip_rectangle(0, SOURCE_LIST_Y, 628, viewport_bottom);
		vita2d_enable_clipping();
		for (int i = top; i < count && i < top + source_visible_rows(); i++) {
			int y = SOURCE_LIST_Y + (i - top) * SOURCE_ROW_H;
			ui_panel(SOURCE_LIST_X, y, SOURCE_LIST_W, SOURCE_ROW_H - 8,
			         i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         sources[i].protocol == VT_NETWORK_SFTP ? VT_THEME_BLUE_LIGHT
			                                              : VT_THEME_BLUE_BRIGHT,
			         i == selected);
			if (body) ui_font_draw_text(body, SOURCE_LIST_X + 22, y + 29,
			                            VT_THEME_TEXT, UI_FONT_BODY, sources[i].name);
			if (small) {
				char endpoint[320];
				snprintf(endpoint, sizeof(endpoint), "%s:%u",
				         sources[i].host, sources[i].port);
				ui_font_draw_text(small, SOURCE_LIST_X + 22, y + 49,
				                  VT_THEME_TEXT, UI_FONT_SMALL, endpoint);
				const char *protocol = vt_network_protocol_name(sources[i].protocol);
				int width = ui_font_text_width(small, UI_FONT_SMALL, protocol);
				ui_font_draw_text(small, SOURCE_LIST_X + SOURCE_LIST_W - width - 18,
				                  y + 32, VT_THEME_TEXT, UI_FONT_SMALL, protocol);
			}
		}
		vita2d_disable_clipping();
		const VtNetworkSource *source = &sources[selected];
		ui_panel(642, SOURCE_LIST_Y, 272, 174, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_LIGHT, 0);
		if (small) {
			ui_font_draw_text(small, 666, 154, VT_THEME_TEXT, UI_FONT_SMALL,
			                  "CONNECTION");
			ui_font_draw_text(small, 666, 188, VT_THEME_TEXT, UI_FONT_SMALL,
			                  vt_network_protocol_name(source->protocol));
			ui_font_draw_text(small, 666, 218, VT_THEME_TEXT, UI_FONT_SMALL,
			                  source->username);
			ui_font_draw_text(small, 666, 248, VT_THEME_TEXT, UI_FONT_SMALL,
			                  source->protocol == VT_NETWORK_SFTP
			                      ? (source->host_key_sha256[0] ? "Host key pinned"
			                                                     : "Host key not verified")
			                      : "Authenticated session");
		}
		ui_action_button(642, 314, 272, 42, VT_THEME_BLUE_BRIGHT,
		                 "Cross", "Browse", 1);
		ui_action_button(642, 366, 272, 42, VT_THEME_SURFACE_FOCUS,
		                 "Triangle", "Edit", 0);
		ui_action_button(642, 418, 272, 42, VT_THEME_SURFACE,
		                 "Select", "Remove", 0);
	}
	if (sidebar && sidebar->animation > .01f)
		ui_sections_sidebar_draw(sidebar->cursor, sidebar->animation,
		                         sidebar->focus_cursor);
	ui_mini_player_draw();
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static void draw_browser(const VtNetworkSource *source, const char *path,
	                     const VtNetworkEntry *entries, int count,
	                     int selected, int top) {
	ui_mini_player_pump();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_header_placeholder(NULL, source->name);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	char breadcrumb[192];
	snprintf(breadcrumb, sizeof(breadcrumb), "%s / %s", source->name,
	         path && path[0] ? path : "");
	ui_panel(LIST_X, 68, LIST_W, 34, VT_THEME_SURFACE,
	         VT_THEME_BLUE_LIGHT, 0);
	if (small) ui_font_draw_text(small, LIST_X + 16, 91, VT_THEME_TEXT,
	                            UI_FONT_SMALL, breadcrumb);
	if (!count) {
		ui_panel(220, 202, 520, 132, VT_THEME_SURFACE,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 280, 258, VT_THEME_TEXT,
		                             UI_FONT_BODY, "This folder has no playable media");
		if (small) ui_font_draw_text(small, 280, 290, VT_THEME_TEXT,
		                              UI_FONT_SMALL, "Open another folder or go back.");
	} else {
		int viewport_bottom = ui_mini_player_visible() ? UI_MINI_PLAYER_Y : 532;
		ui_focus_glow_draw(LIST_X, LIST_Y + (selected - top) * ROW_H,
		                   LIST_W, ROW_H - 6, sceKernelGetProcessTimeWide(),
		                   LIST_Y, viewport_bottom);
		vita2d_set_clip_rectangle(0, LIST_Y, 960, viewport_bottom);
		vita2d_enable_clipping();
		for (int i = top; i < count && i < top + visible_rows(); i++) {
			int y = LIST_Y + (i - top) * ROW_H;
			const VtNetworkEntry *entry = &entries[i];
			ui_panel(LIST_X, y, LIST_W, ROW_H - 6,
			         i == selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         entry->is_directory ? VT_THEME_BLUE_LIGHT : VT_THEME_BLUE_BRIGHT,
			         i == selected);
			if (body) {
				char title[192];
				clip(body, UI_FONT_BODY, entry->name, title, sizeof(title), 600);
				ui_font_draw_text(body, LIST_X + 22, y + 33, VT_THEME_TEXT,
				                  UI_FONT_BODY, title);
			}
			if (small) {
				char detail[64];
				if (entry->is_directory) snprintf(detail, sizeof(detail), "Folder");
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

static int source_valid(const VtNetworkSource *source) {
	return source && source->name[0] && source->host[0] &&
	       source->username[0] &&
	       (source->protocol != VT_NETWORK_SMB || source->share[0]);
}

static int source_editor(VtNetworkSource *source, int is_new) {
	VtNetworkSource draft;
	memset(&draft, 0, sizeof(draft));
	if (!is_new) draft = *source;
	else {
		draft.protocol = VT_NETWORK_WEBDAV;
		draft.port = 443;
		snprintf(draft.name, sizeof(draft.name), "Media server");
	}
	int cursor = 0;
	unsigned previous = 0;
	for (;;) {
		ui_mini_player_pump();
		vita2d_start_drawing();
		vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
		ui_brand_draw_header_placeholder(NULL, is_new ? "Add source" : "Edit source");
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		const char *labels[] = { "Protocol", "Name", "Host", "Port", "Root path",
		                         "SMB share", "Username", "SMB domain", "Save" };
		char port[16]; snprintf(port, sizeof(port), "%u", draft.port);
		const char *values[] = { vt_network_protocol_name(draft.protocol), draft.name,
		                         draft.host, port, draft.root_path, draft.share,
		                         draft.username, draft.domain, "" };
		ui_panel(42, 84, 220, 376, VT_THEME_SURFACE_RAISED,
		         VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 66, 126, VT_THEME_TEXT, UI_FONT_BODY,
		                            vt_network_protocol_name(draft.protocol));
		if (small) {
			ui_font_draw_text(small, 66, 162, VT_THEME_TEXT, UI_FONT_SMALL,
			                  draft.protocol == VT_NETWORK_WEBDAV
			                      ? "HTTPS media library"
			                      : draft.protocol == VT_NETWORK_SFTP
			                            ? "Encrypted SSH files"
			                            : "Signed SMB2/SMB3 share");
			ui_font_draw_text(small, 66, 218, VT_THEME_TEXT, UI_FONT_SMALL,
			                  "Credentials are requested");
			ui_font_draw_text(small, 66, 240, VT_THEME_TEXT, UI_FONT_SMALL,
			                  "when the source opens.");
			ui_font_draw_text(small, 66, 302, VT_THEME_TEXT, UI_FONT_SMALL,
			                  draft.protocol == VT_NETWORK_SFTP
			                      ? "SSH fingerprint is pinned"
			                      : "Passwords are never saved");
			ui_font_draw_text(small, 66, 374, VT_THEME_TEXT, UI_FONT_SMALL,
			                  source_valid(&draft) ? "Ready to save"
			                                       : "Name, host and user required");
		}
		ui_action_button(62, 406, 180, 42, VT_THEME_SURFACE,
		                 "Circle", "Cancel", 0);
		for (int i = 0; i < 8; i++) {
			int y = 84 + i * 43;
			unsigned fill = i == cursor ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE;
			ui_panel(288, y, 626, 37, fill, VT_THEME_BLUE_BRIGHT, i == cursor);
			if (small) ui_font_draw_text(small, 306, y + 25, VT_THEME_TEXT,
			                             UI_FONT_SMALL, labels[i]);
			if (body && values[i][0]) {
				char value[192];
				clip(body, UI_FONT_BODY, values[i], value, sizeof(value), 394);
				int width = ui_font_text_width(body, UI_FONT_BODY, value);
				ui_font_draw_text(body, 892 - width, y + 27, VT_THEME_TEXT,
				                  UI_FONT_BODY, value);
			}
		}
		ui_action_button(288, 432, 626, 42,
		                 cursor == 8 ? VT_THEME_BLUE_BRIGHT : VT_THEME_SURFACE_FOCUS,
		                 "Cross", source_valid(&draft)
		                     ? (is_new ? "Save source" : "Save changes")
		                     : "Complete required fields",
		                 cursor == 8);
		ui_mini_player_draw();
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		SceCtrlData controls;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous;
		previous = controls.buttons;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch)) touch_flags = 0;
		if (ui_mini_player_input_locked()) pressed = 0;
		if (touch_flags & UI_TOUCH_EVENT_TAP) {
			if (ui_touch_hit_rect(touch.x, touch.y, 288, 84, 626, 344)) {
				int row = (touch.y - 84) / 43;
				if (row >= 0 && row < 8) {
					cursor = row;
					pressed |= row == 0 ? SCE_CTRL_RIGHT : SCE_CTRL_CROSS;
				}
			} else if (ui_touch_hit_rect(touch.x, touch.y, 288, 432, 626, 42)) {
				cursor = 8;
				pressed |= SCE_CTRL_CROSS;
			} else if (ui_touch_hit_rect(touch.x, touch.y, 62, 406, 180, 42)) {
				pressed |= SCE_CTRL_CIRCLE;
			}
		}
		if ((pressed & SCE_CTRL_UP) && cursor > 0) cursor--;
		if ((pressed & SCE_CTRL_DOWN) && cursor < 8) cursor++;
		if (cursor == 0 && (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
			int protocol = (int)draft.protocol + ((pressed & SCE_CTRL_RIGHT) ? 1 : -1);
			if (protocol < VT_NETWORK_WEBDAV) protocol = VT_NETWORK_SMB;
			if (protocol > VT_NETWORK_SMB) protocol = VT_NETWORK_WEBDAV;
			draft.protocol = (VtNetworkProtocol)protocol;
			if (draft.protocol == VT_NETWORK_WEBDAV) draft.port = 443;
			else if (draft.protocol == VT_NETWORK_SFTP) draft.port = 22;
			else draft.port = 445;
		}
		if (pressed & SCE_CTRL_CROSS) {
			if (cursor == 1) edit_text("Source name", draft.name, sizeof(draft.name));
			else if (cursor == 2) edit_text("Server host", draft.host, sizeof(draft.host));
			else if (cursor == 3) {
				char value[16]; snprintf(value, sizeof(value), "%u", draft.port);
				if (edit_text("Port", value, sizeof(value)) > 0)
					draft.port = (uint16_t)strtoul(value, NULL, 10);
			} else if (cursor == 4) edit_text("Root path", draft.root_path, sizeof(draft.root_path));
			else if (cursor == 5) edit_text("SMB share", draft.share, sizeof(draft.share));
			else if (cursor == 6) edit_text("Username", draft.username, sizeof(draft.username));
			else if (cursor == 7) edit_text("SMB domain", draft.domain, sizeof(draft.domain));
			else if (cursor == 8 && source_valid(&draft)) {
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
	int ret = ui_loading_run("Reading remote folder", list_task, &task,
	                         NULL, NULL, NULL);
	if (ret < 0 || task.result < 0) {
		ui_message_show("Connection failed",
		                task.detail[0] ? task.detail : "Unable to read this source", 2800);
		return task.result < 0 ? task.result : ret;
	}
	*count = task.result;
	return 0;
}

static int prepare_sftp_trust(VtNetworkSource *source) {
	if (source->protocol != VT_NETWORK_SFTP || source->host_key_sha256[0]) return 0;
	FingerprintTask task = { source, { 0 }, { 0 } };
	int ret = ui_loading_run("Checking SSH host key", fingerprint_task,
	                         &task, NULL, NULL, NULL);
	if (ret < 0 || !task.fingerprint[0]) {
		ui_message_show("Host key unavailable",
		                task.detail[0] ? task.detail : "SSH handshake failed", 2800);
		return -1;
	}
	char confirmed[VT_NETWORK_FINGERPRINT_MAX];
	int accepted = ui_text_input("Verify SSH SHA-256 fingerprint",
	                            task.fingerprint, confirmed, sizeof(confirmed));
	if (accepted <= 0 || strcmp(confirmed, task.fingerprint) != 0) return -1;
	snprintf(source->host_key_sha256, sizeof(source->host_key_sha256), "%s",
	         task.fingerprint);
	return 1;
}

static int browse_source(const VtNetworkSource *source,
	                     const VtNetworkCredential *credential,
	                     UiNetworkSelection *selection) {
	VtNetworkEntry *entries = calloc(VT_NETWORK_MAX_ENTRIES, sizeof(*entries));
	if (!entries) {
		ui_message_show("Out of memory", "Unable to open the remote browser", 2600);
		return 0;
	}
	char path[VT_NETWORK_PATH_MAX] = "";
	int count = 0;
	if (load_directory(source, credential, path, entries, &count) < 0) {
		free(entries);
		return 0;
	}
	int selected = 0, top = 0;
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
		if (ui_mini_player_input_locked()) pressed = 0;
		if ((touch_flags & UI_TOUCH_EVENT_UP) &&
		    !(touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
			int dy = touch.y - touch.down_y;
			if (dy < -36) selected += 3;
			else if (dy > 36) selected -= 3;
			if (selected < 0) selected = 0;
			if (selected >= count) selected = count - 1;
		}
		if ((touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
			int slot = (touch.y - LIST_Y) / ROW_H;
			if (touch.y >= LIST_Y && slot >= 0 && slot < visible_rows() &&
			    top + slot < count) {
				selected = top + slot;
				pressed |= SCE_CTRL_CROSS;
			}
		}
		if ((pressed & SCE_CTRL_UP) && selected > 0) selected--;
		if ((pressed & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
		if (selected < top) top = selected;
		if (selected >= top + visible_rows()) top = selected - visible_rows() + 1;
		if ((pressed & SCE_CTRL_CROSS) && count > 0) {
			VtNetworkEntry *entry = &entries[selected];
			if (entry->is_directory) {
				snprintf(path, sizeof(path), "%s", entry->path);
				if (load_directory(source, credential, path, entries, &count) == 0)
					selected = top = 0;
			} else if (entry->is_video) {
				if (selection) {
					memset(selection, 0, sizeof(*selection));
					selection->source = *source;
					selection->credential = *credential;
					snprintf(selection->path, sizeof(selection->path), "%s", entry->path);
					snprintf(selection->title, sizeof(selection->title), "%s", entry->name);
				}
				free(entries);
				return UI_NETWORK_ACTION_PLAY;
			}
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			char *slash = strrchr(path, '/');
			if (!path[0]) {
				free(entries);
				return 0;
			}
			if (slash) *slash = '\0'; else path[0] = '\0';
			if (load_directory(source, credential, path, entries, &count) == 0)
				selected = top = 0;
		}
		draw_browser(source, path, entries, count, selected, top);
		sceKernelDelayThread(1000);
	}
}

int ui_network_sources_screen(UiNetworkSelection *selection) {
	VtNetworkSource sources[VT_NETWORK_MAX_SOURCES];
	int count = vt_network_sources_load(sources, VT_NETWORK_MAX_SOURCES);
	if (count < 0) count = 0;
	int selected = 0, top = 0;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_NETWORK);
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		if (ui_mini_player_input_locked()) {
			pressed = 0;
			controls.buttons &= SCE_CTRL_SELECT;
			controls.ly = 128;
		}
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (ui_mini_player_handle_touch(touch_flags, &touch)) touch_flags = 0;
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		                                                  controls.buttons, controls.ly);
		if (sidebar.open) {
			int touched = ui_sections_sidebar_handle_touch(&sidebar, touch_flags,
			                                                touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE)
			return UI_NETWORK_ACTION_SECTION_BASE + section;
		if (!sidebar.open) {
			if ((touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 720, 70, 194, 43))
				pressed |= SCE_CTRL_SQUARE;
			if ((touch_flags & UI_TOUCH_EVENT_UP) &&
			    !(touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
				int dy = touch.y - touch.down_y;
				if (dy < -36) selected += 3;
				else if (dy > 36) selected -= 3;
				if (selected < 0) selected = 0;
				if (selected >= count) selected = count - 1;
			}
			if ((touch_flags & UI_TOUCH_EVENT_TAP) && count > 0) {
				int slot = (touch.y - SOURCE_LIST_Y) / SOURCE_ROW_H;
				if (touch.x >= SOURCE_LIST_X && touch.x < SOURCE_LIST_X + SOURCE_LIST_W &&
				    touch.y >= SOURCE_LIST_Y && slot >= 0 &&
				    slot < source_visible_rows() &&
				    top + slot < count) {
					selected = top + slot;
					pressed |= SCE_CTRL_CROSS;
				}
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 314, 272, 42))
					pressed |= SCE_CTRL_CROSS;
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 366, 272, 42))
					pressed |= SCE_CTRL_TRIANGLE;
				if (ui_touch_hit_rect(touch.x, touch.y, 642, 418, 272, 42))
					pressed |= SCE_CTRL_SELECT;
			}
			if ((pressed & SCE_CTRL_UP) && selected > 0) selected--;
			if ((pressed & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
			if (selected < top) top = selected;
			if (selected >= top + source_visible_rows())
				top = selected - source_visible_rows() + 1;
			if ((pressed & SCE_CTRL_SQUARE) && count < VT_NETWORK_MAX_SOURCES) {
				VtNetworkSource source;
				if (source_editor(&source, 1)) {
					sources[count++] = source;
					selected = count - 1;
					vt_network_sources_save(sources, count);
				}
			}
			if ((pressed & SCE_CTRL_TRIANGLE) && count > 0 &&
			    source_editor(&sources[selected], 0))
				vt_network_sources_save(sources, count);
			if ((pressed & SCE_CTRL_SELECT) && count > 0) {
				for (int i = selected; i + 1 < count; i++) sources[i] = sources[i + 1];
				count--;
				if (selected >= count && selected > 0) selected--;
				vt_network_sources_save(sources, count);
			}
			if ((pressed & SCE_CTRL_CROSS) && count > 0) {
				int trust = prepare_sftp_trust(&sources[selected]);
				if (trust > 0) vt_network_sources_save(sources, count);
				if (trust < 0) continue;
				VtNetworkCredential credential;
				memset(&credential, 0, sizeof(credential));
				if (ui_text_input("Password (not saved)", "", credential.password,
				                  sizeof(credential.password)) <= 0) continue;
				int browse_result = browse_source(&sources[selected], &credential,
				                                  selection);
				memset(&credential, 0, sizeof(credential));
				if (browse_result == UI_NETWORK_ACTION_PLAY)
					return UI_NETWORK_ACTION_PLAY;
			}
			if (pressed & SCE_CTRL_CIRCLE) return UI_NETWORK_ACTION_BACK;
		}
		draw_sources(sources, count, selected, top, &sidebar);
		sceKernelDelayThread(1000);
	}
}

#include "ui/folder_picker.h"
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include "i18n/i18n.h"
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
#define DESTINATION_MAX_FOLDERS 96

static int destination_join(const char *directory, const char *name,
	                        char *out, size_t out_size) {
	size_t length = strlen(directory);
	const char *separator = length && directory[length - 1] == ':' ? "" : "/";
	int written = snprintf(out, out_size, "%s%s%s", directory, separator, name);
	return written > 0 && written < (int)out_size ? 0 : -1;
}

static void destination_parent(char *path) {
	char *colon, *slash;
	if (!path || !path[0]) return;
	colon = strchr(path, ':');
	slash = strrchr(path, '/');
	if (slash && (!colon || slash > colon)) *slash = '\0';
	else if (colon && colon[1]) colon[1] = '\0';
}

static int destination_read_folders(const char *path,
	                                char names[][128], int capacity) {
	SceUID directory = sceIoDopen(path);
	if (directory < 0) return directory;
	int count = 0;
	while (count < capacity) {
		SceIoDirent entry;
		memset(&entry, 0, sizeof(entry));
		int result = sceIoDread(directory, &entry);
		if (result <= 0) break;
		if (entry.d_name[0] == '.' || !SCE_S_ISDIR(entry.d_stat.st_mode)) continue;
		snprintf(names[count++], 128, "%s", entry.d_name);
	}
	sceIoDclose(directory);
	return count;
}

static void draw_destination_picker(const char *path, char names[][128],
	                                int count, int selected, int top) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_header_placeholder(NULL,
	                                 vt_i18n_str(VT_STR_NETWORK_DESTINATION_TITLE));
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* This page deliberately avoids a second scene heading and explanatory copy:
	 * the top bar already names the task, while the action buttons describe the
	 * available operations. The label and path each own a full-width row. */
	ui_panel(66, 76, 828, 62, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
	if (small) {
		char fitted_path[512];
		const char *label = vt_i18n_str(VT_STR_NETWORK_CURRENT_DESTINATION);
		ui_font_draw_text(small, 84, 100, VT_THEME_SIGNAL_LIGHT,
		                  UI_FONT_SMALL, label);
		ui_font_fit_text(small, UI_FONT_SMALL, path, fitted_path,
		                 sizeof(fitted_path), 792);
		ui_font_draw_text(small, 84, 128, VT_THEME_TEXT,
		                  UI_FONT_SMALL, fitted_path);
	}
	if (!count) {
		ui_panel(66, 152, 828, 256, VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 94, 232, VT_THEME_TEXT, UI_FONT_BODY,
		                             vt_i18n_str(VT_STR_NETWORK_EMPTY_FOLDER));
	} else {
		for (int i = top; i < count && i < top + 4; i++) {
			int y = 152 + (i - top) * 64;
			int focused = i == selected;
			ui_panel(66, y, 828, 58,
			         focused ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			         focused ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BLUE_LIGHT, 0);
			if (focused) {
				vita2d_draw_rectangle(66, y, 6, 58, VT_THEME_SPECTRAL);
				vita2d_draw_rectangle(72, y, 2, 58, VT_THEME_SIGNAL_BRIGHT);
				vita2d_draw_rectangle(66, y, 828, 3, VT_THEME_SIGNAL_LIGHT);
			}
			if (body) ui_font_draw_text(body, 92, y + 38, VT_THEME_TEXT,
			                             UI_FONT_BODY, names[i]);
		}
	}
	ui_action_button(66, 468, 250, 42, VT_THEME_BLUE_BRIGHT, "Start",
	                 vt_i18n_str(VT_STR_NETWORK_USE_THIS_FOLDER), 0);
	ui_action_button(350, 468, 250, 42, VT_THEME_SURFACE_RAISED, "Triangle",
	                 vt_i18n_str(VT_STR_NETWORK_NEW_FOLDER), 0);
	ui_action_button(634, 468, 260, 42, VT_THEME_SURFACE, "Circle",
	                 strchr(path, ':') && !strchr(path, ':')[1] ? vt_i18n_str(VT_STR_NETWORK_CANCEL) : "Up", 0);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static int destination_name_valid(const char *name) {
	return name && name[0] && !strchr(name, '/') && !strchr(name, '\\') && !strchr(name, ':') &&
	       strcmp(name, ".") && strcmp(name, "..");
}

int ui_folder_picker(const char *initial, char *out, size_t out_size) {
	char path[512];
	char names[DESTINATION_MAX_FOLDERS][128];
	SceCtrlData previous;
	UiNavRepeat nav_repeat;
	int selected = 0, top = 0;
	/* Opening the picker is read-only. Prefer the system downloads directory
	 * when it exists, but never create it (or any other folder) implicitly. */
	SceUID downloads_dir = sceIoDopen("ux0:download");
	if (downloads_dir >= 0) {
		sceIoDclose(downloads_dir);
		snprintf(path, sizeof(path), "ux0:download");
	} else {
		snprintf(path, sizeof(path), "ux0:");
	}
	if (initial && initial[0]) {
		if (strlen(initial) >= sizeof(path) || !strchr(initial, ':')) return 0;
		snprintf(path, sizeof(path), "%s", initial);
	}
	if (!out || out_size == 0) return 0;
	ui_touch_reset();
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_nav_repeat_reset(&nav_repeat);
	for (;;) {
		int count = destination_read_folders(path, names, DESTINATION_MAX_FOLDERS);
		if (count < 0) {
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_FOLDER_CREATE_FAILED), path, 2600);
			return 0;
		}
		if (selected >= count) selected = count > 0 ? count - 1 : 0;
		if (top > selected) top = selected;
		if (selected >= top + 4) top = selected - 3;
		ui_mini_player_pump();
		draw_destination_picker(path, names, count, selected, top);
		SceCtrlData controls;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		unsigned nav = ui_nav_repeat_update(&nav_repeat, pressed, controls.buttons,
		                                    controls.lx, controls.ly,
		                                    SCE_CTRL_UP | SCE_CTRL_DOWN);
		if ((nav & SCE_CTRL_UP) && selected > 0) selected--;
		if ((nav & SCE_CTRL_DOWN) && selected + 1 < count) selected++;
		UiTouchEvent touch;
		unsigned flags = ui_touch_poll(&touch);
		if (flags & UI_TOUCH_EVENT_TAP) {
			for (int i = top; i < count && i < top + 4; i++)
				if (ui_touch_hit_rect(touch.x, touch.y, 66, 152 + (i - top) * 64, 828, 58)) {
					selected = i;
					pressed |= SCE_CTRL_CROSS;
				}
			if (ui_touch_hit_rect(touch.x, touch.y, 66, 468, 250, 42)) pressed |= SCE_CTRL_START;
			if (ui_touch_hit_rect(touch.x, touch.y, 350, 468, 250, 42)) pressed |= SCE_CTRL_TRIANGLE;
			if (ui_touch_hit_rect(touch.x, touch.y, 634, 468, 260, 42)) pressed |= SCE_CTRL_CIRCLE;
		}
		if (pressed & SCE_CTRL_START) {
			if (strlen(path) >= out_size) return 0;
			ui_touch_reset();
			snprintf(out, out_size, "%s", path);
			return 1;
		}
		if ((pressed & SCE_CTRL_CROSS) && count > 0) {
			char next[512];
			if (destination_join(path, names[selected], next, sizeof(next)) == 0) {
				snprintf(path, sizeof(path), "%s", next);
				selected = top = 0;
			}
		}
		if (pressed & SCE_CTRL_TRIANGLE) {
			char name[128] = "";
			if (ui_text_input(vt_i18n_str(VT_STR_NETWORK_FOLDER_NAME_PROMPT), "",
			                  name, sizeof(name)) > 0 && destination_name_valid(name)) {
				char next[512];
				if (destination_join(path, name, next, sizeof(next)) < 0 ||
				    sceIoMkdir(next, 0777) < 0)
					ui_message_show(vt_i18n_str(VT_STR_NETWORK_FOLDER_CREATE_FAILED),
					                name, 2600);
			}
			ui_touch_reset();
			sceCtrlPeekBufferPositive(0, &previous, 1);
			ui_nav_repeat_reset(&nav_repeat);
		}
		if (pressed & SCE_CTRL_CIRCLE) {
			if (strchr(path, ':') && !strchr(path, ':')[1]) {
				ui_touch_reset();
				return 0;
			}
			destination_parent(path);
			selected = top = 0;
		}
		sceKernelDelayThread(16 * 1000);
	}
}

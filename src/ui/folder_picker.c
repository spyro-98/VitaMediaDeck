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
#include "ui/layout.h"
#define DESTINATION_ROWS 6
#define DESTINATION_Y 126
#define DESTINATION_STEP 58
#include "ui/components.h"
#include "ui/focus_glow.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/theme_scene.h"
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

static UiPageView destination_page(const char *path,int top,int count) {
 return ui_theme_page(UI_SCENE_PICKER,"destination",path,top,count,1,DESTINATION_ROWS,DESTINATION_STEP);
}
static UiThemeRect destination_item_rect(int index,int top) {
 return ui_theme_page_rect(UI_SCENE_PICKER,(UiThemeRect){UI_CONTENT_X,
  DESTINATION_Y+(index-top)*DESTINATION_STEP,UI_CONTENT_W,54});
}
static void draw_destination_picker(const char *path, char names[][128],
	                                int count, int selected, int top, UiFocusMotion *motion) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_LIGHT);
	ui_brand_draw_path(vt_i18n_str(VT_STR_NETWORK_DESTINATION_TITLE), path);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	/* This page deliberately avoids a second scene heading and explanatory copy:
	 * the top bar already names the task, while the action buttons describe the
	 * available operations. The label and path each own a full-width row. */
	ui_panel(UI_CONTENT_X, 62, UI_CONTENT_W, 56, VT_THEME_SURFACE_RAISED, VT_THEME_BLUE_LIGHT, 0);
	if (small) {
		char fitted_path[512];
		const char *label = vt_i18n_str(VT_STR_NETWORK_CURRENT_DESTINATION);
		ui_font_draw_text(small, 32, 84, VT_THEME_SIGNAL_LIGHT,
		                  UI_FONT_SMALL, label);
		ui_font_fit_text(small, UI_FONT_SMALL, path, fitted_path,
		                 sizeof(fitted_path), UI_CONTENT_W - 28);
		ui_font_draw_text(small, 32, 109, VT_THEME_TEXT,
		                  UI_FONT_SMALL, fitted_path);
	}
	if (!count) {
		ui_panel(UI_CONTENT_X, DESTINATION_Y, UI_CONTENT_W, 344, VT_THEME_SURFACE, VT_THEME_BLUE_LIGHT, 0);
		if (body) ui_font_draw_text(body, 94, 232, VT_THEME_TEXT, UI_FONT_BODY,
		                             vt_i18n_str(VT_STR_NETWORK_EMPTY_FOLDER));
	} else {
        UiPageView page=destination_page(path,top,count);
        vita2d_set_clip_rectangle(0,DESTINATION_Y-4,960,484);vita2d_enable_clipping();
        for (int i=page.first;i<page.end;i++) {
            UiThemeRect rect=destination_item_rect(i,top);int y=rect.y;
			int focused = i == selected;
			ui_panel(rect.x, y, UI_CONTENT_W, 54,
			         VT_THEME_SURFACE, VT_THEME_BORDER_DIM, 0);
			if (focused)ui_focus_motion_tick(motion,rect.x,y,UI_CONTENT_W,54);
			if (body) {
				char fitted[128];
				ui_font_fit_text(body, UI_FONT_BODY, names[i], fitted, sizeof(fitted), UI_CONTENT_W - 32);
				ui_font_draw_text(body, rect.x+16, y + 35, VT_THEME_TEXT, UI_FONT_BODY, fitted);
			}
		}
        vita2d_disable_clipping();
        ui_focus_glow_draw(motion->x,motion->y,motion->width,motion->height,0,DESTINATION_Y,484);
        ui_theme_page_finish(UI_SCENE_PICKER,UI_CONTENT_X,DESTINATION_Y,UI_CONTENT_W,484);
	}
	ui_action_button(18, 490, 302, 46, VT_THEME_BLUE_BRIGHT, "Start",
	                 vt_i18n_str(VT_STR_NETWORK_USE_THIS_FOLDER), 0);
	ui_action_button(329, 490, 302, 46, VT_THEME_SURFACE_RAISED, "Triangle",
	                 vt_i18n_str(VT_STR_NETWORK_NEW_FOLDER), 0);
	ui_action_button(640, 490, 302, 46, VT_THEME_SURFACE, "Circle",
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
	int selected = 0, top = 0, count=-1, folders_dirty=1;
	UiFocusMotion motion={0};
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
        if(folders_dirty){
            count=destination_read_folders(path,names,DESTINATION_MAX_FOLDERS);
            folders_dirty=0;
        }
		if (count < 0) {
			ui_message_show(vt_i18n_str(VT_STR_NETWORK_FOLDER_CREATE_FAILED), path, 2600);
			return 0;
		}
		if (selected >= count) selected = count > 0 ? count - 1 : 0;
		if (top > selected) top = selected;
		if (selected >= top + DESTINATION_ROWS) top = selected - DESTINATION_ROWS + 1;
		ui_mini_player_pump();
		draw_destination_picker(path, names, count, selected, top, &motion);
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
            UiPageView page=destination_page(path,top,count);
            for(int i=page.first;i<page.end;i++) {
                UiThemeRect rect=destination_item_rect(i,top);
                if(touch.y>=DESTINATION_Y&&touch.y<484&&ui_touch_hit_rect(touch.x,touch.y,rect.x,rect.y,rect.w,rect.h)) {
					selected = i;
					pressed |= SCE_CTRL_CROSS;
				}
            }
			if (ui_touch_hit_rect(touch.x, touch.y, 18, 490, 302, 46)) pressed |= SCE_CTRL_START;
			if (ui_touch_hit_rect(touch.x, touch.y, 329, 490, 302, 46)) pressed |= SCE_CTRL_TRIANGLE;
			if (ui_touch_hit_rect(touch.x, touch.y, 640, 490, 302, 46)) pressed |= SCE_CTRL_CIRCLE;
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
				selected = top = 0;folders_dirty=1;
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
            folders_dirty=1;
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
			selected = top = 0;folders_dirty=1;
		}
		sceKernelDelayThread(16 * 1000);
	}
}

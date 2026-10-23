#include "ui/text_reader.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "settings/preferences.h"
#include "i18n/i18n.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/touch.h"

#define TEXT_READER_MAX_LINES 8192
#define TEXT_READER_VISIBLE_LINES 17
#define TEXT_READER_TABBED_VISIBLE_LINES 16
#define TEXT_READER_LINE_HEIGHT 24
#define TEXT_READER_TOP 108
#define TEXT_READER_TABBED_TOP 144
#define TEXT_READER_WIDTH 872
#define TEXT_READER_TAB_X 36
#define TEXT_READER_TAB_Y 72
#define TEXT_READER_TAB_W 178
#define TEXT_READER_TAB_H 42
#define TEXT_READER_TAB_GAP 12

typedef struct {
	int offset;
	int length;
} TextReaderLine;

static TextReaderLine g_reader_lines[TEXT_READER_MAX_LINES];
static int g_reader_line_count;

static size_t utf8_character_length(const char *text) {
	unsigned char lead = (unsigned char)text[0];
	size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2
	              : (lead & 0xF0) == 0xE0 ? 3
	              : (lead & 0xF8) == 0xF0 ? 4 : 1;
	for (size_t i = 1; i < length; i++)
		if (!text[i] || ((unsigned char)text[i] & 0xc0U) != 0x80U) return 1;
	return length;
}

static void index_text(vita2d_font *font, const char *text, int max_width) {
	g_reader_line_count = 0;
	if (!font || !text) return;
	int offset = 0;
	while (text[offset] && g_reader_line_count < TEXT_READER_MAX_LINES) {
		while (text[offset] == '\r' || text[offset] == '\n') {
			g_reader_lines[g_reader_line_count++] = (TextReaderLine){ offset, 0 };
			if (text[offset] == '\r' && text[offset + 1] == '\n') offset++;
			offset++;
			if (g_reader_line_count >= TEXT_READER_MAX_LINES) return;
		}
		if (!text[offset]) break;
		int begin = offset;
		int last_space = -1;
		char measured[512];
		int measured_length = 0;
		while (text[offset] && text[offset] != '\r' && text[offset] != '\n') {
			size_t char_length = utf8_character_length(text + offset);
			if (measured_length + (int)char_length >= (int)sizeof(measured)) break;
			memcpy(measured + measured_length, text + offset, char_length);
			measured_length += (int)char_length;
			measured[measured_length] = '\0';
			if (text[offset] == ' ') last_space = offset;
			if (ui_font_text_width(font, UI_FONT_SMALL, measured) > max_width) {
				if (last_space >= begin) offset = last_space;
				break;
			}
			offset += (int)char_length;
		}
		if (offset <= begin) offset = begin + 1;
		g_reader_lines[g_reader_line_count++] =
		    (TextReaderLine){ begin, offset - begin };
		while (text[offset] == ' ') offset++;
		if (text[offset] == '\r') offset++;
		if (text[offset] == '\n') offset++;
	}
}

static void draw_reader_tabs(vita2d_font *font, const char *parent_title, const char *title) {
 (void)font;
 ui_tab(TEXT_READER_TAB_X,TEXT_READER_TAB_Y,TEXT_READER_TAB_W,TEXT_READER_TAB_H,parent_title,0);
 ui_tab(TEXT_READER_TAB_X+TEXT_READER_TAB_W+TEXT_READER_TAB_GAP,TEXT_READER_TAB_Y,TEXT_READER_TAB_W,TEXT_READER_TAB_H,title,1);
}

static void draw_reader(const char *parent_title, const char *title,
	                    const char *text, int first_line,
	                    const UiSectionsSidebar *sidebar) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int tabbed = parent_title && parent_title[0];
	int visible_lines = tabbed ? TEXT_READER_TABBED_VISIBLE_LINES
	                           : TEXT_READER_VISIBLE_LINES;
	int text_top = tabbed ? TEXT_READER_TABBED_TOP : TEXT_READER_TOP;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
	ui_brand_draw_breadcrumbs(parent_title, NULL, title);
	if (tabbed)
		draw_reader_tabs(body, parent_title, title);
	else if (body)
		ui_font_draw_text(body, 44, 91, VT_THEME_TEXT, UI_FONT_BODY,
		                       title && title[0] ? title : "VitaMediaDeck");
	ui_panel(32, tabbed ? 124 : 98, 896, tabbed ? 394 : 420,
	         VT_THEME_SURFACE, VT_THEME_BLUE_BRIGHT, 0);
	vita2d_set_clip_rectangle(40, tabbed ? 122 : 96, 920, 522);
	vita2d_enable_clipping();
	for (int row = 0; row < visible_lines; row++) {
		int index = first_line + row;
		if (index >= g_reader_line_count) break;
		TextReaderLine line = g_reader_lines[index];
		if (!line.length) continue;
		char rendered[512];
		int length = line.length;
		if (length >= (int)sizeof(rendered)) length = sizeof(rendered) - 1;
		memcpy(rendered, text + line.offset, length);
		rendered[length] = '\0';
		if (small)
			ui_font_draw_text(small, 44,
				                       text_top + row * TEXT_READER_LINE_HEIGHT,
			                       VT_THEME_TEXT, UI_FONT_SMALL, rendered);
	}
	vita2d_disable_clipping();
	if (g_reader_line_count > visible_lines) {
		float track_y = tabbed ? 134.0f : 106.0f;
		float track_h = tabbed ? 374.0f : 402.0f;
		float thumb_h = track_h * (float)visible_lines /
		                (float)g_reader_line_count;
		if (thumb_h < 28.0f) thumb_h = 28.0f;
		int max_first = g_reader_line_count - visible_lines;
		float thumb_y = track_y + (track_h - thumb_h) *
		                (max_first > 0 ? (float)first_line / (float)max_first : 0.0f);
		vita2d_draw_rectangle(930, track_y, 4, track_h, VT_THEME_SURFACE_FOCUS);
		vita2d_draw_rectangle(930, thumb_y, 4, thumb_h, VT_THEME_BLUE_LIGHT);
	}
	ui_brand_draw_status_indicators();
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static int text_reader_run_internal(const char *parent_title, const char *title,
	                                const char *text) {
	if (!text || !text[0]) return UI_SECTION_NONE;
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	index_text(small, text, TEXT_READER_WIDTH);
	if (g_reader_line_count <= 0) return UI_SECTION_NONE;
	int tabbed = parent_title && parent_title[0];
	int visible_lines = tabbed ? TEXT_READER_TABBED_VISIBLE_LINES
	                           : TEXT_READER_VISIBLE_LINES;
	int first_line = 0;
	int max_first = g_reader_line_count > visible_lines
	              ? g_reader_line_count - visible_lines : 0;
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiNavRepeat nav_repeat;
	ui_nav_repeat_reset(&nav_repeat);
	int touch_y = 0;
	int result = UI_SECTION_NONE;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, tabbed ? UI_SECTION_SETTINGS : UI_SECTION_GUIDE);
	ui_touch_reset();
	for (;;) {
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch;
		unsigned int flags = ui_touch_poll(&touch);
		if (!ui_mini_player_input_locked() &&
		    ui_brand_sections_hit(flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER;
			flags = UI_TOUCH_EVENT_NONE;
		}
		int sidebar_was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(
		    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
		if (sidebar.open) {
			int touched = ui_sections_sidebar_handle_touch(
			    &sidebar, flags, touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			flags = UI_TOUCH_EVENT_NONE;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) { result = section; break; }
		int page_owns_input = !sidebar_was_open && !sidebar.open &&
		                      sidebar.animation <= 0.01f;
		if (!page_owns_input) {
			pressed = 0;
			flags = UI_TOUCH_EVENT_NONE;
		}
		if (page_owns_input && (pressed & SCE_CTRL_CIRCLE)) break;
		if (page_owns_input && tabbed &&
		    (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_LEFT))) break;
		unsigned int nav = ui_nav_repeat_update(
		    &nav_repeat, pressed, page_owns_input ? ctrl.buttons : 0,
		    page_owns_input ? ctrl.lx : 128, page_owns_input ? ctrl.ly : 128,
		    SCE_CTRL_UP | SCE_CTRL_DOWN);
		if ((nav & SCE_CTRL_UP) && first_line > 0) first_line--;
		if ((nav & SCE_CTRL_DOWN) && first_line < max_first) first_line++;
		if (page_owns_input && tabbed && (flags & UI_TOUCH_EVENT_TAP) &&
		    ui_touch_hit_rect(touch.x, touch.y, TEXT_READER_TAB_X,
		                      TEXT_READER_TAB_Y, TEXT_READER_TAB_W,
		                      TEXT_READER_TAB_H))
			break;
		if ((flags & UI_TOUCH_EVENT_TAP) && touch.x >= 900) {
			int track_top = tabbed ? 134 : 106;
			int track_height = tabbed ? 374 : 402;
			if (touch.y >= track_top && touch.y < track_top + track_height &&
			    max_first > 0) {
				first_line = (touch.y - track_top) * max_first / track_height;
				if (first_line < 0) first_line = 0;
				if (first_line > max_first) first_line = max_first;
			}
		}
		if (flags & UI_TOUCH_EVENT_DOWN) touch_y = touch.y;
		if (flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD)) {
			int delta = touch_y - touch.y;
			if (delta >= TEXT_READER_LINE_HEIGHT || delta <= -TEXT_READER_LINE_HEIGHT) {
				first_line += delta / TEXT_READER_LINE_HEIGHT;
				if (first_line < 0) first_line = 0;
				if (first_line > max_first) first_line = max_first;
				touch_y = touch.y;
			}
		}
		draw_reader(parent_title, title, text, first_line, &sidebar);
		sceKernelDelayThread(vt_preferences_reduce_motion() ? 16000 : 1000);
	}
	ui_touch_reset();
	return result;
}

int ui_text_reader_run(const char *title, const char *text) {
	return text_reader_run_internal(NULL, title, text);
}

typedef struct {
 char title[64];
 const char *body;
 size_t length;
} GuideChapter;

/* Only standalone uppercase headings are navigation. The instructional prose
 * remains the localized source of truth, not another set of copied bindings. */
static int guide_chapters(const char *text, GuideChapter *chapters, int capacity) {
 int count=0;
 for(const char *p=text; p && *p;) {
  const char *end=strchr(p,'\n');
  if(!end)break;
  size_t length=(size_t)(end-p);
  int heading=length>0 && length<sizeof(chapters[0].title) && end[1]=='\n';
  for(size_t i=0;heading && i<length;i++)
   if(!((p[i]>='A' && p[i]<='Z') || p[i]==' ' || p[i]=='/'))heading=0;
  if(heading && (p==text || (p-text>=2 && p[-2]=='\n'))) {
   if(count==capacity)return 0;
   if(count)chapters[count-1].length=(size_t)(p-chapters[count-1].body);
   memcpy(chapters[count].title,p,length);chapters[count].title[length]=0;
   chapters[count].body=end+2;chapters[count].length=0;count++;
  }
  p=end+1;
 }
 if(count)chapters[count-1].length=strlen(chapters[count-1].body);
 return count;
}

static int guide_chapter_content(const GuideChapter *chapter,char *out,size_t capacity){
 int prefix=0;
 if(!strcmp(chapter->title,vt_i18n_str(VT_STR_GUIDE_HOME_HEADING)))
  prefix=snprintf(out,capacity,"%s\n%s\n%s\n\n",ui_theme_name(ui_theme_id()),
    ui_theme_home_controls_hint(1),ui_theme_home_controls_hint(0));
 else if(!strcmp(chapter->title,vt_i18n_str(VT_STR_GUIDE_PLAYER_HEADING)))
  prefix=snprintf(out,capacity,"%s\n\n",vt_i18n_str(vt_preferences_player_swap_shoulders()?
    VT_STR_GUIDE_MAPPING_SWAPPED:VT_STR_GUIDE_MAPPING_DEFAULT));
 if(prefix<0 || (size_t)prefix>=capacity || chapter->length>=capacity-(size_t)prefix)return -1;
 memcpy(out+prefix,chapter->body,chapter->length);out[prefix+chapter->length]=0;
 return 0;
}

int ui_controls_guide_run(const char *title, const char *text) {
 if(!text || !*text)return UI_SECTION_NONE;
 GuideChapter chapters[8];int count=guide_chapters(text,chapters,8);
 char content[8192];
 if(!count)return ui_text_reader_run(title,text);
 for(int i=0;i<count;i++)if(chapters[i].length>=sizeof(content))
  return ui_text_reader_run(title,text);
 int cursor=0,loaded=-1,first=0,content_focus=0;
 UiFocusMotion focus={0};UiNavRepeat repeat={0};UiSectionsSidebar sidebar;
 ui_sections_sidebar_init(&sidebar,UI_SECTION_GUIDE);
 SceCtrlData previous={0};sceCtrlPeekBufferPositive(0,&previous,1);
 ui_touch_reset();
 for(;;) {
  ui_mini_player_pump();
  SceCtrlData controls={0};sceCtrlPeekBufferPositive(0,&controls,1);
  unsigned pressed=controls.buttons & ~previous.buttons;previous=controls;
  ui_mini_player_handle_buttons(&pressed);
  UiTouchEvent touch;unsigned flags=ui_touch_poll(&touch);
  if(ui_mini_player_input_locked()) {
   pressed=flags=controls.buttons=0;controls.lx=controls.ly=128;
  } else if(ui_brand_sections_hit(flags,touch.x,touch.y)) {
   pressed|=SCE_CTRL_LTRIGGER;flags=0;
  }
  int was_open=sidebar.open;
  int section=ui_sections_sidebar_handle_buttons(&sidebar,&pressed,controls.buttons,controls.ly);
  if(was_open || sidebar.open) {
   int touched=ui_sections_sidebar_handle_touch(&sidebar,flags,touch.x,touch.y);
   if(touched!=UI_SECTION_NONE)section=touched;
   flags=0;
  } else if(sidebar.animation<=.01f && ui_mini_player_handle_touch(flags,&touch))flags=0;
  ui_sections_sidebar_tick(&sidebar);
  if(section!=UI_SECTION_NONE){ui_touch_reset();return section;}
  int owns=!was_open && !sidebar.open && sidebar.animation<=.01f && !ui_mini_player_input_locked();
  int visible=(ui_mini_player_top()-154)/TEXT_READER_LINE_HEIGHT;
  if(visible<1)visible=1;
  if(owns) {
   if(pressed&SCE_CTRL_CIRCLE){ui_touch_reset();return UI_SECTION_NONE;}
   unsigned nav=ui_nav_repeat_update(&repeat,pressed,controls.buttons,controls.lx,controls.ly,
                                    SCE_CTRL_UP|SCE_CTRL_DOWN|SCE_CTRL_LEFT|SCE_CTRL_RIGHT);
   if(nav&SCE_CTRL_LEFT)content_focus=0;
   if((nav&SCE_CTRL_RIGHT) || (pressed&SCE_CTRL_CROSS))content_focus=1;
   if(nav&SCE_CTRL_UP){if(content_focus)first--;else if(cursor>0)cursor--;}
   if(nav&SCE_CTRL_DOWN){if(content_focus)first++;else if(cursor+1<count)cursor++;}
   if(flags&UI_TOUCH_EVENT_TAP) {
    for(int i=0;i<count;i++)if(ui_touch_hit_rect(touch.x,touch.y,18,62+i*52,264,44)){
     cursor=i;content_focus=0;
    }
    if(touch.x>=298 && touch.y>=62 && touch.y<ui_mini_player_top()-36)content_focus=1;
   }
   if(content_focus && (flags&UI_TOUCH_EVENT_UP) && !(flags&UI_TOUCH_EVENT_TAP))
    first+=(touch.down_y-touch.y)/TEXT_READER_LINE_HEIGHT;
  } else ui_nav_repeat_reset(&repeat);
  if(cursor!=loaded) {
   if(guide_chapter_content(&chapters[cursor],content,sizeof(content))<0)
    return ui_text_reader_run(title,text);
   index_text(ui_runtime_font(UI_FONT_SMALL),content,604);loaded=cursor;first=0;
  }
  int max_first=g_reader_line_count>visible?g_reader_line_count-visible:0;
  if(first<0)first=0;
  if(first>max_first)first=max_first;
  vita2d_start_drawing();vita2d_clear_screen();
  ui_chrome_background(VT_THEME_BG,VT_THEME_SIGNAL);
  ui_brand_draw_breadcrumbs(title,NULL,chapters[cursor].title);
  for(int i=0;i<count;i++)ui_tab(18,62+i*52,264,44,chapters[i].title,i==cursor);
  int panel_height=ui_mini_player_top()-106;
  ui_panel(298,62,644,panel_height,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
  ui_text_line(318,94,604,chapters[cursor].title,VT_THEME_TEXT,UI_FONT_BODY);
  for(int i=0;i<visible && first+i<g_reader_line_count;i++) {
   TextReaderLine line=g_reader_lines[first+i];char rendered[512];
   int n=line.length;if(n>511)n=511;
   memcpy(rendered,content+line.offset,n);rendered[n]=0;
   ui_text_line(318,128+i*24,604,rendered,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
  }
  if(owns) {
   ui_focus_motion_tick(&focus,content_focus?298:18,content_focus?62:62+cursor*52,
                        content_focus?644:264,content_focus?panel_height:44);
   ui_focus_outline_draw(&focus);
  }
  ui_scroll_rail(934,112,panel_height-58,first,visible,g_reader_line_count);
  ui_footer(ui_mini_player_top(),vt_i18n_str(content_focus?VT_STR_GUIDE_TEXT_HINT:VT_STR_GUIDE_INDEX_HINT),
            vt_i18n_str(VT_STR_GUIDE_EXIT_HINT));
  ui_mini_player_draw();
  if(sidebar.animation>.01f)ui_sections_sidebar_draw(&sidebar);
  vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();
  sceKernelDelayThread(16000);
 }
}

int ui_text_reader_run_tabbed(const char *parent_title, const char *title,
	                          const char *text) {
	return text_reader_run_internal(parent_title, title, text);
}

#include "ui/online_audio_screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include "app_paths.h"
#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/video_thumbnail.h"
#include "network/online_audio.h"
#include "network/download_manager.h"
#include "ui/brand.h"
#include "ui/home_tabs.h"
#include "ui/components.h"
#include "ui/folder_picker.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/music_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/text_reader.h"
#include "ui/theme.h"
#include "ui/touch.h"

static VtOnlineQuery g_query;
static int g_tab = VT_ONLINE_RADIO;
static int g_episodes, g_selected, g_top, g_initialized;
static int g_catalog, g_return_valid, g_return_selected, g_return_top, g_return_catalog;
static VtOnlineQuery g_return_query;
static char g_country_name[160] = "";
static int g_home, g_mode_changed=1, g_menu;
void ui_online_audio_set_home(int favorites_only) {g_home=!!favorites_only;g_mode_changed=1;}
#define LIST_Y (g_home && !g_episodes ? 132 : 210)
static VtOnlineQuery g_lazy;
static SceUID g_lazy_thread = -1;
static volatile int g_lazy_done;
static int g_lazy_result, g_lazy_direction;
static int g_page_sizes[4], g_pages=1, g_return_sizes[4], g_return_pages;
static int g_lazy_failed_offset=-1;
static VtOnlineItem g_cover_item;
static SceUID g_cover_thread=-1;
static volatile int g_cover_cancel,g_cover_done;
static int cover_worker(SceSize args,void *ctx) {
	(void)args;(void)ctx;vt_online_fetch_cover(&g_cover_item,&g_cover_cancel);
	__sync_synchronize();g_cover_done=1;return sceKernelExitThread(0);
}
static void cover_stop(void) {
	if(g_cover_thread<0)return;
	g_cover_cancel=1;sceKernelWaitThreadEnd(g_cover_thread,NULL,NULL);
	sceKernelDeleteThread(g_cover_thread);g_cover_thread=-1;
}
static void cover_tick(int rows) {
	if(g_cover_thread>=0&&g_cover_done) {
		sceKernelWaitThreadEnd(g_cover_thread,NULL,NULL);sceKernelDeleteThread(g_cover_thread);g_cover_thread=-1;
		for(int i=0;i<g_query.count;i++)if(!strcmp(g_query.items[i].url,g_cover_item.url)) {
			int recovered=!g_query.items[i].artwork_url[0]&&g_cover_item.artwork_url[0];
			g_query.items[i]=g_cover_item;
			snprintf(g_query.items[i].artwork_path,sizeof(g_query.items[i].artwork_path),"%s",g_cover_item.artwork_path[0]?g_cover_item.artwork_path:"-");
			if(recovered&&!g_catalog&&!g_episodes)vt_online_save(&g_query.items[i]);
		}
	}
	if(g_cover_thread>=0)return;
	for(int r=-1;r<rows;r++) {
		int i=r<0?g_selected:g_top+r;
		if(i<0||i>=g_query.count||g_query.items[i].artwork_path[0])continue;
		if(!g_query.items[i].artwork_url[0] && !(g_query.items[i].kind==VT_ONLINE_RADIO&&g_query.items[i].id[0]))continue;
		g_cover_item=g_query.items[i];g_cover_cancel=g_cover_done=0;
		g_cover_thread=sceKernelCreateThread("VmdCover",cover_worker,0x10000100,0x40000,0,0,NULL);
		if(g_cover_thread>=0&&sceKernelStartThread(g_cover_thread,0,NULL)<0){sceKernelDeleteThread(g_cover_thread);g_cover_thread=-1;}
		if(g_cover_thread<0)snprintf(g_query.items[i].artwork_path,sizeof(g_query.items[i].artwork_path),"-");
		break;
	}
}
static int cover_loading(void *ctx) {return vt_online_fetch_cover(ctx,&g_cover_cancel);}
static int show_loading(void *ctx) {return vt_online_resolve_show(ctx,&g_cover_cancel);}
static int lazy_worker(SceSize args, void *ctx) {
	(void)args;(void)ctx;g_lazy_result=vt_online_fetch_query(&g_lazy);
	__sync_synchronize();g_lazy_done=1;return sceKernelExitThread(0);
}
static void lazy_stop(void) {
	if(g_lazy_thread<0)return;
	g_lazy.cancel=1;sceKernelWaitThreadEnd(g_lazy_thread,NULL,NULL);
	sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;
}
static int online_leave(int section) {lazy_stop();cover_stop();ui_brand_set_loading(0);return section;}
static void lazy_tick(int rows) {
	if(g_lazy_thread>=0 && g_lazy_done) {
		sceKernelWaitThreadEnd(g_lazy_thread,NULL,NULL);sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;
		if(g_lazy_result>=0) {
			if(g_lazy_direction>0) {
				if(g_pages==4) {int drop=g_page_sizes[0];memmove(g_query.items,g_query.items+drop,(g_query.count-drop)*sizeof(g_query.items[0]));g_query.count-=drop;g_selected-=drop;g_top-=drop;g_query.offset+=64;memmove(g_page_sizes,g_page_sizes+1,3*sizeof(int));g_pages--;}
				memcpy(g_query.items+g_query.count,g_lazy.items,g_lazy.count*sizeof(g_lazy.items[0]));
				g_query.count+=g_lazy.count;g_page_sizes[g_pages++]=g_lazy.count;g_query.has_more=g_lazy.has_more;
			} else {
				if(g_pages==4){g_query.count-=g_page_sizes[3];g_pages--;g_query.has_more=1;}
				memmove(g_query.items+g_lazy.count,g_query.items,g_query.count*sizeof(g_query.items[0]));
				memcpy(g_query.items,g_lazy.items,g_lazy.count*sizeof(g_lazy.items[0]));g_query.count+=g_lazy.count;
				memmove(g_page_sizes+1,g_page_sizes,g_pages*sizeof(int));g_page_sizes[0]=g_lazy.count;g_pages++;
				g_selected+=g_lazy.count;g_top+=g_lazy.count;g_query.offset-=64;
			}
			if(g_selected<0)g_selected=0;
			if(g_top<0)g_top=0;
		} else g_lazy_failed_offset=(int)g_lazy.offset;
	}
	if((!g_catalog && !g_episodes) || g_lazy_thread>=0)return;
	int direction=g_query.has_more && g_selected+rows*2>=g_query.count ? 1 :
	    g_query.offset>0 && g_selected<3 ? -1 : 0;
	if(!direction)return;
	memset(&g_lazy,0,sizeof(g_lazy));g_lazy.kind=g_query.kind;
	g_lazy.browse=g_query.browse;g_lazy.search_field=g_query.search_field;
	memcpy(g_lazy.country,g_query.country,sizeof(g_lazy.country));snprintf(g_lazy.query,sizeof(g_lazy.query),"%s",g_query.query);
	g_lazy.offset=direction>0?g_query.offset+g_pages*64:g_query.offset-64;
	if((int)g_lazy.offset==g_lazy_failed_offset)return;
	g_lazy_direction=direction;g_lazy_done=0;
	g_lazy_thread=sceKernelCreateThread("VmdCatalogPage",lazy_worker,0x10000100,0x40000,0,0,NULL);
	if(g_lazy_thread>=0 && sceKernelStartThread(g_lazy_thread,0,NULL)<0){sceKernelDeleteThread(g_lazy_thread);g_lazy_thread=-1;g_query.has_more=0;}
	if(g_lazy_thread<0)g_lazy_failed_offset=(int)g_lazy.offset;
}

static int list_count(void) {
	return g_query.count + (g_query.kind == VT_ONLINE_COUNTRY);
}

static void remember_list(void) {
	lazy_stop();cover_stop();
	g_return_query = g_query;
	memcpy(g_return_sizes,g_page_sizes,sizeof(g_page_sizes));g_return_pages=g_pages;
	g_return_selected = g_selected; g_return_top = g_top;
	g_return_catalog = g_catalog; g_return_valid = 1;
}

static void load_saved(void) {
	g_menu=0;
	lazy_stop();cover_stop();g_pages=1;g_lazy_failed_offset=-1;
	g_query.count = vt_online_load_saved(g_query.items, VT_ONLINE_MAX);
	int used = 0;
	for (int i = 0; i < g_query.count; i++)
		if (g_query.items[i].kind == g_tab) g_query.items[used++] = g_query.items[i];
	g_query.count = used;
	g_query.kind = g_tab;
	g_query.offset = 0; g_query.has_more = 0;
	g_catalog = g_return_valid = 0;
	g_selected = g_top = g_episodes = 0;
}

static void restore_list(void) {
	lazy_stop(); cover_stop(); g_lazy_failed_offset = -1;
	g_query = g_return_query;
	g_selected = g_return_selected; g_top = g_return_top;
	memcpy(g_page_sizes, g_return_sizes, sizeof(g_page_sizes));
	g_pages = g_return_pages; g_catalog = g_return_catalog;
	g_episodes = g_query.kind == VT_ONLINE_EPISODE;
	g_menu = g_query.kind == VT_ONLINE_MENU;
	g_return_valid = 0;
}

static void playback_failed(void) {
	/* A failed episode must not strand Home on its episode action toolbar. */
	if (!g_home || !g_episodes) return;
	if (g_return_valid && !g_return_catalog &&
	    g_return_query.kind == VT_ONLINE_FEED) restore_list();
	else load_saved();
}

static int radio_fullscreen(uint64_t position, void *ctx) {
	(void)position; (void)ctx;
	VtBackgroundPlaybackSnapshot snapshot;
	vt_background_playback_snapshot(&snapshot);
	int action = ui_music_player_run(snapshot.thumbnail_url, snapshot.is_live ? vt_i18n_str(VT_STR_ONLINE_LIVE_HINT) : "Podcast",
	    snapshot.is_live ? "MP3 / Live" : "Audio", 0);
	if (action == UI_MUSIC_PLAYER_MINIMIZE) return 0;
	if (action >= UI_MUSIC_PLAYER_SECTION_BASE) return action;
	vt_background_playback_snapshot(&snapshot);
	int failed = snapshot.state == VT_BACKGROUND_ERROR;
	vt_background_playback_stop();
	if (failed) playback_failed();
	return failed ? -1 : 0;
}

static void reset_input(SceCtrlData *previous, UiNavRepeat *repeat) {
	ui_touch_reset();
	sceCtrlPeekBufferPositive(0, previous, 1);
	ui_nav_repeat_reset(repeat);
}

static int fetch_page(int kind, const char *text, unsigned offset, int remember_parent) {
	lazy_stop();cover_stop();g_lazy_failed_offset=-1;
	/* Reuse the stopped pager's storage as a transaction: failure/cancellation
	 * must never replace favorites with an empty episode view. No large stack
	 * copy or additional permanent query buffer is needed. */
	g_lazy = g_query;
	g_lazy.kind = kind; g_lazy.cancel = 0; g_lazy.cancel_parent = NULL;
	g_lazy.offset = offset;
	snprintf(g_lazy.query, sizeof(g_lazy.query), "%s", text);
	int ret = ui_loading_run(vt_i18n_str(VT_STR_ONLINE_LOADING),
	    vt_online_fetch_query, &g_lazy, &g_lazy.cancel, NULL, NULL);
	if (g_lazy.cancel || (kind == VT_ONLINE_EPISODE && !offset && !g_lazy.count)) ret = -1;
	log_printf("online navigation: kind=%d result=%d cancelled=%d count=%d home=%d committed=%d\n",
	    kind, ret, !!g_lazy.cancel, g_lazy.count, g_home, ret >= 0);
	if (ret < 0) {
		log_save(VITAMEDIADECK_SESSION_LOG_PATH);
		if (!g_lazy.cancel)
			ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED),
			    vt_i18n_str(VT_STR_ONLINE_RETRY), 3000);
		return ret;
	}
	if (remember_parent) remember_list();
	g_query = g_lazy; g_menu = 0;
	g_selected = g_top = 0;
	g_pages=1;g_page_sizes[0]=g_query.count;
	g_catalog = kind != VT_ONLINE_EPISODE;
	g_episodes = kind == VT_ONLINE_EPISODE;
	return ret;
}

static int fetch_query(int kind, const char *text) { return fetch_page(kind, text, 0, 0); }

static void browse_menu(void) {
	lazy_stop();cover_stop();g_menu=1;g_episodes=g_catalog=g_return_valid=0;
	g_query.count=0;g_query.has_more=0;g_query.offset=0;g_query.kind=VT_ONLINE_MENU;
	g_query.browse=0;g_query.search_field=0;g_query.query[0]=0;g_selected=g_top=0;
	const VtStringId labels[]={VT_STR_ONLINE_POPULAR,VT_STR_ONLINE_REGIONS,VT_STR_ONLINE_GENRES,VT_STR_ONLINE_FAVORITES};
	for(int i=0;i<4;i++) {
		if(i==2&&g_tab!=VT_ONLINE_RADIO)continue;
		VtOnlineItem *item=&g_query.items[g_query.count++];memset(item,0,sizeof(*item));
		item->kind=VT_ONLINE_MENU;snprintf(item->id,sizeof(item->id),"%d",i);
		snprintf(item->title,sizeof(item->title),"%s",vt_i18n_str(labels[i]));
	}
}

static void select_country(const char *code, const char *name) {
	int remember_parent = g_query.kind == VT_ONLINE_COUNTRY;
	snprintf(g_query.country, sizeof(g_query.country), "%.2s", code);
	snprintf(g_country_name, sizeof(g_country_name), "%s", name);
	g_query.browse=1;g_query.search_field=0;
	fetch_page(g_tab==VT_ONLINE_RADIO?VT_ONLINE_RADIO:VT_ONLINE_SHOW, "", 0, remember_parent);
}

int ui_online_audio_screen(void) {
	if(g_home) {
		int requested_tab=ui_home_tab()==HOME_RADIO?VT_ONLINE_RADIO:VT_ONLINE_FEED;
		if(g_tab!=requested_tab||g_mode_changed){g_tab=requested_tab;load_saved();}
	} else if(g_mode_changed||!g_initialized)browse_menu();
	g_mode_changed=0;g_initialized=1;
	int focus_tabs = 1;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, g_home?UI_SECTION_LOCAL_MEDIA:UI_SECTION_ONLINE_AUDIO);
	SceCtrlData previous = {0};
	UiNavRepeat repeat;
	reset_input(&previous, &repeat);
	for (;;) {
		ui_mini_player_pump();
		SceCtrlData controls = {0};
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		ui_mini_player_handle_buttons(&pressed);
		UiTouchEvent touch;
		unsigned flags = ui_touch_poll(&touch);
		if (ui_mini_player_input_locked()) {
			pressed = flags = 0;
			controls.buttons = 0; controls.lx = controls.ly = 128;
		}
		if (ui_brand_sections_hit(flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER; flags = 0;
		}
		int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
		    controls.buttons, controls.ly);
		if (sidebar.open) {
			int selected = ui_sections_sidebar_handle_touch(&sidebar, flags, touch.x, touch.y);
			if (selected != UI_SECTION_NONE) section = selected;
			flags = 0;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) return online_leave(section);
		int rows = (ui_mini_player_top() - 28 - LIST_Y) / 52;
		if (rows < 1) rows = 1;
		if (rows > (g_home&&!g_episodes?7:5)) rows = g_home&&!g_episodes?7:5;
		lazy_tick(rows);
		cover_tick(rows);vt_video_thumbnail_resume();vt_video_thumbnail_pump();
		if (!sidebar.open && sidebar.animation <= 0.01f) {
			if (ui_mini_player_handle_touch(flags, &touch)) flags = 0;
			if((flags&UI_TOUCH_EVENT_UP)&&!(flags&UI_TOUCH_EVENT_TAP)&&touch.down_y>=LIST_Y&&touch.down_y<LIST_Y+rows*52) {
				int delta=(touch.down_y-touch.y)/52;
				g_selected+=delta;focus_tabs=0;
				if(g_selected<0)g_selected=0;
				if(g_selected>=list_count())g_selected=list_count()?list_count()-1:0;
			}
			if (flags & UI_TOUCH_EVENT_TAP) {
				if(!g_home) {
					if (ui_touch_hit_rect(touch.x, touch.y, 650, 166, 244, 32)) pressed |= SCE_CTRL_RTRIGGER;
					if (ui_touch_hit_rect(touch.x, touch.y, 66, 116, 262, 44)) pressed |= SCE_CTRL_TRIANGLE;
				}
				if (!g_home||g_episodes) {
					if (ui_touch_hit_rect(touch.x, touch.y, 346, 116, 262, 44)) pressed |= SCE_CTRL_START;
					if (ui_touch_hit_rect(touch.x, touch.y, 626, 116, 268, 44)) pressed |= SCE_CTRL_SQUARE;
				}
				for (int i = 0; i < rows && g_top + i < list_count(); i++)
					if (ui_touch_hit_rect(touch.x, touch.y, 66, LIST_Y + i * 52, 828, 46)) {
						focus_tabs = 0; g_selected = g_top + i; pressed |= SCE_CTRL_CROSS;
					}
			}
			unsigned tab_nav=ui_home_tabs_navigation(pressed,focus_tabs?controls.buttons:0,focus_tabs?controls.lx:128,128);
			if (g_home && ui_home_tabs_input(tab_nav, flags, &touch, focus_tabs)) {
				focus_tabs = 1;
				if (!ui_home_tab_online()) return online_leave(UI_SECTION_LOCAL_MEDIA);
				g_tab = ui_home_tab() == HOME_RADIO ? VT_ONLINE_RADIO : VT_ONLINE_FEED;
				load_saved(); reset_input(&previous, &repeat);
			}
			if(!g_home) {
				int next=g_tab;
				if(focus_tabs&&(tab_nav&SCE_CTRL_LEFT))next=VT_ONLINE_RADIO;
				if(focus_tabs&&(tab_nav&SCE_CTRL_RIGHT))next=VT_ONLINE_FEED;
				if((flags&UI_TOUCH_EVENT_TAP)&&touch.y>=68&&touch.y<108){next=touch.x<480?VT_ONLINE_RADIO:VT_ONLINE_FEED;focus_tabs=1;}
				if(next!=g_tab){g_tab=next;browse_menu();reset_input(&previous,&repeat);}
			}
			if (focus_tabs && (pressed & (SCE_CTRL_DOWN|SCE_CTRL_CROSS))) {focus_tabs=0;pressed&=~(SCE_CTRL_DOWN|SCE_CTRL_CROSS);controls.buttons&=~SCE_CTRL_DOWN;}
			else if (!focus_tabs && g_selected == 0 && (pressed & SCE_CTRL_UP)) {focus_tabs=1;pressed&=~SCE_CTRL_UP;}
			unsigned nav = ui_nav_repeat_update(&repeat, pressed, controls.buttons,
			    controls.lx, controls.ly, SCE_CTRL_UP | SCE_CTRL_DOWN);
			if (!focus_tabs && (nav & SCE_CTRL_UP) && g_selected > 0) g_selected--;
			if (!focus_tabs && (nav & SCE_CTRL_DOWN) && g_selected + 1 < list_count()) g_selected++;
			if (pressed & SCE_CTRL_CIRCLE) {
				if (g_return_valid) {
					restore_list();
				} else if(g_home)load_saved();else browse_menu();
				reset_input(&previous, &repeat);
			}
			if ((pressed & SCE_CTRL_RTRIGGER) && !g_home) {
				browse_menu();focus_tabs=0;
				reset_input(&previous, &repeat);
			}
			if (!g_home && (pressed & SCE_CTRL_START) && g_query.kind == VT_ONLINE_COUNTRY) {
				select_country("", ""); reset_input(&previous, &repeat);
				pressed = 0;
			}
			if (!g_home && (pressed & SCE_CTRL_TRIANGLE)) {
				char text[128] = "";
				int kind = g_query.kind == VT_ONLINE_COUNTRY ? VT_ONLINE_COUNTRY : g_tab;
				if (ui_text_input(vt_i18n_str(kind == VT_ONLINE_FEED ? VT_STR_ONLINE_DISCOVER_PROMPT :
				    kind == VT_ONLINE_COUNTRY ? VT_STR_ONLINE_COUNTRY_SEARCH : VT_STR_ONLINE_SEARCH), "", text, sizeof(text)) > 0) {
					g_episodes = 0;
					g_query.browse=0;g_query.search_field=0;
					fetch_query(kind, text);
				}
				reset_input(&previous, &repeat);
			}
			if (!g_home && (pressed & SCE_CTRL_START) && !g_episodes && g_query.kind != VT_ONLINE_COUNTRY) {
				VtOnlineItem item = {.kind = g_tab};
				if (ui_text_input(vt_i18n_str(g_tab == VT_ONLINE_RADIO ? VT_STR_ONLINE_MP3_URL : VT_STR_ONLINE_RSS_URL),
				    "", item.url, sizeof(item.url)) > 0 && vt_online_url_valid(item.url) &&
				    ui_text_input(vt_i18n_str(VT_STR_ONLINE_NAME), "", item.title, sizeof(item.title)) > 0) {
					if (vt_online_save(&item) < 0)
						ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED), vt_i18n_str(VT_STR_ONLINE_SAVE_FAILED), 2600);
					load_saved();
				}
				reset_input(&previous, &repeat);
			}
			if (((pressed&SCE_CTRL_SELECT)||(g_episodes&&(pressed&SCE_CTRL_SQUARE))) && g_selected<g_query.count) {
				VtOnlineItem *item=&g_query.items[g_selected];char text[1536];
				snprintf(text,sizeof(text),"%s\n\n%s\n\n%s",item->author,item->detail,item->description);
				int result=ui_text_reader_run(item->title,text);
				if(result!=UI_SECTION_NONE)return online_leave(result);
				reset_input(&previous,&repeat);
			}
			if (!g_home && !g_menu && (pressed & SCE_CTRL_SQUARE) && !g_episodes && g_query.kind != VT_ONLINE_COUNTRY && g_query.kind!=VT_ONLINE_TAG && g_selected < g_query.count) {
				if(g_query.items[g_selected].kind==VT_ONLINE_SHOW){g_cover_cancel=0;
					if(ui_loading_run(vt_i18n_str(VT_STR_ONLINE_LOADING),show_loading,&g_query.items[g_selected],&g_cover_cancel,NULL,NULL)<0){reset_input(&previous,&repeat);continue;}}
				if (vt_online_save(&g_query.items[g_selected]) < 0)
					ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED), vt_i18n_str(VT_STR_ONLINE_SAVE_FAILED), 2600);
				else ui_message_show_success(vt_i18n_str(VT_STR_ONLINE_SAVED), g_query.items[g_selected].title, 1400);
				reset_input(&previous, &repeat);
			}
			if (((pressed & SCE_CTRL_CROSS) || (g_episodes && (pressed & SCE_CTRL_START))) && g_selected < list_count()) {
				if (g_selected >= g_query.count) {
					int extra = g_selected - g_query.count;
					if (g_query.kind == VT_ONLINE_COUNTRY && extra == 0) {
						select_country("", "");
					} else {
						extra -= g_query.kind == VT_ONLINE_COUNTRY;
						unsigned offset = g_query.offset > 0 && extra == 0
						    ? g_query.offset - VT_ONLINE_MAX : g_query.offset + VT_ONLINE_MAX;
						fetch_page(g_query.kind, g_query.query, offset, 0);
					}
					reset_input(&previous, &repeat);
					continue;
				}
				VtOnlineItem item = g_query.items[g_selected];
				if(item.kind==VT_ONLINE_SHOW){g_cover_cancel=0;
					if(ui_loading_run(vt_i18n_str(VT_STR_ONLINE_LOADING),show_loading,&item,&g_cover_cancel,NULL,NULL)<0){ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED),vt_i18n_str(VT_STR_ONLINE_RETRY),2500);reset_input(&previous,&repeat);continue;}}
				if(item.kind==VT_ONLINE_MENU) {
					int action=atoi(item.id);g_query.browse=1;g_query.search_field=0;
					if(action==0)fetch_query(g_tab==VT_ONLINE_RADIO?VT_ONLINE_RADIO:VT_ONLINE_SHOW,"");
					else if(action==1)fetch_query(VT_ONLINE_COUNTRY,"");
					else if(action==2)fetch_query(VT_ONLINE_TAG,"");else load_saved();
				} else if(item.kind==VT_ONLINE_TAG) {
					g_query.browse=1;g_query.search_field=1;fetch_page(VT_ONLINE_RADIO,item.id,0,1);
				} else if (item.kind == VT_ONLINE_COUNTRY) {
					select_country(item.id, item.title);
				} else if (item.kind == VT_ONLINE_FEED) {
					fetch_page(VT_ONLINE_EPISODE, item.url, 0, 1);
				} else if (item.kind == VT_ONLINE_RADIO || (item.kind == VT_ONLINE_EPISODE && (pressed & SCE_CTRL_CROSS))) {
					lazy_stop();cover_stop();g_cover_cancel=0;
					if(item.artwork_url[0]||(item.kind==VT_ONLINE_RADIO&&item.id[0]))ui_loading_run(vt_i18n_str(VT_STR_ONLINE_LOADING),cover_loading,&item,&g_cover_cancel,NULL,NULL);
					if(g_cover_cancel){reset_input(&previous,&repeat);continue;}
					vt_video_thumbnail_prepare_playback();
					int prepared = item.kind == VT_ONLINE_RADIO
					    ? vt_background_playback_prepare_radio(item.url, item.title)
					    : vt_background_playback_prepare_podcast(item.url, item.title);
					if(prepared==0)vt_background_playback_set_audio_details(item.author[0]?item.author:item.detail,item.artwork_path);
					if (prepared == 0 &&
					    vt_background_playback_activate(0) == 0) {
						vt_background_playback_set_fullscreen_resume(radio_fullscreen, NULL);
						int result = radio_fullscreen(0, NULL);
						if (result >= UI_MUSIC_PLAYER_SECTION_BASE)
							return online_leave(result - UI_MUSIC_PLAYER_SECTION_BASE);
					} else {
						vt_background_playback_stop();
						ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED),
						    vt_i18n_str(VT_STR_ONLINE_RETRY), 2800);
						playback_failed();
					}
				} else if (!vt_download_background_active()) {
					char folder[768];
					if (ui_folder_picker(NULL, folder, sizeof(folder))) {
						VtDownloadJob job;
						vt_download_job_init_url(&job, item.url);
						vt_download_job_set_destination(&job, folder);
						if (vt_download_background_start(&job) == 0) return online_leave(UI_SECTION_DOWNLOAD);
						ui_message_show(vt_i18n_str(VT_STR_ONLINE_FAILED), vt_i18n_str(VT_STR_ONLINE_RETRY), 2600);
					}
				} else return online_leave(UI_SECTION_DOWNLOAD);
				reset_input(&previous, &repeat);
			}
		}
		if (g_selected < g_top) g_top = g_selected;
		if (g_selected >= g_top + rows) g_top = g_selected - rows + 1;
		vita2d_start_drawing(); vita2d_clear_screen();
		ui_chrome_background(VT_THEME_BG, VT_THEME_COLD_LIGHT);
		ui_brand_set_loading(g_lazy_thread>=0);
		ui_brand_draw_header_placeholder(NULL, vt_i18n_str(VT_STR_SECTIONS_ONLINE_AUDIO));
		if(g_home)ui_home_tabs_draw(focus_tabs && !sidebar.open);
		else {
			ui_action_button(66,68,400,38,VT_THEME_SURFACE,"",vt_i18n_str(VT_STR_ONLINE_RADIO),g_tab==VT_ONLINE_RADIO);
			ui_action_button(494,68,400,38,VT_THEME_SURFACE,"","Podcast",g_tab==VT_ONLINE_FEED);
			ui_action_button(650, 166, 244, 32, VT_THEME_SURFACE, "R1", vt_i18n_str(VT_STR_ONLINE_BROWSE),g_menu);
			ui_action_button(66, 116, 262, 44, VT_THEME_SURFACE, "Triangle", vt_i18n_str(VT_STR_ONLINE_SEARCH), 0);
		}
		if (g_episodes) ui_action_button(346, 116, 262, 44, VT_THEME_SURFACE, "Start", "Download", 0);
		if (g_episodes) ui_action_button(626, 116, 268, 44, VT_THEME_SURFACE, "Square", "Info", 0);
		if (g_query.kind == VT_ONLINE_COUNTRY)
			ui_action_button(346, 116, 262, 44, VT_THEME_SURFACE, "Start", vt_i18n_str(g_tab == VT_ONLINE_RADIO ? VT_STR_ONLINE_WORLD : VT_STR_ONLINE_US_STORE), 0);
		if (!g_home && !g_episodes && g_query.kind != VT_ONLINE_COUNTRY) {
			ui_action_button(346, 116, 262, 44, VT_THEME_SURFACE, "Start", vt_i18n_str(VT_STR_ONLINE_ADD), 0);
			if(!g_menu&&g_query.kind!=VT_ONLINE_TAG)ui_action_button(626, 116, 268, 44, VT_THEME_SURFACE, "Square", vt_i18n_str(VT_STR_ONLINE_SAVE), 0);
		}
		vita2d_font *font = ui_runtime_font(UI_FONT_SMALL);
		if (font) {
			char context[384], fitted[384];
			if (g_menu)snprintf(context,sizeof(context),"%s",vt_i18n_str(VT_STR_ONLINE_BROWSE));
			else if (!g_catalog) snprintf(context, sizeof(context), "%s", vt_i18n_str(g_episodes ? VT_STR_ONLINE_EPISODES : VT_STR_ONLINE_FAVORITES));
			else snprintf(context, sizeof(context), "%s %u / %s / %.100s",
			    vt_i18n_str(VT_STR_ONLINE_PAGE), g_query.offset / VT_ONLINE_MAX + 1,
			    g_query.kind == VT_ONLINE_COUNTRY ? vt_i18n_str(VT_STR_ONLINE_REGIONS) :
			    g_country_name[0] ? g_country_name : vt_i18n_str(g_tab == VT_ONLINE_RADIO ? VT_STR_ONLINE_WORLD : VT_STR_ONLINE_US_STORE),
			    g_query.query[0] ? g_query.query : vt_i18n_str(VT_STR_ONLINE_CATALOG));
			ui_font_fit_text(font, UI_FONT_SMALL, context, fitted, sizeof(fitted), 568);
			ui_font_draw_text(font, 66, g_home&&!g_episodes?124:192, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
		}
		for (int row = 0; row < rows && row + g_top < list_count(); row++) {
			int i = row + g_top, y = LIST_Y + row * 52;
			ui_panel(66, y, 828, 46, i == g_selected ? VT_THEME_SURFACE_FOCUS : VT_THEME_SURFACE,
			    VT_THEME_COLD_LIGHT, i == g_selected);
			char fitted[192];
			const char *title;
			int extra = i - g_query.count;
			if (i < g_query.count) title = g_query.items[i].title;
			else if (g_query.kind == VT_ONLINE_COUNTRY && extra == 0) title = vt_i18n_str(VT_STR_ONLINE_WORLD);
			else title = vt_i18n_str(g_query.offset > 0 && extra == (g_query.kind == VT_ONLINE_COUNTRY)
			    ? VT_STR_ONLINE_PREVIOUS : VT_STR_ONLINE_NEXT);
			if(i<g_query.count && g_query.items[i].artwork_path[0] && strcmp(g_query.items[i].artwork_path,"-")) {
				vita2d_texture *cover=vt_image_thumbnail_get(g_query.items[i].artwork_path,0);
				if(cover){float w=vita2d_texture_get_width(cover),h=vita2d_texture_get_height(cover);float scale=38.0f/(w>h?w:h);
					vita2d_draw_texture_scale(cover,74+(38-w*scale)/2,y+4+(38-h*scale)/2,scale,scale);}
			}
			ui_font_fit_text(font, UI_FONT_SMALL, title, fitted, sizeof(fitted), 752);
			int detail = i < g_query.count && g_query.items[i].detail[0];
			if (font) ui_font_draw_text(font, 122, y + (detail ? 19 : 29), VT_THEME_TEXT, UI_FONT_SMALL, fitted);
			if (font && detail) {
				ui_font_fit_text(font, UI_FONT_SMALL, g_query.items[i].detail, fitted, sizeof(fitted), 752);
				ui_font_draw_text(font, 122, y + 38, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
			}
		}
		if (font && !list_count())
			ui_font_draw_text(font, 86, LIST_Y + 30, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, vt_i18n_str(g_home?VT_STR_ONLINE_HOME_EMPTY:VT_STR_ONLINE_EMPTY));
		if (font) {
			char fitted[256];
			ui_font_fit_text(font, UI_FONT_SMALL, vt_i18n_str(g_episodes ? VT_STR_ONLINE_EPISODE_HINT :
			    g_home?VT_STR_ONLINE_HOME_HINT:
			    g_query.kind == VT_ONLINE_COUNTRY ? VT_STR_ONLINE_REGION_HINT : VT_STR_ONLINE_LIST_HINT),
			    fitted, sizeof(fitted), 828);
			ui_font_draw_text(font, 66, LIST_Y + rows * 52 + 18, VT_THEME_TEXT_MUTED, UI_FONT_SMALL, fitted);
		}
		ui_mini_player_draw();
		if (sidebar.animation > 0.01f) ui_sections_sidebar_draw(&sidebar);
		vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
		sceKernelDelayThread(16000);
	}
}

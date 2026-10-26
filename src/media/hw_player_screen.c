#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "media/hw_player_screen.h"
#include "common/text_log.h"

#include "ui/subtitle.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include <libavutil/error.h>

#include "history/playback_history.h"
#include "library/library.h"
#include "network/iptv_session.h"
#include "i18n/i18n.h"
#include "media/audio_volume.h"
#include "media/player_input_lock.h"
#include "media/player_power_save.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "system/performance.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/theme_scene.h"
#include "ui/frame_pacing.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define TIMELINE_X (ui_theme_video_layout().seek_x)
#define TIMELINE_Y (ui_theme_video_layout().seek_y)
#define TIMELINE_W (ui_theme_video_layout().seek_w)
#define STICK_CENTER 128
#define STICK_DEADZONE 42
#define RIGHT_PANEL_WIDTH 430.0f
#define RIGHT_PANEL_ROW_Y 72.0f
#define RIGHT_PANEL_ROW_STEP 58.0f
#define RIGHT_PANEL_ROW_HEIGHT 50.0f
#define PLAYER_BACK_X (ui_theme_video_layout().back_x)
#define PLAYER_PLAY_X (ui_theme_video_layout().play_x)
#define PLAYER_FORWARD_X (ui_theme_video_layout().next_x)
#define PLAYER_TRANSPORT_Y (ui_theme_video_layout().y)
#define SEEK_REPEAT_DELAY_US 360000ULL
#define SEEK_REPEAT_INTERVAL_US 220000ULL

typedef struct {
	VtDecoderPlayer *player;
	VtDecoderPlayerConfig config;
	int live;
	VtIptvSession *live_session;
} OpenTask;

static int open_task(void *opaque) {
	OpenTask *task = opaque;
	if (task->live_session) {
		/* Startup may need a master, a media playlist and a complete TS segment.
		 * A single 25-second deadline cut off still-progressing HLS requests before
		 * the bounded transport retry could finish. Keep cancellation immediate. */
		uint64_t started = sceKernelGetProcessTimeWide();
		uint64_t deadline = started + 65000000ULL;
		for (;;) {
			if (task->config.cancel_flag && *task->config.cancel_flag) return AVERROR_EXIT;
			VtIptvSourceStatus status;
			vt_iptv_session_status(task->live_session, &status);
			if (status.startup_ready) {
				log_printf("IPTV prebuffer: chunks=%u bytes=%u elapsed=%llu ms\n",
				    status.buffered_segments, (unsigned)status.buffered_bytes,
				    (unsigned long long)((sceKernelGetProcessTimeWide()-started)/1000));
				task->config.live_audio_sample_rate = status.audio_sample_rate;
				task->config.live_audio_channels = status.audio_channels;
				break;
			}
			if (status.terminal || sceKernelGetProcessTimeWide() >= deadline) {
				log_printf("IPTV startup: source=%d last_error=%d retries=%u elapsed=%llu ms\n",
				    status.terminal,status.last_error,status.consecutive_failures,
				    (unsigned long long)((sceKernelGetProcessTimeWide()-started)/1000));
				return AVERROR(EIO);
			}
			sceKernelDelayThread(5000);
		}
	}
	int result = task->live ? vt_decoder_open_live(task->player, &task->config)
	                        : vt_decoder_open(task->player, &task->config);
	if (result >= 0 && task->live_session) {
		/* A direct HLS media playlist may omit variant dimensions. Check actual
		 * stream metadata as well, without changing either decoder library API. */
		VtDecoderPlayerStatus status;
		vt_decoder_get_status(task->player, &status);
		if (status.width > 1280 || status.height > 720) {
			log_printf("IPTV resolution rejected: %ux%u exceeds 1280x720\n",
			    status.width, status.height);
			vt_decoder_request_stop(task->player);
			return AVERROR(ENOSYS);
		}
	}
	return result;
}

typedef struct {
	VtDecoderPlayer *player;
	uint64_t position_ms;
} SeekTask;

typedef struct {
	VtDecoderPlayer *player;
	uint64_t position_ms;
	int track;
	volatile int *operation_cancel;
} TrackTask;

static int seek_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_seek(task->player, task->position_ms);
}

static int fallback_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_fallback_to_software(task->player, task->position_ms);
}

static int audio_track_task(void *opaque) {
	TrackTask *task = opaque;
	return vt_decoder_select_audio_track_with_cancel(
	    task->player, task->track, task->position_ms, task->operation_cancel);
}

static void cancel_player_operation(void *opaque) {
	vt_decoder_request_stop((VtDecoderPlayer *)opaque);
}

static void cancel_audio_operation(void *opaque) {
	vt_decoder_interrupt_audio_operation((VtDecoderPlayer *)opaque);
}

static void fence_decoder_frame(VtDecoderPlayer *player) {
	/* Seek/fallback runs on the cancellable loading worker. Retire the preceding
	 * texture on the UI thread first so decoder teardown never calls vita2d/GXM
	 * while the loading scene is drawing. */
	vita2d_wait_rendering_done();
	vt_decoder_render_complete(player);
	vt_decoder_prepare_background_restart(player);
}

/* A disposable 480x272 RGB565 copy costs 255 KiB. It owns no decoder frame,
 * survives either backend restarting, and is used only on the UI thread. */
static vita2d_texture *capture_seek_preview(VtDecoderPlayer *player) {
 VtDecoderPlayerStatus status;
 vt_decoder_get_status(player,&status);
 if(!status.frames_shown)return NULL;
 vita2d_wait_rendering_done();
 SceDisplayFrameBuf frame={.size=sizeof(frame)};
 if(sceDisplayGetFrameBuf(&frame,SCE_DISPLAY_SETBUF_IMMEDIATE)<0 || !frame.base ||
    frame.pixelformat!=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 || frame.width!=960 ||
    frame.height!=544 || frame.pitch<960 || frame.pitch>4096)return NULL;
 vita2d_texture *copy=vita2d_create_empty_texture_format(480,272,SCE_GXM_TEXTURE_FORMAT_R5G6B5);
 if(!copy)return NULL;
 unsigned stride=vita2d_texture_get_stride(copy);
 unsigned char *pixels=vita2d_texture_get_datap(copy);
 if(!pixels || stride<480*sizeof(uint16_t)) {
  vita2d_free_texture(copy);return NULL;
 }
 const uint32_t *source=frame.base;
 for(unsigned y=0;y<272;y++) {
  uint16_t *row=(uint16_t *)(pixels+y*stride);
  for(unsigned x=0;x<480;x++) {
   uint32_t rgba=source[(y*2)*frame.pitch+x*2];
   row[x]=(uint16_t)(((rgba&0xf8)<<8)|((rgba>>5)&0x7e0)|((rgba>>19)&0x1f));
  }
 }
 vita2d_texture_set_filters(copy,SCE_GXM_TEXTURE_FILTER_LINEAR,SCE_GXM_TEXTURE_FILTER_LINEAR);
 return copy;
}
static void draw_seek_preview(void *opaque) {
 vita2d_draw_texture_scale(opaque,0,0,2,2);
}

static int run_seek(VtDecoderPlayer *player, UiPlayerLoadingInfo *loading,
	                volatile int *cancel, uint64_t position_ms) {
	SeekTask task = { player, position_ms };
	if (cancel) *cancel = 0;
	vita2d_texture *preview=capture_seek_preview(player);
	UiPlayerLoadingInfo seek_loading=*loading;
	if(preview) {
		seek_loading.draw_backdrop=draw_seek_preview;
		seek_loading.backdrop_ctx=preview;
	}
	fence_decoder_frame(player);
	int result=ui_player_loading_run(&seek_loading,seek_task,&task,cancel,NULL,NULL);
	/* The loading worker has joined and every loading frame is fenced. */
	if(preview) {
		vita2d_wait_rendering_done();
		vita2d_free_texture(preview);
	}
	return result;
}

/* AUTO is a global policy; this is a session-only diagnostic choice. Reopen
 * the reusable stream factory at the current timeline position and roll back
 * to the active backend if the requested decoder cannot open the media. */
static int run_backend_change(VtDecoderPlayer **player_io, OpenTask *open,
	                          UiPlayerLoadingInfo *loading, volatile int *cancel,
	                          VtDecoderBackend target, uint64_t position_ms,
	                          int audio_track, int subtitle_track) {
	if (!player_io || !*player_io || !open || !loading) return -1;
	VtDecoderPlayerStatus previous_status;
	vt_decoder_get_status(*player_io, &previous_status);
	open->config.start_paused = previous_status.paused;
	VtDecoderBackend previous_preference = open->config.preferred_backend;
	fence_decoder_frame(*player_io);
	vt_decoder_destroy(*player_io);
	*player_io = vt_decoder_create();
	if (!*player_io) return -1;
	open->player = *player_io;
	open->config.preferred_backend = target;
	open->config.start_position_ms = position_ms;
	open->config.audio_track = audio_track;
	open->config.subtitle_track = subtitle_track;
	loading->cancel_ctx = *player_io;
	if (cancel) *cancel = 0;
	int ret = ui_player_loading_run(loading, open_task, open, cancel, NULL, NULL);
	if (ret >= 0) return 0;

	vt_decoder_destroy(*player_io);
	*player_io = vt_decoder_create();
	if (!*player_io) return ret;
	open->player = *player_io;
	open->config.preferred_backend = previous_preference;
	loading->cancel_ctx = *player_io;
	if (cancel) *cancel = 0;
	return ui_player_loading_run(loading, open_task, open, cancel, NULL, NULL) >= 0
	     ? 1 : ret;
}

static int run_track_change(VtDecoderPlayer *player,
	                        UiPlayerLoadingInfo *loading,
	                        volatile int *cancel, uint64_t position_ms,
	                        int subtitles, int track) {
	(void)cancel;
	int count = subtitles ? vt_decoder_subtitle_track_count(player)
	                      : vt_decoder_audio_track_count(player);
	int choices = subtitles ? count + 1 : count;
	if (track < 0 || track >= choices) return -1;
	int current = subtitles ? vt_decoder_active_subtitle_track(player)
	                        : vt_decoder_active_audio_track(player);
	if (track == current) {
		if (!subtitles) return 0;
		VtDecoderSubtitleState subtitle_state =
		    vt_decoder_subtitle_state(player);
		if (subtitle_state != VT_DECODER_SUBTITLE_PENDING &&
		    subtitle_state != VT_DECODER_SUBTITLE_FAILED) return 0;
	}
	/* Subtitle open/probe/seek is owned by the persistent subtitle worker. Post
	 * the serial request directly so video, input, and the right drawer remain
	 * live; importantly, do not reset or pass the decoder session cancel flag. */
	if (subtitles)
		return vt_decoder_select_subtitle_track(player, track, position_ms);
	volatile int operation_cancel = 0;
	TrackTask task = { player, position_ms, track, &operation_cancel };
	loading->status = vt_i18n_str(VT_STR_PLAYER_CHANGE_AUDIO);
	UiPlayerLoadingInfo audio_loading = *loading;
	audio_loading.cancel_action = cancel_audio_operation;
	audio_loading.cancel_ctx = player;
	int result = ui_player_loading_run(&audio_loading, audio_track_task, &task,
	                                   &operation_cancel, NULL, NULL);
	/* The decoder restores the previous audio track after an operation-only
	 * cancellation. Only swallow the matching EXIT result: a failed rollback is
	 * deliberately reported as a real error so the player cannot continue with
	 * its audio cursor stopped while the UI claims the action was dismissed. */
	return operation_cancel && result == AVERROR_EXIT ? 0 : result;
}

static uint64_t resync_after_blocking_action(SceCtrlData *controls,
	                                          SceCtrlData *previous,
	                                          int *stick_direction,
	                                          uint64_t *stick_repeat_at) {
	if (controls) sceCtrlPeekBufferPositive(0, controls, 1);
	if (controls && previous) *previous = *controls;
	if (stick_direction) *stick_direction = 0;
	if (stick_repeat_at) *stick_repeat_at = 0;
	ui_touch_reset();
	return sceKernelGetProcessTimeWide();
}

static unsigned fade_color(unsigned color, float opacity) {
	if (opacity < 0.0f) opacity = 0.0f;
	if (opacity > 1.0f) opacity = 1.0f;
	unsigned alpha = (unsigned)(((color >> 24) & 0xffU) * opacity);
	return (color & 0x00ffffffU) | (alpha << 24);
}

static float approach_for_frame(float current, float target,
	                            float response_per_second,
	                            float delta_seconds) {
	/* Exponential smoothing expressed in seconds keeps the same visual duration
	 * when decoding or I/O causes a frame to arrive late. */
	if (delta_seconds < 0.0f) delta_seconds = 0.0f;
	if (delta_seconds > 0.05f) delta_seconds = 0.05f;
	float alpha = 1.0f - expf(-response_per_second * delta_seconds);
	return current + (target - current) * alpha;
}

static void format_time(uint64_t milliseconds, char output[24]) {
	uint64_t seconds = milliseconds / 1000ULL;
	uint64_t hours = seconds / 3600ULL;
	seconds %= 3600ULL;
	if (hours) snprintf(output, 24, "%llu:%02llu:%02llu",
	                    (unsigned long long)hours,
	                    (unsigned long long)(seconds / 60ULL),
	                    (unsigned long long)(seconds % 60ULL));
	else snprintf(output, 24, "%llu:%02llu",
	              (unsigned long long)(seconds / 60ULL),
	              (unsigned long long)(seconds % 60ULL));
}

static void draw_play_icon(float x, float y, float height, unsigned color) {
	int rows = (int)height;
	for (int row = 0; row < rows; row++) {
		float half = row < rows / 2 ? (float)row + 1.0f
		                              : (float)(rows - row);
		vita2d_draw_rectangle(x, y + row, 3.0f + half * 1.3f, 1.25f,
		                      color);
	}
}

static void draw_skip_button(vita2d_font *font, float center_x,
	                         const char *label, float opacity) {
	unsigned fill = fade_color(VT_THEME_GLASS_A(225), opacity);
	unsigned text = fade_color(VT_THEME_TEXT, opacity);
	float left = center_x - 23.0f, top = PLAYER_TRANSPORT_Y - 23.0f;
#ifdef VMD_ENABLE_PAF_NATIVE
    if (ui_native_vita_button_fade(left, top, 46, 46, label, 0, opacity)) return;
#endif
	vita2d_draw_rectangle(left, top, 46.0f, 46.0f, fill);
	if (font) {
		int width = ui_font_text_width(font, UI_FONT_SMALL, label);
		ui_font_draw_text(font, (int)center_x - width / 2,
		                  (int)PLAYER_TRANSPORT_Y + 7, text,
		                  UI_FONT_SMALL, label);
	}
}

static void draw_player_input_lock_at(int x, int y, float opacity) {
	unsigned color = fade_color(VT_THEME_TEXT, opacity);
	unsigned panel = fade_color(RGBA8(7, 14, 17, 230), opacity);
	vita2d_draw_rectangle(x, y, 54, 44, panel);
	vita2d_draw_rectangle(x, y, 3, 44,
	                      fade_color(VT_THEME_COLD_LIGHT, opacity));
	vita2d_draw_line(x + 20, y + 19, x + 20, y + 12, color);
	vita2d_draw_line(x + 20, y + 12, x + 27, y + 6, color);
	vita2d_draw_line(x + 27, y + 6, x + 34, y + 12, color);
	vita2d_draw_line(x + 34, y + 12, x + 34, y + 19, color);
	vita2d_draw_rectangle(x + 16, y + 18, 22, 18, color);
	vita2d_draw_fill_circle((float)x + 27.0f, (float)y + 26.0f, 2.0f, panel);
	vita2d_draw_rectangle(x + 26, y + 26, 3, 6, panel);
}

static void draw_player_power_save_status_at(int x, int y) {
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int text_x = x > 660 ? x - 218 : x + 18;
	int baseline = y > 470 ? y - 5 : y + 16;
	vita2d_draw_rectangle(x, y + 5, 12, 2, VT_THEME_COLD_DIM);
	vita2d_draw_rectangle(x + 4, y + 1, 4, 10, VT_THEME_COLD_A(86));
	if (small)
		ui_font_draw_text(small, text_x, baseline, VT_THEME_COLD_DIM,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_POWER_SAVE_ACTIVE));
}

static void format_video_bitrate(uint64_t bitrate_bps, char out[32]) {
	if (!bitrate_bps) {
		snprintf(out, 32, "-- Mbps");
		return;
	}
	uint64_t tenths = (bitrate_bps + 50000ULL) / 100000ULL;
	if (tenths < 10ULL)
		snprintf(out, 32, "%llu kbps",
		         (unsigned long long)((bitrate_bps + 500ULL) / 1000ULL));
	else
		snprintf(out, 32, "%llu.%llu Mbps",
		         (unsigned long long)(tenths / 10ULL),
		         (unsigned long long)(tenths % 10ULL));
}

static void format_track_value(const VtDecoderPlayer *player, int subtitles,
	                           int selection, char out[128]) {
	int count = subtitles ? vt_decoder_subtitle_track_count(player)
	                      : vt_decoder_audio_track_count(player);
	int active = selection >= 0 ? selection
	           : subtitles ? vt_decoder_active_subtitle_track(player)
	                       : vt_decoder_active_audio_track(player);
	if (!count || (subtitles && active == 0)) {
		snprintf(out, 128, "%s", vt_i18n_str(VT_STR_PLAYER_OFF));
		return;
	}
	int index = subtitles ? active - 1 : active;
	VtDecoderTrackInfo info;
	int result = subtitles
	           ? vt_decoder_subtitle_track_info(player, index, &info)
	           : vt_decoder_audio_track_info(player, index, &info);
	if (result < 0) {
		snprintf(out, 128, "--");
		return;
	}
	char fallback[48];
	const char *name = info.title[0] ? info.title
	                 : info.language[0] ? info.language
	                 : info.codec[0] ? info.codec : NULL;
	if (!name) {
		snprintf(fallback, sizeof(fallback),
		         vt_i18n_str(VT_STR_PLAYER_TRACK_UNNAMED), index + 1);
		name = fallback;
	}
	snprintf(out, 128, vt_i18n_str(VT_STR_PLAYER_TRACK_FORMAT),
	         index + 1, count, name);
}

static void draw_hud(const VtHwPlayerScreenSource *source,
	                 const VtDecoderPlayerStatus *status, int paused,
	                 int volume, float hud_opacity, float volume_opacity,
	                 int dragging, float drag_fraction, int live) {
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (hud_opacity > 0.01f) {
		unsigned text = fade_color(VT_THEME_TEXT, hud_opacity);
		/* Lightweight stepped scrims protect type over both bright and dark video
		 * without turning the picture into a boxed-in page. */
		vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 76,
		                      fade_color(RGBA8(0, 0, 0, 218), hud_opacity));
		vita2d_draw_rectangle(0, 76, SCREEN_WIDTH, 22,
		                      fade_color(RGBA8(0, 0, 0, 118), hud_opacity));
		vita2d_draw_rectangle(0, 98, SCREEN_WIDTH, 16,
		                      fade_color(RGBA8(0, 0, 0, 48), hud_opacity));
		ui_brand_draw_player_breadcrumbs(live ? vt_i18n_str(VT_STR_IPTV_TITLE) :
		    vt_i18n_str(VT_STR_PLAYER_VIDEO), source->location, source->title, hud_opacity);
			if (small) {
				char shortcuts[192];
				ui_font_fit_text(small, UI_FONT_SMALL,
				                 live ? vt_i18n_str(vt_preferences_player_swap_shoulders() ? VT_STR_PLAYER_SWAPPED_SHORTCUTS_HINT : live == 2 ? VT_STR_IPTV_VOD_HINT : VT_STR_IPTV_PLAYER_HINT) : vt_i18n_str(source->allow_minimize
				                     ? VT_STR_PLAYER_SHORTCUTS_HINT
				                     : VT_STR_PLAYER_REMOTE_SHORTCUTS_HINT), shortcuts,
				                 sizeof(shortcuts), 400);
				ui_font_draw_text(small, 292, 73,
				                  fade_color(VT_THEME_COLD_LIGHT, hud_opacity),
				                  UI_FONT_SMALL, shortcuts);
                ui_text_line(292,94,400,vt_i18n_str(VT_STR_PLAYER_INPUT_LOCK_HINT),
                    fade_color(VT_THEME_COLD_LIGHT,hud_opacity),UI_FONT_SMALL);
			}
        if(ui_theme_id()==VT_UI_THEME_LEGACY){
		vita2d_draw_rectangle(0, 390, SCREEN_WIDTH, 30,
		                      fade_color(RGBA8(0, 0, 0, 46), hud_opacity));
		vita2d_draw_rectangle(0, 420, SCREEN_WIDTH, 124,
		                      fade_color(RGBA8(0, 0, 0, 190), hud_opacity));
        }else ui_theme_video_scrim(hud_opacity);
		if(ui_theme_id()!=VT_UI_THEME_LEGACY) {
            ui_theme_scene_begin(UI_SCENE_VIDEO,source->title);
            int theme=ui_theme_id();
            float cx=theme==VT_UI_THEME_DECK_90?240:theme==VT_UI_THEME_SIGNAL?780:theme==VT_UI_THEME_APERTURE?216:theme==VT_UI_THEME_RELAY?770:230;
            ui_theme_scene_model(UI_SCENE_VIDEO,cx,456,theme==VT_UI_THEME_RELAY?98:90,hud_opacity*.85f,!paused);
            ui_theme_scene_transport(PLAYER_PLAY_X-24,PLAYER_TRANSPORT_Y-22,48,44,1,hud_opacity);
        }
		char decoder[16];
		snprintf(decoder, sizeof(decoder), "%s DEC",
		         status->hardware_accelerated ? "HW" : "SW");
		unsigned border = status->hardware_accelerated
		                ? RGBA8(84, 229, 132, 255) : RGBA8(70, 219, 255, 255);
		vita2d_draw_rectangle(48, 431, 92, 32,
		                      fade_color(RGBA8(0, 0, 0, 190), hud_opacity));
		vita2d_draw_rectangle(48, 431, 3, 32, fade_color(border, hud_opacity));
		if (small) ui_font_draw_text(small, 60, 454, text,
		                             UI_FONT_SMALL, decoder);
		char bitrate[32], quality[80];
		format_video_bitrate(status->video_bitrate_bps, bitrate);
		snprintf(quality, sizeof(quality), "%up  %d fps  %s",
		         status->height, status->fps, bitrate);
		if (small) ui_text_line(610,454,332,quality,text,UI_FONT_SMALL);
		if (!live) {
			draw_skip_button(small, PLAYER_BACK_X, "\xE2\x86\x90", hud_opacity);
			draw_skip_button(small, PLAYER_FORWARD_X, "\xE2\x86\x92", hud_opacity);
		}
#ifdef VMD_ENABLE_PAF_NATIVE
        int native_play = ui_native_vita_button_fade(PLAYER_PLAY_X - 24,
            PLAYER_TRANSPORT_Y - 22, 48, 44, paused ? "\xE2\x96\xB6" : "II", 0, hud_opacity);
        if (!native_play) {
#endif
		vita2d_draw_rectangle(PLAYER_PLAY_X - 24, PLAYER_TRANSPORT_Y - 22, 48, 44,
		                      fade_color(VT_THEME_SURFACE_RAISED, hud_opacity));
		if (paused) draw_play_icon(PLAYER_PLAY_X-10, PLAYER_TRANSPORT_Y-16, 32, text);
		else {
			vita2d_draw_rectangle(PLAYER_PLAY_X-10, PLAYER_TRANSPORT_Y-16, 8, 32, text);
			vita2d_draw_rectangle(PLAYER_PLAY_X+5, PLAYER_TRANSPORT_Y-16, 8, 32, text);
		}
#ifdef VMD_ENABLE_PAF_NATIVE
        }
#endif
		if (paused && small) {
			const char *paused_text = vt_i18n_str(VT_STR_PLAYER_PAUSED);
			int width = ui_font_text_width(small, UI_FONT_SMALL, paused_text);
			vita2d_draw_rectangle(PLAYER_PLAY_X - width * .5f - 9, 391,
			                      width + 18, 25,
			                      fade_color(RGBA8(0, 0, 0, 206), hud_opacity));
			ui_font_draw_text(small, (int)PLAYER_PLAY_X - width / 2, 409,
			                  fade_color(VT_THEME_BLUE_LIGHT, hud_opacity),
			                  UI_FONT_SMALL, paused_text);
		}
		if (live) {
			vita2d_draw_fill_circle(55, 508, 4, fade_color(VT_THEME_SIGNAL_BRIGHT, hud_opacity));
			char label[192];
			if (live == 2) {
				char current[24], total[24]; format_time(status->position_ms, current); format_time(status->duration_ms, total);
				snprintf(label, sizeof(label), "VOD  %s / %s   %s", current, total, vt_i18n_str(VT_STR_IPTV_VOD_LINEAR));
			} else snprintf(label, sizeof(label), "%s", vt_i18n_str(paused ? VT_STR_IPTV_PAUSED_LIVE : VT_STR_IPTV_LIVE));
			if (small) ui_font_draw_text(small, 70, 514, text, UI_FONT_SMALL, label);
		} else {
		float progress = dragging ? drag_fraction
		               : status->duration_ms
		                   ? (float)status->position_ms / (float)status->duration_ms
		                   : 0.0f;
		if (progress < 0.0f) progress = 0.0f;
		if (progress > 1.0f) progress = 1.0f;
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W, 6,
		                      fade_color(VT_THEME_BORDER, hud_opacity));
		VtDecoderBufferStatus buffer = {0};
		int has_buffer = source->stream.buffer_status &&
		    source->stream.buffer_status(source->stream.opaque, &buffer) == 0 &&
		    buffer.capacity_bytes > 0;
		if (has_buffer &&
		    buffer.source_size > 0 && buffer.range_end > buffer.range_start) {
			float buffer_start = (float)((double)buffer.range_start /
			                                    (double)buffer.source_size);
			float buffer_end = (float)((double)buffer.range_end /
			                                  (double)buffer.source_size);
			if (buffer_start < 0.0f) buffer_start = 0.0f;
			if (buffer_end > 1.0f) buffer_end = 1.0f;
			if (buffer_end > buffer_start) {
				float buffer_width = TIMELINE_W * (buffer_end - buffer_start);
				/* A bounded network window is intentionally small relative to a
				 * feature film; preserve a visible diagnostic trace on the Vita. */
				if (buffer_width < 3.0f) buffer_width = 3.0f;
				if (TIMELINE_W * buffer_start + buffer_width > TIMELINE_W)
					buffer_width = TIMELINE_W * (1.0f - buffer_start);
				vita2d_draw_rectangle(TIMELINE_X + TIMELINE_W * buffer_start,
				                      TIMELINE_Y, buffer_width,
				                      6, fade_color(VT_THEME_SIGNAL_LIGHT, hud_opacity));
			}
		}
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W * progress, 6,
		                      fade_color(VT_THEME_BLUE_BRIGHT, hud_opacity));
		vita2d_draw_fill_circle(TIMELINE_X + TIMELINE_W * progress,
		                        TIMELINE_Y + 3.0f, dragging ? 9.0f : 6.0f, text);
		char current[24], duration[24];
		uint64_t shown_position = dragging && status->duration_ms
		                        ? (uint64_t)(drag_fraction * status->duration_ms)
		                        : status->position_ms;
		format_time(shown_position, current);
		format_time(status->duration_ms, duration);
		if (small) {
			ui_font_draw_text(small, TIMELINE_X, 499,
			                  dragging ? fade_color(VT_THEME_BLUE_LIGHT, hud_opacity)
			                           : text,
			                  UI_FONT_SMALL, current);
			int duration_width =
			    ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small,
			                  TIMELINE_X + TIMELINE_W - duration_width, 499,
			                  text, UI_FONT_SMALL, duration);
			if (has_buffer) {
				char reserve[64];
				snprintf(reserve, sizeof(reserve),
				         vt_i18n_str(VT_STR_PLAYER_BUFFER_RESERVE),
				         (double)buffer.resident_bytes / (1024.0 * 1024.0),
				         (double)buffer.capacity_bytes / (1024.0 * 1024.0));
				int reserve_width =
				    ui_font_text_width(small, UI_FONT_SMALL, reserve);
				int reserve_x = TIMELINE_X + TIMELINE_W - duration_width -
				                reserve_width - 20;
				ui_font_draw_text(small, reserve_x, 499,
				                  fade_color(VT_THEME_SIGNAL_LIGHT, hud_opacity),
				                  UI_FONT_SMALL, reserve);
			}
		}
	}
	}
	if (volume_opacity > 0.01f) {
		/* Volume never jumps vertically when the rest of the HUD fades. */
		int x = 812, y = 431;
		vita2d_draw_rectangle(x, y, 116, 34,
		                      fade_color(RGBA8(0, 0, 0, 216), volume_opacity));
		vita2d_draw_rectangle(x, y, 3, 34,
		                      fade_color(VT_THEME_BLUE_LIGHT, volume_opacity));
		if (small) ui_font_draw_textf(small, x + 14, y + 24,
		                             fade_color(VT_THEME_TEXT, volume_opacity),
		                             UI_FONT_SMALL,
		                             vt_i18n_str(VT_STR_PLAYER_VOLUME_FORMAT), volume);
	}
}

static void draw_buffering_overlay(float opacity, uint64_t now) {
	if (opacity <= 0.01f) return;
	const float x = 330.0f, y = 238.0f, width = 300.0f, height = 68.0f;
	vita2d_draw_rectangle(x - 4, y - 4, width + 8, height + 8,
	                      fade_color(RGBA8(0, 0, 0, 92), opacity));
	vita2d_draw_rectangle(x, y, width, height,
	                      fade_color(VT_THEME_GLASS_A(232), opacity));
	vita2d_draw_rectangle(x, y, 4, height,
	                      fade_color(VT_THEME_BLUE_LIGHT, opacity));
	if (opacity > 0.16f) ui_draw_spinner_compact(x + 38, y + 34, now);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small)
		ui_font_draw_text(small, (int)x + 70, (int)y + 41,
		                  fade_color(VT_THEME_TEXT, opacity), UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_BUFFERING));
}


static void draw_player_right_sidebar(float animation, float focus_position,
	                                  int cursor,
	                                  const VtDecoderPlayer *player,
	                                  const VtDecoderPlayerStatus *status,
	                                  int resume_available,
	                                  int decoder_switch_visible,
	                                  int pending_audio_track,
	                                  int pending_subtitle_track, int live) {
	if (animation <= 0.01f || !status) return;
#ifdef VMD_ENABLE_PAF_NATIVE
    int native_scope = ui_native_vita_scope(UI_NATIVE_SCOPE_ACTIONS);
#endif
	(void)cursor;
	const float width = RIGHT_PANEL_WIDTH;
	float x = SCREEN_WIDTH - width * animation;
	vita2d_draw_rectangle(x, 0, width, SCREEN_HEIGHT, VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(x, 0, 1, SCREEN_HEIGHT, VT_THEME_BORDER);
	vita2d_draw_rectangle(x + 4, 0, width - 4, 1, VT_THEME_BORDER);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text(body, (int)x + 28, 47, VT_THEME_TEXT,
		                  UI_FONT_BODY, vt_i18n_str(VT_STR_PLAYER_PANEL_TITLE));
	if (small) {
		vita2d_draw_rectangle(x + width - 65, 18, 42, 26, VT_THEME_SURFACE_RAISED);
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "R1");
		ui_font_draw_text(small,
		                  (int)(x + width - 44.0f - key_width * 0.5f), 38,
		                  VT_THEME_TEXT,
		                  UI_FONT_SMALL, "R1");
	}
	const char *labels[6] = {
		vt_i18n_str(VT_STR_PLAYER_FILL_SCREEN),
		vt_i18n_str(VT_STR_PLAYER_LOOP),
		vt_i18n_str(VT_STR_PLAYER_AUDIO_TRACK),
		vt_i18n_str(VT_STR_PLAYER_SUBTITLE_TRACK), NULL, NULL
	};
	char audio_value[128], subtitle_value[128];
	format_track_value(player, 0, pending_audio_track, audio_value);
	format_track_value(player, 1, pending_subtitle_track, subtitle_value);
	VtDecoderSubtitleState subtitle_state = vt_decoder_subtitle_state(player);
	int subtitle_request_visible = pending_subtitle_track > 0 &&
	    pending_subtitle_track == vt_decoder_active_subtitle_track(player);
	if (subtitle_request_visible &&
	    subtitle_state == VT_DECODER_SUBTITLE_PENDING)
		snprintf(subtitle_value, sizeof(subtitle_value), "%s",
		         vt_i18n_str(VT_STR_PLAYER_SUBTITLE_PENDING));
	else if (subtitle_request_visible &&
	         subtitle_state == VT_DECODER_SUBTITLE_FAILED)
		snprintf(subtitle_value, sizeof(subtitle_value), "%s",
		         vt_i18n_str(VT_STR_PLAYER_SUBTITLE_FAILED));
	const char *values[6] = {
		vt_i18n_str(vt_preferences_fill_screen() ? VT_STR_PLAYER_ON
		                                             : VT_STR_PLAYER_OFF),
		vt_i18n_str(vt_preferences_loop_enabled() ? VT_STR_PLAYER_ON
		                                             : VT_STR_PLAYER_OFF),
		audio_value, subtitle_value, NULL, NULL
	};
	int row_count = 4;
	if (live) {
		row_count = 2;
		labels[1] = vt_i18n_str(live == 2 ? VT_STR_PLAYER_RESTART_FROM_BEGINNING : VT_STR_IPTV_GO_LIVE);
		values[1] = "";
	}
	if (decoder_switch_visible) {
		labels[row_count] = vt_i18n_str(VT_STR_PLAYER_DECODER);
		values[row_count++] = vt_i18n_str(status->hardware_accelerated
		    ? VT_STR_PLAYER_DECODER_HARDWARE : VT_STR_PLAYER_DECODER_SOFTWARE);
	}
	if (resume_available) {
		labels[row_count] = vt_i18n_str(VT_STR_PLAYER_RESTART_FROM_BEGINNING);
		values[row_count++] = "00:00";
	}
	for (int i = 0; i < row_count; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		ui_panel(x + 18, y, width - 36, RIGHT_PANEL_ROW_HEIGHT, VT_THEME_SURFACE,
		         VT_THEME_BORDER_DIM, 0);
	}
	float focus_y = RIGHT_PANEL_ROW_Y + focus_position * RIGHT_PANEL_ROW_STEP;
	UiFocusMotion focus={.x=x+18,.y=focus_y,.width=width-36,
	    .height=RIGHT_PANEL_ROW_HEIGHT,.initialized=1};
	ui_focus_outline_draw(&focus);
	for (int i = 0; i < row_count; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		if (small) {
			ui_text_line(x+34,y+32,180,labels[i],VT_THEME_TEXT,UI_FONT_SMALL);
			const char *value = values[i];
			int value_w = ui_font_text_width(small, UI_FONT_SMALL, value);
			if (value_w > 170) {
				static char fitted_values[6][96];
				ui_font_fit_text(small, UI_FONT_SMALL, value, fitted_values[i],
				                 sizeof(fitted_values[i]), 170);
				value = fitted_values[i];
				value_w = ui_font_text_width(small, UI_FONT_SMALL, value);
			}
			unsigned value_color =
			    (i < 2 && !strcmp(value, vt_i18n_str(VT_STR_PLAYER_OFF))) ||
			    (i == 3 && pending_subtitle_track == 0)
			        ? VT_THEME_TEXT_MUTED
			        : (resume_available && i == row_count - 1)
			              ? VT_THEME_COLD_LIGHT : VT_THEME_BLUE_LIGHT;
			if (i == 3 && subtitle_request_visible &&
			    subtitle_state == VT_DECODER_SUBTITLE_PENDING)
				value_color = VT_THEME_SPECTRAL_LIGHT;
			else if (i == 3 && subtitle_request_visible &&
			         subtitle_state == VT_DECODER_SUBTITLE_FAILED)
				value_color = VT_THEME_DANGER;
			ui_font_draw_text(small, (int)x + (int)width - value_w - 34,
			                  (int)y + 32,
			                  value_color,
			                  UI_FONT_SMALL, value);
		}
	}
	int divider_y = (int)(RIGHT_PANEL_ROW_Y + row_count * RIGHT_PANEL_ROW_STEP + 2);
	vita2d_draw_rectangle(x + 18, divider_y, width - 36, 1, VT_THEME_BORDER);
	if (small) {
		char bitrate[32], decoder[96], bitrate_line[64];
		format_video_bitrate(status->video_bitrate_bps, bitrate);
		snprintf(decoder, sizeof(decoder), vt_i18n_str(VT_STR_PLAYER_DECODER_INFO),
		         status->hardware_accelerated ? "HW" : "SW",
		         status->height, status->fps);
		snprintf(bitrate_line, sizeof(bitrate_line),
		         vt_i18n_str(VT_STR_PLAYER_BITRATE_INFO), bitrate);
		char fitted_decoder[96], fitted_bitrate[64];
		ui_font_fit_text(small, UI_FONT_SMALL, decoder, fitted_decoder,
		                 sizeof(fitted_decoder), (int)width - 56);
		ui_font_fit_text(small, UI_FONT_SMALL, bitrate_line, fitted_bitrate,
		                 sizeof(fitted_bitrate), (int)width - 56);
		ui_font_draw_text(small, (int)x + 28, divider_y + 32, VT_THEME_TEXT,
		                  UI_FONT_SMALL, fitted_decoder);
		ui_font_draw_text(small, (int)x + 28, divider_y + 56,
		                  VT_THEME_BLUE_LIGHT, UI_FONT_SMALL, fitted_bitrate);
#ifdef VMD_ENABLE_PAF_NATIVE
        if(ui_native_vita_active())ui_panel(x+14,492,width-28,40,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
#endif
		ui_font_draw_text(small, (int)x + 28, 518, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_PLAYER_PANEL_HINT));
	}
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_scope(native_scope);
#endif
}

static int player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms,
	                        uint64_t *last_duration_ms,
	                        int *last_audio_track,
	                        int *last_subtitle_track, int *last_paused,
	                        VtIptvSession *live_session) {
	int live = live_session != NULL;
	if (!source || (!source->stream.open && !source->stream.open_cancelable))
		return -1;
	VtPerformanceClockGuard clock_guard;
	vt_performance_begin_video(&clock_guard, (int)source->expected_height,
	                           source->expected_fps);
	VtDecoderPlayer *player = vt_decoder_create();
	if (!player) {
		vt_performance_end_video(&clock_guard);
		return -1;
	}
	volatile int cancel = 0;
	OpenTask open;
	memset(&open, 0, sizeof(open));
	open.player = player;
	open.live = live;
	open.live_session = live_session;
	open.config.stream = source->stream;
	open.config.external_subtitle_count = source->external_subtitle_count;
	open.config.external_subtitles_replace_embedded =
	    source->external_subtitles_replace_embedded;
	if (open.config.external_subtitle_count < 0)
		open.config.external_subtitle_count = 0;
	if (open.config.external_subtitle_count > VT_DECODER_MAX_SUBTITLE_TRACKS)
		open.config.external_subtitle_count = VT_DECODER_MAX_SUBTITLE_TRACKS;
	memcpy(open.config.external_subtitles, source->external_subtitles,
	       (size_t)open.config.external_subtitle_count *
	           sizeof(open.config.external_subtitles[0]));
	int decoder_preference = vt_preferences_video_decoder();
	VtDecoderBackend session_backend_override = VT_DECODER_BACKEND_NONE;
	open.config.preferred_backend =
	    decoder_preference == VT_VIDEO_DECODER_HW_H264
	        ? VT_DECODER_BACKEND_HARDWARE
	    : decoder_preference == VT_VIDEO_DECODER_SW_FFMPEG
	        ? VT_DECODER_BACKEND_SOFTWARE
	        : VT_DECODER_BACKEND_NONE;
	open.config.expected_width = source->expected_width;
	open.config.expected_height = source->expected_height;
	open.config.expected_fps = source->expected_fps;
	open.config.start_position_ms = source->start_position_ms;
	open.config.audio_track = source->start_audio_track;
	open.config.subtitle_track = source->start_subtitle_track;
	open.config.start_paused = source->start_paused;
	open.config.volume_percent = vt_audio_volume_percent();
	open.config.cancel_flag = &cancel;
	const char *opening_status = source->authenticated_remote
	                           ? vt_i18n_str(VT_STR_PLAYER_OPEN_REMOTE)
	                           : vt_i18n_str(VT_STR_PLAYER_OPEN_LOCAL);
	UiPlayerLoadingInfo loading = {
		.title = source->title,
		.channel = source->location,
		.status = opening_status,
		.quality_height = source->expected_height,
		.cancel_action = cancel_player_operation,
		.cancel_ctx = player
	};
	volatile int start_over_requested = 0;
	if (open.config.start_position_ms > 0)
		loading.start_over_requested = &start_over_requested;
	int ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	if (start_over_requested) {
		/* The first worker exits through the decoder interrupt callback. Reuse the
		 * source only after it has joined, then perform a clean open at zero with a
		 * fresh player so no cancelled backend survives into the retry. */
		vt_decoder_destroy(player);
		player = vt_decoder_create();
		if (!player) {
			vt_performance_end_video(&clock_guard);
			return -1;
		}
		open.player = player;
		loading.cancel_ctx = player;
		cancel = 0;
		open.config.start_position_ms = 0;
		loading.start_over_requested = NULL;
		ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	}
	if (ret < 0) {
		vt_decoder_destroy(player);
		vt_performance_end_video(&clock_guard);
		return cancel ? 0 : ret;
	}
	int library_started = 0;
	uint64_t library_tick = 0, library_position = source->start_position_ms, library_duration = 0;
	uint64_t vod_duration = 0;
	if (live) {
		VtIptvSourceStatus input;
		vt_iptv_session_status(live_session, &input);
		if (input.finite) {
			live = 2;
			vod_duration = input.duration_ms;
		}
	}
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	PlayerInputLock input_lock;
	memset(&input_lock, 0, sizeof(input_lock));
	input_lock.armed = (previous.buttons & SCE_CTRL_SELECT) == 0;
	PlayerPowerSaveInput power_save_input;
	memset(&power_save_input, 0, sizeof(power_save_input));
	power_save_input.armed = (previous.buttons & SCE_CTRL_START) == 0;
	int energy_saving = 0;
	int energy_lock_position = 0;
	uint64_t energy_lock_move_us = 0;
	int paused = source->start_paused != 0;
	int hud_visible = 1;
	float hud_opacity = 1.0f;
	uint64_t favorite_error_until = 0;
	int dragging = 0;
	float drag_fraction = 0.0f;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, source->authenticated_remote
	                                    ? UI_SECTION_NETWORK
	                                    : UI_SECTION_LOCAL_MEDIA);
	if (live) ui_sections_sidebar_init(&sidebar, UI_SECTION_IPTV);
	int right_open = 0;
	int right_cursor = 0;
	int pending_audio_track = vt_decoder_active_audio_track(player);
	int pending_subtitle_track = vt_decoder_active_subtitle_track(player);
	int resume_available = open.config.start_position_ms > 0;
	float right_animation = 0.0f;
	float right_focus_position = 0.0f;
	UiFocusMotion right_motion={0};
	float buffering_opacity = 0.0f;
	uint64_t hud_deadline = sceKernelGetProcessTimeWide() + 3500000ULL;
	uint64_t volume_deadline = 0;
	uint64_t last_volume_input = 0;
	uint64_t previous_frame_us = sceKernelGetProcessTimeWide();
	int left_seek_direction = 0;
	uint64_t left_seek_repeat_at = 0;
	UiNavRepeat right_nav_repeat;
	ui_nav_repeat_reset(&right_nav_repeat);
	for (;;) {
		uint64_t now = sceKernelGetProcessTimeWide();
		float delta_seconds = (float)(now - previous_frame_us) / 1000000.0f;
		previous_frame_us = now;
		int input_available = sceCtrlPeekBufferPositive(0, &controls, 1) > 0;
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (!input_lock.locked && hud_visible && !energy_saving &&
		    ui_brand_sections_hit(touch_flags, touch.x, touch.y)) {
			pressed |= vt_preferences_player_swap_shoulders() ? SCE_CTRL_LEFT : SCE_CTRL_LTRIGGER;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int lock_changed = player_input_lock_update(&input_lock, controls.buttons,
		                                            now);
		pressed &= ~SCE_CTRL_SELECT;
		if (lock_changed) {
			dragging = 0;
			sidebar.open = 0;
			right_open = 0;
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
			ui_touch_reset();
		}
		if (input_lock.locked) {
			pressed = 0;
			controls.buttons &= SCE_CTRL_SELECT;
			controls.lx = STICK_CENTER;
			controls.ly = STICK_CENTER;
			controls.rx = STICK_CENTER;
			controls.ry = STICK_CENTER;
			left_seek_direction = 0;
			left_seek_repeat_at = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging = 0;
		}
		int power_event = player_power_save_update(
		    &power_save_input, controls.buttons, now,
		    input_available && !input_lock.locked);
		pressed &= ~SCE_CTRL_START;
		if (power_event == PLAYER_POWER_SAVE_EVENT_TOGGLE) {
			energy_saving = !energy_saving;
			energy_lock_position = 0;
			energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			dragging = 0;
			sidebar.open = 0;
			sidebar.animation = 0.0f;
			right_open = 0;
			right_animation = 0.0f;
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		if (power_event == PLAYER_POWER_SAVE_EVENT_SHORT_PRESS &&
		    source->allow_minimize) {
			ret = VT_HW_PLAYER_ACTION_MINIMIZE;
			break;
		}
		/* Eco is display-only, not an input lock. Transport/volume remain usable
		 * in darkness; gestures requiring visible targets bring the UI back. */
		unsigned panel_buttons = vt_preferences_player_swap_shoulders()
		    ? SCE_CTRL_LEFT | SCE_CTRL_RIGHT : SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
		if (energy_saving && !input_lock.locked &&
		    ((pressed & panel_buttons) || (touch_flags & UI_TOUCH_EVENT_DOWN))) {
			energy_saving = 0;
			hud_visible = 1;
			hud_opacity = 1.0f;
			hud_deadline = now + 3500000ULL;
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		VtDecoderPlayerStatus status;
		vt_decoder_get_status(player, &status);
		if (source->history_id && status.opened && !status.error &&
		    (!library_started || now - library_tick >= 1000000ULL)) {
			if (!library_started) {
				vt_library_start_id(source->history_id);
				library_started = 1;
			}
			library_position = status.position_ms;
			if (status.duration_ms)
				library_duration = status.duration_ms;
			vt_library_progress(source->history_id, library_position, library_duration);
			library_tick = now;
		}
		if (live && !paused && vt_iptv_session_event(live_session)) {
			ret = -1;
			break;
		}
		if (status.error && !(live && paused)) {
			if (!live && decoder_preference == VT_VIDEO_DECODER_AUTO &&
			    session_backend_override == VT_DECODER_BACKEND_NONE &&
			    vt_decoder_backend(player) == VT_DECODER_BACKEND_HARDWARE) {
				SeekTask task = {player, status.position_ms};
				cancel = 0;
				loading.status = vt_i18n_str(VT_STR_PLAYER_FALLBACK_SOFTWARE);
				fence_decoder_frame(player);
				ret = ui_player_loading_run(&loading, fallback_task, &task, &cancel, NULL, NULL);
				loading.status = opening_status;
				if (ret >= 0) {
					vt_decoder_set_paused(player, paused);
					vt_library_checkpoint();
					hud_visible = 1;
					now = resync_after_blocking_action(&controls, &previous, &left_seek_direction,
					                                   &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				}
			}
			ret = -1;
			break;
		}
		if (status.eof) {
			if (live || !vt_preferences_loop_enabled()) {
				ret = 0;
				break;
			}
			ret = run_seek(player, &loading, &cancel, 0);
			if (ret < 0)
				break;
			paused = 0;
			vt_decoder_set_paused(player, 0);
			hud_visible = 1;
			now = resync_after_blocking_action(&controls, &previous, &left_seek_direction,
			                                   &left_seek_repeat_at);
			hud_deadline = now + 3500000ULL;
			continue;
		}
		int swapped = vt_preferences_player_swap_shoulders();
		unsigned transport_pressed = pressed;
		unsigned left_toggle = swapped ? SCE_CTRL_LEFT : SCE_CTRL_LTRIGGER;
		unsigned right_toggle = swapped ? SCE_CTRL_RIGHT : SCE_CTRL_RTRIGGER;
		if ((pressed & left_toggle) &&
		    (right_open || right_animation > 0.01f)) {
			right_open = 0;
			right_animation = 0.0f;
		}
		if ((pressed & right_toggle) &&
		    (sidebar.open || sidebar.animation > 0.01f)) {
			sidebar.open = 0;
			sidebar.animation = 0.0f;
		}
		if (swapped) pressed &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
		if (swapped && !sidebar.open && !right_open &&
		    (pressed & SCE_CTRL_LEFT)) {
			pressed &= ~SCE_CTRL_LEFT;
			pressed |= SCE_CTRL_LTRIGGER;
		}
		if (pressed & right_toggle) {
			right_open = !right_open;
			right_cursor = 0;
			right_focus_position = 0.0f;
			pending_audio_track = vt_decoder_active_audio_track(player);
			pending_subtitle_track = vt_decoder_active_subtitle_track(player);
			ui_nav_repeat_reset(&right_nav_repeat);
			dragging = 0;
			pressed &= ~right_toggle;
		}
		if (!right_open && right_animation <= 0.01f) {
			int section = ui_sections_sidebar_handle_buttons(
			    &sidebar, &pressed, controls.buttons, controls.ly);
			if (section != UI_SECTION_NONE) {
				ret = VT_HW_PLAYER_ACTION_SECTION_BASE + section;
				break;
			}
		} else if (right_open) {
			int decoder_row = !live && decoder_preference == VT_VIDEO_DECODER_AUTO ? 4 : -1;
			int restart_row = 4 + (decoder_row >= 0 ? 1 : 0);
			unsigned right_navigation = ui_nav_repeat_update(
			    &right_nav_repeat, pressed, controls.buttons, controls.lx,
			    controls.ly, SCE_CTRL_UP | SCE_CTRL_DOWN);
			if ((right_navigation & SCE_CTRL_UP) && right_cursor > 0) right_cursor--;
			int right_row_count = live ? 2 : restart_row + (resume_available ? 1 : 0);
			if ((right_navigation & SCE_CTRL_DOWN) &&
			    right_cursor + 1 < right_row_count) right_cursor++;
			if (right_cursor == 2 &&
			    (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
				int count = vt_decoder_audio_track_count(player);
				if (count > 1) {
					int direction = (pressed & SCE_CTRL_LEFT) ? -1 : 1;
					pending_audio_track =
					    (pending_audio_track + (direction < 0 ? count - 1 : 1)) % count;
				}
			} else if (right_cursor == 3 &&
			           (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
				int choices = vt_decoder_subtitle_track_count(player) + 1;
				if (choices > 1) {
					int direction = (pressed & SCE_CTRL_LEFT) ? -1 : 1;
					pending_subtitle_track =
					    (pending_subtitle_track + (direction < 0 ? choices - 1 : 1)) % choices;
				}
			}
			if (pressed & SCE_CTRL_CROSS) {
				if (live && right_cursor == 1) { ret = VT_HW_PLAYER_ACTION_GO_LIVE; break; }
				if (right_cursor == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else if (right_cursor == 1)
					vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
				else if (right_cursor == 2 || right_cursor == 3) {
					int selected_track = right_cursor == 3
					                   ? pending_subtitle_track : pending_audio_track;
					ret = run_track_change(player, &loading, &cancel,
					                       status.position_ms,
					                       right_cursor == 3, selected_track);
					loading.status = opening_status;
					if (right_cursor == 2)
						pending_audio_track =
						    vt_decoder_active_audio_track(player);
					if (ret < 0) {
						if (right_cursor == 3) {
							pending_subtitle_track =
							    vt_decoder_active_subtitle_track(player);
							ret = 0;
						} else break;
					}
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				} else if (right_cursor == decoder_row) {
					VtDecoderBackend target = vt_decoder_backend(player) ==
					                              VT_DECODER_BACKEND_HARDWARE
					                          ? VT_DECODER_BACKEND_SOFTWARE
					                          : VT_DECODER_BACKEND_HARDWARE;
					loading.status = vt_i18n_str(VT_STR_PLAYER_CHANGE_DECODER);
					int change_ret = run_backend_change(
					    &player, &open, &loading, &cancel, target,
					    status.position_ms, pending_audio_track,
					    pending_subtitle_track);
					loading.status = opening_status;
					if (change_ret < 0) { ret = change_ret; break; }
					if (change_ret == 0) session_backend_override = target;
					pending_audio_track = vt_decoder_active_audio_track(player);
					pending_subtitle_track = vt_decoder_active_subtitle_track(player);
					vt_decoder_set_paused(player, paused);
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				} else if (resume_available && right_cursor == restart_row) {
					ret = run_seek(player, &loading, &cancel, 0);
					loading.status = opening_status;
					if (ret < 0) break;
					if (source->history_id)
						vt_playback_history_update(source->history_id, 0,
						                           status.duration_ms);
					vt_library_progress(source->history_id, 0, status.duration_ms);
					vt_library_checkpoint();
					paused = 0;
					vt_decoder_set_paused(player, 0);
					resume_available = 0;
					right_open = 0;
					right_cursor = 0;
					right_focus_position = 0.0f;
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				}
			}
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
		} else {
			/* Closing right drawer: keep the frame modal until it clears. */
			pressed = 0;
		}
		/* A drawer remains modal until its closing animation is fully gone.
		 * This prevents a second tap or shoulder press from reaching playback
		 * controls through a still-visible panel, and prevents opposite drawers
		 * from crossing over each other. */
		int panels_open = sidebar.open || right_open ||
		                  sidebar.animation > 0.01f || right_animation > 0.01f;
		int panels_closing = (!sidebar.open && sidebar.animation > 0.01f) ||
		                     (!right_open && right_animation > 0.01f);
		if (panels_closing) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		if (panels_open) {
			dragging = 0;
			left_seek_direction = 0;
			left_seek_repeat_at = 0;
		}
		if (!panels_open && (pressed & SCE_CTRL_CIRCLE)) { ret = 0; break; }
		if (!panels_open && (pressed & SCE_CTRL_CROSS)) {
			if (live == 1 && paused) { ret = VT_HW_PLAYER_ACTION_GO_LIVE; break; }
			paused = !paused;
			vt_decoder_set_paused(player, paused);
			if (source->history_id)
				vt_library_progress(source->history_id, status.position_ms, status.duration_ms);
			vt_library_checkpoint();
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
		}
		unsigned transport = swapped ? transport_pressed : pressed;
		int seek_direction =
		    !panels_open && (transport & (swapped ? SCE_CTRL_RTRIGGER : SCE_CTRL_RIGHT))  ? 1
			: !panels_open && (transport & (swapped ? SCE_CTRL_LTRIGGER : SCE_CTRL_LEFT)) ? -1
			                                                                              : 0;
		int left_x = (int)controls.lx - STICK_CENTER;
		int left_y = (int)controls.ly - STICK_CENTER;
		int abs_left_x = left_x < 0 ? -left_x : left_x;
		int abs_left_y = left_y < 0 ? -left_y : left_y;
		int analog_seek_direction = !panels_open && abs_left_x > STICK_DEADZONE &&
		                            abs_left_x > abs_left_y
		                          ? (left_x < 0 ? -1 : 1) : 0;
		if (!analog_seek_direction) {
			left_seek_direction = 0;
			left_seek_repeat_at = 0;
		} else if (analog_seek_direction != left_seek_direction) {
			left_seek_direction = analog_seek_direction;
			left_seek_repeat_at = now + SEEK_REPEAT_DELAY_US;
			if (!seek_direction) seek_direction = analog_seek_direction;
		} else if (now >= left_seek_repeat_at) {
			left_seek_repeat_at = now + SEEK_REPEAT_INTERVAL_US;
			if (!seek_direction) seek_direction = analog_seek_direction;
		}
		if (seek_direction && status.duration_ms) {
			int64_t target = (int64_t)status.position_ms + seek_direction * 10000LL;
			if (target < 0) target = 0;
			if ((uint64_t)target > status.duration_ms) target = status.duration_ms;
			ret = run_seek(player, &loading, &cancel, (uint64_t)target);
			if (ret < 0) break;
			hud_visible = 1;
			now = resync_after_blocking_action(
			    &controls, &previous, &left_seek_direction,
			    &left_seek_repeat_at);
			hud_deadline = now + 3500000ULL;
			continue;
		}
		int right_y = (int)controls.ry - STICK_CENTER;
		if (!panels_open &&
		    (right_y > STICK_DEADZONE || right_y < -STICK_DEADZONE) &&
		    now - last_volume_input >= 80000ULL) {
			int volume = vt_audio_volume_percent() + (right_y < 0 ? 5 : -5);
			if (volume < 0) volume = 0;
			if (volume > 300) volume = 300;
			vt_audio_volume_set_percent(volume);
			vt_decoder_set_volume(player, volume);
			last_volume_input = now;
			volume_deadline = now + 1200000ULL;
		}
		if (sidebar.open) {
			int section = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (section != UI_SECTION_NONE) {
				ret = VT_HW_PLAYER_ACTION_SECTION_BASE + section;
				break;
			}
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (right_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			int panel_x = (int)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH * right_animation);
			int handled = 0;
			int track_change_failed = 0;
			int track_change_completed = 0;
			int decoder_row = !live && decoder_preference == VT_VIDEO_DECODER_AUTO ? 4 : -1;
			int restart_row = 4 + (decoder_row >= 0 ? 1 : 0);
			int right_row_count = live ? 2 : restart_row + (resume_available ? 1 : 0);
			for (int i = 0; i < right_row_count; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, panel_x + 18,
				                       (int)(RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP),
				                       (int)RIGHT_PANEL_WIDTH - 36,
				                       (int)RIGHT_PANEL_ROW_HEIGHT)) continue;
				right_cursor = i;
				handled = 1;
				if (live && i == 1) { ret = VT_HW_PLAYER_ACTION_GO_LIVE; break; }
				if (i == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else if (i == 1)
					vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
				else if (i == 2 || i == 3) {
					int count = i == 3
					          ? vt_decoder_subtitle_track_count(player) + 1
					          : vt_decoder_audio_track_count(player);
					int *pending = i == 3 ? &pending_subtitle_track
					                      : &pending_audio_track;
					if (count > 1) *pending = (*pending + 1) % count;
					ret = run_track_change(player, &loading, &cancel,
					                       status.position_ms, i == 3, *pending);
					loading.status = opening_status;
					if (i == 2)
						pending_audio_track =
						    vt_decoder_active_audio_track(player);
					if (ret < 0 && i == 3) {
						pending_subtitle_track =
						    vt_decoder_active_subtitle_track(player);
						ret = 0;
					}
					track_change_failed = ret < 0;
					track_change_completed = ret >= 0;
				} else if (i == decoder_row) {
					VtDecoderBackend target = vt_decoder_backend(player) ==
					                              VT_DECODER_BACKEND_HARDWARE
					                          ? VT_DECODER_BACKEND_SOFTWARE
					                          : VT_DECODER_BACKEND_HARDWARE;
					loading.status = vt_i18n_str(VT_STR_PLAYER_CHANGE_DECODER);
					int change_ret = run_backend_change(
					    &player, &open, &loading, &cancel, target,
					    status.position_ms, pending_audio_track,
					    pending_subtitle_track);
					loading.status = opening_status;
					track_change_failed = change_ret < 0;
					track_change_completed = change_ret >= 0;
					if (change_ret == 0) session_backend_override = target;
					if (change_ret >= 0) {
						pending_audio_track = vt_decoder_active_audio_track(player);
						pending_subtitle_track = vt_decoder_active_subtitle_track(player);
						vt_decoder_set_paused(player, paused);
					}
				} else if (resume_available && i == restart_row) {
					ret = run_seek(player, &loading, &cancel, 0);
					loading.status = opening_status;
					track_change_failed = ret < 0;
					track_change_completed = ret >= 0;
					if (ret >= 0) {
						if (source->history_id)
							vt_playback_history_update(source->history_id, 0,
							                           status.duration_ms);
						vt_library_progress(source->history_id, 0, status.duration_ms);
						vt_library_checkpoint();
						paused = 0;
						vt_decoder_set_paused(player, 0);
						resume_available = 0;
						right_open = 0;
						right_cursor = 0;
						right_focus_position = 0.0f;
					}
				}
			}
			if (track_change_failed || ret == VT_HW_PLAYER_ACTION_GO_LIVE) break;
			if (track_change_completed) {
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_deadline = now + 3500000ULL;
				continue;
			}
			if (!handled && touch.x < panel_x) right_open = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int hud_interactive = hud_visible || hud_opacity > 0.65f;
		if (source->favorite && !panels_open && !input_lock.locked &&
		    ((pressed & SCE_CTRL_SQUARE) || (hud_interactive && !energy_saving &&
		      (touch_flags & UI_TOUCH_EVENT_TAP) && ui_touch_hit_rect(touch.x,touch.y,744,76,192,40)))) {
			int favorite_result=source->favorite(source->favorite_ctx,1);
			favorite_error_until=favorite_result<0?now+2500000ULL:0;
			hud_visible=1;hud_deadline=now+3500000ULL;
			pressed&=~SCE_CTRL_SQUARE;touch_flags=UI_TOUCH_EVENT_NONE;
		}
		if (!panels_open && hud_interactive && status.duration_ms &&
		    (touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, TIMELINE_X, TIMELINE_Y - 16,
		                      TIMELINE_W, 42)) {
			dragging = 1;
			drag_fraction = (float)(touch.x - TIMELINE_X) / (float)TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
		}
		if (!panels_open && dragging &&
		    (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD |
		                    UI_TOUCH_EVENT_UP))) {
			drag_fraction = (float)(touch.x - TIMELINE_X) / (float)TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
			if (touch_flags & UI_TOUCH_EVENT_UP) {
				ret = run_seek(player, &loading, &cancel,
				               (uint64_t)(drag_fraction * status.duration_ms));
				dragging = 0;
				if (ret < 0) break;
				hud_visible = 1;
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_deadline = now + 3500000ULL;
				continue;
			}
		} else if (!panels_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			if (!hud_interactive) {
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
			} else if (ui_touch_hit_rect(touch.x, touch.y,
			                             (int)PLAYER_PLAY_X - 34,
			                             (int)PLAYER_TRANSPORT_Y - 34, 68, 68)) {
				if (live == 1 && paused) { ret = VT_HW_PLAYER_ACTION_GO_LIVE; break; }
				paused = !paused;
				vt_decoder_set_paused(player, paused);
				if (source->history_id)
					vt_library_progress(source->history_id, status.position_ms, status.duration_ms);
				vt_library_checkpoint();
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
			} else if (status.duration_ms &&
			           ui_touch_hit_rect(touch.x, touch.y, (int)PLAYER_BACK_X - 28,
			                             (int)PLAYER_TRANSPORT_Y - 28, 56, 56)) {
				uint64_t target = status.position_ms > 10000ULL ? status.position_ms - 10000ULL : 0;
				ret = run_seek(player, &loading, &cancel, target);
				if (ret < 0)
					break;
				now = resync_after_blocking_action(&controls, &previous, &left_seek_direction,
				                                   &left_seek_repeat_at);
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
				continue;
			} else if (status.duration_ms &&
			           ui_touch_hit_rect(touch.x, touch.y, (int)PLAYER_FORWARD_X - 28,
			                             (int)PLAYER_TRANSPORT_Y - 28, 56, 56)) {
				uint64_t target = status.position_ms + 10000ULL;
				if (target > status.duration_ms)
					target = status.duration_ms;
				ret = run_seek(player, &loading, &cancel, target);
				if (ret < 0)
					break;
				now = resync_after_blocking_action(&controls, &previous, &left_seek_direction,
				                                   &left_seek_repeat_at);
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
				continue;
			} else if (touch.y < 390) {
				hud_visible = 0;
			}
		}
		if (!paused && now > hud_deadline)
			hud_visible = 0;
		ui_sections_sidebar_tick(&sidebar);
		if (vt_preferences_reduce_motion()) {
			right_animation = right_open ? 1.0f : 0.0f;
			right_focus_position = (float)right_cursor;
			hud_opacity = hud_visible ? 1.0f : 0.0f;
			buffering_opacity = status.ready_frames == 0 && !paused ? 1.0f : 0.0f;
		} else {
			right_animation = approach_for_frame(
			    right_animation, right_open ? 1.0f : 0.0f, 20.0f, delta_seconds);

			hud_opacity = approach_for_frame(
			    hud_opacity, hud_visible ? 1.0f : 0.0f, 13.5f, delta_seconds);
			buffering_opacity = approach_for_frame(
			    buffering_opacity,
			    status.ready_frames == 0 && !paused ? 1.0f : 0.0f,
			    12.0f, delta_seconds);
		}
		ui_focus_motion_tick(&right_motion,0,(float)right_cursor,1,1);
		right_focus_position=right_motion.y;
		if (right_animation < 0.001f && !right_open) right_animation = 0.0f;
		if (hud_opacity < 0.001f && !hud_visible) hud_opacity = 0.0f;
		if (buffering_opacity < 0.001f && status.ready_frames > 0)
			buffering_opacity = 0.0f;
		float volume_opacity = 0.0f;
		if (now < volume_deadline) {
			uint64_t remaining = volume_deadline - now;
			volume_opacity = remaining >= 280000ULL
			               ? 1.0f : (float)remaining / 280000.0f;
		}
		vt_display_keep_awake_tick();
		const unsigned *theme_before=ui_theme_media_begin();
		vita2d_start_drawing();
		vita2d_clear_screen();
		vita2d_draw_rectangle(0,0,960,544,VT_THEME_MEDIA_BACKDROP);
		if (energy_saving)
			vt_decoder_discard_video_to_clock(player);
		else
			vt_decoder_present(player, vt_preferences_fill_screen());
		vt_decoder_get_status(player, &status);
		if (energy_saving) {
			if (now >= energy_lock_move_us) {
				energy_lock_position =
				    (energy_lock_position + 1) % PLAYER_POWER_SAVE_LOCK_POSITIONS;
				energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			}
			vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
			                      RGBA8(0, 0, 0, 255));
			int lock_x, lock_y;
			player_power_save_lock_position(energy_lock_position,
			                                &lock_x, &lock_y);
			draw_player_power_save_status_at(lock_x, lock_y);
			if (input_lock.locked)
				draw_player_input_lock_at(lock_x,
				    lock_y > 430 ? lock_y - 50 : lock_y + 34, 0.18f);
		} else {
			/* Captions belong to the video layer. Controls, telemetry, buffering
			 * feedback and drawers must always be composited above them. */
			char subtitle[512];
			if (vt_decoder_subtitle_text(player, status.position_ms,
			                             subtitle, sizeof(subtitle)) > 0)
				ui_subtitle_draw(subtitle, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
			if (live == 2) status.duration_ms = vod_duration;
			if (hud_opacity > 0.01f || volume_opacity > 0.01f)
				draw_hud(source, &status, paused, vt_audio_volume_percent(),
				         hud_opacity, volume_opacity, dragging, drag_fraction, live);
			else if (vt_preferences_player_status_always_visible())
				ui_brand_draw_status_indicators_alpha(1.0f);
			draw_buffering_overlay(buffering_opacity, now);
			if (source->favorite && hud_opacity>.01f) {
				int starred=source->favorite(source->favorite_ctx,0)>0;
				vita2d_draw_rectangle(744,76,192,40,fade_color(VT_THEME_SURFACE,hud_opacity));
				ui_text_line(754,103,172,vt_i18n_str(now<favorite_error_until?VT_STR_IPTV_SAVE_FAILED:
				    starred?VT_STR_IPTV_PLAYER_UNSTAR:VT_STR_IPTV_PLAYER_STAR),
				    fade_color(starred?VT_THEME_SIGNAL_BRIGHT:VT_THEME_TEXT,hud_opacity),UI_FONT_SMALL);
			}
			if (input_lock.locked && hud_opacity > 0.01f)
				draw_player_input_lock_at(24, 86, hud_opacity);
			if (sidebar.animation > 0.01f)
				ui_sections_sidebar_draw(&sidebar);
			draw_player_right_sidebar(right_animation, right_focus_position,
			                          right_cursor, player, &status,
			                          resume_available,
			                          !live && decoder_preference == VT_VIDEO_DECODER_AUTO,
			                          pending_audio_track,
			                          pending_subtitle_track, live);
		}
		ui_theme_media_end(theme_before);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vt_decoder_render_complete(player);
		vita2d_swap_buffers();
        /* GXM/vsync and decoding already consumed part of this frame. */
        ui_frame_pace(now);
	}
	VtDecoderPlayerStatus final_status;
	vt_decoder_get_status(player, &final_status);
	if (library_started && source->history_id) {
		if (ret >= 0 && final_status.opened && !final_status.error) {
			library_position = final_status.position_ms;
			if (final_status.duration_ms)
				library_duration = final_status.duration_ms;
		}
		vt_library_progress(source->history_id, library_position, library_duration);
		vt_library_checkpoint();
		if (ret < 0) {
			final_status.position_ms = library_position;
			if (library_duration)
				final_status.duration_ms = library_duration;
		}
	}
	if (last_paused)
		*last_paused = paused;
	if (last_position_ms)
		*last_position_ms = final_status.position_ms;
	if (last_duration_ms)
		*last_duration_ms = final_status.duration_ms;
	if (last_audio_track)
		*last_audio_track = vt_decoder_active_audio_track(player);
	if (last_subtitle_track)
		*last_subtitle_track = vt_decoder_active_subtitle_track(player);
	vt_decoder_destroy(player);
	vt_performance_end_video(&clock_guard);
	if (ret < 0 && cancel) ret = 0;
	return ret;
}

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
    uint64_t *position, uint64_t *duration, int *audio, int *subtitle, int *paused) {
	return player_screen_run(source, position, duration, audio, subtitle, paused, NULL);
}

int vt_hw_player_screen_run_live(const VtHwPlayerScreenSource *source,
                                VtIptvSession *session) {
	if (!source || !session || source->allow_minimize || source->start_position_ms ||
	    source->start_audio_track || source->start_subtitle_track || source->start_paused)
		return -1;
	return player_screen_run(source, NULL, NULL, NULL, NULL, NULL, session);
}

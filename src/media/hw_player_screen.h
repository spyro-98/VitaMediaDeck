#ifndef VITAMEDIADECK_MEDIA_HW_PLAYER_SCREEN_H
#define VITAMEDIADECK_MEDIA_HW_PLAYER_SCREEN_H

#include <stdint.h>

#include "media/vita_decoder.h"
#include "network/iptv_options.h"
#include "network/iptv_quality.h"

typedef struct {
	VtDecoderStreamFactory stream;
	VtDecoderExternalSubtitle external_subtitles[
		VT_DECODER_MAX_SUBTITLE_TRACKS];
	int external_subtitle_count;
	int external_subtitles_replace_embedded;
	const char *title;
	const char *location;
	const char *history_id;
	int authenticated_remote;
	int allow_minimize;
	uint64_t start_position_ms;
	int start_audio_track;
	int start_subtitle_track;
	int start_paused;
	uint32_t expected_width;
	uint32_t expected_height;
	int expected_fps;
	/* Optional app-owned favourite action. Query is allocation/IO-free;
	 * toggle returns the new state or a negative persistence error. */
	int (*favorite)(void *ctx, int toggle);
	void *favorite_ctx;
    int (*saved)(void *ctx,int toggle); /* Independent saved-channel membership. */
    void *saved_ctx;
    VtIptvQualityTrial *quality_trial;
    VtDecoderBackend *iptv_backend;
    const VtIptvOptions *iptv_options;
    VtIptvSelection *iptv_selection;
    int (*iptv_program)(void *ctx,VtIptvProgram *out,volatile int *cancel,int refresh);
    void *iptv_program_ctx;
    void (*iptv_metadata)(void *ctx,const VtIptvProgram *program);
    void *iptv_metadata_ctx;
    size_t iptv_program_ctx_size; /* Deep value snapshot owned by the async job. */
    /* Called once after advancing presented playback has been validated. */
    int (*iptv_started)(void *ctx,uint32_t width,uint32_t height);
    void *iptv_started_ctx;
} VtHwPlayerScreenSource;

/* A player can hand navigation back to the application's shared sections
 * through its L1 sidebar without conflating that with decoder errors. */
#define VT_HW_PLAYER_ACTION_SECTION_BASE 100
#define VT_HW_PLAYER_ACTION_MINIMIZE 1
#define VT_HW_PLAYER_ACTION_GO_LIVE 2
#define VT_HW_PLAYER_ACTION_STREAM_CHANGE 3
#define VT_HW_PLAYER_ACTION_TRIAL_DONE 4
#define VT_HW_PLAYER_ACTION_TRIAL_SKIP 5
#define VT_HW_PLAYER_ACTION_RECALIBRATE 6

struct VtIptvSession;
/* Live-specific entry point. Resume returns GO_LIVE so the owner closes both
 * demuxers/source and reconnects at the current live edge, never old pause data. */
int vt_hw_player_screen_run_live(const VtHwPlayerScreenSource *source,
                                 struct VtIptvSession *session);

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms,
	                        uint64_t *last_duration_ms,
	                        int *last_audio_track,
	                        int *last_subtitle_track, int *last_paused);

#endif

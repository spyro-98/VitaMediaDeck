#ifndef VITAWAVE_SETTINGS_PREFERENCES_H
#define VITAWAVE_SETTINGS_PREFERENCES_H

#define VT_DEFAULT_QUALITY_360 360
#define VT_DEFAULT_QUALITY_480 480
#define VT_DEFAULT_QUALITY_720 720

#define VT_DEFAULT_FRAME_RATE_24 24
#define VT_DEFAULT_FRAME_RATE_25 25
#define VT_DEFAULT_FRAME_RATE_27 27
#define VT_DEFAULT_FRAME_RATE_30 30
#define VT_DEFAULT_FRAME_RATE_50 50
#define VT_DEFAULT_FRAME_RATE_60 60

/* Stored language preference. VT_LANGUAGE_AUTO follows the system language
 * (see src/i18n/i18n.h). It is also the zero value read from reserved[0] in
 * records written before this field existed, preserving the correct default
 * without migrating existing settings.bin files. */
#define VT_LANGUAGE_AUTO 0
#define VT_LANGUAGE_EN   1
#define VT_LANGUAGE_IT   2
#define VT_LANGUAGE_ES   3
#define VT_LANGUAGE_FR   4
#define VT_LANGUAGE_DE   5
#define VT_LANGUAGE_PT   6
#define VT_LANGUAGE_RU   7

#define VT_CLOCK_SOURCE_PSVSHELL 0
#define VT_CLOCK_SOURCE_APP      1

/* Subtitle appearance. Values are stable public preference values; their
 * compact on-disk bit encoding is deliberately private to preferences.c. */
#define VT_SUBTITLE_OUTLINE_1 1
#define VT_SUBTITLE_OUTLINE_2 2
#define VT_SUBTITLE_OUTLINE_3 3

#define VT_SUBTITLE_BORDER_BLACK    0
#define VT_SUBTITLE_BORDER_MIDNIGHT 1
#define VT_SUBTITLE_BORDER_WHITE    2
#define VT_SUBTITLE_BORDER_YELLOW   3

#define VT_SUBTITLE_TEXT_WHITE  0
#define VT_SUBTITLE_TEXT_YELLOW 1
#define VT_SUBTITLE_TEXT_CYAN   2
#define VT_SUBTITLE_TEXT_GREEN  3

#define VT_SUBTITLE_SIZE_SMALL  0
#define VT_SUBTITLE_SIZE_MEDIUM 1
#define VT_SUBTITLE_SIZE_LARGE  2

#define VT_SUBTITLE_WIDTH_60 0
#define VT_SUBTITLE_WIDTH_75 1
#define VT_SUBTITLE_WIDTH_88 2
#define VT_SUBTITLE_WIDTH_96 3

#define VT_SUBTITLE_POSITION_BOTTOM 0
#define VT_SUBTITLE_POSITION_LOW    1
#define VT_SUBTITLE_POSITION_CENTER 2
#define VT_SUBTITLE_POSITION_HIGH   3

/* Loads persistent preferences. A missing file uses defaults and is not an
 * error; a corrupt record is ignored in favor of defaults. */
int vt_preferences_init(void);

/* Preferred maximum height for a normal selection. Format selection chooses
 * the best available variant that does not exceed it. */
int vt_preferences_default_quality(void);

/* Persists 360, 480 or 720 immediately. The in-memory value changes only
 * after the disk commit succeeds. */
int vt_preferences_set_default_quality(int height);

/* Preferred maximum frame rate at the selected resolution. V1 records did
 * not contain the field and migrate to the historical 30 fps behavior. */
int vt_preferences_default_frame_rate(void);
int vt_preferences_set_default_frame_rate(int frame_rate);

/* Stored language preference; may be VT_LANGUAGE_AUTO. */
int vt_preferences_language(void);

/* Persists VT_LANGUAGE_AUTO/EN/IT immediately. The in-memory value changes
 * only after the disk commit succeeds. */
int vt_preferences_set_language(int language);

/* The controls reference is presented once after upgrading/first install.
 * The bit lives in the existing flags word, so 1.0 settings remain readable. */
int vt_preferences_startup_controls_seen(void);
int vt_preferences_set_startup_controls_seen(int seen);

/* Default keeps the original VitaWave mapping: L1/R1 open the two player
 * panels and D-pad left/right seek. The opt-in swaps those two roles. */
int vt_preferences_player_swap_shoulders(void);
int vt_preferences_set_player_swap_shoulders(int enabled);

/* Per-media-type local presentation defaults. */
int vt_preferences_local_video_grid(void);
int vt_preferences_set_local_video_grid(int enabled);
int vt_preferences_local_music_grid(void);
int vt_preferences_set_local_music_grid(int enabled);

/* Music playback can independently keep the OLED/display timers awake. It is
 * enabled for old and new settings records; disabling it restores the normal
 * system timeout while audio continues. */
int vt_preferences_music_keep_display_awake(void);
int vt_preferences_set_music_keep_display_awake(int enabled);

/* Loop and fill-screen defaults persist across playback sessions instead of
 * requiring the user to enable them again from the player sidebar. */
int vt_preferences_loop_enabled(void);
int vt_preferences_set_loop_enabled(int enabled);
int vt_preferences_fill_screen(void);
int vt_preferences_set_fill_screen(int enabled);

/* UI motion preference. */
int vt_preferences_reduce_motion(void);
int vt_preferences_set_reduce_motion(int enabled);

/* Optional developer telemetry drawn over the player. It is deliberately
 * disabled by default and never changes playback behaviour. */
int vt_preferences_player_debug_enabled(void);
int vt_preferences_set_player_debug_enabled(int enabled);

/* Battery, clock and Wi-Fi normally follow the player HUD animation. This
 * opt-in keeps only that compact status block visible when the HUD hides. */
int vt_preferences_player_status_always_visible(void);
int vt_preferences_set_player_status_always_visible(int enabled);

/* The mini-player shows live video whenever the active source can expose
 * frames. Static artwork is an explicit opt-out and remains the automatic
 * fallback for audio-only sources. */
int vt_preferences_mini_player_animated(void);
int vt_preferences_set_mini_player_animated(int enabled);

/* Optional pop-out viewport. The compact transport bar remains unchanged;
 * this controls whether each newly minimized video starts at one-quarter of
 * the screen width instead of requiring a tap on its preview. */
int vt_preferences_mini_player_expanded_default(void);
int vt_preferences_set_mini_player_expanded_default(int enabled);

/* Diagnostic history always remains available in RAM. Persistent .txt log
 * writes are explicit opt-in so normal playback performs no logging I/O. */
int vt_preferences_disk_logs_enabled(void);
int vt_preferences_set_disk_logs_enabled(int enabled);

/* Legacy buffering-policy bit kept for settings-file compatibility. */
int vt_preferences_stream_fallback_enabled(void);
int vt_preferences_set_stream_fallback_enabled(int enabled);

/* Explicit clock ownership. APP is the deterministic default and applies the
 * documented VitaWave video profile. PSVshell is an opt-in and means VitaWave
 * never writes or restores any clock. */
int vt_preferences_clock_source(void);
int vt_preferences_set_clock_source(int source);

/* Subtitle renderer profile. Existing settings.bin files decode to the
 * historical white/black, medium, 88%-wide style at the new bottom position. */
int vt_preferences_subtitle_outline_thickness(void);
int vt_preferences_set_subtitle_outline_thickness(int thickness);
int vt_preferences_subtitle_border_color(void);
int vt_preferences_set_subtitle_border_color(int color);
int vt_preferences_subtitle_text_color(void);
int vt_preferences_set_subtitle_text_color(int color);
int vt_preferences_subtitle_size(void);
int vt_preferences_set_subtitle_size(int size);
int vt_preferences_subtitle_max_width(void);
int vt_preferences_set_subtitle_max_width(int width);
int vt_preferences_subtitle_position(void);
int vt_preferences_set_subtitle_position(int position);

#endif /* VITAWAVE_SETTINGS_PREFERENCES_H */

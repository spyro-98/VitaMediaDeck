#ifndef VITAMEDIADECK_UI_SETTINGS_SCREEN_H
#define VITAMEDIADECK_UI_SETTINGS_SCREEN_H

/* Returns a UI_SECTION_* destination selected from the L1 sidebar, or
 * UI_SECTION_NONE when leaving with CIRCLE. START is reserved for mini-player. */
int ui_settings_screen(void);

/* Scrollable controls map shared by the Settings tab and the one-time
 * startup onboarding shown after a fresh install or 1.0 -> 1.0.1 upgrade. */
void ui_settings_show_controls_reference(void);

#endif /* VITAMEDIADECK_UI_SETTINGS_SCREEN_H */

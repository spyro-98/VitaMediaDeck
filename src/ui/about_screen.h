#ifndef VITATUBE_UI_ABOUT_SCREEN_H
#define VITATUBE_UI_ABOUT_SCREEN_H

/* VitaTube "About" screen: logo, developer credit, and a context-aware
 * button legend. Stays open until CIRCLE — never times out on its
 * own, unlike the generic ui_message_show() popup this replaces.
 * `player_context` is reserved for future context-specific details. Returns a
 * UI_SECTION_* destination chosen through L1, or UI_SECTION_NONE on back. */
int ui_about_screen(int player_context);

#endif /* VITATUBE_UI_ABOUT_SCREEN_H */

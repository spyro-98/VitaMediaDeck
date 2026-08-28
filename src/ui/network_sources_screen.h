#ifndef VITAMEDIADECK_UI_NETWORK_SOURCES_SCREEN_H
#define VITAMEDIADECK_UI_NETWORK_SOURCES_SCREEN_H

#include "network/network_source.h"

typedef struct {
	VtNetworkSource source;
	VtNetworkCredential credential;
	char path[VT_NETWORK_PATH_MAX];
	char title[256];
} UiNetworkSelection;

#define UI_NETWORK_ACTION_BACK 0
#define UI_NETWORK_ACTION_PLAY 1
#define UI_NETWORK_ACTION_SECTION_BASE 10

int ui_network_sources_screen(UiNetworkSelection *selection);

#endif

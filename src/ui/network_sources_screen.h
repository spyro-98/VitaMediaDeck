#ifndef VITAMEDIADECK_UI_NETWORK_SOURCES_SCREEN_H
#define VITAMEDIADECK_UI_NETWORK_SOURCES_SCREEN_H

#include "network/network_source.h"

typedef struct {
	VtNetworkSource source;
	VtNetworkCredential credential;
	char path[VT_NETWORK_PATH_MAX];
	char title[256];
	VtJellyfinMetadata jellyfin_metadata;
	int has_jellyfin_metadata;
} UiNetworkSelection;

#define UI_NETWORK_ACTION_BACK 0
#define UI_NETWORK_ACTION_PLAY 1
#define UI_NETWORK_ACTION_SECTION_BASE 10

int ui_network_sources_screen(UiNetworkSelection *selection);
int ui_jellyfin_sources_screen(UiNetworkSelection *selection);
int ui_network_resume_jellyfin(const VtNetworkSource *, int count, UiNetworkSelection *);
int ui_network_configure_jellyfin(VtNetworkSource *, VtNetworkCredential *, int fresh);
int ui_network_browse_jellyfin(VtNetworkSource *, VtNetworkCredential *, UiNetworkSelection *);

#endif

#ifndef VMD_CATALOG_H
#define VMD_CATALOG_H
#include "library/library.h"
#include "ui/local_media_screen.h"
#include "network/network_source.h"
#include "network/online_audio.h"
#include "network/iptv_catalog.h"
void vt_library_cache(const VtLibraryItem *);
void vt_library_local(const VtLocalMediaItem *, VtLibraryItem *);
void vt_library_remote(const VtNetworkSource *, const VtNetworkEntry *, const char *parent,
                       VtLibraryItem *);
void vt_library_online(const VtOnlineItem *, const char *feed, VtLibraryItem *);
void vt_library_iptv(const VtIptvChannel *, VtLibraryItem *);
int vt_library_source(const VtLibraryItem *, VtNetworkSource *, VtNetworkCredential *);
#endif

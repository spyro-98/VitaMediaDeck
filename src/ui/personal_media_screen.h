#ifndef VMD_PERSONAL_MEDIA_SCREEN_H
#define VMD_PERSONAL_MEDIA_SCREEN_H
#include "network/network_source.h"
void ui_personal_media_shutdown(void);
int ui_network_personal_resume_credential(const VtNetworkSource *, VtNetworkCredential *);
int ui_personal_media_screen(VtNetworkProtocol);
int ui_personal_media_open(const VtNetworkSource *, const VtNetworkCredential *,
                           const VtNetworkEntry *, const VtNetworkEntry *, int, int);
int ui_network_browse_personal(VtNetworkSource *, VtNetworkCredential *);
#endif

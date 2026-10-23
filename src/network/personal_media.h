#ifndef VMD_PERSONAL_MEDIA_H
#define VMD_PERSONAL_MEDIA_H
#include "network/network_source.h"
int vt_personal_authenticate(const VtNetworkSource *, VtNetworkCredential *, char *, size_t);
int vt_personal_list(const VtNetworkSource *, const VtNetworkCredential *, const char *,
                     VtNetworkEntry *, int, char *, size_t);
int vt_personal_artwork(const VtNetworkSource *, const VtNetworkCredential *, const char *,
                        unsigned char **, size_t *, volatile int *);
int vt_personal_preview(const VtNetworkSource *, const VtNetworkCredential *, const char *,
                        unsigned char **, size_t *, volatile int *);
int vt_personal_stream_url(const VtNetworkSource *, const VtNetworkCredential *, const char *,
                           char *, size_t);
int vt_personal_base_url(const VtNetworkSource *, char *, size_t);
#endif

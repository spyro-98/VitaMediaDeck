#ifndef VITAWAVE_NETWORK_INTERNAL_H
#define VITAWAVE_NETWORK_INTERNAL_H

#include "network/network_source.h"

int vt_webdav_list(const VtNetworkSource *, const VtNetworkCredential *,
	               const char *, VtNetworkEntry *, int, char *, size_t);
int vt_webdav_open_stream(const VtNetworkSource *, const VtNetworkCredential *,
	                      const char *, VtDecoderStreamHandle *);
int vt_webdav_probe_public_key(const VtNetworkSource *,
	                           const VtNetworkCredential *, char *, size_t,
	                           char *, size_t);

int vt_sftp_list(const VtNetworkSource *, const VtNetworkCredential *,
	             const char *, VtNetworkEntry *, int, char *, size_t);
int vt_sftp_open_stream(const VtNetworkSource *, const VtNetworkCredential *,
	                    const char *, VtDecoderStreamHandle *);
int vt_sftp_probe_fingerprint(const VtNetworkSource *, char *, size_t,
	                          char *, size_t);

int vt_smb_list(const VtNetworkSource *, const VtNetworkCredential *,
	            const char *, VtNetworkEntry *, int, char *, size_t);
int vt_smb_open_stream(const VtNetworkSource *, const VtNetworkCredential *,
	                   const char *, VtDecoderStreamHandle *);

#endif

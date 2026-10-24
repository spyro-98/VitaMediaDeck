#ifndef VMD_LIBRARY_SEARCH_H
#define VMD_LIBRARY_SEARCH_H
#include "library/library.h"
enum { VT_SEARCH_IDLE, VT_SEARCH_LOADING, VT_SEARCH_READY, VT_SEARCH_LOGIN, VT_SEARCH_ERROR };
typedef struct {
    int state[VT_LIBRARY_PROVIDER_COUNT], updating[VT_LIBRARY_PROVIDER_COUNT], pending;
    unsigned generation, revision;
} VtLibrarySearchStatus;
#include "network/network_source.h"
int vt_library_search_get_session(const VtNetworkSource *, VtNetworkCredential *);
void vt_library_search_set_session(const VtNetworkSource *, const VtNetworkCredential *);
int vt_library_search_init(void);
/* UI thread snapshots credentials before dispatch. Never prompts on a worker. */
void vt_library_search_begin(const char *query, const VtLibraryItem *folder);
/* Copies a worker-prepared snapshot; no disk access, allocations or sorting. */
int vt_library_search_snapshot(VtLibraryResult *out, int capacity, VtLibrarySearchStatus *status);
unsigned vt_library_search_revision(void);
void vt_library_search_cover(const VtLibraryItem *);
void vt_library_search_local_refresh(void);
void vt_library_search_cancel(void);
void vt_library_search_shutdown(void);
#endif

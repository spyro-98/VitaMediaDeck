#ifndef VMD_STARTUP_H
#define VMD_STARTUP_H

#include <stdint.h>

/* Only the local playback registry is required before entering Home. Network
 * setup and artwork discovery have independent, process-owned workers. */
void vt_startup_background_begin(uint64_t started_us);
int vt_startup_load_local(void *unused);
int vt_startup_network_ready(void);
void vt_startup_background_join(void);

#endif

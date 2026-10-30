#ifndef VMD_IPTV_PROGRAM_ASYNC_H
#define VMD_IPTV_PROGRAM_ASYNC_H
#include "network/iptv_options.h"
typedef int (*VtIptvProgramFetch)(void *,VtIptvProgram *,volatile int *,int);
typedef struct VtIptvProgramJob VtIptvProgramJob;
/* Copies immutable value-only context. Neither the callback nor its context may
 * refer to UI/player stack storage after this call. One running + one latest job. */
VtIptvProgramJob *vt_iptv_program_request(VtIptvProgramFetch fetch,const void *context,size_t size,int refresh);
#define VT_IPTV_GUIDE_BATCH_MAX 6
/* One latest viewport, with each ready row published independently. */
VtIptvProgramJob *vt_iptv_program_request_batch(VtIptvProgramFetch,const void *contexts,
                                              size_t stride,unsigned count,int refresh);
int vt_iptv_program_poll_row(VtIptvProgramJob *,unsigned row,VtIptvProgram *out);
/* 0 pending, 1 successful result copied, negative fetch error. Never waits. */
int vt_iptv_program_poll(VtIptvProgramJob *,VtIptvProgram *out);
/* Cancels and drops UI ownership without waiting for network/parser teardown. */
void vt_iptv_program_release(VtIptvProgramJob *);
void vt_iptv_program_shutdown(void);
#endif

#ifndef VMD_IPTV_TRACE_H
#define VMD_IPTV_TRACE_H
/* Device diagnostics deliberately contain no URLs, query tokens or payloads. */
#ifdef __vita__
#include "common/text_log.h"
#include <psp2/kernel/processmgr.h>
#define IPTV_TRACE(...) log_printf(__VA_ARGS__)
static inline uint64_t iptv_trace_ms(void) {return sceKernelGetProcessTimeWide()/1000;}
#elif defined(VMD_IPTV_DIAGNOSTICS)
#include <stdio.h>
#include <time.h>
#define IPTV_TRACE(...) fprintf(stderr,__VA_ARGS__)
static inline uint64_t iptv_trace_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
#else
#define IPTV_TRACE(...) ((void)0)
static inline uint64_t iptv_trace_ms(void) {return 0;}
#endif
#endif

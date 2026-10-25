#ifndef HOST_SYSMODULE_H
#define HOST_SYSMODULE_H
#include <stdint.h>
#define SCE_SYSMODULE_INTERNAL_PAF 0x80000008u
typedef struct {
    uint32_t flags;
    int *result;
    uint32_t unused[2];
} SceSysmoduleOpt;
int sceSysmoduleIsLoadedInternal(unsigned id);
int sceSysmoduleLoadModuleInternalWithArg(unsigned id, unsigned bytes, void *args,
                                          const SceSysmoduleOpt *opt);
#endif

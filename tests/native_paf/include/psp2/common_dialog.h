#ifndef HOST_COMMON_DIALOG_H
#define HOST_COMMON_DIALOG_H
#include <stdint.h>
typedef struct {
    void *depthSurfaceData, *colorSurfaceData;
    uint32_t surfaceType, colorFormat, width, height, strideInPixels;
    uint8_t reserved[32];
} SceCommonDialogRenderTargetInfo;
typedef struct {
    SceCommonDialogRenderTargetInfo renderTarget;
    void *displaySyncObject;
    uint8_t reserved[32];
} SceCommonDialogUpdateParam;
#endif

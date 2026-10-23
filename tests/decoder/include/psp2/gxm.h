#pragma once
#define SCE_GXM_MEMORY_ATTRIB_READ 1
#define SCE_GXM_MEMORY_ATTRIB_WRITE 2
#define SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1 1
#define SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC1 2
#define SCE_GXM_TEXTURE_FILTER_LINEAR 1
typedef struct {void *pixels;unsigned w,h;} SceGxmTexture;
int sceGxmMapMemory(void*,unsigned,unsigned);
int sceGxmUnmapMemory(void*);
int sceGxmTextureInitLinear(SceGxmTexture*,void*,int,unsigned,unsigned,unsigned);

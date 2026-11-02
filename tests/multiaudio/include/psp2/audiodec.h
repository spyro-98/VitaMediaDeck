#pragma once
#include <stdint.h>
#include "psp2/types.h"
#define SCE_AUDIODEC_TYPE_AAC 0x1005u
#define SCE_AUDIODEC_TYPE_MP3 0x1004u
#define SCE_AUDIODEC_AAC_MAX_ES_SIZE 1536u
#define SCE_AUDIODEC_MP3_MAX_ES_SIZE 1441u
#define SCE_AUDIODEC_AAC_MAX_SAMPLES 2048u
#define SCE_AUDIODEC_MP3_MAX_SAMPLES 1152u
#define SCE_AUDIODEC_ROUND_UP(n) (((n)+255u)&~255u)
#define SCE_AUDIODEC_WORD_LENGTH_16BITS 16
#define SCE_AUDIODEC_MP3_MPEG_VERSION_1 3
#define SCE_AUDIODEC_MP3_MPEG_VERSION_2 2
#define SCE_AUDIODEC_MP3_MPEG_VERSION_2_5 0
typedef uint32_t SceUInt32;
typedef struct {unsigned size,totalStreams;} InitStreams;
typedef union {InitStreams aac,mp3;} SceAudiodecInitParam;
typedef union {
 struct {unsigned size,isAdts,ch,samplingRate,isSbr;} aac;
 struct {unsigned size,ch,version;} mp3;
} SceAudiodecInfo;
typedef struct {
 unsigned size;int handle;uint8_t *pEs;unsigned inputEsSize,maxEsSize;
 void *pPcm;unsigned outputPcmSize,maxPcmSize,wordLength;SceAudiodecInfo *pInfo;
} SceAudiodecCtrl;
int sceAudiodecInitLibrary(unsigned,SceAudiodecInitParam *);
int sceAudiodecTermLibrary(unsigned);
int sceAudiodecCreateDecoder(SceAudiodecCtrl *,unsigned);
int sceAudiodecDeleteDecoder(SceAudiodecCtrl *);
int sceAudiodecDecode(SceAudiodecCtrl *);

#ifndef VMD_TEST_VIDEODEC_H
#define VMD_TEST_VIDEODEC_H
#include <stdint.h>
typedef int SceUID;typedef uint32_t SceUInt32,SceUIntVAddr,SceSize;typedef int32_t SceInt32;
typedef enum{SCE_VIDEODEC_TYPE_HW_AVCDEC=0x1001}SceVideodecType;
typedef enum{SCE_AVCDEC_PIXELFORMAT_RGBA8888=0,SCE_AVCDEC_PIXELFORMAT_RGBA565=1,SCE_AVCDEC_PIXELFORMAT_RGBA5551=2,SCE_AVCDEC_PIXELFORMAT_YUV420_RASTER=16,SCE_AVCDEC_PIXELFORMAT_YUV420_PACKED_RASTER=32}SceAvcdecPixelFormat;
#define SCE_AVCDEC_ERROR_ES_BUFFER_FULL ((int)0x8062000a)
typedef struct{uint32_t size,horizontal,vertical,numOfRefFrames,numOfStreams;}SceVideodecQueryInitInfoHwAvcdec;
typedef union{uint8_t reserved[32];SceVideodecQueryInitInfoHwAvcdec hwAvc;}SceVideodecQueryInitInfo;
typedef struct{uint32_t upper,lower;}SceVideodecTimeStamp;
typedef struct{uint32_t horizontal,vertical,numOfRefFrames;}SceAvcdecQueryDecoderInfo;
typedef struct{uint32_t frameMemSize;}SceAvcdecDecoderInfo;
typedef struct{void*pBuf;uint32_t size;}SceAvcdecBuf;
typedef struct{uint32_t handle;SceAvcdecBuf frameBuf;}SceAvcdecCtrl;
typedef struct{SceVideodecTimeStamp pts,dts;SceAvcdecBuf es;}SceAvcdecAu;
typedef struct{uint32_t numUnitsInTick,timeScale;uint8_t fixedFrameRateFlag,aspectRatioIdc;uint16_t sarWidth,sarHeight;uint8_t colourPrimaries,transferCharacteristics,matrixCoefficients,videoFullRangeFlag,picStruct[2],ctType;SceVideodecTimeStamp pts;}SceAvcdecInfo;
typedef union{uint8_t reserved[16];struct{uint8_t alpha,cscCoefficient,reserved[14];}rgba;}SceAvcdecFrameOption;
typedef struct{uint32_t pixelType,framePitch,frameWidth,frameHeight,horizontalSize,verticalSize,frameCropLeftOffset,frameCropRightOffset,frameCropTopOffset,frameCropBottomOffset;SceAvcdecFrameOption opt;void*pPicture[2];}SceAvcdecFrame;
typedef struct{uint32_t size;SceAvcdecFrame frame;SceAvcdecInfo info;}SceAvcdecPicture;
typedef struct{uint32_t numOfOutput,numOfElm;SceAvcdecPicture**pPicture;}SceAvcdecArrayPicture;
int sceVideodecTermLibrary(SceVideodecType);
int sceAvcdecDeleteDecoder(SceAvcdecCtrl*);
int sceAvcdecDecode(const SceAvcdecCtrl*,const SceAvcdecAu*,SceAvcdecArrayPicture*);
#endif

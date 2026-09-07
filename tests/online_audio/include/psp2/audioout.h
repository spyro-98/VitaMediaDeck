#define SCE_AUDIO_OUT_PORT_TYPE_BGM 1
#define SCE_AUDIO_OUT_MODE_STEREO 1
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
int sceAudioOutOpenPort(int,int,int,int);
int sceAudioOutReleasePort(int);
int sceAudioOutOutput(int,const void *);
int sceAudioOutSetVolume(int,int,int *);

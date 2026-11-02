/* Real app discovery through its seekable source factory, no video player mocks. */
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "../../src/media/media_tracks.c"
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
void log_printf(const char *fmt,...){(void)fmt;}
static int read_file(void *p,void *out,size_t n){return (int)fread(out,1,n,p);}
static int64_t seek_file(void *p,int64_t n,int mode){return fseeko(p,n,mode)?-1:ftello(p);}
static unsigned closes;
static void close_file(void *p){fclose(p);closes++;}
static int open_file(void *path,VtDecoderStreamHandle *out){FILE *f=fopen(path,"rb");if(!f)return -1;fseeko(f,0,SEEK_END);int64_t size=ftello(f);rewind(f);*out=(VtDecoderStreamHandle){.opaque=f,.read=read_file,.seek=seek_file,.close=close_file,.size=size};return 0;}
int main(int argc,char **argv){assert(argc==2);VtDecoderStreamFactory factory={.open=open_file,.opaque=argv[1]};VtDecoderTrackInfo audio[16],subtitles[16];int count=0,sub_count=0;volatile int cancel=0;
 assert(!vt_media_tracks_probe(&factory,audio,&count,subtitles,&sub_count,&cancel));assert(count==5&&!sub_count&&closes==1);
 const char *codecs[]={"aac","ac3","mp3","mp3","mp3"};
 for(int i=0;i<count;i++){assert(audio[i].stream_index==i+1&&!strcmp(audio[i].codec,codecs[i]));}
 assert(audio[1].channels==6&&!strcmp(audio[1].language,"ita"));assert(!strcmp(audio[2].language,"rus"));
 puts("App audio discovery: all five AAC/AC3/MP3 tracks, stream indices, 5.1 metadata and languages match playback ordinals; source cursor closed.");return 0;}

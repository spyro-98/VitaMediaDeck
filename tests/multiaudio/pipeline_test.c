/* Real package audio, demux, PCM queue, clocks, drain and teardown. Only Vita
 * firmware, kernel threads and AudioOut are substituted. Native MP3 API calls
 * are observed; host decoding is not proof of firmware/audio performance. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <psp2/sysmodule.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <psp2/audiodec.h>
#include <psp2/kernel/sysmem.h>
#include "../../src/media/audio_playback.c"
typedef VitaAudioPlayback ElementaryAudioState;
static int start_audio(VitaAudioPlayback *state,AVFormatContext *format,int index,uint64_t position,volatile int *cancel,volatile int *gate){
 memset(state,0,sizeof(*state));return vita_audio_playback_start(state,vt_audio_playback_driver(),format,index,position,cancel,gate);
}
#define join_audio vita_audio_playback_join
#define gain_audio vita_audio_playback_set_volume
static const AVCodecParameters *native_parameters;
static AVCodecContext *native_decoder;static AVFrame *native_frame;static SwrContext *native_resampler;
static int fail_native,library,port,port_frames,port_channels,port_rate,thread_next=1;
static unsigned native_creates[2],native_deletes,system_decodes,ports,block_count;
static uint64_t submitted;static long double energy[2];
static int16_t *recording;static size_t recorded,record_capacity;
static struct {pthread_t thread;int(*entry)(SceSize,void*);void *arg;unsigned bytes;int running;} threads[64];
static struct {void *p;unsigned size;} blocks[32];
static void *thread_entry(void *ptr){int i=(int)(intptr_t)ptr;threads[i].entry(threads[i].bytes,threads[i].arg);free(threads[i].arg);threads[i].arg=NULL;return NULL;}
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelDelayThread(unsigned us){usleep(us>1000?1000:us);return 0;}
int sceKernelGetThreadCurrentPriority(void){return 66;}
int sceKernelCreateThread(const char *name,int(*entry)(SceSize,void*),int priority,unsigned stack,int a,int b,void *c){(void)name;(void)priority;(void)stack;(void)a;(void)b;(void)c;assert(thread_next<64);int i=thread_next++;threads[i].entry=entry;return i;}
int sceKernelStartThread(int i,SceSize bytes,void *arg){assert(i>0&&i<64);threads[i].arg=malloc(bytes);assert(threads[i].arg);memcpy(threads[i].arg,arg,bytes);threads[i].bytes=bytes;threads[i].running=1;return pthread_create(&threads[i].thread,NULL,thread_entry,(void*)(intptr_t)i);}
int sceKernelWaitThreadEnd(int i,void *a,void *b){(void)a;(void)b;assert(threads[i].running);assert(!pthread_join(threads[i].thread,NULL));threads[i].running=0;return 0;}
int sceKernelDeleteThread(int i){assert(!threads[i].running);return 0;}
int sceKernelExitThread(int result){return result;}
void log_printf(const char *format,...){(void)format;}
SceUID sceKernelAllocMemBlock(const char *name,int type,unsigned bytes,void *unused){(void)name;(void)type;(void)unused;assert(!(bytes%4096));if(fail_native==2)return -1;for(int i=1;i<32;i++)if(!blocks[i].p){blocks[i].p=calloc(1,bytes);blocks[i].size=bytes;block_count++;return i;}return -1;}
int sceKernelGetMemBlockBase(int i,void **out){assert(blocks[i].p);*out=blocks[i].p;return 0;}
int sceKernelFreeMemBlock(int i){assert(blocks[i].p);free(blocks[i].p);blocks[i].p=NULL;block_count--;return 0;}
int sceSysmoduleLoadModule(int type){assert(type==SCE_SYSMODULE_AVCDEC);return -1;}
int sceSysmoduleUnloadModule(int type){(void)type;assert(0);return -1;}
int sceAudiodecInitLibrary(unsigned type,SceAudiodecInitParam *p){assert((type==SCE_AUDIODEC_TYPE_AAC||type==SCE_AUDIODEC_TYPE_MP3)&&p->aac.totalStreams==1&&!library);if(fail_native==4||(fail_native==1&&type==SCE_AUDIODEC_TYPE_MP3))return -1;library=(int)type;return 0;}
int sceAudiodecTermLibrary(unsigned type){assert(library==(int)type&&!native_decoder);library=0;return 0;}
int sceAudiodecCreateDecoder(SceAudiodecCtrl *c,unsigned type){
 assert(library==(int)type&&!native_decoder&&c->wordLength==16&&c->maxEsSize&&c->maxPcmSize);
 if(fail_native==3)return -1;
 unsigned channels=type==SCE_AUDIODEC_TYPE_MP3?c->pInfo->mp3.ch:c->pInfo->aac.ch;
 assert(channels==(unsigned)native_parameters->ch_layout.nb_channels);
 if(type==SCE_AUDIODEC_TYPE_MP3)assert(c->pInfo->mp3.version==(unsigned)(native_parameters->sample_rate>=32000?3:native_parameters->sample_rate>=16000?2:0));
 const AVCodec *codec=avcodec_find_decoder(type==SCE_AUDIODEC_TYPE_MP3?AV_CODEC_ID_MP3:AV_CODEC_ID_AAC);assert(codec);
 native_decoder=avcodec_alloc_context3(codec);native_frame=av_frame_alloc();assert(native_decoder&&native_frame);
 assert(!avcodec_parameters_to_context(native_decoder,native_parameters));native_decoder->flags2|=AV_CODEC_FLAG2_SKIP_MANUAL;
 assert(!avcodec_open2(native_decoder,codec,NULL));c->handle=1;native_creates[type==SCE_AUDIODEC_TYPE_MP3]++;return 0;
}
int sceAudiodecDeleteDecoder(SceAudiodecCtrl *c){assert(c->handle==1&&native_decoder);swr_free(&native_resampler);av_frame_free(&native_frame);avcodec_free_context(&native_decoder);native_deletes++;return 0;}
int sceAudiodecDecode(SceAudiodecCtrl *c){
 system_decodes++;AVPacket *p=av_packet_alloc();assert(p&&!av_new_packet(p,(int)c->inputEsSize));memcpy(p->data,c->pEs,c->inputEsSize);
 int r=avcodec_send_packet(native_decoder,p);av_packet_free(&p);if(r<0)return r;
 av_frame_unref(native_frame);r=avcodec_receive_frame(native_decoder,native_frame);if(r==AVERROR(EAGAIN)){c->outputPcmSize=0;return 0;}if(r<0)return r;
 if(!native_resampler){assert(!swr_alloc_set_opts2(&native_resampler,&native_frame->ch_layout,AV_SAMPLE_FMT_S16,native_frame->sample_rate,&native_frame->ch_layout,(enum AVSampleFormat)native_frame->format,native_frame->sample_rate,0,NULL));assert(!swr_init(native_resampler));}
 uint8_t *out=c->pPcm;int frames=swr_convert(native_resampler,&out,(int)(c->maxPcmSize/(2*native_frame->ch_layout.nb_channels)),(const uint8_t**)native_frame->extended_data,native_frame->nb_samples);
 assert(frames>=0);c->outputPcmSize=(unsigned)frames*2*native_frame->ch_layout.nb_channels;assert(c->outputPcmSize<=c->maxPcmSize);return 0;
}
int sceAudioOutOpenPort(int kind,int frames,int rate,int mode){assert(!port&&kind==1&&(frames==576||frames==1024||frames==1152||frames==1536));port=1;port_frames=frames;port_channels=mode==0?1:2;port_rate=rate;ports++;return 1;}
int sceAudioOutReleasePort(int p){assert(p==1&&port);port=0;return 0;}
int sceAudioOutSetVolume(int p,int flags,int *values){assert(p==1&&flags==3&&values[0]==values[1]);return 0;}
int sceAudioOutOutput(int p,const void *data){
 assert(p==1&&port);if(!data)return 0;const int16_t *pcm=data;
 for(int i=0;i<port_frames;i++)for(int ch=0;ch<port_channels;ch++){int sample=pcm[i*port_channels+ch];energy[ch]+=(long double)sample*sample;}
 size_t n=(size_t)port_frames*port_channels;assert(recorded+n<=record_capacity);memcpy(recording+recorded,pcm,n*sizeof(int16_t));recorded+=n;__atomic_add_fetch(&submitted,(unsigned)port_frames,__ATOMIC_RELEASE);usleep(50);return 0;
}
static void test_track(const char *path,int ordinal,int fallback,uint64_t seek_ms){
 AVFormatContext *f=NULL;assert(!avformat_open_input(&f,path,NULL,NULL));assert(avformat_find_stream_info(f,NULL)>=0);
 int index=-1;for(unsigned i=0;i<f->nb_streams;i++)if(f->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO&&ordinal--==0){index=(int)i;break;}assert(index>=0);
 native_parameters=f->streams[index]->codecpar;enum AVCodecID codec=native_parameters->codec_id;
 if(seek_ms)assert(av_seek_frame(f,-1,(int64_t)seek_ms*1000,AVSEEK_FLAG_BACKWARD)>=0);
 unsigned before_native=native_creates[1];fail_native=fallback;submitted=recorded=0;energy[0]=energy[1]=0;thread_next=1;
 ElementaryAudioState st;volatile int cancel=0,gate=0;assert(!start_audio(&st,f,index,seek_ms,&cancel,&gate));
 assert(codec!=AV_CODEC_ID_AC3||((AudioPlaybackEngine*)st.implementation)->software);assert(codec!=AV_CODEC_ID_MP3||fallback||(!((AudioPlaybackEngine*)st.implementation)->software&&native_creates[1]==before_native+1));
 for(int tick=0;tick<10000&&!st.first_packet_ready&&!st.eof&&!st.had_error;tick++)usleep(100);
 assert(st.first_packet_ready&&!st.had_error);assert(!st.first_packet_pts_valid||st.first_packet_pts_ms+50>=seek_ms);
 gate=1;join_audio(&st);assert(!st.implementation);join_audio(&st);assert(st.eof&&!st.had_error&&!port&&!library&&!block_count);
 assert(submitted>(seek_ms?2u:5u)*(unsigned)port_rate&&submitted<7u*(unsigned)port_rate);
 assert(sqrt((double)(energy[0]/submitted))>300);if(port_channels==2)assert(sqrt((double)(energy[1]/submitted))>300);
 assert(st.played_until_ms>5900&&st.played_until_ms<6250);
 /* Continuous tone must not acquire a padded silence gap at every MP3/AC3
  * packet boundary. Only the single final partial output block may be padded. */
 unsigned run=0,max_run=0;for(size_t i=(size_t)(port_rate/4)*port_channels;i<recorded-(size_t)port_frames*port_channels;i+=port_channels){int silent=1;for(int ch=0;ch<port_channels;ch++)if(recording[i+ch]>2||recording[i+ch]<-2)silent=0;if(silent){if(++run>max_run)max_run=run;}else run=0;}
 assert(max_run<100);
 printf("Package pipeline %s %s seek=%llu: %llu frames, grain=%d, %d Hz/%d ch, final=%u ms; continuous PCM and balanced teardown.\n",avcodec_get_name(codec),codec==AV_CODEC_ID_MP3&&!fallback?"system":"compatibility",(unsigned long long)seek_ms,(unsigned long long)submitted,port_frames,port_rate,port_channels,st.played_until_ms);
 avformat_close_input(&f);
}
static void test_cancel(const char *path,int ordinal){
 AVFormatContext *f=NULL;assert(!avformat_open_input(&f,path,NULL,NULL)&&avformat_find_stream_info(f,NULL)>=0);
 int index=-1;for(unsigned i=0;i<f->nb_streams;i++)if(f->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO&&ordinal--==0){index=(int)i;break;}assert(index>=0);
 native_parameters=f->streams[index]->codecpar;fail_native=0;submitted=recorded=0;thread_next=1;ElementaryAudioState st;volatile int cancel=0,gate=0;
 assert(!start_audio(&st,f,index,0,&cancel,&gate));
 for(int i=0;i<10000&&!st.first_packet_ready&&!st.had_error;i++)usleep(100);assert(st.first_packet_ready&&!st.had_error);
 st.paused=1;gate=1;usleep(10000);assert(!__atomic_load_n(&submitted,__ATOMIC_ACQUIRE));st.paused=0;
 for(int i=0;i<10000&&__atomic_load_n(&submitted,__ATOMIC_ACQUIRE)<((AudioPlaybackEngine*)st.implementation)->output_grain*2;i++)usleep(100);
 __atomic_store_n(&cancel,1,__ATOMIC_RELEASE);join_audio(&st);assert(!st.had_error&&!port&&!library&&!block_count);avformat_close_input(&f);
 puts("Actual audio producer/output: gated start, pause without consumption, resume, bounded cancellation and reopen-safe cleanup passed.");
}
static void test_mode(const char *path,int mode,int ordinal,int failure){
 AVFormatContext *format=NULL;assert(!avformat_open_input(&format,path,NULL,NULL));assert(avformat_find_stream_info(format,NULL)>=0);
 int index=-1;for(unsigned i=0;i<format->nb_streams;i++)if(format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO&&ordinal--==0){index=(int)i;break;}assert(index>=0);
 native_parameters=format->streams[index]->codecpar;enum AVCodecID codec=native_parameters->codec_id;
 unsigned before=native_creates[0]+native_creates[1];fail_native=failure;thread_next=1;submitted=recorded=0;
 volatile int cancel=0,gate=0;VitaAudioPlayback state={0};
 int result=vita_audio_playback_start(&state,vt_audio_playback_driver_for_mode(mode),format,index,0,&cancel,&gate);
 if(mode==VT_AUDIO_DECODE_HW&&(codec==AV_CODEC_ID_AC3||failure)){
  assert(result<0&&!state.implementation&&!port&&!library&&!block_count);
 }else{
  assert(!result&&state.implementation);AudioPlaybackEngine *engine=state.implementation;
  int software=mode==VT_AUDIO_DECODE_SW||codec==AV_CODEC_ID_AC3||failure;
  assert(!!engine->software==software&&!!engine->decoder==!software);
  if(software)assert(native_creates[0]+native_creates[1]==before);
  else assert(native_creates[0]+native_creates[1]==before+1);
  gate=1;vita_audio_playback_join(&state);assert(state.eof&&!state.had_error&&!state.implementation&&!port&&!library&&!block_count);
 }
 avformat_close_input(&format);fail_native=0;
}
static void test_modes(const char *path){
 for(int mode=VT_AUDIO_DECODE_AUTO;mode<=VT_AUDIO_DECODE_SW;mode++)for(int track=0;track<5;track++)test_mode(path,mode,track,0);
 test_mode(path,VT_AUDIO_DECODE_AUTO,0,4);test_mode(path,VT_AUDIO_DECODE_HW,0,4);
 test_mode(path,VT_AUDIO_DECODE_HW,2,1);
 assert(vt_audio_playback_driver_for_mode(-1)==vt_audio_playback_driver());
 puts("Audio mode matrix: AUTO/HW/SW across AAC, AC3 and MPEG-1/2/2.5 MP3; HW strict errors, SW avoids native decoding, AUTO AAC init-error fallback and balanced cleanup passed.");
}
static void test_public_native_lifecycle(const char *path){
 AVFormatContext *format=NULL;assert(!avformat_open_input(&format,path,NULL,NULL));assert(avformat_find_stream_info(format,NULL)>=0);
 VitaHwAudioDecoder *decoder=vita_hw_audio_decoder_create();assert(decoder);fail_native=0;
 for(unsigned i=0;i<format->nb_streams;i++){
  const AVCodecParameters *params=format->streams[i]->codecpar;
  if(params->codec_id!=AV_CODEC_ID_AAC&&params->codec_id!=AV_CODEC_ID_MP3)continue;
  native_parameters=params;VitaHwAudioDecoderConfig config={.codec=params->codec_id==AV_CODEC_ID_AAC?VITA_AUDIO_CODEC_AAC:VITA_AUDIO_CODEC_MP3,.channels=(unsigned)params->ch_layout.nb_channels,.sample_rate=(unsigned)params->sample_rate};
  assert(!vita_hw_audio_decoder_open(decoder,&config));assert(library&&block_count);
  assert(vita_hw_audio_decoder_open(decoder,NULL)<0);assert(!library&&!block_count);
  assert(!vita_hw_audio_decoder_open(decoder,&config));vita_hw_audio_decoder_close(decoder);vita_hw_audio_decoder_close(decoder);assert(!library&&!block_count);
 }
 vita_hw_audio_decoder_destroy(decoder);avformat_close_input(&format);
 puts("Opaque native codec API: same-object AAC/MP3 reopen, invalid-open cleanup and repeated close passed.");
}
int main(int argc,char **argv){assert(argc==2);alarm(30);av_log_set_level(AV_LOG_ERROR);record_capacity=8*48000*2;recording=malloc(record_capacity*sizeof(int16_t));assert(recording);
 test_track(argv[1],0,0,0);test_track(argv[1],1,0,0);test_track(argv[1],2,0,0);test_track(argv[1],2,1,0);test_track(argv[1],2,2,0);test_track(argv[1],2,3,0);
 test_track(argv[1],3,0,0);test_track(argv[1],3,1,0);test_track(argv[1],4,0,0);test_track(argv[1],4,1,0);
 test_cancel(argv[1],1);test_cancel(argv[1],2);
 test_track(argv[1],1,0,3000);test_track(argv[1],2,0,3000);
 test_modes(argv[1]);
 test_public_native_lifecycle(argv[1]);
 assert(native_creates[0]&&native_creates[1]>=2&&native_deletes==native_creates[0]+native_creates[1]);free(recording);alarm(0);return 0;}

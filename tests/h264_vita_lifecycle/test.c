#include <stdio.h>
#include <stdarg.h>
#include VMD_PINNED_VITA_DECODER_SOURCE
static struct{void*p;int live;} blocks[256];
static int next_id=1,init_calls,term_calls,create_calls,delete_calls,submit_calls,duplicates;
static int allocation_calls,fail_allocation,fail_phase,engine_live,decoder_live;
static int live_unmap,live_va;
SceUID sceKernelAllocMemBlock(const char*n,int type,SceSize size,const SceKernelAllocMemBlockOpt*o){(void)n;(void)type;(void)o;allocation_calls++;if(allocation_calls==fail_allocation)return -1;int id=next_id++;if(id>=256)return -1;blocks[id].p=calloc(1,size);blocks[id].live=1;return id;}
int sceKernelGetMemBlockBase(SceUID id,void**p){*p=blocks[id].p;return 0;}
int sceKernelFreeMemBlock(SceUID id){if(id<=0||id>=next_id||!blocks[id].live){duplicates++;return -1;}free(blocks[id].p);blocks[id].live=0;return 0;}
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
SceUID sceCodecEngineOpenUnmapMemBlock(void*p,SceSize n){(void)p;(void)n;if(fail_phase==1)return -1;live_unmap++;return 42;}
SceInt32 sceCodecEngineCloseUnmapMemBlock(SceUID id){(void)id;if(!live_unmap)duplicates++;else live_unmap--;return 0;}
SceUIntVAddr sceCodecEngineAllocMemoryFromUnmapMemBlock(SceUID id,SceUInt32 s,SceUInt32 a){(void)id;(void)s;(void)a;if(fail_phase==2)return 0;live_va++;return 43;}
SceInt32 sceCodecEngineFreeMemoryFromUnmapMemBlock(SceUID id,SceUIntVAddr a){(void)id;(void)a;if(!live_va)duplicates++;else live_va--;return 0;}
void sceAvcdecSetDecodeMode(SceVideodecType t,SceInt32 m){(void)t;(void)m;}
void sceVideodecSetConfigInternal(SceVideodecType t,SceInt32 c){(void)t;(void)c;}
void sceVideodecQueryMemSizeInternal(SceVideodecType t,SceVideodecQueryInitInfo*q,SceUInt32*s){(void)t;(void)q;*s=1048576;}
SceInt32 sceAvcdecQueryDecoderMemSizeInternal(SceVideodecType t,SceAvcdecQueryDecoderInfo*q,SceAvcdecDecoderInfo*d){(void)t;(void)q;d->frameMemSize=262144;return 0;}
SceInt32 sceVideodecInitLibraryWithUnmapMemInternal(SceVideodecType t,SceVideodecCtrl*c,SceVideodecQueryInitInfo*q){(void)t;(void)c;(void)q;if(fail_phase==3)return -1;init_calls++;engine_live++;return 0;}
SceInt32 sceAvcdecCreateDecoderInternal(SceVideodecType t,SceAvcdecCtrl*c,SceAvcdecQueryDecoderInfo*q){(void)t;(void)c;(void)q;if(fail_phase==4)return -1;create_calls++;decoder_live++;return 0;}
int sceAvcdecDeleteDecoder(SceAvcdecCtrl*c){(void)c;delete_calls++;if(!decoder_live)duplicates++;else decoder_live--;return 0;}
int sceVideodecTermLibrary(SceVideodecType t){(void)t;term_calls++;if(!engine_live)duplicates++;else engine_live--;return 0;}
SceInt32 sceAvcdecDecodeAvailableSize(SceAvcdecCtrl*c){(void)c;return 1048576;}
int sceAvcdecDecode(const SceAvcdecCtrl*c,const SceAvcdecAu*a,SceAvcdecArrayPicture*p){(void)c;(void)a;submit_calls++;p->numOfOutput=0;return 0;}
SceInt32 sceAvcdecDecodeFlush(SceAvcdecCtrl*c){(void)c;return 0;}
SceInt32 sceAvcdecDecodeStop(SceAvcdecCtrl*c,SceAvcdecArrayPicture*p){(void)c;p->numOfOutput=0;return 0;}
static int cleanup_ok(void){int live=0;for(int i=1;i<next_id;i++)live+=blocks[i].live;return !live&&!engine_live&&!decoder_live&&!live_unmap&&!live_va&&!duplicates;}
static AVCodecContext *context(void){AVCodecContext*c=avcodec_alloc_context3(NULL);c->pix_fmt=AV_PIX_FMT_YUV420P;c->pkt_timebase=(AVRational){1,90000};c->priv_data=av_mallocz(sizeof(VitaDecodeContext));((VitaDecodeContext*)c->priv_data)->clz=(AVClass *)&vita_h264_dec_class;return c;}
static void close_context(AVCodecContext**c){vita_close(*c);av_freep(&(*c)->priv_data);avcodec_free_context(c);}
static AVPacket *read_packet(const char*name){FILE*f=fopen(name,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);AVPacket*p=av_packet_alloc();av_new_packet(p,(int)n);if(fread(p->data,1,n,f)!=(size_t)n)return NULL;fclose(f);p->pts=p->dts=90000;return p;}
static void reset_counts(void) {
 init_calls=term_calls=create_calls=delete_calls=submit_calls=duplicates=0;
 allocation_calls=fail_allocation=fail_phase=engine_live=decoder_live=live_unmap=live_va=0;
 next_id=1;memset(blocks,0,sizeof(blocks));
}
static int meta_size(const AVPacket*p) {
 for(int i=2;i+1<p->size;i++)if(p->data[i]==1&&!p->data[i-1]&&!p->data[i-2]&&((p->data[i+1]&31)==5 ||(p->data[i+1]&31)==1))return i-2;
 return 0;
}
int main(int argc,char**argv){
 if(argc!=3)return 2;av_log_set_level(AV_LOG_QUIET);
 AVPacket *a=read_packet(argv[1]),*b=read_packet(argv[2]);if(!a||!b)return 2;
 AVFrame*f=av_frame_alloc();int got=0,ok=1,cases=0;
 AVCodecContext*c=context();vita_init(c);int ret=vita_decode(c,f,&got,a);
 printf("No extradata/in-band SPS: init=%d create=%d delete-before-first-AU=%d submit=%d ret=%d\n",init_calls,create_calls,delete_calls,submit_calls,ret);
 if(init_calls!=1||create_calls!=1||delete_calls||submit_calls!=1||ret<0)ok=0;
 for(int i=0;i<3;i++)vita_decode(c,f,&got,a);
 if(init_calls!=1)ok=0;
 close_context(&c);if(!cleanup_ok())ok=0;cases++;
 /* Existing MP4/global metadata must retain its single initialization. */
 reset_counts();c=context();int extra=meta_size(a);if(extra<=0)return 2;
 c->extradata=av_mallocz(extra+AV_INPUT_BUFFER_PADDING_SIZE);memcpy(c->extradata,a->data,extra);c->extradata_size=extra;
 vita_init(c);vita_decode(c,f,&got,a);
 if(init_calls!=1||delete_calls||submit_calls!=1)ok=0;
 close_context(&c);if(!cleanup_ok())ok=0;cases++;
 /* NEW_EXTRADATA can also trigger initialization before the first-AU branch. */
 reset_counts();c=context();vita_init(c);AVPacket*side=av_packet_clone(a);
 uint8_t*metadata=av_packet_new_side_data(side,AV_PKT_DATA_NEW_EXTRADATA,extra);memcpy(metadata,a->data,extra);
 vita_decode(c,f,&got,side);if(init_calls!=1||delete_calls||submit_calls!=1)ok=0;
 close_context(&c);av_packet_free(&side);if(!cleanup_ok())ok=0;cases++;
 /* A real dimension change consumes one teardown/recreate; repeated identical
  * SPS/PPS must not initialize again. */
 reset_counts();c=context();vita_init(c);vita_decode(c,f,&got,a);vita_decode(c,f,&got,b);vita_decode(c,f,&got,b);
 if(init_calls!=2||create_calls!=2||delete_calls!=1||submit_calls!=3)ok=0;
 close_context(&c);if(!cleanup_ok())ok=0;cases++;
 /* Partial allocations and API failures, both at startup and during resize.
  * Explicit close plus normal FFmpeg cleanup must be idempotent. */
 for(int resize=0;resize<2;resize++)for(int failure=1;failure<=7;failure++) {
  reset_counts();c=context();vita_init(c);if(resize)vita_decode(c,f,&got,a);
  if(failure<=3)fail_allocation=allocation_calls+failure;else fail_phase=failure-3;
  ret=vita_decode(c,f,&got,resize?b:a);if(ret>=0)ok=0;
  fail_allocation=fail_phase=0;vita_close(c);close_context(&c);
  if(!cleanup_ok())ok=0;
  printf("Failure cleanup: resize=%d point=%d duplicate-releases=%d balanced=%d\n",resize,failure,duplicates,cleanup_ok());cases++;
 }
 av_frame_free(&f);av_packet_free(&a);av_packet_free(&b);
 printf("Lifecycle invariants %s: %d cases, actual pinned source with mocked hardware boundaries; no Vita execution.\n",ok?"PASS":"FAIL",cases);return ok?0:1;
}

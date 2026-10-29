#include <assert.h>
#include <webp/encode.h>
#include "../../../src/media/video_thumbnail.c"
static uint64_t now=1000000;
static unsigned checks,priority_calls,frees,fences,aborts;
static int allocation_fails;
#define CHECK(x) do{checks++;assert(x);}while(0)
uint64_t sceKernelGetProcessTimeWide(void){return now;}
int sceKernelDelayThread(unsigned us){now+=us;return 0;}
int sceKernelChangeThreadPriority(SceUID id,int p){(void)id;(void)p;priority_calls++;return 0;}
void log_printf(const char *s,...){(void)s;}
void vita2d_wait_rendering_done(void){fences++;}
void vita2d_free_texture(vita2d_texture *p){frees++;free(p);}
vita2d_texture *vita2d_create_empty_texture_format(unsigned w,unsigned h,int fmt){CHECK(((w==264&&h==148)||(w==64&&h==64)||(w==192&&h==192))&&fmt==SCE_GXM_TEXTURE_FORMAT_R5G6B5);if(allocation_fails)return NULL;vita2d_texture *p=calloc(1,sizeof(*p));p->stride=w*2;return p;}
unsigned vita2d_texture_get_stride(const vita2d_texture *p){return p->stride;}
void *vita2d_texture_get_datap(const vita2d_texture *p){return (void *)p->pixels;}
void vita2d_texture_set_filters(vita2d_texture *p,int a,int b){(void)p;(void)a;(void)b;}
int vt_artwork_cache_current(const char *p,char *o,size_t n){(void)p;(void)o;(void)n;return -1;}
static void abort_cursor(void *ctx){(void)ctx;aborts++;}
int main(void) {
 g_thumbnail.initialized=1;g_thumbnail.enabled=1;g_thumbnail.generation=5;
 unsigned char rgb[16*16*3];for(unsigned i=0;i<sizeof(rgb);i++)rgb[i]=(unsigned char)(i*13);
 unsigned char *webp=NULL;size_t webp_size=WebPEncodeLosslessRGB(rgb,16,16,16*3,&webp);
 CHECK(webp_size>0);uint16_t output[THUMB_PIXELS];ThumbnailInterrupt live={.generation=5};
 CHECK(decode_webp_cover(webp,webp_size,output,&live)==0);
 CHECK(thumbnail_information_score(output)>0);CHECK(decode_webp_cover(webp,10,output,&live)<0);WebPFree(webp);
 g_thumbnail.initialized=1;g_thumbnail.enabled=0;g_thumbnail.generation=9;
 g_thumbnail.active_cancel=1;g_thumbnail.worker_busy=1;
 g_thumbnail.failures[0].retry_after_us=now+30000000;
 vt_video_thumbnail_resume();CHECK(g_thumbnail.enabled && !g_thumbnail.active_cancel);
 CHECK(!g_thumbnail.failures[0].retry_after_us);CHECK(priority_calls==1);
 /* Old decoder work remains cancelled across an immediate scene return. */
 ThumbnailInterrupt old={.generation=8};CHECK(thumbnail_interrupted(&old));
 /* Repeated visible-frame resumes must not erase a live preemption. */
 g_thumbnail.active_cancel=1;vt_video_thumbnail_resume();
 CHECK(g_thumbnail.active_cancel && priority_calls==1);
 g_thumbnail.active_cancel=0;
 vt_video_thumbnail_get_priority("ux0:movies/test.mkv",100,100);
 CHECK(g_thumbnail.request_count==1);
 g_thumbnail.active_abort=abort_cursor;g_thumbnail.active_abort_opaque=&now;
 vt_video_thumbnail_suspend();CHECK(!g_thumbnail.enabled && g_thumbnail.active_cancel);
 CHECK(aborts==1 && !g_thumbnail.request_count && g_thumbnail.generation==10);
 g_thumbnail.worker_busy=0;vt_video_thumbnail_resume();
 CHECK(g_thumbnail.enabled && !g_thumbnail.active_cancel);
 vt_video_thumbnail_get_priority("ux0:movies/test.mkv",100,100);CHECK(g_thumbnail.request_count==1);
 /* A temporarily full GPU heap retains the decoded result and retries. */
 ThumbnailRequest request=g_thumbnail.requests[0];
 g_thumbnail.result=(ThumbnailResult){.key=request.key,.source_size=100,.generation=10,.width=264,.height=148};
 strcpy(g_thumbnail.result.path,request.path);
 g_thumbnail.result.pixels=calloc(THUMB_PIXELS,sizeof(uint16_t));g_thumbnail.result.pixels[0]=0xABCD;
 allocation_fails=1;vt_video_thumbnail_pump();CHECK(g_thumbnail.result.pixels);
 CHECK(g_thumbnail.result_retry_after_us==now+THUMB_UPLOAD_RETRY_US);
 now+=THUMB_UPLOAD_RETRY_US;allocation_fails=0;vt_video_thumbnail_pump();
 CHECK(!g_thumbnail.result.pixels && g_thumbnail.textures[0].texture);
 CHECK(g_thumbnail.textures[0].texture->pixels[0]==0xABCD);
 CHECK(vt_video_thumbnail_get_priority(request.path,100,100)==g_thumbnail.textures[0].texture);
 vt_video_thumbnail_prepare_playback();CHECK(!g_thumbnail.enabled && frees==1 && fences>0);
 vt_video_thumbnail_resume();CHECK(g_thumbnail.enabled);
 /* Eighteen visible/prefetched country flags survive unrelated cover churn. */
 for(int n=0;n<18;n++) {
  char path[8];snprintf(path,sizeof(path),"flag:A%c",'A'+n);
  g_thumbnail.result=(ThumbnailResult){.key=thumbnail_key(path,0,3),.generation=g_thumbnail.generation,.width=64,.height=64};
  strcpy(g_thumbnail.result.path,path);g_thumbnail.result.pixels=calloc(64*64,2);vt_video_thumbnail_pump();
 }
 for(int n=0;n<16;n++) {
  char path[64];snprintf(path,sizeof(path),"ux0:movies/churn-%d.mp4",n);
  g_thumbnail.result=(ThumbnailResult){.key=thumbnail_key(path,0,0),.generation=g_thumbnail.generation,.width=264,.height=148};
  strcpy(g_thumbnail.result.path,path);g_thumbnail.result.pixels=calloc(THUMB_PIXELS,2);now++;vt_video_thumbnail_pump();
  for(int k=0;k<18;k++){char code[3]={'A','A'+k,0};CHECK(vt_country_flag_get(code)!=NULL);}
 }
 /* Artwork has independent RAM slots and survives video churn/playback. */
 char first[192]="";
 for(int n=0;n<80;n++){
  char path[192];snprintf(path,sizeof(path),"ux0:data/VitaMediaDeck/artwork/%064x-0000000000000001.png",n+1);
  if(!n)strcpy(first,path);
  g_thumbnail.result=(ThumbnailResult){.key=thumbnail_key(path,0,2),.generation=g_thumbnail.generation,.width=192,.height=192};
  strcpy(g_thumbnail.result.path,path);g_thumbnail.result.pixels=calloc(THUMB_PIXELS,2);g_thumbnail.result.pixels[0]=n+1;now++;vt_video_thumbnail_pump();
 }
 vita2d_texture *retained=vt_artwork_thumbnail_get(first);CHECK(retained&&retained->pixels[0]==1);
 for(int n=0;n<40;n++){
  char path[64];snprintf(path,sizeof(path),"ux0:movies/churn-again-%d.mp4",n);
  g_thumbnail.result=(ThumbnailResult){.key=thumbnail_key(path,0,0),.generation=g_thumbnail.generation,.width=264,.height=148};strcpy(g_thumbnail.result.path,path);g_thumbnail.result.pixels=calloc(THUMB_PIXELS,2);now++;vt_video_thumbnail_pump();
 }
 CHECK(vt_artwork_thumbnail_get(first)==retained);
 char changed[192];strcpy(changed,first);strcpy(strstr(changed,"-0000000000000001"),"-0000000000000002.png");
 CHECK(vt_artwork_thumbnail_get(changed)==retained); /* Old image while new revision loads. */
 vt_video_thumbnail_prepare_playback();vt_video_thumbnail_resume();CHECK(vt_artwork_thumbnail_get(first)==retained);
 char a[96],b[96],temp[96];cache_paths(1,1,a,temp);cache_paths(129,1,b,temp);CHECK(strcmp(a,b));
 /* Close retained graphics resources in the host process. */
 g_thumbnail.active_valid=1;g_thumbnail.active=(ThumbnailRequest){.priority=100};g_thumbnail.active_cancel=0;
 vt_artwork_thumbnail_get("ux0:data/VitaMediaDeck/artwork/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-1.png");CHECK(g_thumbnail.active_cancel);
 g_thumbnail.active=(ThumbnailRequest){.priority=200,.still_image=2};g_thumbnail.active_cancel=0;
 vt_artwork_thumbnail_get("ux0:data/VitaMediaDeck/artwork/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-1.png");CHECK(!g_thumbnail.active_cancel);
 for(int i=0;i<THUMB_TEXTURE_CAPACITY;i++)if(g_thumbnail.textures[i].texture){vita2d_free_texture(g_thumbnail.textures[i].texture);g_thumbnail.textures[i].texture=NULL;}
 printf("PASS: %u thumbnail lifecycle checks\n",checks);
}

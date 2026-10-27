#include <assert.h>
#include <stdlib.h>
#include "../../../src/media/hw_player_screen.c"
static unsigned checks, shown=1, frees, captures, draws, fences,prepared;
static int no_memory,no_frame,seek_result;
static uint32_t framebuffer[544][1024];
static uint16_t texture[272][512];
#define CHECK(x) do{checks++;assert(x);}while(0)
void vt_decoder_get_status(VtDecoderPlayer *p,VtDecoderPlayerStatus *s){(void)p;memset(s,0,sizeof(*s));s->frames_shown=shown;}
int sceDisplayGetFrameBuf(SceDisplayFrameBuf *f,int sync){(void)sync;captures++;*f=(SceDisplayFrameBuf){.size=sizeof(*f),.base=framebuffer,.pitch=1024,.width=960,.height=544};return no_frame?-1:0;}
void vita2d_wait_rendering_done(void){fences++;}
vita2d_texture *vita2d_create_empty_texture_format(unsigned w,unsigned h,int fmt){(void)fmt;CHECK(w==480&&h==272);return no_memory?NULL:(void*)texture;}
unsigned vita2d_texture_get_stride(const vita2d_texture *t){(void)t;return 1024;}
void *vita2d_texture_get_datap(const vita2d_texture *t){return (void*)t;}
void vita2d_texture_set_filters(vita2d_texture *t,int a,int b){(void)t;(void)a;(void)b;}
void vita2d_free_texture(vita2d_texture *t){CHECK(t==(void*)texture);frees++;}
void vita2d_draw_texture_scale(const vita2d_texture *t,float x,float y,float sx,float sy){CHECK(t==(void*)texture&&x==0&&y==0&&sx==2&&sy==2);draws++;}
void vt_decoder_render_complete(VtDecoderPlayer *p){(void)p;CHECK(fences);}
void vt_decoder_prepare_background_restart(VtDecoderPlayer *p){(void)p;prepared++;}
int vt_decoder_seek(VtDecoderPlayer *p,uint64_t pos){(void)p;CHECK(pos==10000);return seek_result;}
int ui_player_loading_run(const UiPlayerLoadingInfo *info,UiLoadingTaskFn fn,void *ctx,volatile int *cancel,const volatile long *cur,const volatile long *tot){(void)cancel;(void)cur;(void)tot;CHECK(prepared);if(info->draw_backdrop)info->draw_backdrop(info->backdrop_ctx);return fn(ctx);}
static int opens, fail_first, backend_calls, backend_hardware, source_stops;
static uint64_t expected_position;
static VtDecoderBackend opened_backend[3];
uint64_t sceKernelGetProcessTimeWide(void){return 1000000;}
int sceKernelDelayThread(unsigned us){(void)us;return 0;}
void log_printf(const char *f,...){(void)f;}
void vt_iptv_session_cancel(VtIptvSession *s){(void)s;}
void vt_iptv_session_stop_and_wait(VtIptvSession *s){(void)s;source_stops++;}
void vt_iptv_session_status(VtIptvSession *session,VtIptvSourceStatus *status){(void)session;memset(status,0,sizeof(*status));status->startup_ready=1;}
void vt_decoder_request_stop(VtDecoderPlayer *p){(void)p;}
VtDecoderPlayer *vt_decoder_create(void){return (VtDecoderPlayer *)&texture;}
void vt_decoder_destroy(VtDecoderPlayer *p){CHECK(p==(void*)&texture);}
void vt_performance_select_backend(VtPerformanceClockGuard *g,int hardware){CHECK(g!=(void*)0);backend_calls++;backend_hardware=hardware;}
static int mock_open(const VtDecoderPlayerConfig *cfg) {
 CHECK(cfg->start_position_ms==expected_position&&cfg->audio_track==0&&cfg->subtitle_track==0);
 CHECK(cfg->backend_start&&cfg->backend_start_ctx);
 cfg->backend_start(cfg->backend_start_ctx,cfg->preferred_backend);
 opened_backend[opens++]=cfg->preferred_backend;
 return fail_first&&opens==1?-7:0;
}
int vt_decoder_open(VtDecoderPlayer *p,const VtDecoderPlayerConfig *cfg){(void)p;return mock_open(cfg);}
int vt_decoder_open_live(VtDecoderPlayer *p,const VtDecoderPlayerConfig *cfg){(void)p;CHECK(!cfg->start_position_ms);return mock_open(cfg);}
int main(void) {
 /* Source and texture have padding: check sampling, channel order and bounds. */
 framebuffer[0][0]=0xff0000ff;framebuffer[0][2]=0xff00ff00;framebuffer[0][4]=0xffff0000;
 framebuffer[542][958]=0xffffffff;
 UiPlayerLoadingInfo info={.title="test"};volatile int cancel=1;
 CHECK(run_seek(NULL,&info,&cancel,10000)==0);CHECK(cancel==0);
 CHECK(texture[0][0]==0xf800&&texture[0][1]==0x07e0&&texture[0][2]==0x001f);
 CHECK(texture[271][479]==0xffff);CHECK(frees==1&&draws==1&&prepared==1);
 CHECK(!info.draw_backdrop&&!info.backdrop_ctx);
 /* Preview allocation/read failure never cancels or blocks the real seek. */
 no_memory=1;CHECK(run_seek(NULL,&info,&cancel,10000)==0);CHECK(draws==1&&frees==1);
 no_memory=0;no_frame=1;CHECK(run_seek(NULL,&info,&cancel,10000)==0);CHECK(draws==1&&frees==1);
 no_frame=0;shown=0;unsigned before=captures;
 CHECK(run_seek(NULL,&info,&cancel,10000)==0);CHECK(captures==before);
 shown=1;seek_result=-7;CHECK(run_seek(NULL,&info,&cancel,10000)==-7);CHECK(frees==2);
 /* Run the actual reopening/rollback helper: IPTV must not inherit a seek,
  * and both successful and restored backends notify the CPU policy. */
 VtPerformanceClockGuard guard={0};
 OpenTask task={.live=1,.live_session=(void*)1,.clock_guard=&guard};
 task.config.preferred_backend=VT_DECODER_BACKEND_SOFTWARE;
 task.config.backend_start=decoder_clock_start;task.config.backend_start_ctx=&task;
 VtDecoderPlayer *player=(void*)&texture;
 expected_position=0;opens=0;
 CHECK(run_backend_change(&player,&task,&info,&cancel,VT_DECODER_BACKEND_HARDWARE,43000,0,0)==0);
 CHECK(opens==1&&backend_hardware&&task.startup_backend==VT_DECODER_BACKEND_HARDWARE);
 CHECK(opened_backend[0]==VT_DECODER_BACKEND_HARDWARE);
 task.config.preferred_backend=VT_DECODER_BACKEND_SOFTWARE;opens=0;fail_first=1;
 CHECK(run_backend_change(&player,&task,&info,&cancel,VT_DECODER_BACKEND_HARDWARE,43000,0,0)==1);
 CHECK(opens==2&&opened_backend[0]==VT_DECODER_BACKEND_HARDWARE&&opened_backend[1]==VT_DECODER_BACKEND_SOFTWARE);
 CHECK(!backend_hardware&&task.startup_backend==VT_DECODER_BACKEND_SOFTWARE&&backend_calls==3);
 CHECK(!source_stops); /* Failed HW switch must preserve producer for SW rollback. */
 task.live_session=NULL;task.live=0;opens=0;fail_first=0;expected_position=43000;
 CHECK(!run_backend_change(&player,&task,&info,&cancel,VT_DECODER_BACKEND_HARDWARE,43000,0,0));
 CHECK(opens==1&&task.config.start_position_ms==43000);
 printf("PASS: %u seek preview and actual live/file decoder reopen/clock/rollback checks\n",checks);
}

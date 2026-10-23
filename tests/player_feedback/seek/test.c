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
 printf("PASS: %u seek preview lifecycle/pixel checks\n",checks);
}

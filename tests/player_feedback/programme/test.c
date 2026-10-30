/* Actual player request/poll/release helpers plus real asynchronous service.
 * A blocked guide must never own the video/UI thread or leak its old channel. */
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "../../../src/media/hw_player_screen.c"
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
typedef struct {uint64_t magic;char channel[64];int error;} Context;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed=PTHREAD_COND_INITIALIZER;
static int allow_first,entered,fetches,refreshes,skipped,active,max_active;
static pthread_t ui_thread;
static unsigned metadata_updates;
static void publish_metadata(void *opaque,const VtIptvProgram *program){
 CHECK(opaque==&metadata_updates&&pthread_equal(pthread_self(),ui_thread));
 CHECK(!strcmp(program->channel_name,"Channel C"));metadata_updates++;
}
static int fetch_program(void *opaque,VtIptvProgram *out,volatile int *cancel,int refresh){
 Context *ctx=opaque;assert(!pthread_equal(pthread_self(),ui_thread)&&ctx->magic==0x123456789abcdef0ULL);
 pthread_mutex_lock(&gate);fetches++;active++;if(active>max_active)max_active=active;
 if(!strcmp(ctx->channel,"B"))skipped++;
 if(refresh)refreshes++;
 if(!strcmp(ctx->channel,"A")){
  entered=1;pthread_cond_broadcast(&gate_changed);
  /* Deliberately slow cancellation: release/switch must still return instantly. */
  while(!allow_first)pthread_cond_wait(&gate_changed,&gate);
  assert(!strcmp(ctx->channel,"A")&&__atomic_load_n(cancel,__ATOMIC_ACQUIRE));
 }
 int error=ctx->error;active--;pthread_mutex_unlock(&gate);
 out->available=1;out->start=time(NULL)-60;out->end=time(NULL)+3600;
 snprintf(out->channel_name,sizeof(out->channel_name),"Channel %s",ctx->channel);
 snprintf(out->title,sizeof(out->title),"Programme %s",ctx->channel);return error?-7:0;
}
static void wait_ready(PlayerProgram *state,const VtHwPlayerScreenSource *source,UiIptvPlayerPanel *panel){
 for(int i=0;i<5000&&panel->program_loading;i++){player_program_tick(state,source,panel,1);usleep(1000);}
 CHECK(!panel->program_loading);
}
int ui_loading_run(const char *title,UiLoadingTaskFn task,void *ctx,volatile int *cancel,const volatile long *cur,const volatile long *total){
 (void)title;(void)task;(void)ctx;(void)cancel;(void)cur;(void)total;assert(!"Programme loading must not call the full-screen loader");return -1;
}
int main(void){
 alarm(15);ui_thread=pthread_self();Context ctx={.magic=0x123456789abcdef0ULL,.channel="A"};
 VtHwPlayerScreenSource source={.iptv_program=fetch_program,.iptv_program_ctx=&ctx,.iptv_program_ctx_size=sizeof(ctx),.iptv_metadata=publish_metadata,.iptv_metadata_ctx=&metadata_updates};
 UiIptvPlayerPanel panel={0};PlayerProgram state={0};
 player_program_start(&state,&source,&panel,1,0);CHECK(panel.program_loading&&state.job&&!panel.page);
 pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&gate_changed,&gate);pthread_mutex_unlock(&gate);
 strcpy(ctx.channel,"mutated caller storage");
 for(int frame=0;frame<120;frame++)player_program_tick(&state,&source,&panel,1);
 CHECK(panel.program_loading&&!panel.program.available); /* UI continues while the request is blocked. */
 strcpy(ctx.channel,"B");player_program_start(&state,&source,&panel,1,0);CHECK(panel.program_loading);
 strcpy(ctx.channel,"C");player_program_start(&state,&source,&panel,1,0);CHECK(panel.program_loading);
 pthread_mutex_lock(&gate);CHECK(fetches==1);allow_first=1;pthread_cond_broadcast(&gate_changed);pthread_mutex_unlock(&gate);
 wait_ready(&state,&source,&panel);CHECK(!panel.program_error&&!strcmp(panel.program.title,"Programme C"));
 pthread_mutex_lock(&gate);CHECK(!skipped&&fetches==2&&max_active==1);pthread_mutex_unlock(&gate);
 /* Refresh is asynchronous and retains valid details if the new request fails. */
 ctx.error=1;player_program_start(&state,&source,&panel,1,1);
 CHECK(panel.program_loading&&!strcmp(panel.program.title,"Programme C"));
 wait_ready(&state,&source,&panel);CHECK(panel.program_error&&!strcmp(panel.program.title,"Programme C"));
 CHECK(refreshes==1&&metadata_updates==1);
 /* Recorded VOD and manual benchmark trials never fetch current live schedules. */
 player_program_start(&state,&source,&panel,2,0);CHECK(!state.job&&!panel.program_loading);
 VtIptvQualityTrial trial={0};source.quality_trial=&trial;
 player_program_start(&state,&source,&panel,1,0);CHECK(!state.job&&!panel.program_loading);source.quality_trial=NULL;
 source.iptv_program_ctx_size=0;player_program_start(&state,&source,&panel,1,0);CHECK(panel.program_error&&!state.job);
 vt_iptv_program_release(state.job);
 Context batch[6];for(unsigned i=0;i<6;i++){batch[i]=(Context){.magic=0x123456789abcdef0ULL};snprintf(batch[i].channel,sizeof(batch[i].channel),"Grid %u",i);}
 VtIptvProgramJob *grid=vt_iptv_program_request_batch(fetch_program,batch,sizeof(batch[0]),6,0);CHECK(grid);
 memset(batch,0,sizeof(batch));VtIptvProgram row;
 for(unsigned i=0;i<6;i++){
  int ready=0;for(int tick=0;tick<5000&&!ready;tick++){ready=vt_iptv_program_poll_row(grid,i,&row);if(!ready)usleep(1000);}
  char title[64];snprintf(title,sizeof(title),"Programme Grid %u",i);CHECK(ready==1&&!strcmp(row.title,title));
 }
 CHECK(vt_iptv_program_poll_row(grid,6,&row)==VT_IPTV_INVALID);vt_iptv_program_release(grid);
 CHECK(!vt_iptv_program_request_batch(fetch_program,&ctx,sizeof(ctx),7,0));
 vt_iptv_program_shutdown();
 CHECK(!active);alarm(0);
 printf("Background programme loading: %u checks; starts before R1, 120 UI ticks during blocked fetch, deep context lifetime, cancelled channel switch/latest-only queue, no stale publication, refresh error retention, VOD/trial exclusion and clean shutdown passed.\n",checks);
}

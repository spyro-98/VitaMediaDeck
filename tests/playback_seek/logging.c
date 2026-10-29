#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../../src/common/text_log.c"

int psvDebugScreenPuts(const char *text){(void)text;return 0;}
int sceKernelDelayThread(unsigned us){usleep(us);return 0;}
static int disk_enabled=1,fail_rotation;
int vt_preferences_disk_logs_enabled(void){return disk_enabled;}
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoWrite(int f,const void *p,unsigned n){return (int)write(f,p,n);}
int sceIoClose(int f){return close(f);}
int sceIoRemove(const char *p){return remove(p);}
int sceIoRename(const char *a,const char *b){return fail_rotation&&!strcmp(a,"session.txt")?-1:rename(a,b);}
int sceIoGetstat(const char *p,SceIoStat *s){struct stat value;int r=stat(p,&value);if(!r)s->st_size=value.st_size;return r;}
static void contents(const char *path,char out[1024]){FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,1023,f);out[n]=0;fclose(f);}
static void *worker(void *p){long index=(long)p;for(int i=0;i<128;i++)log_printf("worker %ld event %d",index,i);return NULL;}
int main(void){
 log_init();log_printf("first");log_printf("second\n");
 size_t header=(size_t)(strchr(g_log_buf,'\n')+1-g_log_buf);
 assert(!strncmp(g_log_buf,"Session UTC: ",13));
 assert(!strcmp(g_log_buf+header,"first\nsecond\n")&&g_log_total_lines==3);
 char text[1024];memset(text,'x',sizeof(text)-1);text[sizeof(text)-1]=0;
 log_printf("%s",text);
 assert(g_log_total_lines==4&&g_log_buf[g_log_len-1]=='\n'&&g_log_len==header+strlen("first\nsecond\n")+LOG_LINE_MAX);
 log_init();pthread_t threads[4];
 for(long i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)i));
 for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(g_log_total_lines==513);
 unsigned lines=0;char *start=strchr(g_log_buf,'\n')+1;
 while(*start){assert(!strncmp(start,"worker ",7));char *end=strchr(start,'\n');assert(end);lines++;start=end+1;}
 assert(lines==512);
 for(int i=0;i<3000;i++)log_printf("%04d %s",i,text);
 log_printf("latest playback failure");assert(strstr(g_log_buf,"latest playback failure")&&g_log_len<LOG_BUF_SIZE);
 assert(!strncmp(g_log_buf,"Session UTC: ",13));
 unsigned actual=0;for(size_t i=0;i<g_log_len;i++)if(g_log_buf[i]=='\n')actual++;
 assert(actual==(unsigned)g_log_total_lines);
 char directory[]="/private/tmp/vmd-session-log.XXXXXX";assert(mkdtemp(directory)&&!chdir(directory));
 char body[1024];
 for(int session=0;session<20;session++){
  log_init();log_printf("session number %d",session);assert(!log_save("session.txt"));
  log_printf("end of session %d",session);assert(!log_save("session.txt"));
  contents("session.txt",body);char expected[64];snprintf(expected,sizeof(expected),"end of session %d",session);assert(strstr(body,expected));
  if(session){contents("session.txt.1",body);snprintf(expected,sizeof(expected),"end of session %d",session-1);assert(strstr(body,expected));}
 }
 contents("session.txt.16",body);assert(strstr(body,"end of session 3"));assert(access("session.txt.17",F_OK));
 disk_enabled=0;log_init();log_printf("disabled");assert(!log_save("session.txt"));contents("session.txt",body);assert(strstr(body,"session 19"));
 disk_enabled=1;fail_rotation=1;assert(log_save("session.txt")<0);contents("session.txt",body);assert(strstr(body,"session 19"));
 puts("Production logs: record boundaries/truncation, 4 concurrent producers, newest-error retention beyond 256 KiB, UTC session header, 16 bounded previous sessions, same-session saves, disabled logging and failed-rotation preservation passed.");
}

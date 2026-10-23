#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/common/text_log.c"

int psvDebugScreenPuts(const char *text){(void)text;return 0;}
int sceKernelDelayThread(unsigned us){usleep(us);return 0;}
static void *worker(void *p){long index=(long)p;for(int i=0;i<128;i++)log_printf("worker %ld event %d",index,i);return NULL;}
int main(void){
 log_init();log_printf("first");log_printf("second\n");
 assert(!strcmp(g_log_buf,"first\nsecond\n")&&g_log_total_lines==2);
 char text[1024];memset(text,'x',sizeof(text)-1);text[sizeof(text)-1]=0;
 log_printf("%s",text);
 assert(g_log_total_lines==3&&g_log_buf[g_log_len-1]=='\n'&&g_log_len==strlen("first\nsecond\n")+LOG_LINE_MAX);
 log_init();pthread_t threads[4];
 for(long i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)i));
 for(int i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 assert(g_log_total_lines==512);
 unsigned lines=0;char *start=g_log_buf;
 while(*start){assert(!strncmp(start,"worker ",7));char *end=strchr(start,'\n');assert(end);lines++;start=end+1;}
 assert(lines==512);
 puts("Production log records: newline, existing newline, truncation and 4 concurrent producers passed.");
}

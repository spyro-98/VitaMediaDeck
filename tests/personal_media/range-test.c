#include "network/personal_media.h"
#include <vita_https.h>
#include <openssl/md5.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
int vita_https_net_init(void){return 0;}void vita_https_net_term(void){}int vita_https_net_is_connected(void){return 1;}int vita_https_net_wifi_signal_percent(int*p){*p=100;return 0;}
int mbedtls_md5(const unsigned char *p,size_t n,unsigned char d[16]){MD5(p,n,d);return 0;}
int sceKernelGetRandomNumber(void*p,size_t n){static int next;memset(p,++next,n);return 0;}
#include "network/network_internal.h"
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
static pthread_t threads[8];static int (*entries[8])(SceSize,void*);static int next_thread;static struct{SceSize n;void *p;} args[8];
int sceKernelCreateThread(const char*n,int(*f)(SceSize,void*),int p,unsigned stack,int a,int b,void*c){(void)n;(void)p;(void)stack;(void)a;(void)b;(void)c;assert(next_thread<7);entries[++next_thread]=f;return next_thread;}
static void *run(void *ctx){int id=(int)(intptr_t)ctx;entries[id](args[id].n,args[id].p);free(args[id].p);return NULL;}
int sceKernelStartThread(SceUID id,SceSize n,void *p){args[id].n=n;args[id].p=malloc(n);memcpy(args[id].p,p,n);return pthread_create(&threads[id],NULL,run,(void*)(intptr_t)id);}
int sceKernelWaitThreadEnd(SceUID id,void*a,void*b){(void)a;(void)b;return pthread_join(threads[id],NULL);}
int sceKernelDeleteThread(SceUID id){(void)id;return 0;}int sceKernelExitThread(int e){return e;}int sceKernelChangeThreadPriority(SceUID id,int p){(void)id;(void)p;return 0;}
int sceKernelDelayThread(unsigned n){return usleep(n);}
void log_printf(const char *format,...){assert(!strstr(format,"http"));}
SceUID sceKernelGetThreadId(void){return 1;}
static unsigned char pattern(size_t at){return (unsigned char)((at*13+17)%251);}
int main(int argc,char **argv){assert(argc==2);assert(vita_https_init()==0);VtNetworkSource s={.protocol=VT_NETWORK_NAVIDROME};VtNetworkCredential c={0};snprintf(s.host,sizeof(s.host),"%s",argv[1]);snprintf(s.username,sizeof(s.username),"user");snprintf(c.password,sizeof(c.password),"password");char url[3072];assert(vt_personal_stream_url(&s,&c,"song:song1",url,sizeof(url))==0);volatile int cancel=0;VtDecoderStreamHandle stream={0};VtNetworkBufferTelemetry telemetry={0};volatile uintptr_t owner=0;
 assert(vt_authenticated_range_stream(&s,url,&stream,&cancel,&telemetry,&owner)==0);assert(stream.size==3*1024*1024+173);unsigned char bytes[12001];size_t positions[]={0,1048570,2097120,31,3145700};unsigned total=0;
 for(size_t i=0;i<sizeof(positions)/sizeof(*positions);i++){size_t at=positions[i];assert(stream.seek(stream.opaque,(int64_t)at,SEEK_SET)==(int64_t)at);int n=stream.read(stream.opaque,bytes,sizeof(bytes));assert(n>0);for(int k=0;k<n;k++){assert(bytes[k]==pattern(at+(size_t)k));total++;}}
 assert(telemetry.capacity_bytes==8*1024*1024&&owner!=0);assert(stream.seek(stream.opaque,0,SEEK_SET)==0);stream.abort(stream.opaque);assert(stream.read(stream.opaque,bytes,sizeof(bytes))<0);stream.close(stream.opaque);assert(owner==0);
 vita_https_shutdown();printf("Authenticated native range cache: %u byte checks, cross-chunk seeks, telemetry, abort and joined close passed\n",total);return 0;}

/* Actual source database commit path: only successful writes publish changes. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "network/network_source.h"
#include <psp2/io/stat.h>
static int fail_publish,fail_flush;
int sceIoOpen(const char *p,int flags,int mode){return open(p,flags,mode);}
int sceIoRead(int fd,void *p,unsigned n){return (int)read(fd,p,n);}
int sceIoWrite(int fd,const void *p,unsigned n){return (int)write(fd,p,n);}
int sceIoClose(int fd){return close(fd);}
int sceIoMkdir(const char *p,int mode){return mkdir(p,mode);}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRename(const char *a,const char *b){if(fail_publish&&strstr(a,".tmp"))return -1;return rename(a,b);}
int sceIoSyncByFd(int fd,int flags){(void)flags;return fail_flush?-1:fsync(fd);}
int sceIoSync(const char *p,int flags){(void)p;(void)flags;return 0;}
int sceIoGetstat(const char *p,SceIoStat *out){struct stat s;int r=stat(p,&s);if(!r)out->st_size=s.st_size;return r;}
#include "source-save.inc"
int main(void){
 char dir[]="/private/tmp/vmd-source-changes.XXXXXX";assert(mkdtemp(dir)&&!chdir(dir));assert(!mkdir("ux0:data",0700));assert(!mkdir("ux0:data/VitaMediaDeck",0700));
 VtNetworkSource source={.protocol=VT_NETWORK_SFTP};strcpy(source.name,"Before");strcpy(source.host,"fixture.test");
 unsigned before=vt_network_sources_revision();assert(!vt_network_sources_save(&source,1)&&vt_network_sources_revision()==before+1);
 VtNetworkSource loaded;assert(vt_network_sources_load(&loaded,1)==1&&!strcmp(loaded.name,"Before"));
 before=vt_network_sources_revision();strcpy(source.name,"After");fail_publish=1;
 assert(vt_network_sources_save(&source,1)<0&&vt_network_sources_revision()==before);fail_publish=0;
 assert(vt_network_sources_load(&loaded,1)==1&&!strcmp(loaded.name,"Before"));
 fail_flush=1;assert(vt_network_sources_save(&source,1)<0&&vt_network_sources_revision()==before);fail_flush=0;
 assert(!vt_network_sources_save(&source,1)&&vt_network_sources_revision()==before+1);
 assert(vt_network_sources_load(&loaded,1)==1&&!strcmp(loaded.name,"After"));
 assert(!vt_network_sources_save(NULL,0)&&vt_network_sources_revision()==before+2);
 assert(!vt_network_sources_load(&loaded,1));
 puts("Saved-source change publication: successful add/edit/remove notify immediately; failed sync/rename keep the old data and revision.");
}

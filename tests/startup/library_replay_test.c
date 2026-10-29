#include <unistd.h>
static unsigned replay_reads,limit_read;
static ssize_t replay_read(int fd,void *p,size_t size){
 replay_reads++;if(limit_read&&size>97)size=97;return read(fd,p,size);
}
#define read replay_read
#define main existing_library_main
#include "../library/core_test.c"
#undef main
#undef read
int main(void){
 main_thread=pthread_self();char path[]="/private/tmp/vmd-library-startup.XXXXXX";
 CHECK(mkdtemp(path)&&!chdir(path));CHECK(!mkdir("ux0:data",0700));CHECK(!mkdir("ux0:data/VitaMediaDeck",0700));
 int fd=open(DB,O_CREAT|O_TRUNC|O_WRONLY,0600);CHECK(fd>=0);
 for(unsigned round=0;round<4;round++)for(unsigned i=0;i<VT_LIBRARY_MAX;i++){
  char locator[64],title[64];snprintf(locator,sizeof(locator),"ux0:video/%u.mp4",i);
  snprintf(title,sizeof(title),"Cached item %u revision %u",i,round);
  VtLibraryItem item=make_item(VT_LIBRARY_LOCAL,locator,title);CHECK(!frame(fd,1,&item,sizeof(item)));
 }
 CHECK(!close(fd));struct stat info;CHECK(!stat(DB,&info));
 replay_reads=0;set(&io_on_main,0);CHECK(!load());
 CHECK(count==VT_LIBRARY_MAX&&replay_reads<256&&!get(&io_on_main));
 unsigned buffered_calls=replay_reads;unsigned size=(unsigned)info.st_size;
 /* Real init schedules compaction of an oversized valid journal off the UI. */
 CHECK(!vt_library_init(NULL));
 for(unsigned n=0;n<3000;n++){CHECK(!stat(DB,&info));if(info.st_size<size)break;usleep(1000);}
 vt_library_shutdown();CHECK(!stat(DB,&info));CHECK(info.st_size<size/2&&!compact_pending&&!get(&io_on_main));
 CHECK(count==VT_LIBRARY_MAX);
 for(int i=0;i<count;i++)CHECK(strstr(slots[i].item.title,"revision 3"));
 /* Buffered reader also accepts short storage reads and catches torn records. */
 limit_read=1;count=journal_bytes=0;CHECK(!load()&&count==VT_LIBRARY_MAX);limit_read=0;
 fd=open(DB,O_WRONLY);CHECK(fd>=0);unsigned char byte=0xff;CHECK(pwrite(fd,&byte,1,info.st_size-1)==1);CHECK(!close(fd));
 count=journal_bytes=0;CHECK(load()<0&&count>0);
 fd=open(DB,O_WRONLY|O_TRUNC);CHECK(fd>=0&&write(fd,"bad",3)==3);CHECK(!close(fd));
 count=journal_bytes=0;CHECK(load()<0&&!count);
 printf("Library startup: %u-byte legacy journal, %u buffered reads, compatible replay, background compaction, short reads, CRC failure and torn-header detection passed.\n",size,buffered_calls);
}

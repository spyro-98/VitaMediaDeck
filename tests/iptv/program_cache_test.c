/* Count actual XML reader creation, not just HTTP requests: a raw-file hit
 * previously still decompressed/scanned the whole guide on every revisit. */
#include <libxml/xmlreader.h>
#include <time.h>
#include <sys/wait.h>
static unsigned readers;
static time_t test_wall=1793363400;
static time_t test_time(time_t *out){if(out)*out=test_wall;return test_wall;}
static xmlTextReaderPtr counted_reader(xmlInputReadCallback read,xmlInputCloseCallback close,void *ctx,const char *url,const char *encoding,int options){
 readers++;return xmlReaderForIO(read,close,ctx,url,encoding,options);
}
#define time test_time
#define xmlReaderForIO counted_reader
#include "../../src/network/iptv_options.c"
#undef xmlReaderForIO
#define main original_programme_test
#include "programme_test.c"
#undef main
#undef time
static char fixture[4096];
static void setup_guide(void){
 char a[40],b[40],c[40];stamp(test_wall-600,a);stamp(test_wall+600,b);stamp(test_wall+1200,c);
 snprintf(fixture,sizeof(fixture),"<tv><channel id='Rai 1.it'><display-name>Rai 1</display-name></channel><programme channel='Rai 1.it' start='%s' stop='%s'><title lang='it'>Notizie</title><title lang='en'>News</title></programme><programme channel='Rai 1.it' start='%s' stop='%s'><title>Next</title></programme></tv>",a,b,b,c);
 body=fixture;length=strlen(fixture);
}
int main(int argc,char **argv){
 char binary[4096];CHECK(realpath(argv[0],binary));setup_guide();VtIptvProgram p;volatile int cancel=0;
 VtIptvGuideQuery q={.url="https://cached.test/private-guide?access=never-stored",.channel="Rai1.it"};
 if(argc>1){
  CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.cached);
  q.channel="Unknown.it";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_NO_CHANNEL);
  CHECK(!readers&&!fetches);return 0;
 }
 char work[]="/private/tmp/vmd-guide-cache-test.XXXXXX";CHECK(mkdtemp(work)&&!chdir(work));
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&readers==1&&fetches==1);
 unsigned parsed=readers;
 for(unsigned i=0;i<100;i++)CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.cached);
 CHECK(readers==parsed&&fetches==1);
 q.channel="Unknown.it";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&!p.available);
 parsed=readers;unsigned downloads=fetches;
 for(unsigned i=0;i<100;i++)CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_NO_CHANNEL);
 CHECK(readers==parsed&&fetches==downloads);
 /* Re-exec starts with no worker/global cache: both results persist on device. */
 pid_t child=fork();CHECK(child>=0);if(!child){execl(binary,binary,"--cached",(char*)NULL);_exit(127);}
 int status;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));
 q.channel="Rai1.it";
 /* A torn/replaced result is never trusted, and an interrupted atomic rename
  * may recover the prior valid backup without reparsing. */
 char result_path[192],backup_path[200],key[17];unsigned slot=program_key(&q,"it",key);
 snprintf(result_path,sizeof(result_path),PROGRAM_DIR "/%u.bin",slot);snprintf(backup_path,sizeof(backup_path),"%s.bak",result_path);
 /* Previous app builds cached comparisons against a different clock path.
  * Even a CRC-valid old record must be reselected from the raw UTC guide. */
 FILE *legacy=fopen(result_path,"r+b");CHECK(legacy);ProgramRecord old;
 CHECK(fread(&old,1,sizeof(old),legacy)==sizeof(old));memcpy(old.magic,"VMDPRG1",8);old.crc=vt_crc32(&old,offsetof(ProgramRecord,crc));
 rewind(legacy);CHECK(fwrite(&old,1,sizeof(old),legacy)==sizeof(old)&&!fclose(legacy));parsed=readers;
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&readers==parsed+1&&fetches==downloads);
 CHECK(!rename(result_path,backup_path));parsed=readers;
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&readers==parsed);
 CHECK(!rename(backup_path,result_path));FILE *corrupt=fopen(result_path,"r+b");CHECK(corrupt);
 CHECK(!fseek(corrupt,(long)offsetof(ProgramRecord,program)+16,SEEK_SET));CHECK(fputc(0xff,corrupt)!=EOF&&!fclose(corrupt));
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&!strcmp(p.title,"Notizie")&&readers==parsed+1);
 parsed=readers;
 q.channel="Rai1.it";
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"en",&p,&cancel)&&!strcmp(p.title,"News")&&readers==parsed+1);
 q.shift_seconds=60;CHECK(!vt_iptv_program_load_channel(&q,NULL,"en",&p,&cancel)&&p.start==test_wall-540);q.shift_seconds=0;
 /* Current-programme boundary expires the result, but reuses the raw guide. */
 test_wall+=601;parsed=readers;
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&!strcmp(p.title,"Next")&&readers==parsed+1&&fetches==downloads);
 /* Replacing the raw guide invalidates old absent-match results. */
 const char *new_guide="<tv><channel id='Unknown.it'/><programme channel='Unknown.it' start='20261030000000 +0000' stop='20261101000000 +0000'><title>New listing</title></programme></tv>";
 guide_cache(q.url,new_guide,strlen(new_guide),test_wall);q.channel="Unknown.it";
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&!strcmp(p.title,"New listing"));
 /* Per-guide failure backoff covers different channels, not only one query. */
 q.url="https://offline.test/guide";q.channel="Offline-A";network_error=1;unsigned before=fetches;
 CHECK(vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)==VT_IPTV_TIMEOUT);
 q.channel="Offline-B";CHECK(vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)==VT_IPTV_TIMEOUT&&fetches==before+1);
 test_wall+=301;CHECK(vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)==VT_IPTV_TIMEOUT&&fetches==before+2);
 q.refresh=1;CHECK(vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)==VT_IPTV_TIMEOUT&&fetches==before+3);
 /* Name-only known Italian brands can use the public guide. Foreign IDs and
  * unknown/generic brands never trigger this country fallback. */
 network_error=0;setup_guide();q=(VtIptvGuideQuery){.title="Rai 1 HD (720p)"};
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.public_source);
 q.channel="Rai1.it@SD";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.public_source);
 before=fetches;q.channel="Rai1.es@HD";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_MISSING&&fetches==before);
 q.channel="";q.title="News TV";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_MISSING&&fetches==before);
 q.title="Rai 1 +1";CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.guide_status==VT_IPTV_GUIDE_MISSING&&fetches==before);
 q=(VtIptvGuideQuery){.url="https://empty.test/guide.xml",.channel="Rai1.it@HD",.title="Rai 1 HD"};
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available&&p.public_source);
 /* Cancellation is not remembered as absence; next request remains usable. */
 q=(VtIptvGuideQuery){.url="https://cancel.test/guide",.channel="Rai1.it"};cancel=1;
 CHECK(vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)==VT_IPTV_CANCELLED);cancel=0;
 CHECK(!vt_iptv_program_load_channel(&q,NULL,"it",&p,&cancel)&&p.available);
 printf("Programme cache: %u checks; single XML pass, 100 positive/100 negative revisits without parsing, fresh-process persistence, language/shift/time expiry, source replacement, shared failure backoff, explicit refresh and safe name-only fallback passed.\n",checks);
}

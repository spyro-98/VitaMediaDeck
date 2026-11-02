/* Disposable per-query results, including absent matches and failures. Lookup
 * identities are hashed; provider query URLs and credentials are not stored.
 * Called on guide workers, never from drawing/input code. Raw guide replacement
 * invalidates results before their normal expiry. Fixed slots bound storage. */
#include "common/crc32.h"
#define PROGRAM_SLOTS 256
#define PROGRAM_DIR GUIDE_DIR "/programmes"
typedef struct {
 char magic[8],key[17];int result;
 int64_t saved,until,guide_stamp;
 VtIptvProgram program;uint32_t crc;
} ProgramRecord;
static uint64_t program_hash(uint64_t h,const char *text){
 for(const unsigned char *p=(const unsigned char*)(text?text:"");*p;p++){h^=*p;h*=1099511628211ULL;}
 return (h^255)*1099511628211ULL;
}
static unsigned program_key(const VtIptvGuideQuery *q,const char *language,char key[17]){
 uint64_t h=1469598103934665603ULL;
 h=program_hash(h,q->url);h=program_hash(h,q->channel);h=program_hash(h,q->name);
 h=program_hash(h,q->title);h=program_hash(h,language);
 char shift[32];snprintf(shift,sizeof(shift),"%d",q->shift_seconds);h=program_hash(h,shift);
 snprintf(key,17,"%016llx",(unsigned long long)h);return h%PROGRAM_SLOTS;
}
static int64_t program_guide_stamp(const char *url){
 char key[17];vt_iptv_choice_key(url,key);if(!key[0])return 0;
 for(int i=0;i<4;i++){
  char path[160];snprintf(path,sizeof(path),GUIDE_DIR "/%d.bin",i);
  GuideCacheHeader h;FILE *f=fopen(path,"rb");int valid=f&&fread(&h,1,sizeof(h),f)==sizeof(h)&&!memcmp(h.magic,"VMDEPG1",8)&&!memcmp(h.key,key,17);
  if(f)fclose(f);
  if(valid)return h.fetched;
 }
 return 0;
}
static int program_cached(const VtIptvGuideQuery *q,const char *language,VtIptvProgram *out,int *result){
 if(q->refresh)return 0;
 ProgramRecord record;char path[192],key[17];unsigned slot=program_key(q,language,key);
 snprintf(path,sizeof(path),PROGRAM_DIR "/%u.bin",slot);FILE *f=fopen(path,"rb");if(!f){char backup[200];snprintf(backup,sizeof(backup),"%s.bak",path);f=fopen(backup,"rb");}if(!f)return 0;
 int valid=fread(&record,1,sizeof(record),f)==sizeof(record)&&fgetc(f)==EOF;fclose(f);
 int64_t now=vt_iptv_time_now(),stamp=program_guide_stamp(q->url);
 if(!valid||memcmp(record.magic,"VMDPRG2",8)||memcmp(record.key,key,17)||
    record.crc!=vt_crc32(&record,offsetof(ProgramRecord,crc))||record.saved>now||now>=record.until||
    (stamp&&stamp!=record.guide_stamp)||record.program.upcoming_count>VT_IPTV_UPCOMING_MAX)return 0;
 *out=record.program;out->cached=1;*result=record.result;return 1;
}
static void program_store(const VtIptvGuideQuery *q,const char *language,const VtIptvProgram *program,int result){
 if(result==VT_IPTV_CANCELLED)return;
 ProgramRecord record={0};memcpy(record.magic,"VMDPRG2",8);record.result=result;record.program=*program;
 record.saved=vt_iptv_time_now();record.guide_stamp=program_guide_stamp(q->url);
 record.until=record.saved+(result<0||program->stale?300:program->guide_status==VT_IPTV_GUIDE_MISSING||program->guide_status==VT_IPTV_GUIDE_NO_CHANNEL?21600:900);
 if(result>=0){
  if(program->available&&program->end>record.saved&&program->end<record.until)record.until=program->end;
  if(program->next_start>record.saved&&program->next_start<record.until)record.until=program->next_start;
 }
 unsigned slot=program_key(q,language,record.key);record.crc=vt_crc32(&record,offsetof(ProgramRecord,crc));
 mkdir("ux0:data",0777);mkdir("ux0:data/VitaMediaDeck",0777);mkdir(GUIDE_DIR,0777);mkdir(PROGRAM_DIR,0777);
 char path[192],temp[200];snprintf(path,sizeof(path),PROGRAM_DIR "/%u.bin",slot);snprintf(temp,sizeof(temp),"%s.tmp",path);
 FILE *f=fopen(temp,"wb");if(!f)return;
 int ok=fwrite(&record,1,sizeof(record),f)==sizeof(record);if(fflush(f))ok=0;if(fclose(f))ok=0;
 if(ok){
  char backup[200];snprintf(backup,sizeof(backup),"%s.bak",path);struct stat st;int old=!stat(path,&st);
  if(old){remove(backup);if(rename(path,backup)){remove(temp);return;}}
  if(!rename(temp,path)){remove(backup);return;}
  if(old)rename(backup,path);
 }
 remove(temp);
}
/* Back off by guide, too: six visible channels must not make six identical
 * failed downloads. Manual refresh explicitly bypasses this transient state. */
typedef struct {char key[17];int result;int64_t until;} GuideRetry;
static GuideRetry guide_retries[8];
static unsigned guide_retry_next;
static int guide_retry(const char *url,int refresh){
 if(refresh)return 0;
 char key[17];vt_iptv_choice_key(url,key);
 for(unsigned i=0;i<8;i++)if(!strcmp(key,guide_retries[i].key)&&vt_iptv_time_now()<guide_retries[i].until)return guide_retries[i].result;
 return 0;
}
static void guide_retry_store(const char *url,int result){
 if(result==VT_IPTV_CANCELLED)return;
 char key[17];vt_iptv_choice_key(url,key);GuideRetry *entry=NULL;
 for(unsigned i=0;i<8;i++)if(!strcmp(key,guide_retries[i].key)){entry=&guide_retries[i];break;}
 if(!entry)entry=&guide_retries[guide_retry_next++%8];
 snprintf(entry->key,sizeof(entry->key),"%s",key);entry->result=result;entry->until=vt_iptv_time_now()+(result?300:0);
}

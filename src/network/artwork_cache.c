#include "network/artwork_cache.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __vita__
#include <mbedtls/sha256.h>
#else
#include <openssl/sha.h>
#endif
#define ROOT "ux0:data/VitaMediaDeck/artwork"
#define ITEM_LIMIT (2u*1024u*1024u)
#define CACHE_LIMIT (256u*1024u*1024u)
#define CACHE_ITEMS 4096
#define REFRESH_SECONDS (24*60*60)
#define RETRY_SECONDS 300
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t initialized=PTHREAD_ONCE_INIT;
static const char *extensions[]={"jpg","png","webp","gif","bmp"};
typedef struct {char key[65],path[192];size_t bytes;time_t fetched,used,retry;} Entry;
static Entry entries[CACHE_ITEMS];
static int encoding(const unsigned char *p,size_t n){
 if(n>=3&&p[0]==255&&p[1]==216&&p[2]==255)return 0;
 if(n>=8&&!memcmp(p,"\x89PNG\r\n\x1a\n",8))return 1;
 if(n>=12&&!memcmp(p,"RIFF",4)&&!memcmp(p+8,"WEBP",4))return 2;
 if(n>=6&&(!memcmp(p,"GIF87a",6)||!memcmp(p,"GIF89a",6)))return 3;
 return n>=2&&!memcmp(p,"BM",2)?4:-1;
}
static int digest(const void *data,size_t size,unsigned char out[32]){
#ifdef __vita__
 return mbedtls_sha256(data,size,out,0);
#else
 return SHA256(data,size,out)?0:-1;
#endif
}
static int url_key(const char *url,char key[65]){
 if(!url||!*url)return -1;
 unsigned char d[32];if(digest(url,strlen(url),d))return -1;
 for(int i=0;i<32;i++)snprintf(key+i*2,3,"%02x",d[i]);
 return 0;
}
static Entry *find(const char *key){for(unsigned i=0;i<CACHE_ITEMS;i++)if(!strcmp(entries[i].key,key))return &entries[i];return NULL;}
static Entry *slot(const char *key){
 Entry *e=find(key);if(e)return e;
 for(unsigned i=0;i<CACHE_ITEMS;i++)if(!entries[i].key[0]){snprintf(entries[i].key,sizeof(entries[i].key),"%s",key);return &entries[i];}
 return NULL;
}
static int known_file(const char *name){
 size_t n=strlen(name);if(n<68||strspn(name,"0123456789abcdef")!=64)return 0;
 const char *ext=name+64;
 if(*ext=='-'){if(n<85||strspn(ext+1,"0123456789abcdef")!=16)return 0;ext+=17;}
 if(*ext++!='.')return 0;
 for(unsigned i=0;i<5;i++)if(!strcmp(ext,extensions[i]))return 1;
 return 0;
}
static int valid_file(const char *path){
 unsigned char head[12];FILE *f=fopen(path,"rb");if(!f)return 0;
 size_t n=fread(head,1,sizeof(head),f);fclose(f);return encoding(head,n)>=0;
}
/* One tiny, atomic pointer per URL hash lets a worker restore a cached image
 * directly, without waiting for the full directory walk after every launch.
 * Only validated cache basenames are accepted; no URLs or private tokens. */
static void save_reference(const char *key,const char *path){
 char temp[192],ref[192];
 snprintf(temp,sizeof(temp),ROOT "/%s.ref.tmp",key);
 snprintf(ref,sizeof(ref),ROOT "/%s.ref",key);
 const char *name=strrchr(path,'/');if(!name||!known_file(++name)||strncmp(name,key,64))return;
 FILE *f=fopen(temp,"wb");if(!f)return;
 int ok=fprintf(f,"%s\n",name)>0;if(fflush(f))ok=0;if(fclose(f))ok=0;
 if(!ok||rename(temp,ref))remove(temp);
}
static void restore_reference(const char *key){
 char ref[192],name[96],path[192];struct stat st;
 pthread_mutex_lock(&mutex);int found=find(key)!=NULL;pthread_mutex_unlock(&mutex);
 if(found)return;
 snprintf(ref,sizeof(ref),ROOT "/%s.ref",key);
 FILE *f=fopen(ref,"rb");int valid=f&&fgets(name,sizeof(name),f);if(f)fclose(f);
 if(!valid)return;
 name[strcspn(name,"\r\n")]=0;
 if(!known_file(name)||strncmp(name,key,64))return;
 snprintf(path,sizeof(path),ROOT "/%s",name);
 if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>ITEM_LIMIT||!valid_file(path))return;
 pthread_mutex_lock(&mutex);
 Entry *e=find(key);if(!e)e=slot(key);
 if(e&&!e->path[0]){snprintf(e->path,sizeof(e->path),"%s",path);e->bytes=st.st_size;e->fetched=e->used=st.st_mtime;}
 pthread_mutex_unlock(&mutex);
}
static void initialize(void){
 /* Filesystem discovery is background-only. Publish each completed entry in a
  * short critical section so visible cache hits survive a slow card scan. */
 DIR *dir=opendir(ROOT);if(!dir)return;struct dirent *d;
 while((d=readdir(dir))){
  if(!known_file(d->d_name))continue;
  char path[192],key[65];snprintf(path,sizeof(path),ROOT "/%.90s",d->d_name);
  struct stat st;if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>ITEM_LIMIT||!valid_file(path))continue;
  memcpy(key,d->d_name,64);key[64]=0;
  pthread_mutex_lock(&mutex);Entry *e=slot(key);
  if(e&&(!e->path[0]||st.st_mtime>e->fetched)){snprintf(e->path,sizeof(e->path),"%s",path);e->bytes=st.st_size;e->fetched=e->used=st.st_mtime;}
  pthread_mutex_unlock(&mutex);
 }
 closedir(dir);
}
void vt_artwork_cache_init(void){pthread_once(&initialized,initialize);}
int vt_artwork_cache_peek(const char *url,char *path,size_t capacity){
 char key[65];if(!path||!capacity||url_key(url,key))return -1;
 /* Never wait for filesystem work or network completion in a render pass. */
 if(pthread_mutex_trylock(&mutex))return -1;
 Entry *e=find(key);int result=-1;
 if(e&&e->path[0]&&strlen(e->path)<capacity){snprintf(path,capacity,"%s",e->path);e->used=time(NULL);result=0;}
 pthread_mutex_unlock(&mutex);return result;
}
int vt_artwork_cache_current(const char *old_path,char *path,size_t capacity){
 const char *root=ROOT "/";size_t n=strlen(root);
 if(!old_path||strncmp(old_path,root,n)||strlen(old_path)<n+65||!path||!capacity)return -1;
 char key[65];memcpy(key,old_path+n,64);key[64]=0;if(strspn(key,"0123456789abcdef")!=64)return -1;
 if(pthread_mutex_trylock(&mutex))return -1;
 Entry *e=find(key);int result=-1;
 if(e&&e->path[0]&&strlen(e->path)<capacity){snprintf(path,capacity,"%s",e->path);e->used=time(NULL);result=0;}
 pthread_mutex_unlock(&mutex);return result;
}
int vt_artwork_cache_due(const char *url){
 char key[65];if(url_key(url,key))return 0;
 if(pthread_mutex_trylock(&mutex))return 0;
 Entry *e=find(key);time_t now=time(NULL);
 int due=!e||(now>=e->retry&&(!e->path[0]||now-e->fetched>=REFRESH_SECONDS));
 pthread_mutex_unlock(&mutex);return due;
}
static int make_room(size_t incoming,const char *keep){
 for(;;){size_t bytes=0;unsigned count=0;Entry *victim=NULL;
  for(unsigned i=0;i<CACHE_ITEMS;i++)if(entries[i].key[0]){
   count++;bytes+=entries[i].bytes;
   if(strcmp(entries[i].key,keep)&&(!victim||entries[i].used<victim->used))victim=&entries[i];
  }
  Entry *current=find(keep);size_t old=current?current->bytes:0;
  if((current||count<CACHE_ITEMS)&&bytes-old+incoming<=CACHE_LIMIT)return 0;
  if(!victim)return -1;
  if(victim->path[0])remove(victim->path);
  char ref[192];snprintf(ref,sizeof(ref),ROOT "/%s.ref",victim->key);remove(ref);
  memset(victim,0,sizeof(*victim));
 }
}
static int artwork_get(const char *url,char *path,size_t capacity,volatile int *cancel,VtArtworkFetch fetch,int refresh){
 if(!path||capacity<160||!fetch)return -1;
 path[0]=0;
 if(cancel&&*cancel)return -1;
 char key[65];if(url_key(url,key))return -1;
 /* A directory scan may still be running, or its worker could not start.
  * Keyed reads are independent and remain useful in both cases. */
 restore_reference(key);pthread_mutex_lock(&mutex);
 Entry *e=find(key);time_t now=time(NULL);int hit=e&&e->path[0];
 if(hit&&!valid_file(e->path)){remove(e->path);e->path[0]=0;e->bytes=0;e->retry=0;hit=0;}
 if(hit)snprintf(path,capacity,"%s",e->path);
 int due=refresh||!e||(!hit?now>=e->retry:now>=e->retry&&now-e->fetched>=REFRESH_SECONDS);
 if(e)e->used=now;
 pthread_mutex_unlock(&mutex);
 if(!due)return hit?0:-1;
 size_t size=0;char *data=fetch(url,cancel,&size);int format=data?encoding((unsigned char*)data,size):-1;
 if(format<0||!size||size>ITEM_LIMIT||(cancel&&*cancel)){
  free(data);pthread_mutex_lock(&mutex);e=slot(key);if(e&&!(cancel&&*cancel))e->retry=now+RETRY_SECONDS;pthread_mutex_unlock(&mutex);
  return hit?0:-1; /* Never discard the last image when an update fails. */
 }
 unsigned char content[32];if(digest(data,size,content)){free(data);return hit?0:-1;}
 char revision[17];for(int i=0;i<8;i++)snprintf(revision+i*2,3,"%02x",content[i]);
 pthread_mutex_lock(&mutex);int result=hit?0:-1;
 mkdir(ROOT,0777);
 if(!make_room(size,key)){
  char temp[192],next[192],old[192]="";snprintf(temp,sizeof(temp),ROOT "/%s.tmp",key);
  snprintf(next,sizeof(next),ROOT "/%s-%s.%s",key,revision,extensions[format]);
  e=find(key);if(e)snprintf(old,sizeof(old),"%s",e->path);
  FILE *f=fopen(temp,"wb");int ok=0;
  if(f){ok=fwrite(data,1,size,f)==size;if(fflush(f))ok=0;if(fclose(f))ok=0;}
  if(cancel&&*cancel)ok=0;
  if(ok&&!rename(temp,next)){
   e=slot(key);if(e){snprintf(e->path,sizeof(e->path),"%s",next);e->bytes=size;e->fetched=e->used=now;e->retry=0;snprintf(path,capacity,"%s",next);save_reference(key,next);result=0;}
   if(old[0]&&strcmp(old,next))remove(old);
  }else remove(temp);
 }
 e=find(key);if(e)e->retry=now+RETRY_SECONDS;
 pthread_mutex_unlock(&mutex);free(data);return result;
}

int vt_artwork_cache_get(const char *url,char *path,size_t n,volatile int *cancel,VtArtworkFetch fetch){return artwork_get(url,path,n,cancel,fetch,0);}
int vt_artwork_cache_refresh(const char *url,char *path,size_t n,volatile int *cancel,VtArtworkFetch fetch){return artwork_get(url,path,n,cancel,fetch,1);}

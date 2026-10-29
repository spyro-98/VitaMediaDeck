#include "network/artwork_cache.h"
#include "network/iptv_catalog.h"
#include "ui/online_catalog_layout.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#include <time.h>
static time_t clock_seconds=1800000000;
static time_t cache_time(time_t *out){if(out)*out=clock_seconds;return clock_seconds;}
#define time cache_time
#include "../../src/network/artwork_cache.c"
#undef time
static unsigned requests;
static size_t response_size=64;
static int bad;
static char *fetch(const char *url,volatile int *cancel,size_t *size) {
    (void)url;if(cancel&&*cancel)return NULL;requests++;*size=response_size;
    char *data=calloc(1,*size);memcpy(data,bad?"NOTIMAGE":"\x89PNG\r\n\x1a\n",8);data[12]=(char)requests;return data;
}
static void bounds(void) {
    DIR *d=opendir("ux0:data/VitaMediaDeck/artwork");assert(d);struct dirent *entry;size_t bytes=0;unsigned count=0;
    while((entry=readdir(d)))if(strstr(entry->d_name,".png")) {
        char path[512];struct stat st;snprintf(path,sizeof(path),"ux0:data/VitaMediaDeck/artwork/%s",entry->d_name);
        assert(!stat(path,&st));bytes+=st.st_size;count++;
    }
    closedir(d);assert(count<=4096&&bytes<=256u*1024*1024);
}
int main(int argc,char **argv) {
    clock_seconds=time(NULL);
    if(argc==4&&!strcmp(argv[1],"--restart")){
        char current[192];volatile int cancel=0;vt_artwork_cache_init();
        assert(!vt_artwork_cache_peek(argv[2],current,sizeof(current))&&!strcmp(current,argv[3]));
        assert(!vt_artwork_cache_get(argv[2],current,sizeof(current),&cancel,fetch)&&!requests);
        puts("Artwork cold restart: persisted index reconstructed; cached image returned without a network request.");return 0;
    }
    mkdir("ux0:data",0700);mkdir("ux0:data/VitaMediaDeck",0700);
    char path[192],saved_path[192];volatile int cancel=0;
    assert(!vt_artwork_cache_get("https://example.org/a?private-token",path,sizeof(path),&cancel,fetch));
    strcpy(saved_path,path);assert(!strstr(path,"private"));unsigned before=requests;
    assert(!vt_artwork_cache_get("https://example.org/a?private-token",path,sizeof(path),&cancel,fetch)&&requests==before);
    assert(!strcmp(saved_path,path));
    FILE *f=fopen(path,"wb");assert(f);fputs("bad",f);fclose(f);
    assert(!vt_artwork_cache_get("https://example.org/a?private-token",path,sizeof(path),&cancel,fetch)&&requests==before+1);
    for(int i=0;i<140;i++){char url[96];snprintf(url,sizeof(url),"https://example.org/small/%d",i);assert(!vt_artwork_cache_get(url,path,sizeof(path),&cancel,fetch));}bounds();
    assert(!vt_artwork_cache_peek("https://example.org/a?private-token",path,sizeof(path))&&!access(path,F_OK));
    response_size=2*1024*1024;
    for(int i=0;i<140;i++){char url[96];snprintf(url,sizeof(url),"https://example.org/large/%d",i);assert(!vt_artwork_cache_get(url,path,sizeof(path),&cancel,fetch));}bounds();
    response_size++;assert(vt_artwork_cache_get("https://example.org/oversized",path,sizeof(path),&cancel,fetch)<0);
    bad=1;response_size=64;assert(vt_artwork_cache_get("https://example.org/bad",path,sizeof(path),&cancel,fetch)<0);
    cancel=1;before=requests;assert(vt_artwork_cache_get("https://example.org/cancel",path,sizeof(path),&cancel,fetch)<0&&requests==before);
    cancel=bad=0;response_size=64;
    const char *refresh_url="https://example.org/refresh?secret-token";
    assert(!vt_artwork_cache_get(refresh_url,path,sizeof(path),&cancel,fetch));strcpy(saved_path,path);
    before=requests;char immediate[192]="";assert(!vt_artwork_cache_peek(refresh_url,immediate,sizeof(immediate))&&!strcmp(immediate,path));
    assert(!vt_artwork_cache_due(refresh_url)&&requests==before);
    clock_seconds+=24*60*60+1;assert(vt_artwork_cache_due(refresh_url));
    bad=1;assert(!vt_artwork_cache_get(refresh_url,path,sizeof(path),&cancel,fetch)&&!strcmp(path,saved_path));
    assert(!vt_artwork_cache_peek(refresh_url,immediate,sizeof(immediate))&&!strcmp(immediate,saved_path));
    assert(!vt_artwork_cache_due(refresh_url));clock_seconds+=301;bad=0;
    assert(!vt_artwork_cache_get(refresh_url,path,sizeof(path),&cancel,fetch)&&strcmp(path,saved_path));
    assert(access(saved_path,F_OK)&&!access(path,F_OK));
    assert(!vt_artwork_cache_current(saved_path,immediate,sizeof(immediate))&&!strcmp(path,immediate));
    /* A busy publication must never stall the render-side lookup. */
    pthread_mutex_lock(&mutex);assert(vt_artwork_cache_peek(refresh_url,immediate,sizeof(immediate))<0);pthread_mutex_unlock(&mutex);
    pid_t child;char *restart[]={argv[0],"--restart",(char*)refresh_url,path,NULL};
    assert(!posix_spawn(&child,argv[0],NULL,NULL,restart,environ));int status=0;
    assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    VtIptvCatalog *list=calloc(1,sizeof(*list));assert(list);
    const char *m3u="#EXTM3U\n#EXTINF:-1 tvg-logo=\"logo.png\",Channel, with comma\n#EXTVLCOPT:http-user-agent=HbbTV/1.6.1\na.m3u8\n#EXTINF:3600,Movie\nmovie.mp4?token=private\n#EXTINF:-1,DASH\na.mpd\n";
    assert(!vt_iptv_catalog_parse(m3u,strlen(m3u),"https://example.org/list.m3u",list));
    assert(list->count==3&&!strcmp(list->items[0].logo,"https://example.org/logo.png"));
    assert(!strcmp(list->items[0].user_agent,"HbbTV/1.6.1")&&!list->items[1].user_agent[0]);
    assert(!vt_iptv_user_agent_valid("Agent\r\nCookie: injected"));
    assert(!strcmp(list->items[0].title,"Channel, with comma")&&list->items[1].mode==VT_IPTV_FILE&&list->items[2].mode==VT_IPTV_DASH);
    list->items[0].starred=1;assert(!vt_iptv_saved_set(&list->items[0]));assert(!vt_iptv_saved_set(&list->items[1]));
    assert(!vt_iptv_saved_load(list,0)&&list->count==2);assert(!vt_iptv_saved_load(list,1)&&list->count==1&&list->items[0].starred);
    assert(!strcmp(list->items[0].user_agent,"HbbTV/1.6.1"));
    list->items[0].starred=0;assert(!vt_iptv_saved_set(&list->items[0]));assert(!vt_iptv_saved_load(list,1)&&!list->count);
    f=fopen("ux0:data/VitaMediaDeck/iptv-saved.json","wb");assert(f);fputs("broken",f);fclose(f);
    VtIptvChannel item={.url="https://example.org/new.m3u",.mode=VT_IPTV_LIST};assert(vt_iptv_saved_set(&item)<0);
    f=fopen("ux0:data/VitaMediaDeck/iptv-saved.json","rb");char corrupt[8]={0};fread(corrupt,1,6,f);fclose(f);assert(!strcmp(corrupt,"broken"));
    if(argc>1) {
        FILE *fixture=fopen(argv[1],"rb");assert(fixture);char *body=malloc(VT_IPTV_PLAYLIST_LIMIT);assert(body);
        size_t size=fread(body,1,VT_IPTV_PLAYLIST_LIMIT,fixture);assert(feof(fixture));fclose(fixture);
        assert(!vt_iptv_catalog_parse(body,size,"https://raw.githubusercontent.com/Tundrak/IPTV-Italia/main/iptvita.m3u",list));
        assert(!strcmp(list->items[0].user_agent,"HbbTV/1.6.1"));
        unsigned dash=0,logos=0;for(unsigned i=0;i<list->count;i++){dash+=list->items[i].mode==VT_IPTV_DASH;logos+=!!list->items[i].logo[0];}
        printf("Supplied IPTV-Italia playlist: %u entries, %u DASH entries, %u supplied logos\n",list->count,dash,logos);free(body);
    }
    free(list);
    bad=0;response_size=64;cancel=0;
    assert(!vt_artwork_cache_get("ux0:TV/local-icon.png",path,sizeof(path),&cancel,fetch));before=requests;
    char local_cached[192];assert(!vt_artwork_cache_peek("ux0:TV/local-icon.png",local_cached,sizeof(local_cached))&&!strcmp(path,local_cached));
    assert(!vt_artwork_cache_get("ux0:TV/local-icon.png",path,sizeof(path),&cancel,fetch)&&requests==before);
    for(int cols=1;cols<=3;cols++)for(int rows=1;rows<=5;rows++)for(int selected=0;selected<256;selected++) {
        int top=vt_catalog_top(selected,selected>100?200:0,cols,rows);
        assert(top>=0&&top%cols==0&&selected>=top&&selected<top+cols*rows);
        assert(UI_CONTENT_X+cols*vt_catalog_width(cols)+(cols-1)*UI_CONTENT_GAP<=UI_CONTENT_RIGHT);
    }
    puts("Artwork cache, IPTV import/persistence/stars/corruption and catalogue layout checks passed");
}

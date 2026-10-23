#include "media/podcast_metadata.h"
#include <assert.h>
#include <dirent.h>
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "psp2/io/stat.h"
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoRead(int f,void *p,unsigned n){return (int)read(f,p,n);}
int sceIoWrite(int f,const void *p,unsigned n){return (int)write(f,p,n);}
SceOff sceIoLseek(int f,SceOff o,int w){return lseek(f,o,w);}
int sceIoClose(int f){return close(f);}
int sceIoSyncByFd(int f,int flags){(void)flags;return fsync(f);}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRename(const char *a,const char *b){return rename(a,b);}
int sceIoMkdir(const char *p,int m){return mkdir(p,m);}
int sceIoGetstat(const char *p,SceIoStat *s){struct stat st;int r=stat(p,&st);if(!r)s->st_size=st.st_size;return r;}
static void put(const char *p,const void *data,size_t size) {
    FILE *f=fopen(p,"wb");CHECK(f);CHECK(fwrite(data,1,size,f)==size);CHECK(!fclose(f));
}
int main(void) {
    /* All file mutations are confined to a fresh fixture, never real app data. */
    char root[]="/tmp/vmd-offline-podcast.XXXXXX";CHECK(mkdtemp(root));CHECK(!chdir(root));
    CHECK(!mkdir("download",0700));CHECK(!mkdir("moved",0700));
    VtPodcastMetadata input={.title="Episode 日本語 & title",.author="Author",
        .show="My podcast",.description="Description with \"quotes\"\nand lines",.duration_ms=3723000,.published_date="2026-09-07"};
    VtMusicMetadata out;volatile int cancel=0;
    const unsigned char png[]="\x89PNG\r\n\x1a\n12345678";
    put("cache.png",png,sizeof(png));put("download/Server name (2).mp3","audio",5);
    CHECK(!vt_podcast_metadata_save("download/Server name (2).mp3",&input,"cache.png",&cancel));
    CHECK(!vt_podcast_destination_available("download/Server name (2).mp3"));
    CHECK(vt_podcast_destination_available("download/1_Server name (2).mp3"));
    CHECK(!unlink("cache.png")); /* Simulates eviction/offline restart. */
    CHECK(!vt_music_metadata_load("download/Server name (2).mp3",&out));
    CHECK(!strcmp(out.title,input.title)&&!strcmp(out.artist,input.author)&&!strcmp(out.album,input.show));
    CHECK(out.duration_ms==input.duration_ms);
    CHECK(!strcmp(out.artwork_path,"download/.Server name (2).mp3.podcast.png"));
    FILE *f=fopen(out.artwork_path,"rb");CHECK(f);unsigned char bytes[64];
    CHECK(fread(bytes,1,sizeof(bytes),f)==sizeof(png)&&!memcmp(bytes,png,sizeof(png)));fclose(f);
    json_t *json=json_load_file("download/.Server name (2).mp3.podcast.json",0,NULL);CHECK(json);
    CHECK(!strcmp(json_string_value(json_object_get(json,"description")),input.description));
    CHECK(!strcmp(json_string_value(json_object_get(json,"published_date")),input.published_date));json_decref(json);
    CHECK(vt_podcast_metadata_save("download/Server name (2).mp3",&input,"",&cancel)<0);
    CHECK(!rename("download/Server name (2).mp3","moved/new.mp3"));
    put("moved/.new.mp3.podcast.json","unrelated",9);
    CHECK(vt_podcast_companions_move("download/Server name (2).mp3","moved/new.mp3")<0);
    CHECK(!vt_podcast_metadata_load("download/Server name (2).mp3",&out));
    CHECK(!unlink("moved/.new.mp3.podcast.json"));
    CHECK(!vt_podcast_companions_move("download/Server name (2).mp3","moved/new.mp3"));
    CHECK(!vt_music_metadata_load("moved/new.mp3",&out));CHECK(!access(out.artwork_path,F_OK));
    CHECK(!strcmp(out.artwork_path,"moved/.new.mp3.podcast.png"));
    CHECK(!unlink("moved/new.mp3"));CHECK(!vt_podcast_companions_remove("moved/new.mp3"));
    CHECK(access(out.artwork_path,F_OK));
    CHECK(vt_podcast_destination_available("moved/new.mp3"));
    /* No cover is normal; failed/cancelled artwork keeps the audio + metadata. */
    put("plain.mp3","audio",5);CHECK(!vt_podcast_metadata_save("plain.mp3",&input,"",&cancel));
    CHECK(!vt_music_metadata_load("plain.mp3",&out)&&!out.artwork_path[0]);
    put("missing.mp3","audio",5);CHECK(vt_podcast_metadata_save("missing.mp3",&input,"gone.png",&cancel)==1);
    CHECK(!vt_music_metadata_load("missing.mp3",&out)&&!out.artwork_path[0]);
    put("cancel.mp3","audio",5);cancel=1;
    CHECK(vt_podcast_metadata_save("cancel.mp3",&input,"gone.png",&cancel)==1);
    CHECK(!vt_music_metadata_load("cancel.mp3",&out));cancel=0;
    CHECK(vt_podcast_metadata_save("no-audio.mp3",&input,"",&cancel)<0);
    /* Failed metadata publication cleans up the new cover, not the audio or
     * a pre-existing temp belonging to another operation. */
    put("blocked.mp3","audio",5);put("cache.png",png,sizeof(png));
    CHECK(!mkdir(".blocked.mp3.podcast.json.tmp",0700));
    CHECK(vt_podcast_metadata_save("blocked.mp3",&input,"cache.png",&cancel)<0);
    CHECK(!access("blocked.mp3",F_OK)&&access(".blocked.mp3.podcast.png",F_OK));
    CHECK(!access(".blocked.mp3.podcast.json.tmp",F_OK));
    put("bad-cover.mp3","audio",5);put("invalid.png","not an image",12);
    CHECK(vt_podcast_metadata_save("bad-cover.mp3",&input,"invalid.png",&cancel)==1);
    CHECK(!vt_music_metadata_load("bad-cover.mp3",&out)&&!out.artwork_path[0]);
    put("huge.mp3","audio",5);
    f=fopen("huge.png","wb");CHECK(f);CHECK(fwrite(png,1,sizeof(png),f)==sizeof(png));
    CHECK(!ftruncate(fileno(f),2u*1024u*1024u+1));CHECK(!fclose(f));
    CHECK(vt_podcast_metadata_save("huge.mp3",&input,"huge.png",&cancel)==1);
    /* Existing audio metadata stays backwards compatible. */
    VtMusicMetadata legacy={.title="Local song",.artist="Artist",.album="Album",.duration_ms=1000};
    put("song.mp3","audio",5);CHECK(!vt_music_metadata_save("song.mp3",&legacy));
    CHECK(!vt_music_metadata_load("song.mp3",&out));CHECK(!memcmp(&legacy,&out,sizeof(out)));
    /* JSON cannot inject an absolute/traversing artwork path. */
    const char *bad="{\"version\":1,\"cover\":\"../../private.png\",\"duration_ms\":-1}";
    put(".hostile.mp3.podcast.json",bad,strlen(bad));
    CHECK(!vt_podcast_metadata_load("hostile.mp3",&out)&&!out.artwork_path[0]&&!out.duration_ms);
    put(".bad.mp3.podcast.json","{bad",4);CHECK(vt_podcast_metadata_load("bad.mp3",&out)<0);
    /* Every cached raster format gets its actual format, not the URL suffix. */
    const unsigned char *headers[]={ (unsigned char *)"\xff\xd8\xff" ,png,
        (unsigned char *)"RIFF1234WEBP",(unsigned char *)"GIF89a",(unsigned char *)"BM"};
    const size_t sizes[]={3,sizeof(png),12,6,2};
    const char *ext[]={"jpg","png","webp","gif","bmp"};
    for(int i=0;i<5;i++) {
        char media[64],expected[128];snprintf(media,sizeof(media),"format%d.mp3",i);
        put(media,"audio",5);put("cached.data",headers[i],sizes[i]);
        CHECK(!vt_podcast_metadata_save(media,&input,"cached.data",&cancel));
        CHECK(!vt_music_metadata_load(media,&out));
        snprintf(expected,sizeof(expected),".%s.podcast.%s",media,ext[i]);CHECK(!strcmp(out.artwork_path,expected));
    }
    printf("Offline podcast: %u checks passed; fixture %s\n",checks,root);
    return 0;
}

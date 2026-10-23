#include "media/podcast_metadata.h"
#include <errno.h>
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PATH_CAP 1024
#define COVER_LIMIT (2u * 1024u * 1024u)
static const char *formats[] = {"jpg", "png", "webp", "gif", "bmp"};
static int stopped(volatile int *cancel) { return cancel && *cancel; }
static int companion(const char *media, const char *suffix, char out[PATH_CAP]) {
    if (!media || !*media) return -1;
    const char *name = strrchr(media, '/');
    if (!name) name = strrchr(media, ':');
    name = name ? name + 1 : media;
    if (!*name) return -1;
    int n = snprintf(out, PATH_CAP, "%.*s.%s.podcast.%s",
        (int)(name - media), media, name, suffix);
    return n > 0 && n < PATH_CAP ? 0 : -1;
}
static int format_index(const char *name) {
    for (unsigned i=0; i<sizeof(formats)/sizeof(*formats); i++)
        if (name && !strcmp(name, formats[i])) return (int)i;
    return -1;
}
int vt_podcast_destination_available(const char *media) {
    for(int i=-1;i<(int)(sizeof(formats)/sizeof(*formats));i++) {
        char path[PATH_CAP],temp[PATH_CAP+8];struct stat st;
        if(companion(media,i<0?"json":formats[i],path))return 0;
        if(!lstat(path,&st) || errno!=ENOENT)return 0;
        snprintf(temp,sizeof(temp),"%s.tmp",path);
        if(!lstat(temp,&st) || errno!=ENOENT)return 0;
    }
    return 1;
}
static int image_type(const unsigned char *p, size_t n) {
    if (n>=3 && p[0]==255 && p[1]==216 && p[2]==255) return 0;
    if (n>=8 && !memcmp(p,"\x89PNG\r\n\x1a\n",8)) return 1;
    if (n>=12 && !memcmp(p,"RIFF",4) && !memcmp(p+8,"WEBP",4)) return 2;
    if (n>=6 && (!memcmp(p,"GIF87a",6) || !memcmp(p,"GIF89a",6))) return 3;
    return n>=2 && !memcmp(p,"BM",2) ? 4 : -1;
}
/* Publish only complete files. Never overwrite a pre-existing companion. */
static int publish(FILE *f, const char *temp, const char *path, int ok) {
    if (f) {
        if (fflush(f) || fsync(fileno(f))) ok=0;
        if (fclose(f)) ok=0;
    } else ok=0;
    struct stat st;
    if (!lstat(path,&st) || errno!=ENOENT) ok=0;
    if (ok && !rename(temp,path)) return 0;
    remove(temp); return -1;
}
static int copy_cover(const char *media, const char *source, volatile int *cancel,
                      char path[PATH_CAP]) {
    path[0]=0;
    if (!source || !*source || stopped(cancel)) return -1;
    FILE *in=fopen(source,"rb"); if (!in) return -1;
    unsigned char head[12]; size_t n=fread(head,1,sizeof(head),in);
    int type=image_type(head,n);
    struct stat st;
    int valid=!fstat(fileno(in),&st) && S_ISREG(st.st_mode) && st.st_size>0 && st.st_size<=COVER_LIMIT;
    if (ferror(in) || !valid || type<0 || companion(media,formats[type],path) ||
        strlen(path)>=sizeof(((VtMusicMetadata *)0)->artwork_path)) {
        fclose(in);path[0]=0;return -1;
    }
    char temp[PATH_CAP+8];snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE *out=fopen(temp,"wbx");
    if (!out) {fclose(in);path[0]=0;return -1;}
    int ok=fwrite(head,1,n,out)==n; size_t total=n;
    unsigned char buffer[16384];
    while(ok && !stopped(cancel) && !feof(in) && (n=fread(buffer,1,sizeof(buffer),in))) {
        if(ferror(in) || n>COVER_LIMIT-total || fwrite(buffer,1,n,out)!=n)ok=0;
        else total+=n;
    }
    if(ferror(in) || total!=(size_t)st.st_size || stopped(cancel))ok=0;
    fclose(in);
    if(publish(out,temp,path,ok)) {path[0]=0;return -1;}
    return type;
}
static json_t *read_metadata(const char *media) {
    char path[PATH_CAP];struct stat st;
    if(companion(media,"json",path) || stat(path,&st) || !S_ISREG(st.st_mode) ||
       st.st_size<=0 || st.st_size>16384) return NULL;
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    char body[16385];size_t n=fread(body,1,sizeof(body),f);int failed=ferror(f);fclose(f);
    if(failed || n>16384)return NULL;
    json_t *root=json_loadb(body,n,JSON_REJECT_DUPLICATES,NULL);
    if(!json_is_object(root) || json_integer_value(json_object_get(root,"version"))!=1) {
        json_decref(root);return NULL;
    }
    return root;
}
static const char *string(json_t *root,const char *key) {
    const char *value=json_string_value(json_object_get(root,key));return value?value:"";
}
int vt_podcast_metadata_save(const char *media,const VtPodcastMetadata *metadata,
                             const char *cover,volatile int *cancel) {
    char path[PATH_CAP],art[PATH_CAP];struct stat st;
    if(!metadata || companion(media,"json",path) || stat(media,&st) || !S_ISREG(st.st_mode))return -1;
    if(!lstat(path,&st) || errno!=ENOENT)return -1;
    int type=copy_cover(media,cover,cancel,art);
    json_t *root=json_pack("{s:i,s:s,s:s,s:s,s:s,s:s}","version",1,
        "title",metadata->title,"author",metadata->author,"show",metadata->show,
        "description",metadata->description,"cover",type>=0?formats[type]:"");
    if(root)json_object_set_new(root,"duration_ms",json_integer(
        metadata->duration_ms<=2592000000ULL?(json_int_t)metadata->duration_ms:0));
    if(root)json_object_set_new(root,"published_date",json_string(metadata->published_date));
    char temp[PATH_CAP+8];snprintf(temp,sizeof(temp),"%s.tmp",path);
    FILE *f=root?fopen(temp,"wbx"):NULL;
    int ok=f && !json_dumpf(root,f,JSON_COMPACT|JSON_ENSURE_ASCII);
    json_decref(root);
    /* Do not remove a temp that this operation did not create. */
    if(!f || publish(f,temp,path,ok)) {
        if(type>=0)remove(art);
        return -1;
    }
    return cover && *cover && type<0 ? 1 : 0;
}
int vt_podcast_metadata_load(const char *media,VtMusicMetadata *metadata) {
    if(!metadata)return -1;
    json_t *root=read_metadata(media);if(!root)return -1;
    memset(metadata,0,sizeof(*metadata));
    snprintf(metadata->title,sizeof(metadata->title),"%s",string(root,"title"));
    snprintf(metadata->artist,sizeof(metadata->artist),"%s",string(root,"author"));
    snprintf(metadata->album,sizeof(metadata->album),"%s",string(root,"show"));
    json_int_t duration=json_integer_value(json_object_get(root,"duration_ms"));
    if(duration>0 && duration<=2592000000LL)metadata->duration_ms=(uint64_t)duration;
    const char *format=string(root,"cover");char path[PATH_CAP];struct stat st;
    if(format_index(format)>=0 && !companion(media,format,path) &&
       strlen(path)<sizeof(metadata->artwork_path) && !stat(path,&st) &&
       S_ISREG(st.st_mode) && st.st_size>0 && st.st_size<=COVER_LIMIT)
        memcpy(metadata->artwork_path,path,strlen(path)+1);
    json_decref(root);return 0;
}
static int owned_paths(const char *media,char paths[2][PATH_CAP]) {
    json_t *root=read_metadata(media);if(!root)return 0;
    int count=0;
    if(!companion(media,"json",paths[count]))count++;
    const char *format=string(root,"cover");struct stat st;
    if(format_index(format)>=0 && !companion(media,format,paths[count]) &&
       !lstat(paths[count],&st) && S_ISREG(st.st_mode))count++;
    json_decref(root);return count;
}
int vt_podcast_companions_move(const char *from,const char *to) {
    char old[2][PATH_CAP],next[2][PATH_CAP];int count=owned_paths(from,old);
    for(int i=0;i<count;i++) {
        const char *suffix=strrchr(old[i],'.');struct stat st;
        if(!suffix || companion(to,suffix+1,next[i]) || !lstat(next[i],&st) || errno!=ENOENT)return -1;
    }
    for(int i=0;i<count;i++)if(rename(old[i],next[i])) {
        for(int j=i-1;j>=0;j--)rename(next[j],old[j]);
        return -1;
    }
    return 0;
}
int vt_podcast_companions_remove(const char *media) {
    char paths[2][PATH_CAP];int count=owned_paths(media,paths);
    for(int i=count-1;i>=0;i--)if(remove(paths[i]))return -1;
    return 0;
}

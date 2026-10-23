#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
int sceIoSync(const char *,int);
#include "../../src/settings/preferences.c"
static int fail_write;
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoRead(int f,void *p,unsigned n){return (int)read(f,p,n);}
int sceIoWrite(int f,const void *p,unsigned n){return fail_write?-1:(int)write(f,p,n);}
int sceIoClose(int f){return close(f);}
int sceIoSyncByFd(int f,int flags){(void)flags;return fsync(f);}
int sceIoSync(const char *p,int flags){(void)p;(void)flags;return 0;}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRename(const char *a,const char *b){return rename(a,b);}
int sceIoMkdir(const char *p,int m){return mkdir(p,m);}
int sceIoGetstat(const char *p,SceIoStat *s){struct stat st;int r=stat(p,&st);if(!r)s->st_size=st.st_size;return r;}
int main(void){
 char dir[]="/tmp/vmd-crossfade-preferences.XXXXXX";assert(mkdtemp(dir)&&!chdir(dir));
 assert(!mkdir("ux0:data",0700));vt_preferences_init();assert(vt_preferences_audio_layout()==0);
 /* Keep every pre-existing bit, including decoder, subtitle, theme and flatten. */
 uint32_t playback=0xd5a586b3U,subtitle=0xaaaaaaaaU & ~VT_AUDIO_LAYOUT_MASK;
 assert(!persist_subtitle_pair(playback,subtitle));
 for(int layout=0;layout<3;layout++){
  assert(!vt_preferences_set_audio_layout(layout));assert(g_playback_flags==playback);
  assert((g_subtitle_flags&~VT_AUDIO_LAYOUT_MASK)==subtitle);
  g_loaded=0;vt_preferences_init();assert(vt_preferences_audio_layout()==layout);
  assert(g_playback_flags==playback&&(g_subtitle_flags&~VT_AUDIO_LAYOUT_MASK)==subtitle);
 }
 assert(vt_preferences_set_audio_layout(-1)<0&&vt_preferences_set_audio_layout(3)<0);
 fail_write=1;assert(vt_preferences_set_audio_layout(0)<0);assert(vt_preferences_audio_layout()==2);
 fail_write=0;g_loaded=0;vt_preferences_init();assert(vt_preferences_audio_layout()==2);
 /* Real V1/V2 readers migrate with centered artwork and unchanged flags. */
 VtPreferencesDiskV1 v1={.header={.version=1,.record_size=sizeof(v1)},.default_quality=720};
 memcpy(v1.header.magic,g_preferences_magic,8);v1.reserved[1]=playback;v1.header.checksum=disk_v1_checksum(&v1);
 FILE *f=fopen("legacy1","wb");assert(f&&fwrite(&v1,1,sizeof(v1),f)==sizeof(v1));assert(!fclose(f));
 int q,rate,lang;uint32_t flags,extra;
 assert(!load_path("legacy1",&q,&rate,&lang,&flags,&extra)&&flags==playback&&!extra);
 VtPreferencesDiskV2 v2={.header={.version=2,.record_size=sizeof(v2)},.default_quality=720,.default_frame_rate=30};
 memcpy(v2.header.magic,g_preferences_magic,8);v2.reserved[1]=playback;v2.header.checksum=disk_v2_checksum(&v2);
 f=fopen("legacy2","wb");assert(f&&fwrite(&v2,1,sizeof(v2),f)==sizeof(v2));assert(!fclose(f));
 assert(!load_path("legacy2",&q,&rate,&lang,&flags,&extra)&&flags==playback&&!extra);
 /* Interface selection survives reload and leaves every other setting intact. */
 assert(!persist_subtitle_pair(playback,0));
 assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY);
 uint32_t keep=0xb5ac93aaU & ~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH);
 assert(!persist_subtitle_pair(playback,keep));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));
  assert(g_playback_flags==playback&&(g_subtitle_flags&~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH))==keep);
  g_loaded=0;vt_preferences_init();assert(vt_preferences_interface_theme()==theme);
 }
 assert(vt_preferences_set_interface_theme(-1)<0&&vt_preferences_set_interface_theme(VT_UI_THEME_COUNT)<0);
 fail_write=1;assert(vt_preferences_set_interface_theme(0)<0);
 assert(vt_preferences_interface_theme()==VT_UI_THEME_MOSAIC);
 fail_write=0;g_loaded=0;vt_preferences_init();assert(vt_preferences_interface_theme()==VT_UI_THEME_MOSAIC);
 /* Each theme remembers its local/remote browser choice without changing Legacy. */
 assert(!persist_subtitle_pair(0,0));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));
  assert(vt_preferences_file_browser_grid()==(theme<2||theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_MOSAIC));
  assert(!vt_preferences_set_file_browser_grid(!(theme<2||theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_MOSAIC)));
 }
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));g_loaded=0;vt_preferences_init();
  assert(vt_preferences_file_browser_grid()==!(theme<2||theme==VT_UI_THEME_APERTURE||theme==VT_UI_THEME_MOSAIC));
 }
 /* Old V3 IDs retain their exact interpretation when the high bit is zero. */
 for(int old=0;old<4;old++){
  assert(!persist_subtitle_pair(playback,(uint32_t)old<<20));g_loaded=0;vt_preferences_init();
  assert(vt_preferences_interface_theme()==old);
 }
 assert(!persist_subtitle_pair(playback,VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH));
 g_loaded=0;vt_preferences_init();assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY);
 puts("Per-theme browser defaults and independent saved grid/list choices passed");
 puts("Interface themes: default Legacy, all choices persisted, unrelated bits and failed-write rollback passed");
 puts("Audio layout settings: persistence, unrelated flags, invalid values, write failure and V1/V2 migration passed");
}

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
int sceIoSync(const char *,int);
#include "../../src/settings/preferences.c"
static int fail_write, write_calls;
int sceIoOpen(const char *p,int f,int m){return open(p,f,m);}
int sceIoRead(int f,void *p,unsigned n){return (int)read(f,p,n);}
int sceIoWrite(int f,const void *p,unsigned n){write_calls++;return fail_write?-1:(int)write(f,p,n);}
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
 assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY_VITA);
 /* A saved Legacy selection (also crash recovery) overrides the new default. */
 assert(!vt_preferences_set_interface_theme(VT_UI_THEME_LEGACY));
 g_loaded=0;vt_preferences_init();assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY);
 /* Keep every pre-existing bit, including decoder, subtitle, theme and flatten. */
 uint32_t playback=0xd5a586b3U,subtitle=(0xaaaaaaaaU & ~VT_AUDIO_LAYOUT_MASK) | VT_VMD_DEFAULT_APPLIED;
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
 /* IPTV layout persists independently of CPU/theme/playback choices and rolls back failed writes. */
 fail_write=0;uint32_t previous_clocks=g_clock_flags;
 for(int split=0;split<2;split++){
  assert(!vt_preferences_set_iptv_split(split));assert(vt_preferences_iptv_split()==split);
  assert((g_clock_flags&~(1u<<4))==(previous_clocks&~(1u<<4)));
  assert(g_playback_flags==playback);
  g_loaded=0;vt_preferences_init();assert(vt_preferences_iptv_split()==split);
 }
 fail_write=1;assert(vt_preferences_set_iptv_split(0)<0&&vt_preferences_iptv_split()==1);
 fail_write=0;assert(!vt_preferences_set_iptv_split(0));
 uint32_t before_radio=g_clock_flags;
 for(int rows=1;rows<=2;rows++){
  assert(!vt_preferences_set_radio_rows(rows)&&vt_preferences_radio_rows()==rows);
  assert((g_clock_flags&~(1u<<5))==(before_radio&~(1u<<5)));
  g_loaded=0;vt_preferences_init();assert(vt_preferences_radio_rows()==rows);
 }
 fail_write=1;assert(vt_preferences_set_radio_rows(1)<0&&vt_preferences_radio_rows()==2);
 fail_write=0;assert(vt_preferences_set_radio_rows(3)<0&&vt_preferences_radio_rows()==2);
 assert(!vt_preferences_set_radio_rows(1));
 /* Name sorting survives a fresh settings load and preserves every other bit. */
 uint32_t before_sort=g_clock_flags;
 for(int sort=0;sort<2;sort++){
  assert(!vt_preferences_set_iptv_name_sort(sort)&&vt_preferences_iptv_name_sort()==sort);
  assert((g_clock_flags&~(1u<<6))==(before_sort&~(1u<<6)));
  assert(g_playback_flags==playback);
  g_loaded=0;vt_preferences_init();assert(vt_preferences_iptv_name_sort()==sort);
 }
 fail_write=1;assert(vt_preferences_set_iptv_name_sort(0)<0&&vt_preferences_iptv_name_sort()==1);
 fail_write=0;assert(!vt_preferences_set_iptv_name_sort(0));
 printf("IPTV name order: persisted across fresh settings loads; CPU/layout/radio/theme bits retained and failed writes rolled back.\n");
 /* Every restored language keeps its historic preference ID and survives reload. */
 for(int language=0;language<VT_LANGUAGE_COUNT;language++){
  assert(!vt_preferences_set_language(language));g_loaded=0;vt_preferences_init();
  assert(vt_preferences_language()==language);
 }
 assert(vt_preferences_set_language(VT_LANGUAGE_COUNT)<0);
 assert(!vt_preferences_set_language(VT_LANGUAGE_AUTO));
 /* Real V1/V2 readers migrate with centered artwork and unchanged flags. */
 VtPreferencesDiskV1 v1={.header={.version=1,.record_size=sizeof(v1)},.default_quality=720};
 memcpy(v1.header.magic,g_preferences_magic,8);v1.reserved[1]=playback;v1.header.checksum=disk_v1_checksum(&v1);
 FILE *f=fopen("legacy1","wb");assert(f&&fwrite(&v1,1,sizeof(v1),f)==sizeof(v1));assert(!fclose(f));
 int q,rate,lang;uint32_t flags,extra,clocks;
 assert(!load_path("legacy1",&q,&rate,&lang,&flags,&extra,&clocks)&&flags==playback&&!extra&&!clocks);
 VtPreferencesDiskV2 v2={.header={.version=2,.record_size=sizeof(v2)},.default_quality=720,.default_frame_rate=30};
 memcpy(v2.header.magic,g_preferences_magic,8);v2.reserved[1]=playback;v2.header.checksum=disk_v2_checksum(&v2);
 f=fopen("legacy2","wb");assert(f&&fwrite(&v2,1,sizeof(v2),f)==sizeof(v2));assert(!fclose(f));
 assert(!load_path("legacy2",&q,&rate,&lang,&flags,&extra,&clocks)&&flags==playback&&!extra&&!clocks);
 /* Interface selection survives reload and leaves every other setting intact. */
 assert(!persist_subtitle_pair(playback,0));
 assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY);
 uint32_t keep=(0xb5ac93aaU & ~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH)) | VT_VMD_DEFAULT_APPLIED;
 assert(!persist_subtitle_pair(playback,keep));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));
  assert(g_playback_flags==playback&&(g_subtitle_flags&~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH))==keep);
  g_loaded=0;vt_preferences_init();assert(vt_preferences_interface_theme()==theme);
 }
 assert(vt_preferences_set_interface_theme(-1)<0&&vt_preferences_set_interface_theme(VT_UI_THEME_COUNT)<0);
 fail_write=1;assert(vt_preferences_set_interface_theme(0)<0);
 assert(vt_preferences_interface_theme() == VT_UI_THEME_COUNT - 1);
 fail_write = 0;
 g_loaded = 0;
 vt_preferences_init();
 assert(vt_preferences_interface_theme() == VT_UI_THEME_COUNT - 1);
 /* Each theme remembers its local/remote browser choice without changing Legacy. */
 assert(!persist_subtitle_pair(0,0));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));
  assert(vt_preferences_file_browser_grid() ==
         (theme < 2 || theme == VT_UI_THEME_APERTURE || theme == VT_UI_THEME_MOSAIC ||
          theme == VT_UI_THEME_LEGACY_VITA));
  assert(!vt_preferences_set_file_browser_grid(!(theme < 2 || theme == VT_UI_THEME_APERTURE ||
                                                 theme == VT_UI_THEME_MOSAIC ||
                                                 theme == VT_UI_THEME_LEGACY_VITA)));
 }
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  assert(!vt_preferences_set_interface_theme(theme));g_loaded=0;vt_preferences_init();
  assert(vt_preferences_file_browser_grid() ==
         !(theme < 2 || theme == VT_UI_THEME_APERTURE || theme == VT_UI_THEME_MOSAIC ||
           theme == VT_UI_THEME_LEGACY_VITA));
 }
 /* Old custom V3 IDs remain selected; only the former Legacy default migrates. */
 for(int old=0;old<4;old++){
  assert(!persist_subtitle_pair(playback,(uint32_t)old<<20));g_loaded=0;vt_preferences_init();
  assert(vt_preferences_interface_theme()==(old ? old : VT_UI_THEME_LEGACY_VITA));
 }
 assert(!persist_subtitle_pair(playback,VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH));
 g_loaded = 0;
 vt_preferences_init();
 assert(vt_preferences_interface_theme() == VT_UI_THEME_LEGACY_VITA);
 /* Upgrade all unmarked V3 themes, preserving custom choices and all payload
  * fields. A committed marker guarantees no startup writes after migration. */
 uint32_t non_theme=0x19aac53aU & ~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH|VT_VMD_DEFAULT_APPLIED);
 for (int old=0;old<VT_UI_THEME_COUNT;old++) {
  uint32_t extra=non_theme|((unsigned)(old&3)<<VT_INTERFACE_THEME_SHIFT)|((old&4)?VT_INTERFACE_THEME_HIGH:0);
  assert(!persist_record_all(480,25,VT_LANGUAGE_IT,playback,extra));
  g_loaded=0;assert(!vt_preferences_init());
  assert(vt_preferences_interface_theme()==(old?old:VT_UI_THEME_LEGACY_VITA));
  assert(vt_preferences_default_quality()==480&&vt_preferences_default_frame_rate()==25&&vt_preferences_language()==VT_LANGUAGE_IT);
  assert(g_playback_flags==playback&&(g_subtitle_flags&~(VT_INTERFACE_THEME_MASK|VT_INTERFACE_THEME_HIGH|VT_VMD_DEFAULT_APPLIED))==non_theme);
  assert(g_subtitle_flags&VT_VMD_DEFAULT_APPLIED);
  int before=write_calls;g_loaded=0;assert(!vt_preferences_init()&&write_calls==before);
 }
 /* The native error/startup recovery setter uses this same explicit choice.
  * Legacy must stay Legacy after any later restart, without write/retry loops. */
 assert(!vt_preferences_set_interface_theme(VT_UI_THEME_LEGACY));
 int before=write_calls;
 for(int n=0;n<3;n++){g_loaded=0;assert(!vt_preferences_init()&&vt_preferences_interface_theme()==VT_UI_THEME_LEGACY&&write_calls==before);}
 /* An upgrade write failure does not commit a marker or change the loaded
  * Legacy preference. The intact old record can migrate on a later launch. */
 assert(!persist_record_all(480,25,VT_LANGUAGE_IT,playback,non_theme));
 fail_write=1;g_loaded=0;assert(vt_preferences_init()<0);
 assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY&&g_subtitle_flags==non_theme);
 fail_write=0;g_loaded=0;assert(!vt_preferences_init()&&vt_preferences_interface_theme()==VT_UI_THEME_LEGACY_VITA);
 /* V1/V2 load paths also adopt the default without changing playback flags. */
 for(int version=1;version<=2;version++){
  f=fopen(VT_PREFERENCES_PATH,"wb");assert(f);
  if(version==1)assert(fwrite(&v1,1,sizeof(v1),f)==sizeof(v1));
  else assert(fwrite(&v2,1,sizeof(v2),f)==sizeof(v2));
  assert(!fclose(f));g_loaded=0;assert(!vt_preferences_init());
  assert(vt_preferences_interface_theme()==VT_UI_THEME_LEGACY_VITA&&g_playback_flags==playback&&vt_preferences_audio_layout()==0);
 }
 /* Recovery from the known backup path migrates with the same rules. */
 assert(!persist_record_all(480,25,VT_LANGUAGE_IT,playback,non_theme));
 assert(!rename(VT_PREFERENCES_PATH,VT_PREFERENCES_BACKUP));
 g_loaded=0;assert(!vt_preferences_init()&&vt_preferences_interface_theme()==VT_UI_THEME_LEGACY_VITA);
 /* Actual V3 migration retains all payload bits and supplies the new pair. */
 VtPreferencesDiskV3 v3={.header={.version=3,.record_size=sizeof(v3)},.default_quality=480,.default_frame_rate=25,.subtitle_flags=subtitle|VT_VMD_DEFAULT_APPLIED};
 memcpy(v3.header.magic,g_preferences_magic,8);v3.reserved[1]=playback;v3.header.checksum=disk_v3_checksum(&v3);
 f=fopen(VT_PREFERENCES_PATH,"wb");assert(f&&fwrite(&v3,1,sizeof(v3),f)==sizeof(v3));assert(!fclose(f));
 g_loaded=0;assert(!vt_preferences_init());assert(vt_preferences_decoder_cpu_mhz(1)==333&&vt_preferences_decoder_cpu_mhz(0)==444);
 assert(g_playback_flags==playback&&g_subtitle_flags==v3.subtitle_flags);
 for(int hw=0;hw<2;hw++)for(int mhz=222;mhz<=444;mhz+=111){
  int other=vt_preferences_decoder_cpu_mhz(!hw);
  assert(!vt_preferences_set_decoder_cpu_mhz(hw,mhz));
  g_loaded=0;assert(!vt_preferences_init());assert(vt_preferences_decoder_cpu_mhz(hw)==mhz&&vt_preferences_decoder_cpu_mhz(!hw)==other);
  assert(g_playback_flags==playback&&g_subtitle_flags==v3.subtitle_flags);
 }
 assert(!vt_preferences_set_decoder_cpu_mhz(1,333));assert(!vt_preferences_set_decoder_cpu_mhz(0,444));
 assert(vt_preferences_set_decoder_cpu_mhz(1,500)<0&&vt_preferences_set_decoder_cpu_mhz(2,333)<0);
 fail_write=1;assert(vt_preferences_set_decoder_cpu_mhz(0,222)<0&&vt_preferences_decoder_cpu_mhz(0)==444);
 fail_write=0;g_loaded=0;assert(!vt_preferences_init()&&vt_preferences_decoder_cpu_mhz(0)==444);
 /* Unrelated writes preserve the pair, and the known backup recovers V4 too. */
 assert(!vt_preferences_set_decoder_cpu_mhz(1,222));assert(!vt_preferences_set_default_quality(720));
 assert(!rename(VT_PREFERENCES_PATH,VT_PREFERENCES_BACKUP));g_loaded=0;
 assert(!vt_preferences_init()&&vt_preferences_decoder_cpu_mhz(1)==222&&vt_preferences_decoder_cpu_mhz(0)==444);
 puts("CPU pair V4: HW333/SW444 defaults, V1/V2/V3 migration, independent pairs, persistence, invalid values, failed-write rollback and V4 backup passed");
 puts("Per-theme browser defaults and independent saved grid/list choices passed");
 puts("Interface themes: default VMD, one-time Legacy upgrade, later choices/recovery and failed migration preserved, all choices persisted, unrelated bits and failed-write rollback passed");
 puts("Audio layout settings: persistence, unrelated flags, invalid values, write failure and V1/V2 migration passed");
}

#include "media/audio_volume.h"
#include <stdatomic.h>
static atomic_int g_volume_percent=100,g_muted;
int vt_audio_volume_level_percent(void){return atomic_load(&g_volume_percent);}
int vt_audio_volume_muted(void){return atomic_load(&g_muted);}
int vt_audio_volume_percent(void){return vt_audio_volume_muted()?0:vt_audio_volume_level_percent();}
void vt_audio_volume_set_percent(int percent){
 percent=vita_audio_volume_clamp(percent);
 atomic_store(&g_volume_percent,percent);atomic_store(&g_muted,0);
}
int vt_audio_volume_toggle_mute(void){return atomic_fetch_xor(&g_muted,1)^1;}

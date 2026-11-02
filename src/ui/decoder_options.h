#ifndef VMD_UI_DECODER_OPTIONS_H
#define VMD_UI_DECODER_OPTIONS_H
#include "i18n/i18n.h"
#include "ui/components.h"
static inline const char *ui_decoder_mode_label(int mode){return mode==1?"HW":mode==2?"SW":"AUTO";}
static inline const char *ui_decoder_formats(int audio,int mode){
 VtStringId id=audio?(mode==1?VT_STR_SETTINGS_AUDIO_FORMATS_HW:mode==2?VT_STR_SETTINGS_AUDIO_FORMATS_SW:VT_STR_SETTINGS_AUDIO_FORMATS_AUTO):
  mode==1?VT_STR_SETTINGS_VIDEO_FORMATS_HW:mode==2?VT_STR_SETTINGS_VIDEO_FORMATS_SW:VT_STR_SETTINGS_VIDEO_FORMATS_AUTO;
 return vt_i18n_str(id);
}
static inline void ui_audio_decoder_choices(UiContextAction choices[3]){
 for(int mode=0;mode<3;mode++)choices[mode]=(UiContextAction){ui_decoder_mode_label(mode),ui_decoder_formats(1,mode),mode,1};
}
#endif

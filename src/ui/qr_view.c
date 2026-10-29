#include "ui/qr_view.h"
#include <math.h>
#include <stdio.h>
#include <psp2/kernel/processmgr.h>
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/theme_scene.h"

static VtStringId qr_state_label(UiQrState state) {
 static const VtStringId labels[UI_QR_STATE_COUNT] = {
  VT_STR_NETWORK_QR_STARTING, VT_STR_NETWORK_QR_SEARCHING,
  VT_STR_NETWORK_QR_CANDIDATE, VT_STR_NETWORK_QR_DECODING,
  VT_STR_NETWORK_QR_INVALID_PROTOCOL, VT_STR_NETWORK_QR_INVALID_LENGTH,
  VT_STR_NETWORK_QR_RECOGNIZED
 };
 return labels[state >= 0 && state < UI_QR_STATE_COUNT ? state : UI_QR_STARTING];
}

/* No scanning beam obscures the camera pixels. Brackets, film perforations,
 * circuit joints and shelf edges live in the eight-pixel surrounding gutter. */
static void qr_preview_frame(unsigned color) {
 const float x=UI_QR_PREVIEW_X-4, y=UI_QR_PREVIEW_Y-4;
 const float size=UI_QR_PREVIEW_SIZE+8;
 int theme=ui_theme_id();
 for(int corner=0;corner<4;corner++) {
  float cx=x+(corner&1?size:0), cy=y+(corner&2?size:0);
  float sx=corner&1?-1:1, sy=corner&2?-1:1;
  float length=theme==VT_UI_THEME_MOSAIC?54:theme==VT_UI_THEME_DECK_90?18:30;
  vita2d_draw_line(cx,cy,cx+sx*length,cy,color);
  vita2d_draw_line(cx,cy,cx,cy+sy*length,color);
  if(theme==VT_UI_THEME_SIGNAL || theme==VT_UI_THEME_RELAY)
   vita2d_draw_line(cx+sx*8,cy,cx,cy+sy*8,color);
  if(theme==VT_UI_THEME_APERTURE)
   for(int i=1;i<=3;i++)vita2d_draw_rectangle(cx+sx*i*9-2,cy+(sy>0?-1:0),4,2,color);
 }
 float phase=vt_preferences_reduce_motion()?.5f:
   .5f-.5f*cosf((float)(sceKernelGetProcessTimeWide()%2400000ULL)*6.2831853f/2400000.f);
 vita2d_draw_rectangle(x+48+phase*(size-128),y,32,2,color);
}

void ui_qr_view_draw(vita2d_texture *preview,const UiQrStatus *status) {
 UiQrState state=status?status->state:UI_QR_STARTING;
 unsigned accent=state==UI_QR_RECOGNIZED?VT_THEME_SUCCESS:
  state==UI_QR_INVALID_PROTOCOL||state==UI_QR_INVALID_LENGTH?VT_THEME_WARNING:
  VT_THEME_SIGNAL_BRIGHT;
 ui_chrome_background(VT_THEME_BG,accent);
 ui_brand_draw_breadcrumbs(vt_i18n_str(VT_STR_NETWORK_SCAN_QR),NULL,
                          vt_i18n_str(VT_STR_NETWORK_QR_TITLE));
 ui_panel(UI_QR_PREVIEW_X,UI_QR_PREVIEW_Y,UI_QR_PREVIEW_SIZE,
          UI_QR_PREVIEW_SIZE,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
 if(preview) {
  vita2d_draw_texture_part_scale(preview,UI_QR_PREVIEW_X,UI_QR_PREVIEW_Y,
   UI_QR_CAMERA_CROP_X,0,UI_QR_CAMERA_SIZE,UI_QR_CAMERA_SIZE,
   (float)UI_QR_PREVIEW_SIZE/UI_QR_CAMERA_SIZE,
   (float)UI_QR_PREVIEW_SIZE/UI_QR_CAMERA_SIZE);
 } else {
  ui_text_block(46,264,388,28,2,vt_i18n_str(VT_STR_NETWORK_QR_STARTING),
                VT_THEME_TEXT,UI_FONT_BODY);
 }
 qr_preview_frame(accent);
 ui_panel(488,62,454,332,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
 vita2d_draw_rectangle(488,62,3,332,accent);
 ui_text_block(508,100,410,27,2,vt_i18n_str(VT_STR_NETWORK_QR_AIM),
               VT_THEME_TEXT,UI_FONT_BODY);
 ui_text_block(508,166,410,25,3,vt_i18n_str(qr_state_label(state)),accent,UI_FONT_BODY);
 ui_text_block(508,260,410,24,3,vt_i18n_str(VT_STR_NETWORK_QR_DETAIL),
               VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 ui_text_block(508,350,298,22,2,vt_i18n_str(VT_STR_NETWORK_QR_HTTPS),
               VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 ui_theme_scene_begin(UI_SCENE_QR,"qr");
 ui_theme_scene_model(UI_SCENE_QR,872,350,64,.72f,state!=UI_QR_RECOGNIZED);
 if(status) {
  char progress[128];
  snprintf(progress,sizeof(progress),vt_i18n_str(VT_STR_NETWORK_QR_FRAMES),
           status->submitted,status->analyzed);
  ui_text_line(488,426,454,progress,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
 }
 ui_action_button(UI_QR_CANCEL_X,UI_QR_CANCEL_Y,UI_QR_CANCEL_W,UI_QR_CANCEL_H,
  VT_THEME_SURFACE_RAISED,UI_PAD_CIRCLE,
  vt_i18n_str(VT_STR_NETWORK_CANCEL),0);
}

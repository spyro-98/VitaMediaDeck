#ifndef VMD_UI_QR_VIEW_H
#define VMD_UI_QR_VIEW_H
#include <vita2d.h>
/* Camera and decoder share this crop; decoration stays outside the live image. */
#define UI_QR_CAMERA_SIZE 480
#define UI_QR_CAMERA_CROP_X 80
#define UI_QR_PREVIEW_X 18
#define UI_QR_PREVIEW_Y 62
#define UI_QR_PREVIEW_SIZE 444
#define UI_QR_CANCEL_X 488
#define UI_QR_CANCEL_Y 454
#define UI_QR_CANCEL_W 454
#define UI_QR_CANCEL_H 52

typedef enum {
 UI_QR_STARTING, UI_QR_SEARCHING, UI_QR_CANDIDATE, UI_QR_DECODING,
 UI_QR_INVALID_PROTOCOL, UI_QR_INVALID_LENGTH, UI_QR_RECOGNIZED,
 UI_QR_STATE_COUNT
} UiQrState;
typedef struct {
 UiQrState state;
 unsigned submitted, analyzed;
} UiQrStatus;
/* Draw only: caller owns the GXM scene and the borrowed camera texture. */
void ui_qr_view_draw(vita2d_texture *preview, const UiQrStatus *status);
#endif

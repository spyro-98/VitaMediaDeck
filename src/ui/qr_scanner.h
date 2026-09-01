#ifndef VITAMEDIADECK_UI_QR_SCANNER_H
#define VITAMEDIADECK_UI_QR_SCANNER_H

#include <stddef.h>

/* Opens the rear camera and decodes a QR code containing an HTTP(S) resource
 * URL. Returns 1 on success, 0 when cancelled, and <0 on camera/decoder error. */
int ui_qr_scan_https_url(char *out, size_t out_size);

/* Describes the last camera or QR-decoder setup failure.  The returned text
 * remains valid until the next scan attempt. */
const char *ui_qr_scan_last_error(void);

#endif /* VITAMEDIADECK_UI_QR_SCANNER_H */

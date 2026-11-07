/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_QR_H
#define ZYPEROS_QR_H

/* A QR code, for the hotspot's panel: the phone's camera reads the network's
 * name and password off the screen (captive_wifi_qr gives the text).
 *
 * Byte mode, error correction level M (a sixth of the code can be lost: a
 * glare across the panel, a thumb on the corner), the smallest version from
 * 1 to QR_VERSION_MAX that holds the text, and the mask the standard's
 * penalty rules pick. Written for this from ISO/IEC 18004; plain C, no SDL,
 * so tools/qr-check.c holds it on the host. */

#define QR_VERSION_MAX 10
#define QR_SIZE_MAX    (17 + 4 * QR_VERSION_MAX)    /* 57 modules a side */

/* `text`'s code into `out`, row after row, one byte a module, 1 dark and 0
 * light, with no quiet zone: the caller leaves four modules of light around
 * it. Returns the modules on a side (21 for version 1, 4 more a version), or
 * 0 when the text does not fit in QR_VERSION_MAX. `out` holds at least
 * QR_SIZE_MAX * QR_SIZE_MAX bytes. */
int qr_encode(const char *text, unsigned char *out);

#endif

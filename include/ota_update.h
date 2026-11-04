#pragma once
#include <WebServer.h>

/* ---------------------------------------------------------------------------
 * Local firmware update: PUT /firmware with the signed image as body
 * (firmware.signed.bin, see sign_firmware.py):
 *
 *   curl -X PUT -H "Content-Type: application/octet-stream" \
 *        --data-binary @.pio/build/heltec_wifi_lora_32_V3/firmware.signed.bin http://<ip>/firmware
 *
 * Responses: 200 (restart after ~1 s, as soon as no pager call is pending),
 * 400 (no valid ESP32 image), 403 (no or invalid signature), 409 (update already
 * running, also by the update server), 411 (Content-Length missing), 413 (image
 * larger than the OTA partition), 415 (wrong Content-Type), 500 (other error).
 * On any error the running firmware is kept.
 *
 * The body is received in chunks (raw handler), not into RAM. Requires the
 * headers "Content-Type" and "Content-Length" to be collected (collectHeaders).
 * --------------------------------------------------------------------------*/

void ota_update_register(WebServer &server);

#pragma once
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * The one OTA session, shared by the local update (PUT /firmware) and the
 * update server (Souper). Only one of them can write an image at a time.
 *
 *   ota_flash_begin(owner, size)   claim the session (each sector is erased when it is written;
 *                                  size includes the signature trailer)
 *   ota_flash_write(data, len)     append data
 *   ota_flash_finish(activate)     esp_ota_end() (checks the image), check the signature,
 *                                  and, if activate, make it the boot partition.
 *                                  Without activate (dry run) the image is made unbootable.
 *   ota_flash_abort()              discard the image
 *
 * After an activated image the session stays claimed until the restart.
 * The restart is done by ota_restart_loop() (called from loop()), as soon as
 * no pager call is pending, so that no call is lost.
 * --------------------------------------------------------------------------*/

typedef enum {
    OTA_OWNER_NONE = 0,
    OTA_OWNER_LOCAL,     // PUT /firmware
    OTA_OWNER_SOUPER,    // update server
} ota_owner_t;

typedef enum {
    OTA_OK = 0,
    OTA_ERR_BUSY,        // another update is running, or the restart is pending
    OTA_ERR_SIZE,        // image does not fit into the OTA partition (or is too small)
    OTA_ERR_FLASH,       // esp_ota_* failed
    OTA_ERR_IMAGE,       // not a valid ESP32 image (esp_ota_end)
    OTA_ERR_SIGNATURE,   // no or invalid signature
} ota_result_t;

ota_result_t ota_flash_begin(ota_owner_t owner, size_t size, char *err, size_t err_size);
ota_result_t ota_flash_write(const void *data, size_t len, char *err, size_t err_size);
ota_result_t ota_flash_finish(bool activate, char *err, size_t err_size);
void         ota_flash_abort(void);
bool         ota_flash_busy(void);           // a session is claimed (also while the restart is pending)
size_t       ota_flash_written(void);        // bytes written in the current session

void ota_restart_request(void);              // restart into the new firmware, as soon as possible
bool ota_restart_pending(void);
void ota_restart_loop(bool idle);            // call from loop(); idle: no pager call pending

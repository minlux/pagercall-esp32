#include "ota_flash.h"
#include "ota_signature.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static const uint32_t RESTART_DELAY_MS = 1000;  // so that the HTTP response still goes out
static const size_t   SECTOR_SIZE      = 4096;

static portMUX_TYPE           s_mux   = portMUX_INITIALIZER_UNLOCKED;
static ota_owner_t            s_owner = OTA_OWNER_NONE;
static bool                   s_staged;           // activated image, waiting for the restart
static const esp_partition_t *s_part;
static esp_ota_handle_t       s_handle;
static bool                   s_open;             // esp_ota_begin() done, esp_ota_end()/abort() not yet
static size_t                 s_size;
static size_t                 s_written;

static volatile bool          s_restart;
static uint32_t               s_restart_ts;

static void release(void)
{
    portENTER_CRITICAL(&s_mux);
    s_owner = OTA_OWNER_NONE;
    portEXIT_CRITICAL(&s_mux);
}

static bool read_partition(void *ctx, size_t offset, void *buf, size_t len)
{
    return esp_partition_read((const esp_partition_t *)ctx, offset, buf, len) == ESP_OK;
}

// erase the first sector, so the bootloader never boots this image
static void make_unbootable(void)
{
    esp_partition_erase_range(s_part, 0, SECTOR_SIZE);
}

ota_result_t ota_flash_begin(ota_owner_t owner, size_t size, char *err, size_t err_size)
{
    bool claimed = false;
    portENTER_CRITICAL(&s_mux);
    if ((s_owner == OTA_OWNER_NONE) && !s_restart) {
        s_owner = owner;
        claimed = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!claimed) {
        snprintf(err, err_size, "Update already running");
        return OTA_ERR_BUSY;
    }

    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        snprintf(err, err_size, "No OTA partition found");
        release();
        return OTA_ERR_FLASH;
    }
    if ((size <= 8) || (size > s_part->size)) {
        snprintf(err, err_size, "Image size %u does not fit into the OTA partition (%u bytes)",
                 (unsigned)size, (unsigned)s_part->size);
        release();
        return OTA_ERR_SIZE;
    }

    // erase each sector only when it is written: spreads the erasing (which blocks the flash cache) over the
    // download, instead of erasing ~1 MB at once (image size) or the whole partition (OTA_SIZE_UNKNOWN)
    esp_err_t e = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (e != ESP_OK) {
        snprintf(err, err_size, "esp_ota_begin failed: %s", esp_err_to_name(e));
        release();
        return OTA_ERR_FLASH;
    }
    s_open    = true;
    s_size    = size;
    s_written = 0;
    Serial.printf("[ota] begin: %u bytes into %s (owner %d)\n", (unsigned)size, s_part->label, (int)owner);
    return OTA_OK;
}

ota_result_t ota_flash_write(const void *data, size_t len, char *err, size_t err_size)
{
    if (!s_open || (s_written + len > s_size)) {
        snprintf(err, err_size, "More data than announced (%u bytes)", (unsigned)s_size);
        return OTA_ERR_SIZE;
    }
    esp_err_t e = esp_ota_write(s_handle, data, len);
    if (e != ESP_OK) {
        snprintf(err, err_size, "esp_ota_write failed: %s", esp_err_to_name(e));
        return (e == ESP_ERR_OTA_VALIDATE_FAILED) ? OTA_ERR_IMAGE : OTA_ERR_FLASH;  // first byte is no image magic
    }
    s_written += len;
    return OTA_OK;
}

ota_result_t ota_flash_finish(bool activate, char *err, size_t err_size)
{
    if (!s_open || (s_written != s_size)) {
        snprintf(err, err_size, "Incomplete image (%u of %u bytes)", (unsigned)s_written, (unsigned)s_size);
        ota_flash_abort();
        return OTA_ERR_SIZE;
    }

    // checks header, segments, checksum and the SHA256 appended by the IDF. the signature trailer behind is not part of the image
    s_open = false;
    esp_err_t e = esp_ota_end(s_handle);
    if (e != ESP_OK) {
        snprintf(err, err_size, "Invalid image: %s", esp_err_to_name(e));
        release();
        return (e == ESP_ERR_OTA_VALIDATE_FAILED) ? OTA_ERR_IMAGE : OTA_ERR_FLASH;
    }

    // our own signature, read back from the flash (also covers the flash contents)
    const uint32_t t0 = millis();
    const bool valid = ota_signature_verify(read_partition, (void *)s_part, s_size, err, err_size);
    Serial.printf("[ota] signature %s (%u ms)\n", valid ? "valid" : "INVALID", (unsigned)(millis() - t0));
    if (!valid) {
        make_unbootable();
        release();
        return OTA_ERR_SIGNATURE;
    }

    if (!activate) {
        make_unbootable();  // dry run: valid, but never boot it
        release();
        return OTA_OK;
    }

    e = esp_ota_set_boot_partition(s_part);
    if (e != ESP_OK) {
        snprintf(err, err_size, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(e));
        make_unbootable();
        release();
        return OTA_ERR_FLASH;
    }
    s_staged = true;  // stays claimed until the restart
    Serial.printf("[ota] %s staged as boot partition\n", s_part->label);
    return OTA_OK;
}

void ota_flash_abort(void)
{
    if (s_open) {
        esp_ota_abort(s_handle);
        s_open = false;
    }
    if (!s_staged) {
        release();
    }
}

bool ota_flash_busy(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool busy = (s_owner != OTA_OWNER_NONE) || s_restart;
    portEXIT_CRITICAL(&s_mux);
    return busy;
}

size_t ota_flash_written(void)
{
    return s_written;
}

void ota_restart_request(void)
{
    s_restart_ts = millis();
    s_restart    = true;
    Serial.println("[ota] restart requested");
}

bool ota_restart_pending(void)
{
    return s_restart;
}

void ota_restart_loop(bool idle)
{
    if (s_restart && idle && (millis() - s_restart_ts >= RESTART_DELAY_MS)) {
        Serial.println("[ota] restarting");
        Serial.flush();
        esp_restart();
    }
}

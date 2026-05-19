#include <WiFi.h>
#include <esp_ota_ops.h>
#include "ota_update.h"

// ---------------------------------------------------------------------------
// PUT /firmware  (Content-Type: application/octet-stream)
// ---------------------------------------------------------------------------
void ota_put_firmware(WebServer &server)
{
    String lenStr = server.header("Content-Length");
    if (lenStr.isEmpty()) {
        server.send(411, "text/plain", "Content-Length required");
        return;
    }
    size_t remaining = (size_t)lenStr.toInt();

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        server.send(500, "text/plain", "No OTA partition found");
        return;
    }

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        server.send(500, "text/plain",
                    String("esp_ota_begin failed: ") + esp_err_to_name(err));
        return;
    }

    WiFiClient client = server.client();
    uint8_t buf[1024];
    String errMsg;

    while (remaining > 0) {
        size_t toRead = min(sizeof(buf), remaining);
        size_t got    = client.readBytes(buf, toRead);
        if (got == 0) {
            errMsg = "Connection lost during upload";
            break;
        }
        err = esp_ota_write(handle, buf, got);
        if (err != ESP_OK) {
            errMsg = String("esp_ota_write failed: ") + esp_err_to_name(err);
            break;
        }
        remaining -= got;
    }

    if (!errMsg.isEmpty()) {
        esp_ota_abort(handle);
        server.send(500, "text/plain", errMsg);
        return;
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        server.send(500, "text/plain",
                    String("esp_ota_end failed: ") + esp_err_to_name(err));
        return;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        server.send(500, "text/plain",
                    String("esp_ota_set_boot_partition failed: ") + esp_err_to_name(err));
        return;
    }

    server.send(200, "text/plain", "Firmware stored. GET /reset to reboot.");
}

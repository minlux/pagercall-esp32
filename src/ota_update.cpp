#include "ota_update.h"
#include "ota_flash.h"

// state of the current PUT /firmware: the body arrives in raw() before the handler is called
static struct {
    int  status;     // HTTP status of the response (0: everything ok so far)
    char msg[128];
    bool open;       // the OTA session is ours
} s_put;

static void put_error(int status, const char *msg)
{
    if (s_put.status == 0) {
        s_put.status = status;
        snprintf(s_put.msg, sizeof(s_put.msg), "%s", msg);
    }
    if (s_put.open) {
        ota_flash_abort();
        s_put.open = false;
    }
}

static int status_of(ota_result_t r)
{
    switch (r) {
        case OTA_ERR_BUSY:      return 409;
        case OTA_ERR_SIZE:      return 413;
        case OTA_ERR_IMAGE:     return 400;
        case OTA_ERR_SIGNATURE: return 403;
        default:                return 500;
    }
}

static void on_raw(WebServer &server)
{
    HTTPRaw &raw = server.raw();
    char err[128];

    switch (raw.status) {
        case RAW_START: {
            s_put = {};
            String type = server.header("Content-Type");
            const int len = server.clientContentLength();
            if (!type.startsWith("application/octet-stream")) {
                put_error(415, "Content-Type must be application/octet-stream");
            } else if (len <= 0) {
                put_error(411, "Content-Length required");
            } else {
                ota_result_t r = ota_flash_begin(OTA_OWNER_LOCAL, (size_t)len, err, sizeof(err));
                if (r != OTA_OK) {
                    put_error(status_of(r), err);
                } else {
                    s_put.open = true;
                }
            }
            break;
        }
        case RAW_WRITE:
            if (s_put.open) {  // after an error the rest of the body is read, but ignored
                ota_result_t r = ota_flash_write(raw.buf, raw.currentSize, err, sizeof(err));
                if (r != OTA_OK) {
                    put_error(status_of(r), err);
                }
            }
            break;
        case RAW_END:
            if (s_put.open) {
                s_put.open = false;
                ota_result_t r = ota_flash_finish(true, err, sizeof(err));
                if (r != OTA_OK) {
                    put_error(status_of(r), err);
                } else {
                    s_put.status = 200;
                    snprintf(s_put.msg, sizeof(s_put.msg), "OK, firmware installed (%u bytes), restarting",
                             (unsigned)server.clientContentLength());
                    ota_restart_request();
                }
            } else if (s_put.status == 0) {
                put_error(411, "Content-Length required");  // no body at all
            }
            break;
        case RAW_ABORTED:
            // connection lost: discard the partial image. there is nobody to answer
            put_error(500, "Upload aborted");
            Serial.println("[ota] PUT /firmware aborted");
            break;
    }
}

void ota_update_register(WebServer &server)
{
    server.on(
        "/firmware", HTTP_PUT,
        [&server]() {
            const int status = (s_put.status != 0) ? s_put.status : 500;
            Serial.printf("[ota] PUT /firmware -> %d %s\n", status, s_put.msg);
            server.send(status, "text/plain", s_put.msg);
        },
        [&server]() { on_raw(server); });
}

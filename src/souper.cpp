#include "souper.h"
#include "credentials.h"
#include "ota_flash.h"
#include "version.h"

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <cJSON.h>
#include <esp_mac.h>
#include <esp_heap_caps.h>
#include <memory>

#ifndef SOUPER_URL
#define SOUPER_URL ""   // server URL without trailing slash, e.g. "http://example.org/souper" (-D SOUPER_URL=... in platformio.ini)
#endif
#define SOUPER_API_PATH "/assets/api"

static const uint32_t REQUEST_TIMEOUT_MS  = 5000;    // connect and response of the small requests
static const uint32_t DOWNLOAD_TIMEOUT_MS = 10000;   // abort the download, if no data was received for that time
static const size_t   CHUNK_SIZE          = 4096;
static const uint32_t FIRST_POLL_S        = 30;      // first poll after the WLAN connection was established
static const uint32_t TOKEN_POLL_S        = 5;       // poll after the token was set
static const uint32_t POLL_DEFAULT_S      = 3600;    // if the server gives no interval
static const uint32_t POLL_MIN_S          = 300;
static const uint32_t POLL_MAX_S          = 86400;
static const uint32_t RETRY_S             = 300;     // first retry after an error (doubled up to the poll interval)
static const uint32_t UNAUTHORIZED_S      = 86400;   // token invalid
static const int      REPORT_TRIES        = 3;
static const uint32_t REPORT_RETRY_MS     = 2000;
static const int      TRIES_MAX           = 3;       // attempts to install an update, then it is closed as failed
static const int      PENDING_MAX         = 4;       // max. number of pending updates handled per poll
static const uint32_t TASK_STACK          = 12288;

static const int STATUS_RETRY   = 1;
static const int STATUS_SUCCESS = 2;
static const int STATUS_FAILED  = -2;

enum class State { IDLE, UPDATE, DOWNLOAD, REPORT, RESTART };
static const char *const STATE_NAMES[] = { "idle", "update", "download", "report", "restart" };

static const char s_url[] = SOUPER_URL;

// shared between the task and loop() (GET /ota, token changes): protected by s_lock
static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;
static struct {
    bool     token;          // a token is stored
    State    state;
    bool     polled;         // last_poll_ms is valid
    uint32_t last_poll_ms;
    uint32_t next_poll_ms;
    bool     poll_request;   // poll now (POST /ota/poll, token changed)
    uint32_t interval_s;     // poll interval, given by the server
    uint32_t retry_s;        // current retry interval after errors (0: no error)
    char     result[112];    // result of the latest poll or update
    int      pending[PENDING_MAX];
    int      pending_count;
    size_t   size;           // size of the image being downloaded
    size_t   received;
} s;

static char     s_name[32];  // target name, e.g. "PagerCall_48-ca-43-01-02-03"
static uint32_t s_boot_id;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

struct Lock {
    Lock()  { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_lock); }
};

static void set_result(const char *fmt, ...)
{
    char text[sizeof(s.result)];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    {
        Lock lock;
        strcpy(s.result, text);
    }
    Serial.printf("[souper] %s\n", text);
}

static void set_state(State state)
{
    Lock lock;
    s.state = state;
}

static void schedule(uint32_t delay_s)
{
    Lock lock;
    s.next_poll_ms = millis() + delay_s * 1000u;
}

// retry after 5 min, doubled up to the poll interval
static void schedule_error(void)
{
    Lock lock;
    s.retry_s = (s.retry_s == 0) ? RETRY_S : 2 * s.retry_s;
    if (s.retry_s > s.interval_s) {
        s.retry_s = s.interval_s;
    }
    s.next_poll_ms = millis() + s.retry_s * 1000u;
}

static bool has_token(void)
{
    char token[CREDS_TOKEN_SIZE_MAX + 1];
    creds_get_ota_token(token, sizeof(token));
    return token[0] != 0;
}

// "Bearer <token>". the token is read from the NVS for each request, not kept in RAM
static String authorization(void)
{
    char token[CREDS_TOKEN_SIZE_MAX + 1];
    creds_get_ota_token(token, sizeof(token));
    return String("Bearer ") + token;
}

static void set_result_from_code(const char *what, int code)
{
    if (code < 0) {
        set_result("%s: connection failed (%s)", what, HTTPClient::errorToString(code).c_str());
    } else if ((code == 401) || (code == 403)) {
        set_result("%s: HTTP %d, token invalid", what, code);
    } else {
        set_result("%s: HTTP %d", what, code);
    }
}

static String json_string(cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    String result = text ? text : "{}";
    cJSON_free(text);
    return result;
}

// small JSON request to Souper (blocks the task for some 10 ms in the LAN, some 100 ms over the internet).
// returns the HTTP status code, or a negative HTTPC_ERROR_...
static int request(const char *method, const String &url, const String &body, String &response)
{
    WiFiClient tcp;
    HTTPClient http;
    if (!http.begin(tcp, url)) {
        return HTTPC_ERROR_CONNECTION_REFUSED;  // invalid URL
    }
    http.useHTTP10(true);  // no chunked response, no keep-alive
    http.setConnectTimeout(REQUEST_TIMEOUT_MS);
    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.setUserAgent("PagerCall/" PAGERCALL_VERSION_STR);
    http.addHeader("Authorization", authorization());
    if (body.length() > 0) {
        http.addHeader("Content-Type", "application/json");
    }
    const int code = http.sendRequest(method, body);
    if (code == HTTP_CODE_OK) {
        response = http.getString();
    }
    http.end();
    return code;
}

// field names as SGW and Candela, so that the Souper UI shows all of them the same way
static String sysinfo_json(void)
{
    char buf[48];
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "version", PAGERCALL_VERSION_STR);
    cJSON_AddStringToObject(json, "git", pagercall_git_rev);
    cJSON_AddStringToObject(json, "build", pagercall_build_time);
    cJSON_AddStringToObject(json, "name", s_name);
    snprintf(buf, sizeof(buf), "esp32s3 rev=%u", (unsigned)ESP.getChipRevision());
    cJSON_AddStringToObject(json, "chip", buf);
    snprintf(buf, sizeof(buf), "%uMB", (unsigned)(ESP.getFlashChipSize() / (1024u * 1024u)));
    cJSON_AddStringToObject(json, "flash", buf);
    cJSON *heap = cJSON_AddObjectToObject(json, "heap");
    cJSON_AddNumberToObject(heap, "free", ESP.getFreeHeap());
    cJSON_AddNumberToObject(heap, "max_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON *wifi = cJSON_AddObjectToObject(json, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", WiFi.SSID().c_str());
    const uint8_t *b = WiFi.BSSID();
    if (b) {
        snprintf(buf, sizeof(buf), "%02x-%02x-%02x-%02x-%02x-%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        cJSON_AddStringToObject(wifi, "bssid", buf);
    }
    cJSON_AddNumberToObject(wifi, "rssi", WiFi.RSSI());
    cJSON_AddNumberToObject(json, "uptime", millis() / 1000u);
    snprintf(buf, sizeof(buf), "%08x", (unsigned)s_boot_id);
    cJSON_AddStringToObject(json, "boot", buf);
    String text = json_string(json);
    cJSON_Delete(json);
    return text;
}

// ---------------------------------------------------------------------------
// poll and updates
// ---------------------------------------------------------------------------

// PATCH /target/<name> with the system info. the response gives the poll interval and the pending updates.
// returns false on an error
static bool poll(void)
{
    String response;
    const int code = request("PATCH", String(s_url) + SOUPER_API_PATH "/target/" + s_name, sysinfo_json(), response);
    {
        Lock lock;
        s.polled        = true;
        s.last_poll_ms  = millis();
        s.pending_count = 0;
    }
    if (code != HTTP_CODE_OK) {
        set_result_from_code("Poll", code);
        if ((code == 401) || (code == 403)) {
            { Lock lock; s.retry_s = 0; }
            schedule(UNAUTHORIZED_S);
        } else {
            schedule_error();
        }
        return false;
    }

    // { "poll": 3600, "updates": [ 17, 18 ] }
    cJSON *json = cJSON_Parse(response.c_str());
    const cJSON *poll = cJSON_GetObjectItemCaseSensitive(json, "poll");
    uint32_t interval = (cJSON_IsNumber(poll) && (poll->valuedouble > 0)) ? (uint32_t)poll->valuedouble : POLL_DEFAULT_S;
    interval = (interval < POLL_MIN_S) ? POLL_MIN_S : (interval > POLL_MAX_S) ? POLL_MAX_S : interval;
    {
        Lock lock;
        s.interval_s   = interval;
        s.retry_s      = 0;
        s.next_poll_ms = millis() + interval * 1000u;
        const cJSON *id;
        cJSON_ArrayForEach(id, cJSON_GetObjectItemCaseSensitive(json, "updates")) {
            if (cJSON_IsNumber(id) && (id->valueint > 0) && (s.pending_count < PENDING_MAX)) {
                s.pending[s.pending_count++] = id->valueint;
            }
        }
    }
    cJSON_Delete(json);
    set_result("ok");
    return true;
}

// PATCH /update/<id> with { "status": .., "merker": .., "log": ".." }, up to 3 attempts
static void report(int id, int status, int merker, const char *log)
{
    set_state(State::REPORT);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "status", status);
    cJSON_AddNumberToObject(json, "merker", merker);
    cJSON_AddStringToObject(json, "log", log);
    const String body = json_string(json);
    cJSON_Delete(json);

    const String url = String(s_url) + SOUPER_API_PATH "/update/" + id;
    for (int i = 0; i < REPORT_TRIES; ++i) {
        String response;
        const int code = request("PATCH", url, body, response);
        if ((code == HTTP_CODE_OK) || (code == HTTP_CODE_ACCEPTED)) {  // 202: the update is already closed
            break;
        }
        Serial.printf("[souper] report of update %d failed (%d)\n", id, code);
        vTaskDelay(pdMS_TO_TICKS(REPORT_RETRY_MS));
    }
    if (status == STATUS_SUCCESS) {
        set_result("Update %d: %s", id, log);
    } else {
        set_result("Update %d failed (status %d): %s", id, status, log);
    }
}

// result of a download
struct Outcome {
    enum { OK, RETRY, FAILED, DEFERRED } kind;
    char log[112];
};

static Outcome outcome(decltype(Outcome::OK) kind, const char *fmt, ...)
{
    Outcome o;
    o.kind = kind;
    va_list args;
    va_start(args, fmt);
    vsnprintf(o.log, sizeof(o.log), fmt, args);
    va_end(args);
    return o;
}

// download the image from src into the OTA partition and check it
static Outcome download(const char *src, size_t expected_size, bool dryrun)
{
    WiFiClient tcp;
    HTTPClient http;
    if ((strncmp(src, "http://", 7) != 0) || !http.begin(tcp, src)) {
        return outcome(Outcome::FAILED, "Unsupported src (only http): %s", src);
    }
    http.useHTTP10(true);  // no chunked transfer encoding
    http.setConnectTimeout(REQUEST_TIMEOUT_MS);
    http.setTimeout(REQUEST_TIMEOUT_MS);
    http.setUserAgent("PagerCall/" PAGERCALL_VERSION_STR);
    http.addHeader("Authorization", authorization());
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return outcome(Outcome::RETRY, "Download failed; code=%d", code);
    }
    const int size = http.getSize();
    if ((size <= 0) || ((expected_size != 0) && ((size_t)size != expected_size))) {
        http.end();
        return outcome(Outcome::RETRY, "Download failed; size=%d, expected %u", size, (unsigned)expected_size);
    }

    char err[128];
    ota_result_t r = ota_flash_begin(OTA_OWNER_SOUPER, (size_t)size, err, sizeof(err));
    if (r != OTA_OK) {
        http.end();
        return outcome((r == OTA_ERR_BUSY) ? Outcome::DEFERRED : (r == OTA_ERR_SIZE) ? Outcome::FAILED : Outcome::RETRY,
                       "%s", err);
    }
    {
        Lock lock;
        s.size     = size;
        s.received = 0;
        s.state    = State::DOWNLOAD;
    }

    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[CHUNK_SIZE]);
    NetworkClient *stream = http.getStreamPtr();
    size_t received = 0;
    uint32_t data_ms = millis();
    Outcome result = outcome(Outcome::OK, "");
    while (received < (size_t)size) {
        const int avail = (stream && buf) ? stream->available() : -1;
        if (avail > 0) {
            size_t n = ((size_t)avail < CHUNK_SIZE) ? (size_t)avail : CHUNK_SIZE;
            n = (n < (size_t)size - received) ? n : (size_t)size - received;
            const int got = stream->read(buf.get(), n);
            if (got > 0) {
                r = ota_flash_write(buf.get(), got, err, sizeof(err));
                if (r != OTA_OK) {
                    result = outcome(Outcome::RETRY, "%s", err);
                    break;
                }
                received += got;
                data_ms = millis();
                Lock lock;
                s.received = received;
            }
        } else if (!buf) {
            result = outcome(Outcome::RETRY, "Out of memory");
            break;
        } else if (!stream || (!stream->connected() && (stream->available() == 0))) {
            result = outcome(Outcome::RETRY, "Download aborted after %u of %d bytes", (unsigned)received, size);
            break;
        } else if (millis() - data_ms > DOWNLOAD_TIMEOUT_MS) {
            result = outcome(Outcome::RETRY, "Download timeout after %u of %d bytes", (unsigned)received, size);
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    http.end();
    if (result.kind != Outcome::OK) {
        ota_flash_abort();  // the old firmware keeps running
        return result;
    }

    // checks the image and the signature (also catches transmission errors)
    r = ota_flash_finish(!dryrun, err, sizeof(err));
    if (r != OTA_OK) {
        return outcome(Outcome::RETRY, "%s", err);
    }
    return dryrun ? outcome(Outcome::OK, "Dry run: image valid (%d bytes), not activated", size)
                  : outcome(Outcome::OK, "Update installed (%d bytes), restarting", size);
}

// GET /update/<id>, download, check and report it. returns false, if no further update shall be handled now
static bool handle_update(int id)
{
    set_state(State::UPDATE);
    if (ota_flash_busy()) {
        // local update running: neither download nor report (so the merker is not incremented). try again later
        set_result("Update %d deferred (local update running)", id);
        schedule(RETRY_S);
        return false;
    }

    String response;
    const int code = request("GET", String(s_url) + SOUPER_API_PATH "/update/" + id, String(), response);
    if (code != HTTP_CODE_OK) {
        set_result_from_code("Get update", code);
        schedule_error();
        return false;
    }

    // { "id": 17, "merker": 0, "file": { "src": "http://..", "dst": "firmware", "size": 350123 } }
    cJSON *json = cJSON_Parse(response.c_str());
    const cJSON *merker_item = cJSON_GetObjectItemCaseSensitive(json, "merker");
    int merker = cJSON_IsNumber(merker_item) ? merker_item->valueint : 0;
    const cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "file");
    const char *dst = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "dst"));
    const char *src = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(file, "src"));
    const cJSON *size_item = cJSON_GetObjectItemCaseSensitive(file, "size");
    const size_t size = (cJSON_IsNumber(size_item) && (size_item->valuedouble > 0)) ? (size_t)size_item->valuedouble : 0;
    // "md5" is not checked: transmission errors fail the signature check anyway

    if (!cJSON_IsObject(file)) {
        report(id, STATUS_FAILED, merker, "No file to download");
    } else if (!dst || (strncmp(dst, "firmware", 8) != 0)) {
        report(id, STATUS_FAILED, merker, "Destination not supported (dst must be firmware or firmware.dryrun)");
    } else if (!src || !src[0]) {
        report(id, STATUS_FAILED, merker, "Invalid src");
    } else {
        const bool dryrun = (strcmp(dst, "firmware.dryrun") == 0);
        Serial.printf("[souper] update %d: %s -> %s (%u bytes), merker %d\n", id, src, dst, (unsigned)size, merker);
        const Outcome o = download(src, size, dryrun);
        Serial.printf("[souper] update %d: %s; stack free %u\n", id, o.log, (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (o.kind == Outcome::DEFERRED) {
            cJSON_Delete(json);
            set_result("Update %d deferred (%s)", id, o.log);
            schedule(RETRY_S);
            return false;
        }
        if (o.kind == Outcome::OK) {
            report(id, STATUS_SUCCESS, merker, o.log);
            if (!dryrun) {
                // the image is staged: restart into it, even if the status could not be reported.
                // otherwise any reset would activate it without a report
                cJSON_Delete(json);
                set_state(State::RESTART);
                ota_restart_request();
                return false;
            }
        } else if (o.kind == Outcome::FAILED) {
            report(id, STATUS_FAILED, merker, o.log);
        } else {
            // failed, but may succeed next time. after the 3rd attempt the update is closed as failed
            ++merker;
            report(id, (merker >= TRIES_MAX) ? STATUS_FAILED : STATUS_RETRY, merker, o.log);
        }
    }
    cJSON_Delete(json);
    return true;
}

static void task(void *)
{
    bool was_connected = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));  // woken up early by souper_poll_now() / souper_token_changed()
        set_state(State::IDLE);

        bool token, due;
        {
            Lock lock;
            token = s.token;
            if (!token || !s_url[0]) {
                continue;
            }
        }
        if (WiFi.status() != WL_CONNECTED) {
            was_connected = false;
            continue;
        }
        const uint32_t now = millis();
        {
            Lock lock;
            if (!was_connected) {
                // (re-)connected: give the WLAN some time
                was_connected = true;
                if ((int32_t)(s.next_poll_ms - (now + FIRST_POLL_S * 1000u)) < 0) {
                    s.next_poll_ms = now + FIRST_POLL_S * 1000u;
                }
            }
            if (s.poll_request) {
                s.poll_request = false;
                s.next_poll_ms = now;
            }
            due = ((int32_t)(now - s.next_poll_ms) >= 0);
        }
        if (!due || !poll()) {
            continue;
        }

        int pending[PENDING_MAX];
        int count;
        {
            Lock lock;
            count = s.pending_count;
            memcpy(pending, s.pending, sizeof(pending));
        }
        for (int i = 0; i < count; ++i) {
            if (!handle_update(pending[i])) {
                break;
            }
        }
        if (ota_restart_pending()) {
            // wait for the restart (done in loop(), as soon as no pager call is pending)
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        Lock lock;
        s.pending_count = 0;
    }
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------

void souper_begin(void)
{
    if (s_task) {
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_name, sizeof(s_name), "PagerCall_%02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    s_boot_id = esp_random();

    s_lock = xSemaphoreCreateMutex();
    s.state        = State::IDLE;
    s.interval_s   = POLL_DEFAULT_S;
    s.next_poll_ms = millis() + FIRST_POLL_S * 1000u;
    s.token        = has_token();
    snprintf(s.result, sizeof(s.result), "%s", !s_url[0] ? "no URL" : s.token ? "" : "no token");

    // low priority on core 0 (loop() runs on core 1)
    xTaskCreatePinnedToCore(task, "souper", TASK_STACK, NULL, 1, &s_task, 0);
    Serial.printf("[souper] %s, url %s, %s\n", s_name, s_url[0] ? s_url : "(none)", s.token ? "token stored" : "no token");
}

void souper_token_changed(void)
{
    if (!s_task) {
        return;  // setup portal: takes effect after the restart
    }
    const bool token = has_token();
    {
        Lock lock;
        s.token = token;
        if (!token) {
            snprintf(s.result, sizeof(s.result), "no token");
        } else if (s.state == State::IDLE) {
            s.result[0]    = 0;
            s.retry_s      = 0;
            s.next_poll_ms = millis() + TOKEN_POLL_S * 1000u;
        }
    }
    xTaskNotifyGive(s_task);
}

void souper_poll_now(void)
{
    if (!s_task) {
        return;
    }
    {
        Lock lock;
        s.poll_request = true;
        s.retry_s      = 0;
    }
    xTaskNotifyGive(s_task);
}

String souper_state_json(void)
{
    if (!s_task) {
        return "{}";  // not started (setup portal)
    }
    cJSON *json = cJSON_CreateObject();
    {
        Lock lock;
        const uint32_t now = millis();
        cJSON_AddBoolToObject(json, "token", s.token);
        cJSON_AddStringToObject(json, "url", s_url);
        cJSON_AddStringToObject(json, "name", s_name);
        if (s.polled) {
            cJSON_AddNumberToObject(json, "last_poll", (now - s.last_poll_ms) / 1000u);
        } else {
            cJSON_AddNullToObject(json, "last_poll");
        }
        if (s.token && s_url[0] && (s.state == State::IDLE)) {
            const int32_t next = (int32_t)(s.next_poll_ms - now);
            cJSON_AddNumberToObject(json, "next_poll", (next > 0) ? next / 1000 : 0);
        } else {
            cJSON_AddNullToObject(json, "next_poll");
        }
        cJSON_AddStringToObject(json, "result", s.result);
        cJSON *pending = cJSON_AddArrayToObject(json, "pending");
        for (int i = 0; i < s.pending_count; ++i) {
            cJSON_AddItemToArray(pending, cJSON_CreateNumber(s.pending[i]));
        }
        const State state = ota_restart_pending() ? State::RESTART : s.state;
        cJSON_AddStringToObject(json, "state", STATE_NAMES[(int)state]);
        if ((state == State::DOWNLOAD) && (s.size > 0)) {
            cJSON_AddNumberToObject(json, "progress", (int)((uint64_t)s.received * 100u / s.size));
        }
    }
    String text = json_string(json);
    cJSON_Delete(json);
    return text;
}

int souper_progress(void)
{
    if (!s_task) {
        return -1;
    }
    Lock lock;
    return ((s.state == State::DOWNLOAD) && (s.size > 0)) ? (int)((uint64_t)s.received * 100u / s.size) : -1;
}

void souper_register(WebServer &server)
{
    server.on("/ota", HTTP_GET, [&server]() { server.send(200, "application/json", souper_state_json()); });
    server.on("/ota/poll", HTTP_POST, [&server]() {
        souper_poll_now();
        server.send(200, "text/plain", "OK");
    });
}

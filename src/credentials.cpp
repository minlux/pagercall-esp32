#include "credentials.h"
#include "souper.h"

#include <Preferences.h>
#include <WebServer.h>
#include <string.h>
#include <string>

static const size_t BODY_SIZE_MAX = 1024;  // max. size of a PUT /credentials body

static Preferences prefs;

void creds_begin(void)
{
    prefs.begin("pagercall", false);
}

void creds_set(const char *key, const char *value)
{
    if (value[0] == 0) {
        if (prefs.isKey(key)) {
            prefs.remove(key);
        }
    } else {
        prefs.putString(key, value);
    }
}

void creds_get(const char *key, char *buf, size_t len)
{
    buf[0] = 0;
    if (prefs.isKey(key)) {  // getString() of a missing key logs an error
        prefs.getString(key, buf, len);
    }
}

void creds_load(creds_t *creds)
{
    creds_get("ssid", creds->ssid, sizeof(creds->ssid));
    creds_get("password", creds->password, sizeof(creds->password));
    creds_get("ota_token", creds->ota_token, sizeof(creds->ota_token));
}

const char *creds_apply(const char *body, size_t len)
{
    creds_t creds;
    creds_load(&creds);
    creds_changes_t changes;
    const char *error = creds_patch(&creds, body, len, &changes);
    if (error) {
        return error;
    }
    if (changes.wlan) {
        creds_set("ssid", creds.ssid);
        creds_set("password", creds.password);
        Serial.printf("[creds] WLAN set: ssid=\"%s\" (effective after restart)\n", creds.ssid);
    }
    if (changes.token) {
        creds_set("ota_token", creds.ota_token);
        Serial.printf("[creds] ota_token %s\n", creds.ota_token[0] ? "set" : "deleted");
        souper_token_changed();
    }
    return NULL;
}

void creds_erase(void)
{
    creds_set("ssid", "");
    creds_set("password", "");
    creds_set("ota_token", "");
    Serial.println("[creds] erased");
    souper_token_changed();
}

// body of the current PUT /credentials, collected in raw() (the Content-Type is not checked, so the
// WebServer must not parse the body as a form)
static std::string s_body;
static bool        s_too_large;

void creds_register(WebServer &server)
{
    server.on(
        "/credentials", HTTP_PUT,
        [&server]() {
            if (s_too_large) {
                server.send(413, "text/plain", "Body too large");
                return;
            }
            const char *error = creds_apply(s_body.data(), s_body.size());
            s_body.clear();
            if (error) {
                server.send(400, "text/plain", error);
            } else {
                server.send(200, "text/plain", "OK");
            }
        },
        [&server]() {
            HTTPRaw &raw = server.raw();
            if (raw.status == RAW_START) {
                s_body.clear();
                s_too_large = (server.clientContentLength() > (int)BODY_SIZE_MAX);
            } else if ((raw.status == RAW_WRITE) && !s_too_large) {
                s_body.append((const char *)raw.buf, raw.currentSize);
            } else if (raw.status == RAW_ABORTED) {
                s_body.clear();
            }
        });

    server.on("/credentials", HTTP_DELETE, [&server]() {
        creds_erase();
        server.send(200, "text/plain", "OK");
    });
}

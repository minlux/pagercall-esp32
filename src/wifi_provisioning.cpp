#include "wifi_provisioning.h"
#include "credentials.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <cJSON.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static const char    *AP_SSID   = "PagerCall-Setup";
static const uint8_t  DNS_PORT  = 53;

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static bool      s_active = false;
static DNSServer dns;
static WebServer http(80);

// ---------------------------------------------------------------------------
// HTML helpers
// ---------------------------------------------------------------------------

/** Perform a blocking Wi-Fi scan and return <option> elements for a <datalist>. */
static String build_datalist_options(void)
{
    String opts;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        opts += "<option value='";
        opts += WiFi.SSID(i);
        opts += "'>";
    }
    WiFi.scanDelete();
    return opts;
}

static void send_portal_page(void)
{
    String html =
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
          "<meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'>"
          "<title>PagerCall Setup</title>"
          "<style>"
            "* { box-sizing: border-box; }"
            "body { font-family: sans-serif; max-width: 440px; margin: 40px auto;"
                   "padding: 16px; background: #f5f5f5; }"
            "h1 { font-size: 1.4em; margin-bottom: 24px; }"
            "label { display: block; margin-top: 14px; font-size: .9em;"
                    "font-weight: 600; color: #333; }"
            "input { width: 100%; padding: 9px; margin-top: 4px;"
                    "border: 1px solid #ccc; border-radius: 4px;"
                    "font-size: 1em; background: #fff; }"
            "button { margin-top: 24px; width: 100%; padding: 11px;"
                     "background: #0070f3; color: #fff; border: none;"
                     "border-radius: 4px; font-size: 1em; cursor: pointer; }"
            "button:hover { background: #0051c3; }"
            "small { color: #666; }"
            ".check { display: flex; gap: 8px; align-items: center; font-weight: normal; }"
            ".check input { width: auto; margin: 0; }"
          "</style>"
        "</head>"
        "<body>"
          "<h1>PagerCall Wi-Fi Setup</h1>"
          "<form method='POST' action='/save'>"
            "<label>Wi-Fi Network (SSID)"
              "<input name='ssid' list='nets' type='text'"
                     "placeholder='Network name' autocomplete='off' required>"
              "<datalist id='nets'>";

    html += build_datalist_options();

    html +=
              "</datalist>"
            "</label>"
            "<label>Password"
              "<input name='password' type='password' placeholder='Wi-Fi password'>"
            "</label>"
            "<label>Update token (optional)"
              "<input name='ota_token' type='text' autocomplete='off' placeholder='";

    char token[CREDS_TOKEN_SIZE_MAX + 1];
    creds_get_ota_token(token, sizeof(token));
    html += token[0] ? "Token stored (unchanged)" : "No token stored";

    html +=
              "'>"
              "<small>Token of this target on the update server (Souper). Leave empty to keep it.</small>"
            "</label>";
    if (token[0]) {
        html +=
            "<label class='check'><input name='ota_token_delete' type='checkbox' value='1'>"
              "Delete stored token</label>";
    }
    html +=
            "<button type='submit'>Save &amp; Connect</button>"
          "</form>"
        "</body>"
        "</html>";

    http.send(200, "text/html", html);
}

// ---------------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------------

static void handle_root(void)
{
    send_portal_page();
}

static void send_error_page(const char *reason)
{
    String html =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Error</title>"
        "<style>body{font-family:sans-serif;text-align:center;margin-top:60px;}</style>"
        "</head><body>"
        "<h1>Not saved</h1><p>";
    html += reason;  // fixed texts of creds_patch()
    html += "</p><p><a href='/'>Back</a></p></body></html>";
    http.send(400, "text/html", html);
}

static void handle_save(void)
{
    String ssid     = http.arg("ssid");
    String password = http.arg("password");
    String token    = http.arg("ota_token");
    token.trim();

    if (ssid.isEmpty()) {
        http.sendHeader("Location", "/");
        http.send(302, "text/plain", "");
        return;
    }

    // same rules as PUT /credentials
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid", ssid.c_str());
    cJSON_AddStringToObject(json, "password", password.c_str());
    if (!token.isEmpty()) {
        cJSON_AddStringToObject(json, "ota_token", token.c_str());
    } else if (http.hasArg("ota_token_delete")) {
        cJSON_AddNullToObject(json, "ota_token");
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    const char *error = body ? creds_apply(body, strlen(body)) : "Out of memory";
    cJSON_free(body);
    if (error) {
        send_error_page(error);
        return;
    }

    http.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Saved</title>"
        "<style>body{font-family:sans-serif;text-align:center;margin-top:60px;}</style>"
        "</head><body>"
        "<h1>Credentials saved!</h1>"
        "<p>The device will now restart and connect to your Wi&#8209;Fi network.</p>"
        "</body></html>");

    delay(1500);
    ESP.restart();
}

/** Redirect every unrecognised URL to the portal root (captive-portal trigger). */
static void handle_redirect(void)
{
    http.sendHeader("Location", "http://192.168.4.1/");
    http.send(302, "text/plain", "");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool wifi_provisioning_active(void)
{
    return s_active;
}

void wifi_provisioning_begin(void)
{
    s_active = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);

    // DNS: answer every query with the AP's IP so the OS opens the portal.
    dns.start(DNS_PORT, "*", WiFi.softAPIP());

    http.on("/",      HTTP_GET,  handle_root);
    http.on("/save",  HTTP_POST, handle_save);
    http.on("/reset", HTTP_GET,  []() {
        http.send(200, "text/plain", "Rebooting...");
        delay(200);
        ESP.restart();
    });
    creds_register(http);  // PUT/DELETE /credentials, also in the setup portal (then GET /reset)
    http.onNotFound(handle_redirect);
    http.begin();
}

void wifi_provisioning_loop(void)
{
    if (!s_active) return;
    dns.processNextRequest();
    http.handleClient();
}

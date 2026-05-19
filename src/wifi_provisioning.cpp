#include "wifi_provisioning.h"
#include "credentials.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

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
            "<label>PagerCall Server URL"
              "<input name='url' type='url'"
                     "placeholder='https://server.example.com' required>"
              "<small>The URL of your PagerCall service endpoint.</small>"
            "</label>"
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

static void handle_save(void)
{
    String ssid     = http.arg("ssid");
    String password = http.arg("password");
    String url      = http.arg("url");

    if (ssid.isEmpty() || url.isEmpty()) {
        http.sendHeader("Location", "/");
        http.send(302, "text/plain", "");
        return;
    }

    creds_set_ssid(ssid.c_str());
    creds_set_password(password.c_str());
    creds_set_url(url.c_str());

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
    http.onNotFound(handle_redirect);
    http.begin();
}

void wifi_provisioning_loop(void)
{
    if (!s_active) return;
    dns.processNextRequest();
    http.handleClient();
}

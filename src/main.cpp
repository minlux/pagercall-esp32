#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include "credentials.h"
#include "wifi_provisioning.h"
#include "ota_update.h"
#include "ota_flash.h"
#include "souper.h"
#include "version.h"
#include "pagercall.h"
#include "led.h"
#include "button.h"
#include "oled.h"

// ---------------------------------------------------------------------------
// Hardware pins
// ---------------------------------------------------------------------------
static const uint8_t LED_PIN = 35;
static const uint8_t BTN_PIN = 0;   // active-LOW, internal pull-up

// static Led       led(LED_PIN);   // bright, white LED
static Button    btn(BTN_PIN, true);
static WebServer s_http_server(80);

// ---------------------------------------------------------------------------
// Application state machine
// ---------------------------------------------------------------------------
typedef enum { STATE_STARTUP, STATE_PROVISIONING, STATE_NORMAL } AppState;
static AppState s_state      = STATE_STARTUP;
static uint32_t s_startup_ts = 0;

static void enter_provisioning(void)
{
    s_state = STATE_PROVISIONING;
    // led.set(&LED_PAT_SLOW);
    oled_show_provisioning();
    wifi_provisioning_begin();
}

static void enter_normal(void)
{
    s_state = STATE_NORMAL;

    char ssid[64] = {};
    char pass[128] = {};
    creds_get_ssid(ssid, sizeof(ssid));
    creds_get_password(pass, sizeof(pass));

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);

    static const char *headers[] = {"Content-Length", "Content-Type"};
    s_http_server.collectHeaders(headers, 2);
    pagercall_begin();
    s_http_server.on(UriBraces("/pagercall/{}"), HTTP_GET, []() { pagercall_notify(s_http_server); });
    s_http_server.on("/version", HTTP_GET, []() {
        String json = String("{\"version\":\"") + PAGERCALL_VERSION_STR + "\",\"git\":\"" + pagercall_git_rev +
                      "\",\"build\":\"" + pagercall_build_time + "\"}";
        s_http_server.send(200, "application/json", json);
    });
    ota_update_register(s_http_server);   // PUT /firmware
    creds_register(s_http_server);        // PUT/DELETE /credentials
    souper_register(s_http_server);       // GET /ota, POST /ota/poll
    s_http_server.on("/reset", HTTP_GET, []() {
        s_http_server.send(200, "text/plain", "Rebooting...");
        s_http_server.client().clear();
        delay(200);
        esp_restart();
    });
    s_http_server.begin();

    souper_begin();  // update server: polls only, if a token is stored
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.printf("PagerCall %s (%s, %s)\n", PAGERCALL_VERSION_STR, pagercall_git_rev, pagercall_build_time);

    // led.begin();
    btn.begin();
    oled_begin();

    creds_begin();

    // No SSID stored → always provision first, regardless of button.
    char ssid[64] = {};
    creds_get_ssid(ssid, sizeof(ssid));
    if (ssid[0] == '\0') {
        enter_provisioning();
        return;
    }

    // Credentials exist → open the 30-second startup window.
    s_state      = STATE_STARTUP;
    s_startup_ts = millis();
    // led.set(&LED_PAT_FAST);
    oled_show_startup();
}

void loop()
{
    // led.update();
    oled_update();

    switch (s_state) {
        case STATE_STARTUP:
            if (btn.heldFor(3000)) {
                enter_provisioning();
            } else if (millis() - s_startup_ts >= 30000UL) {
                enter_normal();
            }
            break;

        case STATE_PROVISIONING:
            wifi_provisioning_loop();
            break;

        case STATE_NORMAL: {
            bool connected = (WiFi.status() == WL_CONNECTED);
            // led.set(connected ? &LED_PAT_TWICE : &LED_PAT_ONCE);

            static bool s_last_connected = false;
            if (connected != s_last_connected) {
                s_last_connected = connected;
                char ip[16] = {};
                if (connected) {
                    WiFi.localIP().toString().toCharArray(ip, sizeof(ip));
                }
                oled_show_normal(connected, ip);
            }

            s_http_server.handleClient();
            pagercall_update();

            // firmware update: show the download, restart into the new firmware as soon as no call is pending
            static uint32_t s_display_ts = 0;
            static int      s_last_progress = -1;
            static bool     s_restart_shown = false;
            if (ota_restart_pending()) {
                if (!s_restart_shown) {
                    s_restart_shown = true;
                    oled_show_restart();
                }
            } else if (millis() - s_display_ts >= 500) {
                s_display_ts = millis();
                const int progress = souper_progress();
                if (progress != s_last_progress) {
                    s_last_progress = progress;
                    oled_show_update(progress);
                }
            }
            ota_restart_loop(pagercall_idle());

            break;
        }
    }
}

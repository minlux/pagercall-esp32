#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "credentials.h"
#include "wifi_provisioning.h"
#include "ota_update.h"
#include "pagercall.h"
#include "led.h"
#include "button.h"

// ---------------------------------------------------------------------------
// Hardware pins
// ---------------------------------------------------------------------------
static const uint8_t LED_PIN = 35;
static const uint8_t BTN_PIN = 0;   // active-LOW, internal pull-up

static Led       led(LED_PIN);
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
    led.set(&LED_PAT_SLOW);
    wifi_provisioning_begin();
}

static void enter_normal(void)
{
    s_state = STATE_NORMAL;

    char ssid[128] = {}, pass[128] = {};
    creds_get_ssid(ssid, sizeof(ssid));
    creds_get_password(pass, sizeof(pass));

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, pass);

    static const char *headers[] = {"Content-Length"};
    s_http_server.collectHeaders(headers, 1);
    pagercall_begin();
    s_http_server.on("/pagercall/{id}", HTTP_GET, []() { pagercall_notify(s_http_server); });
    s_http_server.on("/firmware", HTTP_PUT, []() { ota_put_firmware(s_http_server); });
    s_http_server.on("/reset", HTTP_GET, []() {
        s_http_server.send(200, "text/plain", "Rebooting...");
        s_http_server.client().clear();
        delay(200);
        esp_restart();
    });
    s_http_server.begin();
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    led.begin();
    btn.begin();

    creds_begin();

    // No SSID stored → always provision first, regardless of button.
    char ssid[8] = {};
    creds_get_ssid(ssid, sizeof(ssid));
    if (ssid[0] == '\0') {
        enter_provisioning();
        return;
    }

    // Credentials exist → open the 30-second startup window.
    s_state      = STATE_STARTUP;
    s_startup_ts = millis();
    led.set(&LED_PAT_FAST);
}

void loop()
{
    led.update();

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

    case STATE_NORMAL:
        led.set(WiFi.status() == WL_CONNECTED ? &LED_PAT_TWICE : &LED_PAT_ONCE);
        s_http_server.handleClient();
        break;
    }
}

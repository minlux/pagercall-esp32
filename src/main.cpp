#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include "credentials.h"
#include "wifi_provisioning.h"
#include "ota_update.h"
#include "pagercall.h"
#include "led.h"
#include "button.h"
#include "oled.h"

// ---------------------------------------------------------------------------
// Hardware pins
// ---------------------------------------------------------------------------
static const uint8_t LED_PIN = 35;
static const uint8_t BTN_PIN = 0;   // active-LOW, internal pull-up

// static Led       led(LED_PIN);
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

    static const char *headers[] = {"Content-Length"};
    s_http_server.collectHeaders(headers, 1);
    pagercall_begin();
    s_http_server.on(UriBraces("/pagercall/{}"), HTTP_GET, []() { pagercall_notify(s_http_server); });
    // s_http_server.on("/firmware", HTTP_PUT, []() { ota_put_firmware(s_http_server); });
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

            // TX state machine — one repetition per loop() invocation
            typedef enum { TX_IDLE, TX_RUNNING } tx_sm_t;
            static tx_sm_t  tx_sm   = TX_IDLE;
            static uint8_t  tx_buf[16];
            static uint32_t tx_len;
            static uint32_t tx_reps;

            switch (tx_sm) {
            case TX_IDLE: {
                uint32_t packed;
                if (pagercall_pop(&packed)) {
                    const uint32_t keyboard = (packed >> 15) & 0x3FFu;
                    const uint32_t pager    = (packed >> 5)  & 0x3FFu;
                    const uint32_t action   = packed & 0x1Fu;
                    tx_len = pagercall_encode_6n1(tx_buf, keyboard, pager, action);
                    char call_id[32];
                    snprintf(call_id, sizeof(call_id), "%u.%u", keyboard, pager);
                    oled_show_calling(call_id);
                    Serial.printf("[pagercall] Calling keyboard=%u pager=%u\n",
                                keyboard, pager);
                    pagercall_cw_start();
                    tx_reps = 32;
                    tx_sm   = TX_RUNNING;
                }
                break;
            }
            case TX_RUNNING:
                Serial1.write(tx_buf, tx_len);
                Serial1.flush();
                delay(5);
                if (--tx_reps == 0) {
                    pagercall_cw_stop();
                    tx_sm = TX_IDLE;
                }
                break;
            }

            break;
        }
    }
}

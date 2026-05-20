#include "oled.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static const uint8_t OLED_SDA  = 17;
static const uint8_t OLED_SCL  = 18;
static const uint8_t OLED_RST  = 21;
static const uint8_t OLED_ADDR = 0x3C;
static const uint8_t VEXT_PIN  = 36;  // active-LOW: LOW = Vext on (powers OLED + I2C pull-ups)

static TwoWire s_wire(1);  // use I2C peripheral 1 — keeps peripheral 0 (Wire) free
static Adafruit_SSD1306 s_display(128, 64, &s_wire, OLED_RST);

static bool     s_calling    = false;
static uint32_t s_calling_ts = 0;
static bool     s_connected  = false;
static char     s_ip[16]     = {};

static void header(const char *title)
{
    s_display.clearDisplay();
    s_display.setTextColor(SSD1306_WHITE);
    s_display.setTextSize(1);
    s_display.setCursor(0, 0);
    s_display.println(title);
    s_display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
}

static void flush()
{
    s_display.display();
}

void oled_begin()
{
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);  // LOW = Vext on → powers OLED and I2C pull-ups
    delay(10);

    s_wire.begin(OLED_SDA, OLED_SCL);
    s_display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    s_display.clearDisplay();
    s_display.display();
}

void oled_show_startup()
{
    header("PagerCall");
    s_display.setCursor(0, 16);
    s_display.println("Starting...");
    s_display.println();
    s_display.setTextSize(1);
    s_display.println("Hold button 3 s");
    s_display.println("to enter setup.");
    flush();
}

void oled_show_provisioning()
{
    header("PagerCall Setup");
    s_display.setCursor(0, 16);
    s_display.println("Connect to WiFi:");
    s_display.println("PagerCall-Setup");
    s_display.println();
    s_display.println("192.168.4.1");
    flush();
}

void oled_show_normal(bool connected, const char *ip)
{
    s_connected = connected;
    strncpy(s_ip, ip ? ip : "", sizeof(s_ip) - 1);

    if (s_calling) return;

    header("PagerCall");
    s_display.setCursor(0, 16);
    if (connected) {
        s_display.println("WiFi: connected");
        s_display.println();
        s_display.println(s_ip);
    } else {
        s_display.println("WiFi: connecting");
    }
    flush();
}

void oled_show_calling(const char *id)
{
    s_calling    = true;
    s_calling_ts = millis();

    header("PagerCall");
    s_display.setCursor(0, 16);
    s_display.println("Calling:");
    s_display.println();
    s_display.setTextSize(2);
    s_display.println(id);
    s_display.setTextSize(1);
    flush();
}

void oled_update()
{
    if (!s_calling) return;
    if (millis() - s_calling_ts >= 5000UL) {
        s_calling = false;
        oled_show_normal(s_connected, s_ip);
    }
}

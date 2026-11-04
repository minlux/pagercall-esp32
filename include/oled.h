#pragma once
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * oled — thin wrapper around Adafruit SSD1306 for the 128×64 display on the
 * Heltec WiFi LoRa 32 V3 (SDA=17, SCL=18, RST=21, I2C addr 0x3C).
 *
 *   void setup() { oled_begin(); }
 *
 *   // call once whenever display content should change:
 *   oled_show_startup();
 *   oled_show_provisioning();
 *   oled_show_normal(connected, ip_str);
 *   oled_show_update(percent);  oled_show_restart();
 * --------------------------------------------------------------------------*/

void oled_begin();
void oled_show_startup();
void oled_show_provisioning();
void oled_show_normal(bool connected, const char *ip);
void oled_show_calling(const char *id);
void oled_show_update(int progress);  // firmware download from the update server; progress < 0: done, back to normal
void oled_show_restart();             // new firmware installed, restart pending
void oled_update();

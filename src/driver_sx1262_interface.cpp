/**
 * Copyright (c) 2015 - present LibDriver All rights reserved
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * @file      driver_sx1262_interface.cpp
 * @brief     driver sx1262 interface implementation for Heltec WiFi LoRa 32 V3 (ESP32-S3)
 * @version   1.0.0
 */

#include <Arduino.h>
#include <SPI.h>
#include <stdarg.h>
#include "driver_sx1262_interface.h"

// Heltec WiFi LoRa 32 V3 — pin assignments from pins_arduino.h:
//   SS=8, SCK=9, MISO=11, MOSI=10  (SPI bus, matches Arduino defaults for this board)
//   RST_LoRa=12, BUSY_LoRa=13

#define SX1262_SPI_FREQ  2000000UL  // 2 MHz conservative default; SX1262 supports up to 16 MHz

uint8_t sx1262_interface_spi_init(void)
{
    pinMode(SS, OUTPUT);
    digitalWrite(SS, HIGH);
    SPI.begin(SCK, MISO, MOSI, SS);
    return 0;
}

uint8_t sx1262_interface_spi_deinit(void)
{
    SPI.end();
    return 0;
}

// SX1262 SPI protocol: write in_buf (command + params), then read out_buf,
// with NSS held low for the entire transaction.
uint8_t sx1262_interface_spi_write_read(uint8_t *in_buf, uint32_t in_len,
                                        uint8_t *out_buf, uint32_t out_len)
{
    SPI.beginTransaction(SPISettings(SX1262_SPI_FREQ, MSBFIRST, SPI_MODE0));
    digitalWrite(SS, LOW);
    for (uint32_t i = 0; i < in_len; i++) {
        SPI.transfer(in_buf[i]);
    }
    for (uint32_t i = 0; i < out_len; i++) {
        out_buf[i] = SPI.transfer(0x00);
    }
    digitalWrite(SS, HIGH);
    SPI.endTransaction();
    return 0;
}

uint8_t sx1262_interface_reset_gpio_init(void)
{
    pinMode(RST_LoRa, OUTPUT);
    digitalWrite(RST_LoRa, HIGH);
    return 0;
}

uint8_t sx1262_interface_reset_gpio_deinit(void)
{
    pinMode(RST_LoRa, INPUT);
    return 0;
}

uint8_t sx1262_interface_reset_gpio_write(uint8_t data)
{
    digitalWrite(RST_LoRa, data ? HIGH : LOW);
    return 0;
}

uint8_t sx1262_interface_busy_gpio_init(void)
{
    pinMode(BUSY_LoRa, INPUT);
    return 0;
}

uint8_t sx1262_interface_busy_gpio_deinit(void)
{
    pinMode(BUSY_LoRa, INPUT);
    return 0;
}

uint8_t sx1262_interface_busy_gpio_read(uint8_t *value)
{
    *value = (uint8_t)digitalRead(BUSY_LoRa);
    return 0;
}

void sx1262_interface_delay_ms(uint32_t ms)
{
    delay(ms);
}

void sx1262_interface_debug_print(const char *const fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
}

void sx1262_interface_receive_callback(uint16_t type, uint8_t *buf, uint16_t len)
{
    switch (type)
    {
        case SX1262_IRQ_TX_DONE:
            sx1262_interface_debug_print("sx1262: irq tx done.\n");
            break;
        case SX1262_IRQ_RX_DONE:
            sx1262_interface_debug_print("sx1262: irq rx done.\n");
            break;
        case SX1262_IRQ_PREAMBLE_DETECTED:
            sx1262_interface_debug_print("sx1262: irq preamble detected.\n");
            break;
        case SX1262_IRQ_SYNC_WORD_VALID:
            sx1262_interface_debug_print("sx1262: irq valid sync word detected.\n");
            break;
        case SX1262_IRQ_HEADER_VALID:
            sx1262_interface_debug_print("sx1262: irq valid header.\n");
            break;
        case SX1262_IRQ_HEADER_ERR:
            sx1262_interface_debug_print("sx1262: irq header error.\n");
            break;
        case SX1262_IRQ_CRC_ERR:
            sx1262_interface_debug_print("sx1262: irq crc error.\n");
            break;
        case SX1262_IRQ_CAD_DONE:
            sx1262_interface_debug_print("sx1262: irq cad done.\n");
            break;
        case SX1262_IRQ_CAD_DETECTED:
            sx1262_interface_debug_print("sx1262: irq cad detected.\n");
            break;
        case SX1262_IRQ_TIMEOUT:
            sx1262_interface_debug_print("sx1262: irq timeout.\n");
            break;
        default:
            sx1262_interface_debug_print("sx1262: unknown code.\n");
            break;
    }
}

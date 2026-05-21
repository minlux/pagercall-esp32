#include <stdint.h>
#include <Arduino.h>
#include "pagercall.h"
#include "oled.h"
#include "driver_sx1262_interface.h"
#include "driver_sx1262.h"


typedef enum {
    TX_STATE_IDLE = 0,
    TX_STATE_DELAY = 1,
    TX_STATE_SETUP = 2,
    TX_STATE_TRANSMITTING = 3,
    TX_STATE_HOLD = 4
} tx_state_t;

#define TX_SETUP_TIME (0)
#define TX_HOLD_TIME  (25)

#define TX_FREQ_HZ  433920000UL  // 433.92 MHz
static const uint8_t  TX_DBG_PIN  = 19;    // mirrors OOK pattern for debugging

static sx1262_handle_t gs_sx1262;

static const uint32_t TX_BAUD     = 4800; // 12943; //3*4800;
static const uint32_t TX_REPEATS  = 32;   // number of transmissions per call


static uint8_t  s_tx_data[16];
static volatile uint32_t s_tx_request; // Upper 16 bits specify the number of repetitions, the lower 16 bits specify the number of bits to transmit (per repetition)
static volatile uint32_t s_tx_delay_time; // used to delay initial transmission


// encode into bytes sent out via a 6N1 inverted uart tx
static uint32_t rtd157_encode_6n1(uint8_t out[13], uint32_t keyboard10, uint32_t pager10, uint32_t action5)
{
    static constexpr uint8_t lookup2[] = { 0x37, 0x07, 0x34, 0x04};
    static constexpr uint8_t lookup1[] = { 0x3F, 0x3C }; //only on

    // encode 10 keyboard bits
    out[4] = lookup2[keyboard10 & 0x03];
    keyboard10 >>= 2;
    out[3] = lookup2[keyboard10 & 0x03];
    keyboard10 >>= 2;
    out[2] = lookup2[keyboard10 & 0x03];
    keyboard10 >>= 2;
    out[1] = lookup2[keyboard10 & 0x03];
    keyboard10 >>= 2;
    out[0] = lookup2[keyboard10 & 0x03];
    // encode 10 pager bits
    out[9] = lookup2[pager10 & 0x03];
    pager10 >>= 2;
    out[8] = lookup2[pager10 & 0x03];
    pager10 >>= 2;
    out[7] = lookup2[pager10 & 0x03];
    pager10 >>= 2;
    out[6] = lookup2[pager10 & 0x03];
    pager10 >>= 2;
    out[5] = lookup2[pager10 & 0x03];
    // encode 5 action bits
    out[12] = lookup1[action5 & 0x01];
    action5 >>= 1;
    out[11] = lookup2[action5 & 0x03];
    action5 >>= 2;
    out[10] = lookup2[action5 & 0x03];

    // return number of ecoded bytes
    return 13;
}


void pagercall_begin()
{
    // we will use the TX output as antenna control (instead of DIO2) for the OOK modulation
    Serial1.begin(4800, SERIAL_6N1, -1, 7, true); // output pattern on GPIO 7

    // Wire up the SX1262 driver handle
    DRIVER_SX1262_LINK_INIT(&gs_sx1262, sx1262_handle_t);
    DRIVER_SX1262_LINK_SPI_INIT(&gs_sx1262, sx1262_interface_spi_init);
    DRIVER_SX1262_LINK_SPI_DEINIT(&gs_sx1262, sx1262_interface_spi_deinit);
    DRIVER_SX1262_LINK_SPI_WRITE_READ(&gs_sx1262, sx1262_interface_spi_write_read);
    DRIVER_SX1262_LINK_RESET_GPIO_INIT(&gs_sx1262, sx1262_interface_reset_gpio_init);
    DRIVER_SX1262_LINK_RESET_GPIO_DEINIT(&gs_sx1262, sx1262_interface_reset_gpio_deinit);
    DRIVER_SX1262_LINK_RESET_GPIO_WRITE(&gs_sx1262, sx1262_interface_reset_gpio_write);
    DRIVER_SX1262_LINK_BUSY_GPIO_INIT(&gs_sx1262, sx1262_interface_busy_gpio_init);
    DRIVER_SX1262_LINK_BUSY_GPIO_DEINIT(&gs_sx1262, sx1262_interface_busy_gpio_deinit);
    DRIVER_SX1262_LINK_BUSY_GPIO_READ(&gs_sx1262, sx1262_interface_busy_gpio_read);
    DRIVER_SX1262_LINK_DELAY_MS(&gs_sx1262, sx1262_interface_delay_ms);
    DRIVER_SX1262_LINK_DEBUG_PRINT(&gs_sx1262, sx1262_interface_debug_print);
    DRIVER_SX1262_LINK_RECEIVE_CALLBACK(&gs_sx1262, sx1262_interface_receive_callback);

    uint8_t ret = sx1262_init(&gs_sx1262);
    if (ret != 0)
    {
        Serial.println("[sx1262] init failed");
        return;
    }

    auto print_status = [&](const char *label, uint8_t rc) {
        delay(1);
        uint8_t st;
        sx1262_get_status(&gs_sx1262, &st);
        Serial.printf("[sx1262] after %-34s ret=%u  status=0x%02X\n", label, rc, st);
    };

    print_status("init", ret);

    // Enable DC-DC converter (must be called in STBY_RC); reduces core power consumption
    ret = sx1262_set_regulator_mode(&gs_sx1262, SX1262_REGULATOR_MODE_DC_DC_LDO);
    print_status("set_regulator_mode(DC_DC)", ret);

    // DIO3 powers the 1.8 V TCXO with an init delay of 10ms
    ret = sx1262_set_dio3_as_tcxo_ctrl(&gs_sx1262, SX1262_TCXO_VOLTAGE_1P8V, 640);
    print_status("set_dio3_as_tcxo_ctrl", ret);

    // Set RF frequency
    uint32_t freq_reg;
    sx1262_frequency_convert_to_register(&gs_sx1262, TX_FREQ_HZ, &freq_reg);
    ret = sx1262_set_rf_frequency(&gs_sx1262, freq_reg);
    print_status("set_rf_frequency", ret);

    // PA config: pa_duty_cycle=0x04, hp_max=0x07 → up to ~22 dBm on SX1262
    ret = sx1262_set_pa_config(&gs_sx1262, 0x04, 0x07);
    print_status("set_pa_config", ret);

    // TX power and ramp time (10 µs ramp keeps OOK edges sharp at 4800 baud)
    ret = sx1262_set_tx_params(&gs_sx1262, 14, SX1262_RAMP_TIME_10US);
    print_status("set_tx_params", ret);

    // Start in standby (carrier off)
    ret = sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
    print_status("set_standby(XOSC)", ret);

}


void pagercall_notify(WebServer &server)
{
    String id = server.pathArg(0);
    Serial.printf("[pagercall] GET /pagercall/%s\n", id.c_str());

    // Parse "rtd157-274.1" → prefix="rtd157", keyboard=274, pager=1
    // %[^-] reads all chars up to (but not including) the '-' delimiter
    char prefix[32] = {};
    unsigned int keyboard = 0, pager = 0;
    if (sscanf(id.c_str(), "%31[^-]-%u.%u", prefix, &keyboard, &pager) != 3)
    {
        server.send(400, "text/plain", "Bad id: " + id);
        return;
    }

    Serial.printf("[pagercall] prefix=%s keyboard=%u pager=%u\n",
                  prefix, keyboard, pager);

    if (strcmp(prefix, "rtd157") == 0)
    {
        if (s_tx_request != 0)
        {
            server.send(503, "text/plain", "Busy");
            return;
        }

        // Show hint on oled
        oled_show_calling(id.c_str());

        // Enable continous wave
        sx1262_set_tx_continuous_wave(&gs_sx1262);
        delay(10);

        // Trigger pager call
        const uint32_t action = 4;
        const uint32_t num = rtd157_encode_6n1(s_tx_data, keyboard, pager, action);
        Serial1.write(s_tx_data, num);

        // Wait until transmission has completed
        Serial1.flush();

        // Go into standy mode -> carrier off
        sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
        server.send(200, "text/plain", "OK: " + id);
    }
    else
    {
        server.send(501, "text/plain", "Not implemented: " + String(prefix));
    }
}

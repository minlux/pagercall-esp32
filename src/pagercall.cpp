#include <stdint.h>
#include <Arduino.h>
#include "pagercall.h"
#include "oled.h"
#include "driver_sx1262_interface.h"
#include "driver_sx1262.h"


#define TX_FREQ_HZ  433920000UL  // 433.92 MHz
static sx1262_handle_t gs_sx1262;

// Thread-safe 16-element ring buffer FIFO; packed = (keyboard10<<15)|(pager10<<5)|action5
#define FIFO_SIZE 16
static portMUX_TYPE  s_fifo_mux   = portMUX_INITIALIZER_UNLOCKED;
static uint32_t      s_fifo[FIFO_SIZE];
static uint32_t      s_fifo_head;   // next write index
static uint32_t      s_fifo_tail;   // next read index
static uint32_t      s_fifo_count;

bool pagercall_push(uint32_t packed)
{
    bool ok = false;
    portENTER_CRITICAL(&s_fifo_mux);
    if (s_fifo_count < FIFO_SIZE) {
        s_fifo[s_fifo_head] = packed;
        s_fifo_head         = (s_fifo_head + 1) % FIFO_SIZE;
        s_fifo_count++;
        ok = true;
    }
    portEXIT_CRITICAL(&s_fifo_mux);
    return ok;
}

bool pagercall_pop(uint32_t *packed)
{
    bool ok = false;
    portENTER_CRITICAL(&s_fifo_mux);
    if (s_fifo_count > 0) {
        *packed     = s_fifo[s_fifo_tail];
        s_fifo_tail = (s_fifo_tail + 1) % FIFO_SIZE;
        s_fifo_count--;
        ok = true;
    }
    portEXIT_CRITICAL(&s_fifo_mux);
    return ok;
}

void pagercall_cw_start(void)
{
    sx1262_set_tx_continuous_wave(&gs_sx1262);
    delay(10);
}

void pagercall_cw_stop(void)
{
    sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
}


// Encode a RTD157 pager call into a byte sequence suitable for direct output via
// an inverted 6N1 UART, producing a precisely timed OOK bitstream on the TX pin.
//
// Encoding principle — 2 input bits → 1 output byte:
//   Each pair of input bits is mapped to a 6-bit symbol via a lookup table:
//     input "0" → symbol 1000  (short mark, long space)
//     input "1" → symbol 1110  (long mark, short space)
//   Two such 4-bit symbols fill one 8-bit UART data byte (high nibble + low nibble).
//
// Why this works with UART 6N1 inverted:
//   A UART frame with 6 data bits consists of: START(0) + D0..D5 + STOP(1).
//   With inverted TX the line idles LOW, START is HIGH, STOP is LOW — which
//   matches the OOK symbols: every symbol starts with a '1' (mark) and ends
//   with a '0' (space).  The hardware therefore provides the framing bits for
//   free, and the 6 data bits in between carry the OOK pattern.
//   Because the TX line is inverted, the data bits are also inverted in
//   hardware, so the lookup table values are pre-inverted accordingly.
//   The result is bit-perfect OOK timing driven entirely by the UART baud
//   clock — no software bit-banging required.
//
// Output:
//   13 bytes written to 'out' (5 keyboard bytes + 5 pager bytes + 3 action bytes).
//   Returns 13.
uint32_t pagercall_encode_6n1(uint8_t out[13], uint32_t keyboard10, uint32_t pager10, uint32_t action5)
{
    static constexpr uint8_t lookup2[] = { 0x37, 0x07, 0x34, 0x04}; //two bits
    static constexpr uint8_t lookup1[] = { 0x3F, 0x3C }; //only one bit

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

    // Initialize the driver
    uint8_t ret = sx1262_init(&gs_sx1262);
    if (ret != 0)
    {
        Serial.println("[sx1262] init failed");
        return;
    }

    // Enable DC-DC converter (must be called in STBY_RC); reduces core power consumption
    sx1262_set_regulator_mode(&gs_sx1262, SX1262_REGULATOR_MODE_DC_DC_LDO);

    // DIO3 powers the 1.8 V TCXO with an init delay of 10ms
    sx1262_set_dio3_as_tcxo_ctrl(&gs_sx1262, SX1262_TCXO_VOLTAGE_1P8V, 640);

    // Set RF frequency
    uint32_t freq_reg;
    sx1262_frequency_convert_to_register(&gs_sx1262, TX_FREQ_HZ, &freq_reg);
    sx1262_set_rf_frequency(&gs_sx1262, freq_reg);

    // PA config: pa_duty_cycle=0x04, hp_max=0x07 → up to ~22 dBm on SX1262
    sx1262_set_pa_config(&gs_sx1262, 0x04, 0x07);

    // TX power and ramp time (10 µs ramp keeps OOK edges sharp at 4800 baud)
    sx1262_set_tx_params(&gs_sx1262, 14, SX1262_RAMP_TIME_10US);

#if 1
    // Disable DIO2 output (note: writing a 1 disables the output)
    uint8_t oe;
    sx1262_get_dio_output_enable(&gs_sx1262, &oe);
    sx1262_set_dio_output_enable(&gs_sx1262, oe | (1 << 2));

    // Now init UART1, as we will use the TX output as antenna control (instead of DIO2) for the OOK modulation
    // I modified the hardware for this, to connect gpio7 to dio2!
    Serial1.begin(4800, SERIAL_6N1, -1, 7, true); // output pattern on GPIO 7
#else
    // Reference: classic DIO2-as-RF-switch approach (chip drives DIO2 HIGH during TX)
    sx1262_set_dio2_as_rf_switch_ctrl(&gs_sx1262, SX1262_BOOL_TRUE);
#endif

    // Start in standby (carrier off)
    sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
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

    if (strcmp(prefix, "rtd157") == 0)
    {
        const uint32_t action = 4;
        uint32_t packed = ((uint32_t)keyboard << 15) | (pager << 5) | action;
        if (!pagercall_push(packed))
        {
            server.send(503, "text/plain", "Busy");
            return;
        }
        server.send(200, "text/plain", "OK: " + id);
    }
    else
    {
        server.send(501, "text/plain", "Not implemented: " + String(prefix));
    }
}

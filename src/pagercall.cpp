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


// ---------------------------------------------------------------------------
// OOK transmit via SX1262 continuous-wave / standby switching
// ---------------------------------------------------------------------------
#define TX_FREQ_HZ  433920000UL  // 433.92 MHz
static const uint8_t  TX_DBG_PIN  = 19;    // mirrors OOK pattern for debugging

static sx1262_handle_t gs_sx1262;

static void IRAM_ATTR set_ook_bit(int on)
{
    if (on) sx1262_interface_isr_set_cw();
    else    sx1262_interface_isr_set_fs();
    digitalWrite(TX_DBG_PIN, on);
}

static const uint32_t TX_BAUD     = 4800;
static const uint32_t TX_REPEATS  = 32;   // number of transmissions per call

static hw_timer_t *s_timer = nullptr;

static uint8_t  s_tx_data[16];
static volatile uint32_t s_tx_request; // Upper 16 bits specify the number of repetitions, the lower 16 bits specify the number of bits to transmit (per repetition)
static volatile uint32_t s_tx_delay_time; // used to delay initial transmission


// ISR: called at TX_BAUD Hz; outputs one bit per call, MSB-first.
static void IRAM_ATTR on_tx_timer()
{
    static tx_state_t tx_state;
    static uint32_t tx_repetition;  // current repetition number (downcounting)
    static uint32_t tx_bit_numbers; // total number of bits
    static uint32_t tx_bit_number;  // current bit number (downcounting)
    static int32_t  tx_bit_state;   // last transmitted bit state
    static uint32_t tx_timer; //upcounting

    switch (tx_state)
    {
    default:
    case TX_STATE_IDLE:
        if (s_tx_request > 0)
        {
            const uint32_t tx_request = s_tx_request;
            tx_repetition = tx_request >> 16;
            tx_bit_numbers = tx_request & 0x7Fu; // for now, deal with at most 127 bits (wich is ok, because we expect only 100)!
            tx_timer = 0;
            tx_state = TX_STATE_DELAY;
        }
        break;

    case TX_STATE_DELAY:
        // Wait for delay time to expire
        if (++tx_timer >= s_tx_delay_time)
        {
            sx1262_interface_isr_set_fs();  // lock PLL before first CW toggle
            tx_timer = 0;
            tx_state = TX_STATE_SETUP;
        }
        break;

    case TX_STATE_SETUP:
        // Wait for setup time to expire
        if (++tx_timer >= TX_SETUP_TIME)
        {
            tx_bit_state = -1;
            tx_bit_number = tx_bit_numbers;
            tx_state = TX_STATE_TRANSMITTING;
        }
        break;

    case TX_STATE_TRANSMITTING:
        if (tx_bit_number > 0)
        {
            --tx_bit_number;
            // Get state of next tx bit
            uint32_t idx = tx_bit_number / 8; // Get byte index
            const uint32_t byte = s_tx_data[idx]; // Access byte
            idx = tx_bit_number % 8; // Get bit index
            const int32_t bit = (byte >> idx) & 1; // Isolate bit
            if (bit != tx_bit_state)
            {
                set_ook_bit(bit);
                tx_bit_state = bit;
            }
            break;
        }
        // Switch off
        set_ook_bit(0);
        // Go to hold state
        tx_timer = 0;
        tx_state = TX_STATE_HOLD;
        break;

    case TX_STATE_HOLD:
        // Wait for hold time to expire
        if (++tx_timer >= TX_HOLD_TIME)
        {
            if (tx_repetition > 0)
            {
                --tx_repetition;
                tx_timer = 0;
                tx_state = TX_STATE_SETUP;
                break;
            }
            sx1262_interface_isr_set_standby();  // power down after last repetition
            s_tx_request = 0;
            tx_state = TX_STATE_IDLE;
        }
        break;
    }
}


// Symbols for encoding bits (each bit is represented by 4 output bits)
// A '0' bit is represented by 1000 (short on, long off)
// A '1' bit is represented by 1110 (long on, short off)
#define SYMBOL_0    0x08 //0b1000
#define SYMBOL_1    0x0E //0b1110

// Encode input bits into output symbols
// Each input bit is represented by 4 output bits. Thus, 2 input bits fit into one output byte.
// The output is written to the 'out' buffer
// The input is taken from the 'in' variable, which contains 'num' bits (from MSB to LSB)
// The function returns the number of output bits written to the 'out' buffer
static uint32_t rtd157_encode_bits(uint8_t * out, const uint32_t in, const uint32_t num)
{
    uint32_t nibble = num;
    uint32_t mask = 1u << num;
    out[(nibble | 1) / 2] = 0;
    while (nibble > 0)
    {
        --nibble;
        mask = mask >> 1;
        uint8_t val = (in & mask) ? SYMBOL_1 : SYMBOL_0;
        if (nibble & 1) //high nibble
        {
            out[nibble / 2] = (val << 4);
        }
        else //low nibble
        {
            out[nibble / 2] |= val;
        }
    }
    return 4 * num;
}


void pagercall_begin()
{
    // Debug GPIO mirrors the OOK pattern
    pinMode(TX_DBG_PIN, OUTPUT);
    digitalWrite(TX_DBG_PIN, LOW);

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

    // DIO3 powers the 1.8 V TCXO; delay 10 ms = 640 × 15.625 µs ticks
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

    // DIO2 drives the on-board RF switch (required on Heltec V3 for the carrier to reach the antenna)
    ret = sx1262_set_dio2_as_rf_switch_ctrl(&gs_sx1262, SX1262_BOOL_TRUE);
    print_status("set_dio2_as_rf_switch_ctrl", ret);

    // TX power and ramp time (10 µs ramp keeps OOK edges sharp at 4800 baud)
    ret = sx1262_set_tx_params(&gs_sx1262, 14, SX1262_RAMP_TIME_10US);
    print_status("set_tx_params", ret);

    ret = sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_RC_13M);
    print_status("set_standby(RC)", ret);
    delay(100);
    
    ret = sx1262_set_tx_continuous_wave(&gs_sx1262);
    print_status("set_tx_continuous_wave", ret);
    delay(100);
    
    // Start in standby (carrier off)
    ret = sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
    print_status("set_standby(XOSC)", ret);
    delay(100);

    // Timer runs continuously at TX_BAUD Hz; ISR is a no-op while idle
    s_timer = timerBegin(TX_BAUD);
    timerAttachInterrupt(s_timer, &on_tx_timer);
    timerAlarm(s_timer, 1, true, 0);
}


void pagercall_set_mode(WebServer &server)
{
    String arg = server.pathArg(0);
    int mode = arg.toInt();

    switch (mode)
    {
    case 0:
    {
        uint8_t status = 0;
        sx1262_get_status(&gs_sx1262, &status);
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", status);
        server.send(200, "text/plain", buf);
        break;
    }
    case 2:
        sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_RC_13M);
        server.send(200, "text/plain", "STBY_RC");
        break;
    case 3:
        sx1262_set_standby(&gs_sx1262, SX1262_CLOCK_SOURCE_XTAL_32MHZ);
        server.send(200, "text/plain", "STBY_XOSC");
        break;
    case 4:
        sx1262_set_frequency_synthesis(&gs_sx1262);
        server.send(200, "text/plain", "FS");
        break;
    case 6:
        sx1262_set_tx_continuous_wave(&gs_sx1262);
        server.send(200, "text/plain", "CW");
        break;
    default:
        server.send(400, "text/plain", "Unknown mode: " + arg);
        break;
    }
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
        // Trigger pager call
        const uint32_t action = 4;
        const uint32_t code   = ((uint32_t)keyboard << 15) | (pager << 5) | action;
        const uint32_t len = rtd157_encode_bits(s_tx_data, code, 25);
        Serial.printf("[DBG] code=0x%08X  len=%u  s_tx_data=", code, len);
        for (uint32_t i = 0; i < sizeof(s_tx_data); i++) Serial.printf("%02X ", s_tx_data[i]);
        Serial.println();
        s_tx_delay_time = 0; // may be set via query parameter
        s_tx_request = (TX_REPEATS << 16) | len;
        server.send(200, "text/plain", "OK: " + id);
    }
    else
    {
        server.send(501, "text/plain", "Not implemented: " + String(prefix));
    }
}

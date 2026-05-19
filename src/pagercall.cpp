#include <stdint.h>
#include <Arduino.h>
#include "pagercall.h"


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
// GPIO transmit output
// ---------------------------------------------------------------------------
static const uint8_t  TX_PIN      = 2;
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
            tx_bit_numbers = tx_request & 0x1Fu; // for now, we deal with at most 31 bits
            tx_timer = 0;
            tx_state = TX_STATE_DELAY;
        }
        break;

    case TX_STATE_DELAY:
        // Wait for setup time to expire
        if (++tx_timer >= s_tx_delay_time)
        {
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
                digitalWrite(TX_PIN, (uint8_t)bit);
                tx_bit_state = bit;
            }
            break;
        }
        // Switch off
        digitalWrite(TX_PIN, 0);
        // Go to hold state
        tx_timer = 0;
        tx_state = TX_STATE_HOLD;
        break;

    case TX_STATE_HOLD:
        // Wait for setup time to expire
        if (++tx_timer >= TX_HOLD_TIME)
        {
            if (tx_repetition > 0)
            {
                --tx_repetition;
                tx_timer = 0;
                tx_state = TX_STATE_SETUP;
                break;
            }
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
    pinMode(TX_PIN, OUTPUT);
    digitalWrite(TX_PIN, LOW);
    // Timer runs continuously at TX_BAUD Hz; ISR is a no-op while idle
    s_timer = timerBegin(TX_BAUD);
    timerAttachInterrupt(s_timer, &on_tx_timer);
    timerAlarm(s_timer, 1, true, 0);
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

        const uint32_t action = 4;
        const uint32_t code   = ((uint32_t)keyboard << 15) | (pager << 5) | action;
        const uint32_t len = rtd157_encode_bits(s_tx_data, code, 25);
        s_tx_delay_time = 0; // may be set via query parameter
        s_tx_request = (TX_REPEATS << 16) | len;
        server.send(200, "text/plain", "OK: " + id);
    }
    else
    {
        server.send(501, "text/plain", "Not implemented: " + String(prefix));
    }
}

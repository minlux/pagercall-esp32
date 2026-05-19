#pragma once
#include <stdint.h>

/**
 * A pattern is a sequence of durations (ms).  Step 0 is LED ON, step 1 is
 * OFF, step 2 is ON again, … cycling forever.
 */
struct LedPattern {
    const uint16_t *steps;
    uint8_t         count;
};

/* Pre-defined patterns -------------------------------------------------------
 *   LED_PAT_FAST  — 100 ON / 100 OFF        startup window
 *   LED_PAT_SLOW  — 500 ON / 500 OFF        provisioning mode
 *   LED_PAT_ONCE  —  50 ON / 950 OFF        normal, Wi-Fi disconnected
 *   LED_PAT_TWICE —  50 ON / 200 OFF /
 *                    50 ON / 700 OFF        normal, Wi-Fi connected
 * --------------------------------------------------------------------------*/
extern const LedPattern LED_PAT_FAST;
extern const LedPattern LED_PAT_SLOW;
extern const LedPattern LED_PAT_ONCE;
extern const LedPattern LED_PAT_TWICE;

/* ---------------------------------------------------------------------------
 * Led — non-blocking LED pattern engine.
 *
 * Multiple instances can coexist, each driving its own pin.
 *
 *   Led led(35);          // declare (pin stored, no hardware access yet)
 *
 *   void setup() {
 *       led.begin();       // pinMode — call from setup(), not globally
 *   }
 *
 *   void loop() {
 *       led.set(&LED_PAT_FAST);   // idempotent: won't reset a running pattern
 *       led.update();
 *   }
 * --------------------------------------------------------------------------*/
class Led {
public:
    /**
     * @param pin      GPIO pin the LED is wired to.
     * @param activeLow  true  → LED is ON when pin is LOW (common-anode / active-low).
     *                   false → LED is ON when pin is HIGH (common-cathode / active-high).
     */
    Led(uint8_t pin, bool activeLow = false);

    /** Call once from setup() — configures the pin as OUTPUT. */
    void begin();

    /**
     * Switch to pattern p.  Idempotent: re-applying the current pattern is a
     * no-op, so the engine can be called unconditionally every loop().
     */
    void set(const LedPattern *p);

    /** Advance the pattern state machine.  Call every loop() iteration. */
    void update();

private:
    uint8_t           _pin;
    bool              _activeLow;
    const LedPattern *_pat;
    uint8_t           _step;
    unsigned long     _ts;
};

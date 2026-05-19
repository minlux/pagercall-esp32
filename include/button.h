#pragma once
#include <stdint.h>

class Button {
public:
    /**
     * @param pin      GPIO pin the button is wired to.
     * @param activeLow  true  → button pulls the pin LOW when pressed (INPUT_PULLUP).
     *                   false → button pulls the pin HIGH when pressed (INPUT_PULLDOWN).
     */
    Button(uint8_t pin, bool activeLow = false);

    /** Call once from setup() — configures the pin mode. */
    void begin();

    /**
     * Returns true while the button has been held continuously for at least ms
     * milliseconds.  Resets automatically when the button is released.
     */
    bool heldFor(uint32_t ms);

private:
    uint8_t  _pin;
    bool     _activeLow;
    uint32_t _pressTs;
    bool     _armed;
};

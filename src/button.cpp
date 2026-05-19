#include "button.h"
#include <Arduino.h>

Button::Button(uint8_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow), _pressTs(0), _armed(false)
{
}

void Button::begin()
{
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

bool Button::heldFor(uint32_t ms)
{
    bool pressed = _activeLow ? (digitalRead(_pin) == LOW)
                              : (digitalRead(_pin) == HIGH);
    if (!pressed) {
        _armed   = false;
        _pressTs = 0;
        return false;
    }
    if (!_armed) {
        _armed   = true;
        _pressTs = millis();
    }
    return (millis() - _pressTs) >= ms;
}

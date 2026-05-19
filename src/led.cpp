#include "led.h"
#include <Arduino.h>

/* ---------------------------------------------------------------------------
 * Pattern data
 * --------------------------------------------------------------------------*/
static const uint16_t _FAST[]  = {100, 100};
static const uint16_t _SLOW[]  = {500, 500};
static const uint16_t _ONCE[]  = { 50, 950};
static const uint16_t _TWICE[] = { 50, 200, 50, 700};

const LedPattern LED_PAT_FAST  = {_FAST,  2};
const LedPattern LED_PAT_SLOW  = {_SLOW,  2};
const LedPattern LED_PAT_ONCE  = {_ONCE,  2};
const LedPattern LED_PAT_TWICE = {_TWICE, 4};

/* ---------------------------------------------------------------------------
 * Led implementation
 * --------------------------------------------------------------------------*/
Led::Led(uint8_t pin, bool activeLow)
    : _pin(pin), _activeLow(activeLow), _pat(nullptr), _step(0), _ts(0)
{
}

void Led::begin()
{
    pinMode(_pin, OUTPUT);
}

void Led::set(const LedPattern *p)
{
    if (p == _pat) return;
    _pat  = p;
    _step = 0;
    _ts   = millis();
    digitalWrite(_pin, _activeLow ? LOW : HIGH);
}

void Led::update()
{
    if (!_pat) return;
    if (millis() - _ts < _pat->steps[_step]) return;

    _step = (_step + 1) % _pat->count;
    _ts   = millis();
    bool on = (_step % 2 == 0);
    digitalWrite(_pin, (on ^ _activeLow) ? HIGH : LOW);
}

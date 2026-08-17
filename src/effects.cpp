#include "effects.h"
#include "led_driver.h"
#include "identify.h"
#include <string.h>

// The fixture has two independent light sources: the addressable RGB outer ring
// (`leds`) and the inner CW/WW white string on PWM. Effects that want white use
// the white string; effects that want colour use the ring. Each effect sets both
// so nothing is left lit from the previous one.

static uint32_t speed_to_period_ms(uint8_t speed) {
    return 200 + uint32_t(10000 - 200) * (255 - speed) / 255;
}

static void ring_off(CRGB* leds) {
    memset(leds, 0, RING_NUM_LEDS * sizeof(CRGB));
}

static void white_off() {
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_static_white(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    // Colour temperature rides on `hue`: 0 = fully warm, 255 = fully cool.
    const uint8_t level = on ? p.brightness : 0;
    ring_off(leds);
    led_driver_set_ww(scale8(level, 255 - p.hue));
    led_driver_set_cw(scale8(level, p.hue));
}

static void fx_static_color(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    const CRGB c = on ? scale_brightness(hsv_to_rgb(p.hue, p.sat, 255), p.brightness)
                      : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}

static void fx_warm_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        const uint8_t pos  = (i * 256 / RING_NUM_LEDS + offset) & 0xFF;
        const uint8_t warm = scale8(p.brightness, 255 - pos);
        const uint8_t cool = scale8(p.brightness, pos);
        leds[i] = {scale8(warm, 200), scale8(warm, 100), cool};
    }
    white_off();
}

static void fx_color_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        const uint8_t hue = p.hue + (i * 256 / RING_NUM_LEDS) + offset;
        leds[i] = scale_brightness(hsv_to_rgb(hue, p.sat, 255), p.brightness);
    }
    white_off();
}

static void fx_breathing(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t t      = elapsed_ms % period;
    const uint8_t  half   = (t < period / 2)
        ? uint8_t(t * 255 / (period / 2))
        : uint8_t(255 - (t - period / 2) * 255 / (period / 2));
    const CRGB c = scale_brightness(hsv_to_rgb(p.hue, p.sat, 255), scale8(p.brightness, half));
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}

static void fx_color_cycle(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  hue    = p.hue + uint8_t(elapsed_ms % period * 256 / period);
    const CRGB     c      = scale_brightness(hsv_to_rgb(hue, p.sat, 255), p.brightness);
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}

static void fx_chase(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t step   = period / RING_NUM_LEDS;
    const uint32_t pos    = step ? (elapsed_ms / step) % RING_NUM_LEDS : 0;
    const CRGB     c      = scale_brightness(hsv_to_rgb(p.hue, p.sat, 255), p.brightness);
    for (int i = 0; i < RING_NUM_LEDS; i++)
        leds[i] = (uint32_t(i) == pos) ? c : CRGB{};
    white_off();
}

static void fx_nightlight(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    // These pixels have no white die, so mix a warm white from the RGB dice.
    const CRGB c = on ? warm_white(p.brightness) : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}

// Blue was chosen because nothing else the fixture does looks like it: no
// effect or colour setting produces a blinking blue ring, so there is no
// ambiguity about which can in the ceiling is being identified.
void fx_identify(uint32_t elapsed_ms, CRGB* leds) {
    const bool lit = (elapsed_ms % IDENTIFY_BLINK_PERIOD_MS) < (IDENTIFY_BLINK_PERIOD_MS / 2);
    const CRGB c   = lit ? CRGB{0, 0, 255} : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}

const Effect kEffects[EFFECT_COUNT] = {
    {"Static White",   fx_static_white},
    {"Static Color",   fx_static_color},
    {"Warm Gradient",  fx_warm_gradient},
    {"Color Gradient", fx_color_gradient},
    {"Breathing",      fx_breathing},
    {"Color Cycle",    fx_color_cycle},
    {"Chase",          fx_chase},
    {"Nightlight",     fx_nightlight},
};

#include "effects.h"
#include "led_driver.h"
#include <string.h>

static uint32_t speed_to_period_ms(uint8_t speed) {
    return 200 + uint32_t(10000 - 200) * (255 - speed) / 255;
}

static void fx_static_white(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const uint8_t level = on ? p.brightness : 0;
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = {0, 0, 0, 0};
    led_driver_set_ww(scale8(level, 255 - p.hue));
    led_driver_set_cw(scale8(level, p.hue));
}

static void fx_static_color(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const CRGBW c = on ? scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), p.brightness)
                       : CRGBW{};
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_warm_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < SK6812_NUM_LEDS; i++) {
        const uint8_t pos  = (i * 256 / SK6812_NUM_LEDS + offset) & 0xFF;
        const uint8_t warm = scale8(p.brightness, 255 - pos);
        const uint8_t cool = scale8(p.brightness, pos);
        leds[i] = {scale8(warm, 200), scale8(warm, 100), cool, 0};
    }
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_color_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < SK6812_NUM_LEDS; i++) {
        const uint8_t hue = p.hue + (i * 256 / SK6812_NUM_LEDS) + offset;
        leds[i] = scale_brightness(hsv_to_rgbw(hue, p.sat, 255), p.brightness);
    }
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_breathing(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t t      = elapsed_ms % period;
    const uint8_t  half   = (t < period / 2)
        ? uint8_t(t * 255 / (period / 2))
        : uint8_t(255 - (t - period / 2) * 255 / (period / 2));
    const CRGBW c = scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), scale8(p.brightness, half));
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_color_cycle(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  hue    = p.hue + uint8_t(elapsed_ms % period * 256 / period);
    const CRGBW    c      = scale_brightness(hsv_to_rgbw(hue, p.sat, 255), p.brightness);
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_chase(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t pos    = (elapsed_ms / (period / SK6812_NUM_LEDS)) % SK6812_NUM_LEDS;
    const CRGBW    c      = scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), p.brightness);
    for (int i = 0; i < SK6812_NUM_LEDS; i++)
        leds[i] = (uint32_t(i) == pos) ? c : CRGBW{};
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

static void fx_nightlight(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const CRGBW c = on ? CRGBW{0, 0, 0, p.brightness} : CRGBW{};
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
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

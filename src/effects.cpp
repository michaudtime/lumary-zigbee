#include "effects.h"
#include "brightness.h"
#include "identify.h"
#include <string.h>

// The outer RGB ring's effects. Each fills the `leds` buffer and nothing else:
// the inner CW/WW white string is a separate Zigbee endpoint and a separate
// Home Assistant entity, rendered directly by the render loop, so no effect
// touches it -- this file no longer drives the white string. It is not free of
// hardware headers, though: effects.h pulls in config.h for RING_NUM_LEDS,
// and config.h includes driver/ledc.h, so this still cannot compile on the
// host as-is (unlike pixel_encode.h, which is deliberately kept clean for
// that). Moving RING_NUM_LEDS into a hardware-free header would fix that, but
// is out of scope here.

static uint32_t speed_to_period_ms(uint8_t speed) {
    return 200 + uint32_t(10000 - 200) * (255 - speed) / 255;
}

static void ring_off(CRGB* leds) {
    memset(leds, 0, RING_NUM_LEDS * sizeof(CRGB));
}

void fx_ring_solid(const EffectParams& p, CRGB* leds, bool light_on) {
    const CRGB c = light_on ? scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), p.brightness)
                            : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}

static void fx_warm_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        const uint8_t pos = (i * 256 / RING_NUM_LEDS + offset) & 0xFF;
        // Blend the two ends at full intensity and curve once. Folding
        // brightness into each end and curving them separately would distort
        // the crossfade the same way it would distort colour temperature.
        const CRGB c = blend(CRGB{200, 100, 0}, CRGB{0, 0, 255}, pos);
        leds[i] = scale_brightness_gamma(c, p.brightness);
    }
}

static void fx_color_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        const uint8_t hue = p.hue + (i * 256 / RING_NUM_LEDS) + offset;
        leds[i] = scale_brightness_gamma(hsv_to_rgb(hue, p.sat, 255), p.brightness);
    }
}

static void fx_breathing(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t t      = elapsed_ms % period;
    const uint8_t  half   = (t < period / 2)
        ? uint8_t(t * 255 / (period / 2))
        : uint8_t(255 - (t - period / 2) * 255 / (period / 2));
    const CRGB c = scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), scale8(p.brightness, half));
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}

static void fx_color_cycle(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  hue    = p.hue + uint8_t(elapsed_ms % period * 256 / period);
    const CRGB     c      = scale_brightness_gamma(hsv_to_rgb(hue, p.sat, 255), p.brightness);
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}

static void fx_chase(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t step   = period / RING_NUM_LEDS;
    const uint32_t pos    = step ? (elapsed_ms / step) % RING_NUM_LEDS : 0;
    const CRGB     c      = scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), p.brightness);
    for (int i = 0; i < RING_NUM_LEDS; i++)
        leds[i] = (uint32_t(i) == pos) ? c : CRGB{};
}

static void fx_nightlight(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    // These pixels have no white die, so mix a warm white from the RGB dice.
    // The curve goes on the level, not on the mixed colour -- warm_white()
    // scales fixed ratios, and curving its output would shift them.
    // warm_white_gamma() (not warm_white(gamma8(...))): warm_white() mixes
    // through color.h's scale8, whose >>8 zeroes out the low-end floor that
    // gamma8() bakes in (see brightness.h). warm_white_gamma() mixes the same
    // ratio through scale_by_255 instead, so the floor survives here too.
    const CRGB c = on ? warm_white_gamma(p.brightness) : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}

// Blue was chosen because nothing else the fixture does looks like it: no
// effect or colour setting produces a blinking blue ring, so there is no
// ambiguity about which can in the ceiling is being identified.
void fx_identify(uint32_t elapsed_ms, CRGB* leds) {
    const bool lit = (elapsed_ms % IDENTIFY_BLINK_PERIOD_MS) < (IDENTIFY_BLINK_PERIOD_MS / 2);
    const CRGB c   = lit ? CRGB{0, 0, 255} : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}

const Effect kEffects[EFFECT_COUNT] = {
    {"Warm Gradient",  fx_warm_gradient},
    {"Color Gradient", fx_color_gradient},
    {"Breathing",      fx_breathing},
    {"Color Cycle",    fx_color_cycle},
    {"Chase",          fx_chase},
    {"Nightlight",     fx_nightlight},
};

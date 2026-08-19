#pragma once
#include <stdint.h>
#include "color.h"
#include "brightness.h"
#include "effect_recipe.h"
#include "ring_geometry.h"

// The one renderer. Walks an EffectRecipe and fills the ring buffer.
//
// Replaces the six hand-written effect functions with a single pass that reads
// the recipe's four axes. Everything here is integer arithmetic on purpose:
// the designer's simulator reimplements this file in JavaScript, and golden
// vectors generated from the host build assert the two agree byte for byte.
// A float anywhere would make that agreement a matter of luck.
//
// == Brightness lands exactly ONCE ==
//
// The bench found effects collapsing to black below roughly brightness 20
// (2026-08-18, section 7). The cause was structural: each effect pre-scaled its
// own pixels, and THEN the brightness multiplier landed, so two 8-bit
// truncations stacked and mid-gradient pixels truncated to zero.
//
// This renderer has four things that want to darken a pixel -- the entity's
// brightness, the temporal envelope, the spatial weight, and the palette
// stop's own `v` -- and it multiplies all four together in 32-bit PERCEPTUAL
// space, applies the gamma curve once to the result, and scales the
// full-intensity colour by it once. One truncation, at the end.
//
// This does not turn 8-bit output into 12-bit; the ring's bottom few steps are
// still coarse. It does mean the low end is as good as 8 bits allows instead of
// squandering half its range on redundant rounding, and it leaves exactly one
// place for temporal dithering to go if that is ever built.

// Bit mixer (Chris Wellons' `lowbias32`), used for sparkle phases and the noise
// envelope. A stateless hash rather than a stateful PRNG so a pixel's value
// depends only on its index and the frame -- no ordering dependency, which is
// what makes the golden vectors reproducible from either end.
//
// PORTING HAZARD: JavaScript has no native uint32 arithmetic. `*` overflows
// into doubles and `<<`/`>>` are signed, so the JS twin needs Math.imul for
// every multiply and `>>> 0` to stay unsigned. This function is the single
// most likely source of a simulator that looks right and is wrong, which is
// why the golden vectors cover sparkle and candle specifically.
inline uint32_t fx_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Unchanged from the hand-written effects, so a recipe at a given speed runs at
// the same rate the old effect did: 255 -> 200 ms, 0 -> 10 s.
inline uint32_t recipe_period_ms(uint8_t speed) {
    return 200 + uint32_t(10000 - 200) * (255 - speed) / 255;
}

// Symmetric triangle: 0 -> 255 over the first half of `period`, back down over
// the second. The shape the built-in Breathing effect had.
inline uint8_t recipe_triangle(uint32_t t, uint32_t period) {
    if (period == 0) return 255;
    const uint32_t half = period / 2;
    if (half == 0) return 255;
    return (t < half) ? uint8_t(t * 255 / half)
                      : uint8_t(255 - (t - half) * 255 / half);
}

// Distance between two positions on a 256-step circle, taking the short way.
inline uint8_t recipe_circular_distance(uint8_t a, uint8_t b) {
    const int d = int(a) - int(b);
    const int m = d < 0 ? -d : d;
    return uint8_t(m > 128 ? 256 - m : m);
}

// A colour sampled from the palette, at FULL intensity, plus the weight the
// stop asked for. Keeping the two apart is what lets brightness land once.
struct RecipeSample {
    CRGB    rgb;   // full-intensity colour
    uint8_t v;     // perceptual weight from the stop, 0..255
};

// Samples the palette at `pos` (0..255 around the palette, cyclic).
//
// Blending happens in RGB at full intensity, with the stops' `v` values lerped
// alongside as a separate scalar. Blending in HSV instead would sweep hue
// through intermediate colours nobody asked for -- an amber-to-blue gradient
// would pass through green -- and baking `v` into the colour before the blend
// would pre-scale the pixel, which is the thing this renderer exists to avoid.
inline RecipeSample recipe_palette_at(const EffectRecipe& r, uint8_t pos) {
    if (r.palette_kind == PALETTE_SOLID) {
        return {hsv_to_rgb(r.stops[0].h, r.stops[0].s, 255), r.stops[0].v};
    }
    if (r.palette_kind == PALETTE_HUE_RAMP) {
        const uint8_t hue = uint8_t(r.stops[0].h + pos);
        return {hsv_to_rgb(hue, r.stops[0].s, 255), r.stops[0].v};
    }

    // PALETTE_STOPS, cyclic: the last stop wraps back to the first, so a
    // rotating gradient has no seam.
    const uint8_t  n       = r.stop_count;
    const uint16_t scaled  = uint16_t(pos) * n;          // 0 .. n*255
    const uint8_t  index   = uint8_t(scaled >> 8);       // which stop, 0..n-1
    const uint8_t  frac    = uint8_t(scaled & 0xFF);     // how far into it

    const RecipeStop& a = r.stops[index < n ? index : uint8_t(n - 1)];
    if (r.palette_interp == INTERP_STEP) {
        return {hsv_to_rgb(a.h, a.s, 255), a.v};
    }

    const RecipeStop& b = r.stops[uint8_t((index + 1) % n)];
    const CRGB ca = hsv_to_rgb(a.h, a.s, 255);
    const CRGB cb = hsv_to_rgb(b.h, b.s, 255);
    const uint8_t v = uint8_t(a.v + ((int(b.v) - int(a.v)) * int(frac) >> 8));
    return {blend(ca, cb, frac), v};
}

// Where the pattern has travelled to by `elapsed_ms`, as a 0..255 position.
inline uint8_t recipe_phase(const EffectRecipe& r, uint32_t elapsed_ms) {
    if (r.motion == MOTION_STILL) return 0;

    const uint32_t period = recipe_period_ms(r.speed);
    const uint32_t t      = elapsed_ms % period;
    const uint8_t  raw    = (r.motion == MOTION_BOUNCE)
        ? recipe_triangle(t, period)
        : uint8_t(t * 256 / period);

    return r.direction ? uint8_t(255 - raw) : raw;
}

// The temporal brightness envelope, 0..255.
//
// `envelope_depth` is how much of the swing to take: 0 leaves the effect
// unmodulated whatever the shape, 255 lets it reach full black at the trough.
// That keeps depth and shape independent, so a user can dial a breathe back to
// a shimmer without changing anything else.
inline uint8_t recipe_envelope(const EffectRecipe& r, uint32_t elapsed_ms) {
    if (r.envelope == ENV_NONE || r.envelope_depth == 0) return 255;

    const uint32_t period = recipe_period_ms(r.envelope_speed);
    const uint32_t t      = elapsed_ms % period;

    uint8_t raw;
    switch (r.envelope) {
        case ENV_BREATHE:
            raw = recipe_triangle(t, period);
            break;
        case ENV_PULSE: {
            // Fast attack, slow decay -- a heartbeat rather than a breath.
            const uint32_t attack = period / 8;
            raw = (t < attack && attack > 0)
                ? uint8_t(t * 255 / attack)
                : uint8_t(255 - (t - attack) * 255 / (period - attack));
            break;
        }
        case ENV_SAW:
            raw = uint8_t(t * 255 / period);
            break;
        case ENV_NOISE: {
            // Interpolated value noise: one random target per period, eased
            // into the next, so a candle flickers instead of strobing.
            const uint32_t step = elapsed_ms / period;
            const uint8_t  a    = uint8_t(fx_hash(step)     & 0xFF);
            const uint8_t  b    = uint8_t(fx_hash(step + 1) & 0xFF);
            const uint8_t  mix  = uint8_t(t * 255 / period);
            raw = uint8_t(a + ((int(b) - int(a)) * int(mix) >> 8));
            break;
        }
        default:
            raw = 255;
            break;
    }

    // depth 0 -> always 255; depth 255 -> the raw wave, troughs and all.
    return uint8_t(255 - scale_by_255(r.envelope_depth, uint8_t(255 - raw)));
}

// Curves a perceptual level and scales a full-intensity colour by it, ROUNDING
// rather than truncating the way brightness.h's scale_by_255() does.
//
// This is the "cheap partial" the bench named for the low-end collapse
// (2026-08-18, section 7), applied at the one point this renderer has for it.
// Truncation loses a whole output count on every channel: a mid-gradient pixel
// at {127,...} times a gamma multiplier of 2 computes 254/255, which truncates
// to 0 and goes black. Rounding lands it on 1, which is dim but lit -- and on a
// 62-pixel ring the difference between "dim" and "off" is the difference
// between a gradient and a gap.
//
// Deliberately local to the renderer rather than a change to scale_by_255()
// itself. That function is also on the white string's path via
// warm_white_gamma(), and changing the downlight's low end is a separate change
// with its own bench check -- not a side effect of building an effect editor.
inline CRGB recipe_scale_gamma(CRGB c, uint8_t perceptual) {
    const uint8_t g = gamma8(perceptual);
    return {
        uint8_t((uint16_t(c.r) * g + 127) / 255),
        uint8_t((uint16_t(c.g) * g + 127) / 255),
        uint8_t((uint16_t(c.b) * g + 127) / 255),
    };
}

// Multiplies four perceptual factors and divides once, with rounding.
//
// 255^4 overflows 32 bits, so this folds in two rounded halves rather than one
// product. Rounding rather than truncating for the same reason
// white_mix_gamma() rounds: at the bottom of the range a truncated factor
// collapses a real request to zero, which is precisely the failure this
// renderer is built to avoid.
inline uint8_t recipe_combine(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    const uint32_t ab = (uint32_t(a) * b + 127) / 255;
    const uint32_t cd = (uint32_t(c) * d + 127) / 255;
    return uint8_t((ab * cd + 127) / 255);
}

// Fills `leds` with one frame. `light_on` false blanks the ring -- the entity
// is off, and the recipe is not consulted at all.
//
// `level` is the Zigbee entity's brightness, applied on top of the recipe's own
// so a user can dim a designed effect without editing it.
inline void recipe_render(const EffectRecipe& r, uint32_t elapsed_ms,
                          uint8_t level, bool light_on,
                          CRGB* leds, uint16_t count) {
    if (!light_on) {
        for (uint16_t i = 0; i < count; i++) leds[i] = CRGB{};
        return;
    }

    const uint8_t phase    = recipe_phase(r, elapsed_ms);
    const uint8_t env      = recipe_envelope(r, elapsed_ms);
    const uint8_t base     = scale_by_255(r.brightness, level);
    const uint8_t repeat   = r.repeat ? r.repeat : 1;

    // SEGMENT geometry, in the same 0..255 ring coordinates as everything else.
    const uint8_t half     = uint8_t(r.span / 2);
    const uint16_t reach   = uint16_t(half) + r.falloff;

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t ring_pos = uint8_t(uint32_t(i) * 256 / count);

        uint8_t pos;      // where in the palette this pixel samples
        uint8_t weight;   // how strongly this pixel is lit, before everything else

        switch (r.spatial) {
            case SPATIAL_UNIFORM:
                pos    = phase;
                weight = 255;
                break;

            case SPATIAL_GRADIENT:
                // Minus, not plus, so the pattern travels in +i as phase grows
                // -- the same direction the segment below travels. The two
                // hand-written effects disagreed about this (old Chase ran
                // forward, old Color Gradient ran backward); `direction` is
                // the control for it now, and it means one thing.
                pos    = uint8_t((uint32_t(ring_pos) * repeat + 256 - phase) & 0xFF);
                weight = 255;
                break;

            case SPATIAL_SEGMENT: {
                // `repeat` copies of the band, evenly spaced around the ring,
                // with the lit core sitting on `phase`.
                const uint8_t local = uint8_t((uint32_t(ring_pos) * repeat) & 0xFF);
                const uint8_t d     = recipe_circular_distance(local, phase);
                if (d > reach) {
                    pos    = 0;
                    weight = 0;
                } else if (d <= half || reach == 0) {
                    // Inside the solid core. Sample the palette across the
                    // band so a two-stop segment reads as a comet: head at
                    // stop 0, tail fading into stop 1.
                    pos    = uint8_t(uint16_t(d) * 255 / (reach ? reach : 1));
                    weight = 255;
                } else {
                    pos    = uint8_t(uint16_t(d) * 255 / reach);
                    // Linear ramp out through the tail.
                    weight = uint8_t(255 - uint16_t(d - half) * 255 / (reach - half));
                }
                break;
            }

            case SPATIAL_SPARKLE:
            default: {
                // Each pixel twinkles on its own phase, so the ring shimmers
                // rather than blinking in unison. The hash gives that phase and
                // the palette position, both fixed per pixel and per frame.
                const uint32_t h = fx_hash(uint32_t(i) + 1);
                const uint8_t  offset = uint8_t(h & 0xFF);
                const uint32_t period = recipe_period_ms(r.speed);
                const uint32_t t = (elapsed_ms + uint32_t(offset) * period / 256) % period;
                pos    = uint8_t((h >> 8) & 0xFF);
                weight = recipe_triangle(t, period);
                break;
            }
        }

        if (weight == 0) { leds[i] = CRGB{}; continue; }

        const RecipeSample s = recipe_palette_at(r, pos);

        // The one place brightness lands: four perceptual factors combined,
        // one gamma lookup, one rounded scale of a full-intensity colour.
        const uint8_t perceptual = recipe_combine(base, env, weight, s.v);
        leds[i] = recipe_scale_gamma(s.rgb, perceptual);
    }
}

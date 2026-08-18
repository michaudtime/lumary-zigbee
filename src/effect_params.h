#pragma once
#include <stdint.h>

// Effects belong to the ring. `static_white` used to be index 0; it is now the
// Downlight entity on endpoint 1, not an effect. `static_color` used to be
// index 1; it is now `effect: none` with a colour set, rendered by
// fx_ring_solid, which is deliberately not in this table.
enum EffectType : uint8_t {
    EFFECT_WARM_GRADIENT  = 0,
    EFFECT_COLOR_GRADIENT = 1,
    EFFECT_BREATHING      = 2,
    EFFECT_COLOR_CYCLE    = 3,
    EFFECT_CHASE          = 4,
    EFFECT_NIGHTLIGHT     = 5,
    EFFECT_COUNT          = 6
};

struct EffectParams {
    EffectType type;
    uint8_t    hue;
    uint8_t    sat;
    uint8_t    brightness;
    uint8_t    speed;
};

static const EffectParams kDefaultParams[EFFECT_COUNT] = {
    {EFFECT_WARM_GRADIENT,  8, 220, 200,  60},
    {EFFECT_COLOR_GRADIENT, 0, 255, 200,  60},
    {EFFECT_BREATHING,      0, 255, 255,  80},
    {EFFECT_COLOR_CYCLE,    0, 255, 200, 100},
    {EFFECT_CHASE,          0, 255, 255, 120},
    {EFFECT_NIGHTLIGHT,     8, 180,  50,   0},
};

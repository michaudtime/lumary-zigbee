#pragma once
#include <stdint.h>

enum EffectType : uint8_t {
    EFFECT_STATIC_WHITE   = 0,
    EFFECT_STATIC_COLOR   = 1,
    EFFECT_WARM_GRADIENT  = 2,
    EFFECT_COLOR_GRADIENT = 3,
    EFFECT_BREATHING      = 4,
    EFFECT_COLOR_CYCLE    = 5,
    EFFECT_CHASE          = 6,
    EFFECT_NIGHTLIGHT     = 7,
    EFFECT_COUNT          = 8
};

struct EffectParams {
    EffectType type;
    uint8_t    hue;
    uint8_t    sat;
    uint8_t    brightness;
    uint8_t    speed;
};

static const EffectParams kDefaultParams[EFFECT_COUNT] = {
    {EFFECT_STATIC_WHITE,   0,   0, 255,   0},
    {EFFECT_STATIC_COLOR,   0, 255, 255,   0},
    {EFFECT_WARM_GRADIENT,  8, 220, 200,  60},
    {EFFECT_COLOR_GRADIENT, 0, 255, 200,  60},
    {EFFECT_BREATHING,      0, 255, 255,  80},
    {EFFECT_COLOR_CYCLE,    0, 255, 200, 100},
    {EFFECT_CHASE,          0, 255, 255, 120},
    {EFFECT_NIGHTLIGHT,     8, 180,  50,   0},
};

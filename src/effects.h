#pragma once
#include "effect_params.h"
#include "color.h"
#include "config.h"

typedef void (*EffectFn)(uint32_t elapsed_ms, const EffectParams& p,
                         CRGBW* leds, bool light_on);

struct Effect {
    const char* name;
    EffectFn    fn;
};

extern const Effect kEffects[EFFECT_COUNT];

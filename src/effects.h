#pragma once
#include "effect_params.h"
#include "color.h"
#include "config.h"

typedef void (*EffectFn)(uint32_t elapsed_ms, const EffectParams& p,
                         CRGB* leds, bool light_on);

struct Effect {
    const char* name;
    EffectFn    fn;
};

extern const Effect kEffects[EFFECT_COUNT];

// The Identify blink. Deliberately NOT a member of kEffects: that table is
// positionally indexed by both Home Assistant's effect_list and the NVS scene
// store, so adding identify there would put it in the effect dropdown and in
// the scene table. It is an overlay the render loop draws instead of the
// resolved effect, not something a user can select.
void fx_identify(uint32_t elapsed_ms, CRGB* leds);

// The ring showing a plain colour rather than running an effect -- what
// MODE_COLOR renders. Deliberately NOT in kEffects, for the same reason
// fx_identify is not: that table is positionally indexed by both Home
// Assistant's effect_list and the NVS scene store, and "solid colour" is
// `effect: none`, not a selectable effect.
void fx_ring_solid(const EffectParams& p, CRGB* leds, bool light_on);

#pragma once
#include <stdint.h>
#include "color.h"
#include "effect_params.h"

// Translates what Zigbee tells us (on/off, level, colour, scene) into what the
// fixture should actually do. The fixture has two light sources -- the 62-pixel
// RGB ring and the inner CW/WW white string -- and this layer decides which one
// carries a given command.
//
// Deliberately free of ESP-IDF headers so it can be unit-tested on the host;
// zigbee_light.cpp is the thin adapter that drives it. See test/test_light_state.

// Saturation at or below this reads as "white" and is routed to the inner
// CW/WW string rather than the colour ring.
#define WHITE_SAT_THRESHOLD 32

// The Colour Control cluster reports colour temperature in mireds (1e6 / K), so
// a LARGER number is warmer. These bound the fixture's two white strings and are
// what we advertise to the coordinator via setLightColorTemperatureRange().
#define CCT_MIRED_WARM 370   // 2700 K -- the 27K- (WW) string
#define CCT_MIRED_COOL 154   // 6500 K -- the 65K- (CW) string

// Reported in place of an effect index when the fixture is showing a plain
// colour rather than running one of the built-in effects, and accepted as a
// selection meaning "stop the effect, keep the colour". Home Assistant's effect
// list has no null member -- an effect is just a string from `effect_list` --
// so "no effect" has to be a value like any other. 0xFF cannot collide with a
// real index: the scene table holds NVS_MAX_SCENES (16) entries at most.
#define LIGHT_EFFECT_NONE 0xFF

enum LightMode : uint8_t {
    MODE_SCENE,   // running a stored effect from the scene table
    MODE_COLOR,   // showing a colour set directly over Zigbee
};

struct LightState {
    bool      on;
    uint8_t   level;    // master brightness from the Level Control cluster
    uint8_t   hue;      // last commanded colour
    uint8_t   sat;
    uint8_t   cct;      // inferred white balance, 0 = warm .. 255 = cool
    uint8_t   scene;    // active index into the scene table
    LightMode mode;
};

struct HSV {
    uint8_t h, s, v;
};

// Hue uses the same 43-per-sextant scale as hsv_to_rgb(), so the two round-trip.
inline HSV rgb_to_hsv(CRGB c) {
    const uint8_t mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    const uint8_t mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    if (mx == 0) return {0, 0, 0};
    const uint8_t delta = uint8_t(mx - mn);
    const uint8_t s     = uint8_t((uint16_t(delta) * 255) / mx);
    if (delta == 0) return {0, 0, mx};
    int h;
    if (mx == c.r)      h =       43 * (int(c.g) - int(c.b)) / delta;
    else if (mx == c.g) h =  85 + 43 * (int(c.b) - int(c.r)) / delta;
    else                h = 171 + 43 * (int(c.r) - int(c.g)) / delta;
    if (h < 0) h += 256;
    return {uint8_t(h & 0xFF), s, mx};
}

// Maps a Zigbee colour temperature (mireds) onto the CW/WW mix, where
// 0 = fully warm and 255 = fully cool. Values outside the fixture's range clamp.
inline uint8_t mireds_to_cct(uint16_t mireds) {
    if (mireds >= CCT_MIRED_WARM) return 0;
    if (mireds <= CCT_MIRED_COOL) return 255;
    return uint8_t((uint32_t(CCT_MIRED_WARM - mireds) * 255)
                   / (CCT_MIRED_WARM - CCT_MIRED_COOL));
}

// Fallback for coordinators that express white as an RGB colour rather than a
// colour-temperature command: infer warmth from the red/blue balance.
inline uint8_t rgb_to_cct(CRGB c) {
    const int cct = 128 + (int(c.b) - int(c.r)) / 2;
    return uint8_t(cct < 0 ? 0 : (cct > 255 ? 255 : cct));
}

// Level 255 must leave a value untouched, which plain scale8 can't do (it maps
// 255 -> 254), so scale by level+1 instead.
inline uint8_t scale_level(uint8_t val, uint8_t level) {
    return uint8_t((uint16_t(val) * (uint16_t(level) + 1)) >> 8);
}

inline void light_state_init(LightState* s) {
    s->on    = false;
    s->level = 255;
    s->hue   = 0;
    s->sat   = 0;
    s->cct   = 128;
    s->scene = 0;
    s->mode  = MODE_SCENE;
}

inline void light_state_set_color(LightState* s, CRGB c) {
    const HSV h = rgb_to_hsv(c);
    s->hue  = h.h;
    s->sat  = h.s;
    s->cct  = rgb_to_cct(c);
    s->mode = MODE_COLOR;
}

// A colour-temperature command is a white command: zero saturation, so resolve()
// routes it to the CW/WW string rather than the ring.
inline void light_state_set_cct(LightState* s, uint16_t mireds) {
    s->sat  = 0;
    s->cct  = mireds_to_cct(mireds);
    s->mode = MODE_COLOR;
}

// Selecting a scene returns the light to MODE_SCENE. The index arrives over the
// air from anything that can write the effect attribute, so it is validated
// here rather than at the Zigbee adapter: an out-of-range value is ignored
// outright, leaving both the scene and the current mode untouched. Silently
// clamping instead would strand the light on a scene nobody asked for.
inline void light_state_set_scene(LightState* s, uint8_t index, uint8_t scene_count) {
    if (scene_count == 0 || index >= scene_count) return;
    s->scene = index;
    s->mode  = MODE_SCENE;
}

// Leaves effect mode without disturbing the colour, which is what the fixture
// carries on showing. The inverse of light_state_set_scene, and the firmware
// side of picking "none" in Home Assistant's effect dropdown.
inline void light_state_clear_scene(LightState* s) {
    s->mode = MODE_COLOR;
}

// What the effect attribute should report: the running effect, or
// LIGHT_EFFECT_NONE once a colour command has taken the fixture out of effect
// mode. Without the second half the attribute goes on naming the last effect
// selected, and Home Assistant shows "chase" over a static colour.
inline uint8_t light_state_effect_value(const LightState* s) {
    return s->mode == MODE_SCENE ? s->scene : LIGHT_EFFECT_NONE;
}

inline void light_state_next_scene(LightState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    light_state_set_scene(s, uint8_t((s->scene + 1) % scene_count), scene_count);
}

inline void light_state_prev_scene(LightState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    light_state_set_scene(s, uint8_t((s->scene + scene_count - 1) % scene_count), scene_count);
}

// Combines the live state with the stored parameters of the active scene to
// produce the frame the effect engine should render.
inline EffectParams light_state_resolve(const LightState* s, const EffectParams* scene) {
    EffectParams p;
    if (s->mode == MODE_COLOR) {
        if (s->sat <= WHITE_SAT_THRESHOLD) {
            p.type = EFFECT_STATIC_WHITE;
            p.hue  = s->cct;              // fx_static_white reads hue as colour temp
            p.sat  = 0;
        } else {
            p.type = EFFECT_STATIC_COLOR;
            p.hue  = s->hue;
            p.sat  = s->sat;
        }
        p.brightness = s->level;
        p.speed      = 0;
    } else {
        p            = *scene;
        p.brightness = scale_level(scene->brightness, s->level);
    }
    return p;
}

// ══ Two-entity model (item 9) ═════════════════════════════════════════════
// The fixture's two light sources become two Zigbee endpoints and two Home
// Assistant entities, so they can run at once -- white downlight plus coloured
// accent ring, which the single-entity model could not express at all.
//
// Two purpose-built structs rather than two copies of one combined struct: the
// downlight has no hue and the ring has no colour temperature, and a type that
// carries fields its source does not have cannot tell a reader which are live.
//
// Everything the old model needed WHITE_SAT_THRESHOLD for is gone: a command
// arrives AT the ring or AT the downlight, so there is nothing left to infer.

struct DownlightState {
    bool    on;
    uint8_t level;    // Level Control cluster
    uint8_t cct;      // 0 = fully warm (2700K) .. 255 = fully cool (6500K)
};

struct RingState {
    bool      on;
    uint8_t   level;
    uint8_t   hue;
    uint8_t   sat;
    uint8_t   scene;  // index into the scene table
    LightMode mode;   // MODE_SCENE = running an effect, MODE_COLOR = solid colour
};

struct FixtureState {
    DownlightState down;
    RingState      ring;
};

inline void downlight_state_init(DownlightState* s) {
    s->on    = false;
    s->level = 255;
    s->cct   = 128;
}

inline void ring_state_init(RingState* s) {
    s->on    = false;
    s->level = 255;
    s->hue   = 0;
    s->sat   = 0;
    s->scene = 0;
    s->mode  = MODE_SCENE;
}

inline void fixture_state_init(FixtureState* s) {
    downlight_state_init(&s->down);
    ring_state_init(&s->ring);
}

// The downlight has no modes: it is on at a level, or it is off. The render
// loop hands this straight to white_mix_gamma() along with `cct`.
inline uint8_t downlight_level(const DownlightState* s) {
    return s->on ? s->level : 0;
}

// The downlight endpoint advertises colour-temperature capability only, so it
// always receives mireds -- there is no RGB fallback to infer warmth from.
inline void downlight_set_cct(DownlightState* s, uint16_t mireds) {
    s->cct = mireds_to_cct(mireds);
}

inline void ring_set_color(RingState* s, CRGB c) {
    const HSV h = rgb_to_hsv(c);
    s->hue  = h.h;
    s->sat  = h.s;
    s->mode = MODE_COLOR;
}

// Validated here rather than at the Zigbee adapter: the index arrives over the
// air. An out-of-range value is ignored outright, leaving both the scene and
// the mode untouched -- clamping would strand the ring on a scene nobody asked
// for.
inline void ring_set_scene(RingState* s, uint8_t index, uint8_t scene_count) {
    if (scene_count == 0 || index >= scene_count) return;
    s->scene = index;
    s->mode  = MODE_SCENE;
}

// Leaves effect mode without disturbing the colour, which is what the ring
// carries on showing. The firmware side of picking "none" in the dropdown.
inline void ring_clear_scene(RingState* s) {
    s->mode = MODE_COLOR;
}

inline uint8_t ring_effect_value(const RingState* s) {
    return s->mode == MODE_SCENE ? s->scene : LIGHT_EFFECT_NONE;
}

inline void ring_next_scene(RingState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    ring_set_scene(s, uint8_t((s->scene + 1) % scene_count), scene_count);
}

inline void ring_prev_scene(RingState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    ring_set_scene(s, uint8_t((s->scene + scene_count - 1) % scene_count), scene_count);
}

// Combines the live ring state with the stored parameters of the active scene.
// Unlike the old combined resolve there is no white branch -- the downlight is
// a separate entity that never passes through here.
inline EffectParams ring_state_resolve(const RingState* s, const EffectParams* scene) {
    EffectParams p;
    if (s->mode == MODE_COLOR) {
        // `type` is unread in this branch: the render loop checks `mode` and
        // calls fx_ring_solid without consulting it. It must still hold a valid
        // index -- EffectParams is returned by value, and reading an
        // indeterminate enum later is undefined behaviour. `scene` is always
        // in range, ring_set_scene having rejected anything else.
        p.type       = EffectType(s->scene);
        p.hue        = s->hue;
        p.sat        = s->sat;
        p.brightness = s->level;
        p.speed      = 0;
    } else {
        p            = *scene;
        p.brightness = scale_level(scene->brightness, s->level);
    }
    return p;
}

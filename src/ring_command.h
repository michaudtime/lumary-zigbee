#pragma once
#include <stdint.h>
#include "color.h"
#include "light_state.h"

// Tells a ring colour command apart from a plain On/Off or Level command, and
// applies it. Deliberately free of ESP-IDF headers so it can be unit-tested on
// the host; zigbee_light.cpp is the thin adapter that registers the callbacks
// and publishes the effect attribute. See test/test_ring_command.
//
// ── why this exists ───────────────────────────────────────────────────────
// ZigbeeColorDimmableLight has one callback per colour mode and dispatches
// EVERYTHING through whichever mode is active (lightChangedByMode()), so an
// On/Off or Level write arrives at the RGB or HSV callback carrying the
// library's own current colour rather than a commanded one. After a power
// cycle that colour is the constructor's default -- RGB 255/255/255, hue 0 /
// sat 0 (ZigbeeColorDimmableLight.cpp lines 51-52) -- so a ring that came up
// running a scene had its first On or dim read as "go white", which dropped it
// out of MODE_SCENE. That was the reported bug.
//
// ── how the two are told apart ────────────────────────────────────────────
// Not by comparing colours: hue 0 / sat 0 is both the library's power-up
// default AND a legitimate "make the ring white", so no colour baseline can
// separate them on the first command after boot.
//
// The library gives an exact signal instead. In zbAttributeSet():
//
//   * the On/Off branch calls back ONLY when _current_state actually changed;
//   * the Level branch calls back ONLY when _current_level actually changed;
//   * every colour branch (CurrentX, CurrentY, CurrentHue, CurrentSaturation)
//     calls back WITHOUT touching either -- the hue/sat path only copies
//     _current_hsv.v = _current_level, which is unchanged by definition.
//
// So a dispatch that carries neither a new state nor a new level cannot be an
// On/Off or Level write, and one that carries either cannot be a colour write.
// The test below is exact in both directions, not a heuristic.
//
// RingSync holds the last state/level we saw FROM THE LIBRARY, which is the
// baseline the next dispatch is compared against.

struct RingSync {
    bool    last_state;
    uint8_t last_level;
};

// Seeded to ZigbeeColorDimmableLight's power-up values, so the very first
// dispatch after boot is compared against what the library really holds.
inline void ring_sync_init(RingSync* s) {
    s->last_state = false;
    s->last_level = 255;
}

// Move the baseline without a dispatch. Needed wherever we drive
// setLightState()/setLightLevel() with our callbacks suppressed -- the library
// moves but never calls back, and a stale baseline would make the next genuine
// colour command look like an On/Off.
inline void ring_sync_note(RingSync* s, bool state, uint8_t level) {
    s->last_state = state;
    s->last_level = level;
}

inline bool ring_is_color_command(const RingSync* s, bool state, uint8_t level) {
    return state == s->last_state && level == s->last_level;
}

// Applies one RGB-callback dispatch. Returns true if the ring left MODE_SCENE,
// so the caller can republish the effect attribute -- Home Assistant would
// otherwise go on naming an effect that has stopped.
inline bool ring_apply_rgb(RingState* ring, RingSync* sync,
                           bool state, CRGB color, uint8_t level) {
    const bool is_color = ring_is_color_command(sync, state, level);
    ring_sync_note(sync, state, level);
    ring->on    = state;
    ring->level = level;
    if (!is_color) return false;
    const LightMode was = ring->mode;
    ring_set_color(ring, color);        // moves out of scene mode
    return was == MODE_SCENE;
}

// Applies one HSV-callback dispatch. `value` is the brightness/level component
// -- the HSV callback has no separate level parameter (ZigbeeColorDimmableLight.h).
// Sets hue/sat directly rather than round-tripping through RGB the way the RGB
// path has to, RingState already storing them in this form.
inline bool ring_apply_hsv(RingState* ring, RingSync* sync,
                           bool state, uint8_t hue, uint8_t sat, uint8_t value) {
    const bool is_color = ring_is_color_command(sync, state, value);
    ring_sync_note(sync, state, value);
    ring->on    = state;
    ring->level = value;
    if (!is_color) return false;
    const LightMode was = ring->mode;
    ring->hue  = hue;
    ring->sat  = sat;
    ring->mode = MODE_COLOR;           // moves out of scene mode
    return was == MODE_SCENE;
}

#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "effect_params.h"
#include "light_state.h"
#include "scene_store.h"
#include "zigbee_light.h"
#include "identify.h"

static CRGB leds[RING_NUM_LEDS];

// There was a MAX_BRIGHTNESS ceiling of 24/255 here, clamped ahead of every
// effect to protect rev A's +4V7 traces -- routed at 0.2 mm rather than 0.5 mm
// because KiCad dropped the Power net class before routing (a1270ba), giving
// ~0.74 A at a 10 C rise.
//
// Removed 2026-08-15 after bench measurement retired the premise, and because
// the clamp sat ahead of every effect it also throttled the inner white string
// to ~9% duty -- the fixture's main light source, whose current never touches
// that trace at all. See docs/superpowers/specs/
// 2026-08-15-remove-rev-a-brightness-ceiling-design.md and hardware/calcs.md.
//
//   ring at full brightness   0.48 A measured (1.2 A had been assumed), cold or
//                             warm, with no measurable trace heating in 3 runs
//   white at 100% duty        Q2 at 37 C after 15 min (Task 6.3)
//
// Still unverified in service, and what Task 6.4 must check: the L-SD8E1's
// spare 4.7 V capacity. If that rail cannot carry ring + module, the symptom is
// the ring dimming or glitching rather than an MCU brownout -- the LDO has
// ~1.2 V of margin. A ring-only cap belongs here if that turns out to be real;
// note a global clamp would be the wrong fix, since it throttles white too.

// Set to 1 to cycle every effect on a timer with no Zigbee network, for
// bench-testing the LED path over USB before the light is joined.
#define BENCH_DEMO_MODE 0

#define FRAME_INTERVAL_MS 16

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("boot ok");

    scene_store_init();
    led_driver_init();
    Serial.println("LED driver init ok");

#if !BENCH_DEMO_MODE
    zigbee_light_init();
#endif
}

void loop() {
    static uint32_t frame_start  = 0;
    static uint32_t effect_start = 0;
    static uint8_t  shown_scene  = 0xFF;   // forces a scene load on first frame
    static EffectParams scene;

#if !BENCH_DEMO_MODE
    zigbee_light_loop();
#endif

    const uint32_t now = millis();
    if (now - frame_start < FRAME_INTERVAL_MS) return;
    frame_start = now;

#if BENCH_DEMO_MODE
    static uint8_t demo_idx = 0;
    if (now - effect_start > 5000) {
        demo_idx     = (demo_idx + 1) % EFFECT_COUNT;
        effect_start = now;
        Serial.printf("Effect: %s\n", kEffects[demo_idx].name);
    }
    EffectParams p = kDefaultParams[demo_idx];
    const bool on  = true;
#else
    const LightState* s = zigbee_light_state();

    // Scene params live in NVS; only re-read them when the scene actually
    // changes, and restart the animation clock so effects begin from frame 0.
    if (s->scene != shown_scene) {
        shown_scene  = s->scene;
        effect_start = now;
        scene_store_load(shown_scene, &scene);
    }

    EffectParams p = light_state_resolve(s, &scene);
    const bool on  = s->on;
#endif

    if (p.type >= EFFECT_COUNT) p.type = EFFECT_STATIC_WHITE;   // NVS corruption guard

    // Identify overrides whatever is running, without disturbing it: LightState
    // is untouched, so when the deadline passes the next frame resumes normally.
    // Bench demo mode compiles out zigbee_light_* entirely, so the accessor is
    // not available to link against there.
#if BENCH_DEMO_MODE
    const bool identifying = false;
#else
    const bool identifying = identify_active(now, zigbee_light_identify_until());
#endif

    if (identifying) {
        fx_identify(now, leds);
    } else {
        kEffects[p.type].fn(now - effect_start, p, leds, on);
    }
    led_driver_show(leds, RING_NUM_LEDS);
}

#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "effect_params.h"
#include "light_state.h"
#include "scene_store.h"
#include "zigbee_light.h"

static CRGB leds[RING_NUM_LEDS];

// Hard brightness ceiling -- a HARDWARE limit on rev A, not just a bench guard.
// The +4V7 traces routed at 0.2 mm (KiCad dropped the Power net class before
// routing), which carries ~0.74 A at a 10 C rise. Ring + module at this setting
// draws ~0.55 A; at full brightness it would be ~1.5 A and cook the trace
// (~49 C rise). The safe ceiling is about 37. See hardware/calcs.md.
// Do NOT raise this toward 255 on a rev A board.
#define MAX_BRIGHTNESS 24

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

    if (p.brightness > MAX_BRIGHTNESS) p.brightness = MAX_BRIGHTNESS;
    if (p.type >= EFFECT_COUNT) p.type = EFFECT_STATIC_WHITE;   // NVS corruption guard

    kEffects[p.type].fn(now - effect_start, p, leds, on);
    led_driver_show(leds, RING_NUM_LEDS);
}

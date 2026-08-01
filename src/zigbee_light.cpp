#include "zigbee_light.h"
#include "config.h"
#include "scene_store.h"
#include "effect_params.h"
#include <Arduino.h>
#include "Zigbee.h"

static ZigbeeColorDimmableLight s_ep(LIGHT_ENDPOINT);
static LightState s_state;

// The endpoint reports state, level and colour together on every change, so the
// only way to tell a colour command from a plain dim is to compare against the
// last colour we saw. Without this, nudging the brightness would kick the light
// out of whatever scene it was running.
static uint8_t s_last_r = 255, s_last_g = 255, s_last_b = 255;

// Runs on the Zigbee task, not in loop(). Every field it touches is byte-sized
// and the reader only renders frames from them, so the worst a race can do is
// show one frame of mixed state -- not worth a mutex on the render path.
static void on_light_change(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
    s_state.on    = state;
    s_state.level = level;
    if (r != s_last_r || g != s_last_g || b != s_last_b) {
        s_last_r = r;
        s_last_g = g;
        s_last_b = b;
        light_state_set_color(&s_state, CRGB{r, g, b});   // moves out of scene mode
    }
}

static void on_identify(uint16_t time) {
    log_i("Zigbee identify for %us", time);
}

void zigbee_light_init() {
    light_state_init(&s_state);
    s_state.scene = scene_store_get_active();

    s_ep.onLightChange(on_light_change);
    s_ep.onIdentify(on_identify);
    s_ep.setManufacturerAndModel("Lumary", "LumaryBrainRevA");

    Zigbee.addEndpoint(&s_ep);

    // Router, not end device: these are mains-powered ceiling fixtures, so each
    // one should extend the mesh for the others.
    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        // Deliberately not fatal. A light that can't reach the network must
        // still turn on locally, so carry on rendering and let the caller retry.
        log_e("Zigbee failed to start; continuing with local control only");
        return;
    }
    log_i("Zigbee started, waiting for network");
}

bool zigbee_light_connected() {
    return Zigbee.connected();
}

const LightState* zigbee_light_state() {
    return &s_state;
}

static void set_scene(uint8_t index) {
    scene_store_set_active(index);
    zigbee_light_report();
}

void zigbee_light_next_scene() {
    light_state_next_scene(&s_state, EFFECT_COUNT);
    set_scene(s_state.scene);
}

void zigbee_light_prev_scene() {
    light_state_prev_scene(&s_state, EFFECT_COUNT);
    set_scene(s_state.scene);
}

void zigbee_light_report() {
    s_ep.setLightState(s_state.on);
    s_ep.setLightLevel(s_state.level);
}

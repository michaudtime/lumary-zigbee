#pragma once
#include "light_state.h"

// Thin adapter between the Arduino Zigbee stack and light_state. Everything
// with real decision-making lives in light_state.h (which is host-tested);
// this file only wires the callbacks up and owns the endpoint.

// Brings up the Zigbee stack and the light endpoint. Non-blocking: the light
// renders locally whether or not a network is ever joined.
void zigbee_light_init();

// True once joined to a network.
bool zigbee_light_connected();

// Call every loop(). Handles work that can only happen after joining, such as
// the first OTA query.
void zigbee_light_loop();

// Live state, updated from Zigbee callbacks.
const LightState* zigbee_light_state();

// Selects one of the eight effects and persists it to NVS. Normally driven from
// the coordinator writing LUMARY_ATTR_EFFECT; exposed for a future local button.
// Out-of-range indices are ignored, not clamped.
//
// Stepping (the switch's double-tap) deliberately lives in the hub automation,
// which reads the current effect and writes (n +/- 1) mod EFFECT_COUNT. There is
// no next/prev here because the Inovelli's multi-tap events go to the
// coordinator rather than to a bound light, so a hub is in the loop regardless.
void zigbee_light_set_effect(uint8_t index);

// Mirrors the current state back to the coordinator after a local change, so
// Home Assistant / Z2M don't show stale values.
void zigbee_light_report();

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

// Scene selection. Exposed so a hub automation (or a future local button) can
// step through the stored scenes; both persist the new index to NVS.
void zigbee_light_next_scene();
void zigbee_light_prev_scene();

// Mirrors the current state back to the coordinator after a local change, so
// Home Assistant / Z2M don't show stale values.
void zigbee_light_report();

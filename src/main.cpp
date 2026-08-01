#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "effect_params.h"

static CRGB leds[RING_NUM_LEDS];

// Cap brightness during USB bench testing. 62 RGB pixels at full white would
// draw ~3.7 A (62 × 3 × ~20 mA); this keeps the ring near ~0.35 A so USB can't
// brown out the board. Raise once the fixture's own 4.7 V rail is supplying it.
#define BENCH_BRIGHTNESS 24

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("boot ok");
  led_driver_init();
  Serial.println("LED driver init ok");
}

void loop() {
  static uint8_t  effect_idx   = 0;
  static uint32_t effect_start = 0;
  static uint32_t frame_start  = 0;

  uint32_t now = millis();
  if (now - effect_start > 5000) {
    effect_idx = (effect_idx + 1) % EFFECT_COUNT;
    effect_start = now;
    Serial.printf("Effect: %s\n", kEffects[effect_idx].name);
  }
  if (now - frame_start >= 16) {
    frame_start = now;
    EffectParams p = kDefaultParams[effect_idx];
    p.brightness   = BENCH_BRIGHTNESS;  // USB-safe current limit
    kEffects[effect_idx].fn(now - effect_start, p, leds, true);
    led_driver_show(leds, RING_NUM_LEDS);
  }
}

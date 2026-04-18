#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "effect_params.h"

static CRGBW leds[SK6812_NUM_LEDS];

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
    kEffects[effect_idx].fn(now - effect_start, kDefaultParams[effect_idx], leds, true);
    led_driver_show(leds, SK6812_NUM_LEDS);
  }
}

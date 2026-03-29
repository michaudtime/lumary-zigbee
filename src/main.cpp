// lumary-zigbee/src/main.cpp
#include <Arduino.h>
#include "config.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Lumary Zigbee — boot");
}

void loop() {
  delay(1000);
}

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("boot ok");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}

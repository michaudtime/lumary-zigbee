#pragma once
#include "color.h"
#include "config.h"

void led_driver_init();
void led_driver_show(const CRGBW* leds, uint16_t count);
void led_driver_set_cw(uint8_t level);
void led_driver_set_ww(uint8_t level);
void led_driver_off();

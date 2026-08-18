#pragma once
#include "color.h"
#include "config.h"

void led_driver_init();
void led_driver_show(const CRGB* leds, uint16_t count);
void led_driver_set_cw(uint16_t duty);   // 0..PWM_DUTY_MAX
void led_driver_set_ww(uint16_t duty);   // 0..PWM_DUTY_MAX
void led_driver_off();

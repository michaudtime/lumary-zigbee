// lumary-zigbee/src/config.h
#pragma once
#include "driver/ledc.h"

// ── Pins ──────────────────────────────────────────────
#define PIN_SK6812_DATA       11   // SPI2 MOSI → SK6812 outer ring
#define PIN_SK6812_CLK_DUMMY  12   // SPI2 CLK  → leave unconnected
#define PIN_CW_PWM             2   // Cold white LEDC
#define PIN_WW_PWM             3   // Warm white LEDC
#define PIN_BLE_OTA_BUTTON     9   // BOOT button — hold 5s to enter BLE OTA

// ── SK6812 ────────────────────────────────────────────
#define SK6812_NUM_LEDS       36
#define SK6812_SPI_CLK_HZ     2400000  // 2.4 MHz → 3 SPI bits = one 800kHz NZR bit
// Buffer: 36 LEDs × 32 bits × 3 SPI bits/bit = 3456 bits = 432 bytes
// + 50 zero bytes ≈ 166 µs reset pulse (>80 µs required)
#define SK6812_SPI_BUF_SIZE   482

// ── PWM ───────────────────────────────────────────────
#define PWM_FREQ_HZ           20000
#define PWM_RESOLUTION        LEDC_TIMER_8_BIT
#define PWM_CHANNEL_CW        LEDC_CHANNEL_0
#define PWM_CHANNEL_WW        LEDC_CHANNEL_1

// ── Zigbee ────────────────────────────────────────────
#define LIGHT_ENDPOINT        1
#define ZB_MANUFACTURER_CODE  0x1001
#define ZB_IMAGE_TYPE         0x0001

// ── NVS ───────────────────────────────────────────────
#define NVS_NAMESPACE         "lm_light"
#define NVS_KEY_FMT_VER       "fmt_ver"
#define NVS_KEY_ACTIVE_SCENE  "active_scene"
#define NVS_FMT_VER_CURRENT   1
#define NVS_MAX_SCENES        16

// ── Watchdog ──────────────────────────────────────────
#define WDT_TIMEOUT_MS        5000

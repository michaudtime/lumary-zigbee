// lumary-zigbee/src/config.h
#pragma once
#include "driver/ledc.h"

// Target board: lumary-brain rev A (drop-in replacement for the stock
// KOK-AH-A172C controller). See docs/superpowers/specs/2026-08-01-*.

// ── Pins ──────────────────────────────────────────────
// GPIO2/3/8/9/25 are ESP32-H2 strapping pins. The white PWM channels moved to
// GPIO4/5 on rev A so the MOSFET gate pulldowns can't hold a strapping pin low
// at reset (the Super Mini prototype drove them from GPIO2/3).
#define PIN_RING_DATA         11   // SPI2 MOSI → level buffer → CN1.DIM (ring DIN)
#define PIN_RING_CLK_DUMMY    12   // SPI2 CLK  → leave unconnected
#define PIN_CW_PWM             4   // Cold white LEDC → Q1 gate → CN1.CW-
#define PIN_WW_PWM             5   // Warm white LEDC → Q2 gate → CN1.WW-
#define PIN_BLE_OTA_BUTTON     9   // BOOT button — hold 5s to enter BLE OTA

// ── Outer ring (addressable) ──────────────────────────
// UT-08-ZC03-01-5V3535RGBIC: 62 pixels, 3 colour bytes each (no white die).
#define RING_NUM_LEDS         62
#define RING_SPI_CLK_HZ       2400000  // 2.4 MHz → 3 SPI bits = one 800kHz NZR bit
// Data: 62 LEDs × 24 bits × 3 SPI bits/bit = 4464 bits = 558 bytes.
// Reset: +90 zero bytes ≈ 300 µs latch, generous enough for WS2812B-V5 parts
// (which want >280 µs) as well as older ones (>80 µs).
#define RING_SPI_BUF_SIZE     648

// ── PWM (inner white string) ──────────────────────────
// The external L-SD8E1 driver is a 380 mA constant-current source; the board
// only gates it. A CC supply's control loop can't track fast switching, so keep
// the PWM slow enough for it to settle but above the flicker threshold.
// Verify on the bench (Task 6.3) and adjust if low-end dimming shudders.
#define PWM_FREQ_HZ           1000
#define PWM_RESOLUTION        LEDC_TIMER_8_BIT
#define PWM_CHANNEL_CW        LEDC_CHANNEL_0
#define PWM_CHANNEL_WW        LEDC_CHANNEL_1

// ── Zigbee ────────────────────────────────────────────
#define LIGHT_ENDPOINT        1
#define ZB_MANUFACTURER_CODE  0x1001
#define ZB_IMAGE_TYPE         0x0001

// OTA. Bump ZB_FW_VERSION for every release and pass the same value to
// ota_image_tool.py as --file-version, or the coordinator will not offer the
// update (it only pushes images numbered higher than the running one).
#define ZB_FW_VERSION         0x01000000
#define ZB_FW_VERSION_DL      0x01000001
#define ZB_HW_VERSION         0x0001      // lumary-brain rev A

// ── NVS ───────────────────────────────────────────────
#define NVS_NAMESPACE         "lm_light"
#define NVS_KEY_FMT_VER       "fmt_ver"
#define NVS_KEY_ACTIVE_SCENE  "active_scene"
#define NVS_FMT_VER_CURRENT   1
#define NVS_MAX_SCENES        16

// ── Watchdog ──────────────────────────────────────────
#define WDT_TIMEOUT_MS        5000

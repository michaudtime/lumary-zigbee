// lumary-zigbee/src/config.h
#pragma once
#include "driver/ledc.h"
#include "version.h"

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
#define RING_SPI_CLK_HZ       3200000  // 3.2 MHz → 4 SPI bits = one 800kHz NZR bit
// Data: 62 LEDs × 24 bits × 4 SPI bits/bit = 5952 bits = 744 bytes.
// One SPI byte is 2.5 µs at this clock.
// Reset: +120 trailing zero bytes = 300 µs latch, generous enough for
// WS2812B-V5 parts (which want >280 µs) as well as older ones (>80 µs).
#define RING_SPI_BUF_SIZE     896
// Leading low period, transmitted before the pixel data. MOSI is not guaranteed
// to idle low between transactions, and a line already high when the frame
// starts robs the first NZR bit of its rising edge. Kept as cheap insurance: it
// did not fix the corruption seen during bring-up on 2026-08-15 (a logic
// capture showed the real cause was T0H pulse width, see pixel_encode.h), but a
// guaranteed-low line before the first bit is correct practice for SPI-driven
// NZR. 32 bytes = 80 µs.
#define RING_SPI_LEAD_BYTES   32

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
// Effect selection rides a manufacturer-specific cluster, because nothing
// standard can carry it: the Scenes cluster stores colour/level and knows
// nothing about EffectParams, and the Identify trigger-effect command (which
// is what Z2M's built-in "effect" dropdown drives) has no hook in the Arduino
// library. Writing LUMARY_ATTR_EFFECT selects one of the eight effects.
// See docs/superpowers/specs/2026-08-15-effect-selection-design.md.
// Selection arrives as a COMMAND rather than an attribute write: ZigbeeColor
// DimmableLight declares zbAttributeSet private, so a subclass can override it
// but cannot delegate on/off, level and colour back to the base -- overriding
// would silently break the light. onCustomClusterCommand is public and is the
// library's intended extension point. The attribute is read-only state, kept in
// step so Z2M/HA can display which effect is running.
#define LUMARY_CLUSTER_ID       0xFC00   // manufacturer-specific range
#define LUMARY_ATTR_EFFECT      0x0000   // u8, read-only: effect currently running
#define LUMARY_CMD_SET_EFFECT   0x00     // payload: one u8, the effect index
// ...or LIGHT_EFFECT_NONE (0xFF, src/light_state.h) to stop the effect and hold
// the current colour. The attribute reports that same value whenever a colour
// command has taken the fixture out of effect mode, so Home Assistant's effect
// dropdown -- which has no null member -- always has something true to show.

#define LIGHT_ENDPOINT        1
#define ZB_MANUFACTURER_CODE  0x1001
#define ZB_IMAGE_TYPE         0x0001

// ZB_FW_VERSION / ZB_FW_VERSION_DL are derived in version.h, included at the
// top of this file -- it is ESP-IDF-free so the host tests can reach it.
#define ZB_HW_VERSION         0x0001      // lumary-brain rev A

// ── NVS ───────────────────────────────────────────────
#define NVS_NAMESPACE         "lm_light"
#define NVS_KEY_FMT_VER       "fmt_ver"
#define NVS_KEY_ACTIVE_SCENE  "active_scene"
#define NVS_FMT_VER_CURRENT   1
#define NVS_MAX_SCENES        16

// ── Watchdog ──────────────────────────────────────────
#define WDT_TIMEOUT_MS        5000

# Lumary Zigbee Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build firmware for an ESP32-H2 Super Mini that replaces the Tuya WiFi controller in a Lumary 6" RGBAI recessed light, providing Zigbee control with 8 built-in effects, hub-independent switch binding, and OTA updates.

**Architecture:** PlatformIO + Arduino framework on ESP32-H2. SK6812 outer ring driven via SPI2 NZR bit-encoding (RMT conflicts with Zigbee on H2). CW/WW inner ring driven via LEDC PWM. Zigbee stack runs on a background FreeRTOS task via the Arduino ESP32 Zigbee library. Effect engine runs in `loop()` at ~60fps. NVS stores scenes across power cycles.

**Tech Stack:** PlatformIO, Arduino ESP32 v3.x, Arduino ESP32 Zigbee, esp-idf SPI master, esp-idf LEDC, esp-idf NVS, esp-idf watchdog, NimBLE

---

## File Map

| File | Responsibility |
|---|---|
| `platformio.ini` | Board, framework, build flags |
| `src/config.h` | Pin definitions, constants, NVS keys |
| `src/color.h` | `CRGBW` struct, HSV→RGBW, brightness scale |
| `src/led_driver.h/.cpp` | SPI2 NZR output for SK6812, LEDC PWM for CW/WW |
| `src/effect_params.h` | `EffectType` enum, `EffectParams` struct, defaults |
| `src/effects.h/.cpp` | All 8 effect functions + lookup table |
| `src/scene_store.h/.cpp` | NVS read/write for 16 scenes + active index |
| `src/zigbee_light.h/.cpp` | Zigbee Extended Color Light clusters + callbacks |
| `src/zigbee_ota.h/.cpp` | Zigbee OTA Upgrade cluster |
| `src/ble_ota.h/.cpp` | NimBLE OTA fallback (BOOT button trigger) |
| `src/main.cpp` | setup(), loop(), watchdog, wires all layers |
| `test/test_scene_store/test_main.cpp` | Native unit tests for NVS schema logic |

---

## Task 1: Project Scaffold

**Files:**
- Create: `lumary-zigbee/platformio.ini`
- Create: `lumary-zigbee/src/config.h`
- Create: `lumary-zigbee/src/main.cpp` (stub)

- [ ] **Step 1: Create project directory**

```bash
mkdir -p /root/lumary-zigbee/src
mkdir -p /root/lumary-zigbee/test/test_scene_store
```

- [ ] **Step 2: Write platformio.ini**

```ini
; lumary-zigbee/platformio.ini
[env:esp32h2]
platform = espressif32
board = esp32h2
framework = arduino
monitor_speed = 115200
board_build.partitions = min_spiffs.csv
build_flags =
  -DCORE_DEBUG_LEVEL=3
  -DZIGBEE_MODE_ED
```

- [ ] **Step 3: Write config.h**

```cpp
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
#define ZB_MANUFACTURER_CODE  0x1001   // Arbitrary private code for DIY device
#define ZB_IMAGE_TYPE         0x0001

// ── NVS ───────────────────────────────────────────────
// Namespace: "lm_light" (10 chars, within 15-char limit)
// Keys: all ≤15 chars — do NOT extend the pattern without re-checking
#define NVS_NAMESPACE         "lm_light"
#define NVS_KEY_FMT_VER       "fmt_ver"        // u8, schema version = 1
#define NVS_KEY_ACTIVE_SCENE  "active_scene"   // u8, 0–15
#define NVS_FMT_VER_CURRENT   1
#define NVS_MAX_SCENES        16

// ── Watchdog ──────────────────────────────────────────
#define WDT_TIMEOUT_MS        5000
```

- [ ] **Step 4: Write stub main.cpp**

```cpp
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
```

- [ ] **Step 5: Verify project builds**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2
```

Expected: `SUCCESS` with no errors. Warnings about unused includes are OK.

- [ ] **Step 6: Commit**

```bash
cd /root/lumary-zigbee
git init && git add -A
git commit -m "feat: initial PlatformIO scaffold for Lumary Zigbee firmware"
```

---

## Task 2: Color Utilities

**Files:**
- Create: `src/color.h`

- [ ] **Step 1: Write color.h**

```cpp
// lumary-zigbee/src/color.h
#pragma once
#include <stdint.h>

struct CRGBW {
    uint8_t r, g, b, w;
};

// Scale a byte by another byte (0–255), result 0–255
inline uint8_t scale8(uint8_t val, uint8_t scale) {
    return (uint16_t(val) * scale) >> 8;
}

// HSV → RGBW. W channel used when sat == 0.
// hue: 0–255, sat: 0–255, val: 0–255
inline CRGBW hsv_to_rgbw(uint8_t hue, uint8_t sat, uint8_t val) {
    if (sat == 0) {
        return {0, 0, 0, val};
    }
    uint8_t region    = hue / 43;
    uint8_t remainder = (hue % 43) * 6;
    uint8_t p = scale8(val, 255 - sat);
    uint8_t q = scale8(val, 255 - scale8(sat, remainder));
    uint8_t t = scale8(val, 255 - scale8(sat, 255 - remainder));
    switch (region) {
        case 0:  return {val, t,   p,   0};
        case 1:  return {q,   val, p,   0};
        case 2:  return {p,   val, t,   0};
        case 3:  return {p,   q,   val, 0};
        case 4:  return {t,   p,   val, 0};
        default: return {val, p,   q,   0};
    }
}

// Scale all channels of a CRGBW by brightness (0–255)
inline CRGBW scale_brightness(CRGBW c, uint8_t brightness) {
    return {
        scale8(c.r, brightness),
        scale8(c.g, brightness),
        scale8(c.b, brightness),
        scale8(c.w, brightness)
    };
}

// Blend two CRGBW values. amount=0 → a, amount=255 → b
inline CRGBW blend(CRGBW a, CRGBW b, uint8_t amount) {
    return {
        uint8_t(a.r + (int(b.r - a.r) * amount >> 8)),
        uint8_t(a.g + (int(b.g - a.g) * amount >> 8)),
        uint8_t(a.b + (int(b.b - a.b) * amount >> 8)),
        uint8_t(a.w + (int(b.w - a.w) * amount >> 8)),
    };
}
```

- [ ] **Step 2: Verify build**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2
```

Expected: `SUCCESS`

- [ ] **Step 3: Commit**

```bash
git add src/color.h
git commit -m "feat: add CRGBW color utilities and HSV conversion"
```

---

## Task 3: LED Driver

**Files:**
- Create: `src/led_driver.h`
- Create: `src/led_driver.cpp`

- [ ] **Step 1: Write led_driver.h**

```cpp
// lumary-zigbee/src/led_driver.h
#pragma once
#include "color.h"
#include "config.h"

// Initialize SPI2 for SK6812 and LEDC for CW/WW
void led_driver_init();

// Push pixel buffer to SK6812 outer ring (count must == SK6812_NUM_LEDS)
void led_driver_show(const CRGBW* leds, uint16_t count);

// Set cold-white level 0–255
void led_driver_set_cw(uint8_t level);

// Set warm-white level 0–255
void led_driver_set_ww(uint8_t level);

// Turn all LEDs and PWM outputs off
void led_driver_off();
```

- [ ] **Step 2: Write led_driver.cpp**

```cpp
// lumary-zigbee/src/led_driver.cpp
#include "led_driver.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include <string.h>

static spi_device_handle_t s_spi;
static uint8_t s_spi_buf[SK6812_SPI_BUF_SIZE];

// Set a single bit at position `pos` (MSB-first) in buf
static void set_bit(uint8_t* buf, int pos, uint8_t val) {
    const int byte_idx = pos / 8;
    const int bit_idx  = 7 - (pos % 8);
    if (val) buf[byte_idx] |=  (1 << bit_idx);
    else     buf[byte_idx] &= ~(1 << bit_idx);
}

// Encode RGBW pixel array into SPI NZR bit stream.
// Each NZR "1" bit → SPI bits 110, each "0" → SPI bits 100.
// SK6812 byte order on wire: G R B W.
static void encode_pixels(const CRGBW* leds, uint16_t count) {
    memset(s_spi_buf, 0, SK6812_SPI_BUF_SIZE);
    int out = 0;
    for (uint16_t i = 0; i < count; i++) {
        // Wire order: G R B W
        const uint8_t bytes[4] = {leds[i].g, leds[i].r, leds[i].b, leds[i].w};
        for (int byte_i = 0; byte_i < 4; byte_i++) {
            for (int b = 7; b >= 0; b--) {
                const uint8_t led_bit = (bytes[byte_i] >> b) & 1;
                set_bit(s_spi_buf, out++, 1);
                set_bit(s_spi_buf, out++, led_bit);
                set_bit(s_spi_buf, out++, 0);
            }
        }
    }
    // Remaining bytes stay 0 → reset pulse
}

void led_driver_init() {
    // SPI2 bus — MOSI on PIN_SK6812_DATA, dummy CLK pin (unconnected)
    spi_bus_config_t bus = {};
    bus.mosi_io_num    = PIN_SK6812_DATA;
    bus.miso_io_num    = -1;
    bus.sclk_io_num    = PIN_SK6812_CLK_DUMMY;
    bus.quadwp_io_num  = -1;
    bus.quadhd_io_num  = -1;
    bus.max_transfer_sz = SK6812_SPI_BUF_SIZE;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = SK6812_SPI_CLK_HZ;
    dev.mode           = 0;
    dev.spics_io_num   = -1;
    dev.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    // LEDC timer shared by both PWM channels
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.timer_num       = LEDC_TIMER_0;
    timer.duty_resolution = PWM_RESOLUTION;
    timer.freq_hz         = PWM_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    // Cold white channel
    ledc_channel_config_t cw = {};
    cw.gpio_num   = PIN_CW_PWM;
    cw.speed_mode = LEDC_LOW_SPEED_MODE;
    cw.channel    = PWM_CHANNEL_CW;
    cw.timer_sel  = LEDC_TIMER_0;
    cw.duty       = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&cw));

    // Warm white channel
    ledc_channel_config_t ww = cw;
    ww.gpio_num = PIN_WW_PWM;
    ww.channel  = PWM_CHANNEL_WW;
    ESP_ERROR_CHECK(ledc_channel_config(&ww));
}

void led_driver_show(const CRGBW* leds, uint16_t count) {
    encode_pixels(leds, count);
    spi_transaction_t t = {};
    t.length    = SK6812_SPI_BUF_SIZE * 8;
    t.tx_buffer = s_spi_buf;
    spi_device_polling_transmit(s_spi, &t);
}

void led_driver_set_cw(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW);
}

void led_driver_set_ww(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW);
}

void led_driver_off() {
    static const CRGBW black[SK6812_NUM_LEDS] = {};
    led_driver_show(black, SK6812_NUM_LEDS);
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}
```

- [ ] **Step 3: Update main.cpp to test LED driver on bench**

```cpp
// lumary-zigbee/src/main.cpp
#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "color.h"

static CRGBW leds[SK6812_NUM_LEDS];

void setup() {
  Serial.begin(115200);
  led_driver_init();

  // Test 1: all LEDs red
  for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = {255, 0, 0, 0};
  led_driver_show(leds, SK6812_NUM_LEDS);
  Serial.println("SK6812: all RED — verify outer ring");
  delay(2000);

  // Test 2: all LEDs white via W channel
  for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = {0, 0, 0, 200};
  led_driver_show(leds, SK6812_NUM_LEDS);
  Serial.println("SK6812: all W=200 — verify white");
  delay(2000);

  // Test 3: CW/WW PWM
  led_driver_off();
  led_driver_set_cw(255);
  Serial.println("CW=255, WW=0 — verify cold white inner ring");
  delay(2000);
  led_driver_set_cw(0);
  led_driver_set_ww(255);
  Serial.println("CW=0, WW=255 — verify warm white inner ring");
  delay(2000);

  led_driver_off();
  Serial.println("Phase 1 hardware check complete");
}

void loop() {}
```

- [ ] **Step 4: Flash and verify on bench hardware**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload && pio device monitor
```

Expected serial output:
```
SK6812: all RED — verify outer ring
SK6812: all W=200 — verify white
CW=255, WW=0 — verify cold white inner ring
CW=0, WW=255 — verify warm white inner ring
Phase 1 hardware check complete
```

**Manual checks:**
- Outer ring lights red, then white
- Inner ring: cold white, then warm white
- If colors are wrong (e.g. GRB instead of GRBW), update wire order in `led_driver.cpp` line: `const uint8_t bytes[4] = {leds[i].g, ...}`
- If no output: check SPI logic level — measure data line voltage. If strip is 5V-powered, insert 74AHCT125 level shifter between GPIO 11 and strip data input.

- [ ] **Step 5: Commit**

```bash
git add src/led_driver.h src/led_driver.cpp src/color.h src/main.cpp
git commit -m "feat: add SPI2 NZR SK6812 driver and LEDC CW/WW PWM"
```

---

## Task 4: Effect Params + Scene Storage

**Files:**
- Create: `src/effect_params.h`
- Create: `src/scene_store.h`
- Create: `src/scene_store.cpp`
- Create: `test/test_scene_store/test_main.cpp`

- [ ] **Step 1: Write effect_params.h**

```cpp
// lumary-zigbee/src/effect_params.h
#pragma once
#include <stdint.h>

enum EffectType : uint8_t {
    EFFECT_STATIC_WHITE   = 0,
    EFFECT_STATIC_COLOR   = 1,
    EFFECT_WARM_GRADIENT  = 2,
    EFFECT_COLOR_GRADIENT = 3,
    EFFECT_BREATHING      = 4,
    EFFECT_COLOR_CYCLE    = 5,
    EFFECT_CHASE          = 6,
    EFFECT_NIGHTLIGHT     = 7,
    EFFECT_COUNT          = 8
};

struct EffectParams {
    EffectType type;
    uint8_t    hue;        // 0–255
    uint8_t    sat;        // 0–255 (0 = white via W channel)
    uint8_t    brightness; // 0–255
    uint8_t    speed;      // 0–255 (effect-specific rate)
};

// Factory defaults for the 8 built-in scenes
static const EffectParams kDefaultParams[EFFECT_COUNT] = {
    {EFFECT_STATIC_WHITE,   0,   0, 255,   0},  // 1: Static White
    {EFFECT_STATIC_COLOR,   0, 255, 255,   0},  // 2: Static Color (red)
    {EFFECT_WARM_GRADIENT,  8, 220, 200,  60},  // 3: Warm Gradient
    {EFFECT_COLOR_GRADIENT, 0, 255, 200,  60},  // 4: Color Gradient
    {EFFECT_BREATHING,      0, 255, 255,  80},  // 5: Breathing
    {EFFECT_COLOR_CYCLE,    0, 255, 200, 100},  // 6: Color Cycle
    {EFFECT_CHASE,          0, 255, 255, 120},  // 7: Chase
    {EFFECT_NIGHTLIGHT,     8, 180,  50,   0},  // 8: Nightlight
};
```

- [ ] **Step 2: Write scene_store.h**

```cpp
// lumary-zigbee/src/scene_store.h
#pragma once
#include "effect_params.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize NVS. If schema version mismatch or NVS corrupt, loads defaults.
void scene_store_init();

// Return the index of the active scene (0–15)
uint8_t scene_store_get_active();

// Set and persist the active scene index
void scene_store_set_active(uint8_t index);

// Load scene params for a given index into `out`. Returns false if unset (loads default).
bool scene_store_load(uint8_t index, EffectParams* out);

// Save scene params for a given index
void scene_store_save(uint8_t index, const EffectParams* params);
```

- [ ] **Step 3: Write scene_store.cpp**

```cpp
// lumary-zigbee/src/scene_store.cpp
#include "scene_store.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static nvs_handle_t s_nvs;

// Build NVS key for a scene parameter: "s{index}_{field}" e.g. "s3_hue"
// All keys are ≤ 9 chars, well within the 15-char NVS limit.
static void scene_key(uint8_t index, const char* field, char* out) {
    snprintf(out, 16, "s%u_%s", index, field);
}

void scene_store_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);

    // Check schema version — reset to defaults if missing or changed
    uint8_t ver = 0;
    if (nvs_get_u8(s_nvs, NVS_KEY_FMT_VER, &ver) != ESP_OK || ver != NVS_FMT_VER_CURRENT) {
        // Write defaults for all 8 built-in scenes
        for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
            scene_store_save(i, &kDefaultParams[i]);
        }
        nvs_set_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, 0);
        nvs_set_u8(s_nvs, NVS_KEY_FMT_VER, NVS_FMT_VER_CURRENT);
        nvs_commit(s_nvs);
    }
}

uint8_t scene_store_get_active() {
    uint8_t idx = 0;
    nvs_get_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, &idx);
    if (idx >= NVS_MAX_SCENES) idx = 0;
    return idx;
}

void scene_store_set_active(uint8_t index) {
    if (index >= NVS_MAX_SCENES) index = 0;
    nvs_set_u8(s_nvs, NVS_KEY_ACTIVE_SCENE, index);
    nvs_commit(s_nvs);
}

bool scene_store_load(uint8_t index, EffectParams* out) {
    if (index >= NVS_MAX_SCENES) { *out = kDefaultParams[0]; return false; }
    char key[16];
    uint8_t type, hue, sat, bri, spd;

    scene_key(index, "type", key);
    if (nvs_get_u8(s_nvs, key, &type) != ESP_OK) {
        // Not stored — use built-in default or blank
        if (index < EFFECT_COUNT) *out = kDefaultParams[index];
        else *out = kDefaultParams[0];
        return false;
    }
    scene_key(index, "hue",  key); nvs_get_u8(s_nvs, key, &hue);
    scene_key(index, "sat",  key); nvs_get_u8(s_nvs, key, &sat);
    scene_key(index, "bri",  key); nvs_get_u8(s_nvs, key, &bri);
    scene_key(index, "spd",  key); nvs_get_u8(s_nvs, key, &spd);

    out->type       = (EffectType)type;
    out->hue        = hue;
    out->sat        = sat;
    out->brightness = bri;
    out->speed      = spd;
    return true;
}

void scene_store_save(uint8_t index, const EffectParams* p) {
    if (index >= NVS_MAX_SCENES) return;
    char key[16];
    scene_key(index, "type", key); nvs_set_u8(s_nvs, key, (uint8_t)p->type);
    scene_key(index, "hue",  key); nvs_set_u8(s_nvs, key, p->hue);
    scene_key(index, "sat",  key); nvs_set_u8(s_nvs, key, p->sat);
    scene_key(index, "bri",  key); nvs_set_u8(s_nvs, key, p->brightness);
    scene_key(index, "spd",  key); nvs_set_u8(s_nvs, key, p->speed);
    nvs_commit(s_nvs);
}
```

- [ ] **Step 4: Write native unit tests for scene_store logic**

```cpp
// lumary-zigbee/test/test_scene_store/test_main.cpp
#include <unity.h>
#include "../../src/effect_params.h"
#include "../../src/config.h"

// Tests the pure-logic parts: defaults, key lengths, param bounds.
// NVS I/O is hardware-dependent and tested manually.

void test_default_params_count() {
    TEST_ASSERT_EQUAL(EFFECT_COUNT, 8);
}

void test_default_params_types_in_range() {
    for (int i = 0; i < EFFECT_COUNT; i++) {
        TEST_ASSERT_TRUE(kDefaultParams[i].type < EFFECT_COUNT);
    }
}

void test_default_scene0_is_static_white() {
    TEST_ASSERT_EQUAL(EFFECT_STATIC_WHITE, kDefaultParams[0].type);
    TEST_ASSERT_EQUAL(0, kDefaultParams[0].sat);  // sat=0 means W channel
    TEST_ASSERT_EQUAL(255, kDefaultParams[0].brightness);
}

void test_nvs_namespace_length() {
    // NVS namespace max is 15 chars
    TEST_ASSERT_TRUE(strlen(NVS_NAMESPACE) <= 15);
}

void test_scene_key_lengths() {
    // Verify no generated key exceeds 15 chars
    char key[16];
    for (int i = 0; i < NVS_MAX_SCENES; i++) {
        snprintf(key, 16, "s%u_%s", i, "type");
        TEST_ASSERT_TRUE(strlen(key) <= 15);
        snprintf(key, 16, "s%u_%s", i, "hue");
        TEST_ASSERT_TRUE(strlen(key) <= 15);
        snprintf(key, 16, "s%u_%s", i, "sat");
        TEST_ASSERT_TRUE(strlen(key) <= 15);
        snprintf(key, 16, "s%u_%s", i, "bri");
        TEST_ASSERT_TRUE(strlen(key) <= 15);
        snprintf(key, 16, "s%u_%s", i, "spd");
        TEST_ASSERT_TRUE(strlen(key) <= 15);
    }
}

void test_max_scenes_constant() {
    TEST_ASSERT_EQUAL(16, NVS_MAX_SCENES);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_params_count);
    RUN_TEST(test_default_params_types_in_range);
    RUN_TEST(test_default_scene0_is_static_white);
    RUN_TEST(test_nvs_namespace_length);
    RUN_TEST(test_scene_key_lengths);
    RUN_TEST(test_max_scenes_constant);
    return UNITY_END();
}
```

- [ ] **Step 5: Add test environment to platformio.ini**

```ini
; Add to platformio.ini below [env:esp32h2]:

[env:native_test]
platform = native
build_flags = -std=c++17
test_filter = test_scene_store
```

- [ ] **Step 6: Run unit tests**

```bash
cd /root/lumary-zigbee && pio test -e native_test
```

Expected:
```
test/test_scene_store/test_main.cpp:5 tests, 0 failures
```

- [ ] **Step 7: Commit**

```bash
git add src/effect_params.h src/scene_store.h src/scene_store.cpp \
        test/test_scene_store/test_main.cpp platformio.ini
git commit -m "feat: add effect params, NVS scene store, and unit tests"
```

---

## Task 5: Effects Engine

**Files:**
- Create: `src/effects.h`
- Create: `src/effects.cpp`

- [ ] **Step 1: Write effects.h**

```cpp
// lumary-zigbee/src/effects.h
#pragma once
#include "effect_params.h"
#include "led_driver.h"

// Called once per frame. Updates `leds` buffer and CW/WW PWM.
// elapsed_ms: milliseconds since effect was last reset.
typedef void (*EffectFn)(uint32_t elapsed_ms, const EffectParams& p,
                         CRGBW* leds, bool light_on);

struct Effect {
    const char* name;
    EffectFn    fn;
};

// The 8 built-in effects, indexed by EffectType enum value.
extern const Effect kEffects[EFFECT_COUNT];
```

- [ ] **Step 2: Write effects.cpp**

```cpp
// lumary-zigbee/src/effects.cpp
#include "effects.h"
#include "color.h"
#include "config.h"
#include <math.h>
#include <string.h>

// ── Helpers ───────────────────────────────────────────────────────────────

// Map speed 0–255 to a period in ms (255=fast/200ms, 0=slow/10000ms)
static uint32_t speed_to_period_ms(uint8_t speed) {
    return 200 + uint32_t(10000 - 200) * (255 - speed) / 255;
}

// ── Effect implementations ────────────────────────────────────────────────

// 1. Static White — CW/WW split by hue (hue=0→warm, hue=127→cold), brightness overall
static void fx_static_white(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const uint8_t level = on ? p.brightness : 0;
    const uint8_t ww    = scale8(level, 255 - p.hue);
    const uint8_t cw    = scale8(level, p.hue);
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = {0, 0, 0, 0};
    led_driver_set_ww(ww);
    led_driver_set_cw(cw);
}

// 2. Static Color — solid RGB on outer ring, inner ring off
static void fx_static_color(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const CRGBW c = on ? scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), p.brightness)
                       : CRGBW{};
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 3. Warm Gradient — warm→cool temperature sweep rotating around the ring
static void fx_warm_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t t      = elapsed_ms % period;
    const uint8_t  offset = (t * 256) / period;  // 0–255 phase
    for (int i = 0; i < SK6812_NUM_LEDS; i++) {
        const uint8_t pos  = (i * 256 / SK6812_NUM_LEDS + offset) & 0xFF;
        const uint8_t warm = scale8(p.brightness, 255 - pos);
        const uint8_t cool = scale8(p.brightness, pos);
        leds[i] = {scale8(warm, 200), scale8(warm, 100), cool, 0};
    }
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 4. Color Gradient — multi-hue gradient rotating around the ring
static void fx_color_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < SK6812_NUM_LEDS; i++) {
        const uint8_t hue = p.hue + (i * 256 / SK6812_NUM_LEDS) + offset;
        leds[i] = scale_brightness(hsv_to_rgbw(hue, p.sat, 255), p.brightness);
    }
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 5. Breathing — whole ring fades in and out using a sine-like ramp
static void fx_breathing(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t t      = elapsed_ms % period;
    // Triangle wave 0→255→0
    const uint8_t half = (t < period / 2)
        ? uint8_t(t * 255 / (period / 2))
        : uint8_t(255 - (t - period / 2) * 255 / (period / 2));
    const uint8_t bri = scale8(p.brightness, half);
    const CRGBW   c   = scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), bri);
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 6. Color Cycle — whole ring shifts through the color wheel
static void fx_color_cycle(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  hue    = p.hue + uint8_t(elapsed_ms % period * 256 / period);
    const CRGBW    c      = scale_brightness(hsv_to_rgbw(hue, p.sat, 255), p.brightness);
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 7. Chase — single lit segment travels around the ring
static void fx_chase(uint32_t elapsed_ms, const EffectParams& p, CRGBW* leds, bool on) {
    if (!on) { memset(leds, 0, SK6812_NUM_LEDS * sizeof(CRGBW)); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint32_t pos    = (elapsed_ms / (period / SK6812_NUM_LEDS)) % SK6812_NUM_LEDS;
    const CRGBW    c      = scale_brightness(hsv_to_rgbw(p.hue, p.sat, 255), p.brightness);
    for (int i = 0; i < SK6812_NUM_LEDS; i++)
        leds[i] = (uint32_t(i) == pos) ? c : CRGBW{};
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// 8. Nightlight — dim warm white outer ring only, inner ring off
static void fx_nightlight(uint32_t, const EffectParams& p, CRGBW* leds, bool on) {
    const CRGBW c = on ? CRGBW{0, 0, 0, p.brightness} : CRGBW{};
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}

// ── Lookup table (order must match EffectType enum) ────────────────────────
const Effect kEffects[EFFECT_COUNT] = {
    {"Static White",   fx_static_white},
    {"Static Color",   fx_static_color},
    {"Warm Gradient",  fx_warm_gradient},
    {"Color Gradient", fx_color_gradient},
    {"Breathing",      fx_breathing},
    {"Color Cycle",    fx_color_cycle},
    {"Chase",          fx_chase},
    {"Nightlight",     fx_nightlight},
};
```

- [ ] **Step 3: Update main.cpp to cycle through all effects on bench**

```cpp
// lumary-zigbee/src/main.cpp
#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "effect_params.h"

static CRGBW leds[SK6812_NUM_LEDS];

void setup() {
  Serial.begin(115200);
  led_driver_init();
  Serial.println("Effects bench test — each effect runs for 5 seconds");
}

void loop() {
  static uint8_t  effect_idx  = 0;
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
```

- [ ] **Step 4: Flash and verify all 8 effects visually**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload && pio device monitor
```

Expected: each effect runs for 5 seconds, serial shows effect name. Visually verify:
- Static White: inner ring only (CW or WW depending on hue)
- Static Color: outer ring solid red
- Warm Gradient: outer ring warm→cool rotating sweep
- Color Gradient: outer ring rainbow rotating
- Breathing: outer ring pulsing
- Color Cycle: outer ring shifting hue
- Chase: single segment running around ring
- Nightlight: outer ring dim warm white

- [ ] **Step 5: Commit**

```bash
git add src/effects.h src/effects.cpp src/main.cpp
git commit -m "feat: add 8 built-in effects with lookup table"
```

---

## Task 6: Zigbee Basics (On/Off + Level Control)

**Files:**
- Create: `src/zigbee_light.h`
- Create: `src/zigbee_light.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Write zigbee_light.h**

```cpp
// lumary-zigbee/src/zigbee_light.h
#pragma once
#include "effect_params.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize Zigbee Extended Color Light on endpoint LIGHT_ENDPOINT.
// Registers callbacks and starts Zigbee as an end device.
void zigbee_light_init();

// Returns true if the light is currently on
bool zigbee_light_is_on();

// Returns current brightness level 0–255
uint8_t zigbee_light_get_level();

// Returns currently active scene index (0–15)
uint8_t zigbee_light_get_scene();

// Called by main loop: returns true if state changed since last call
bool zigbee_light_consume_change();

// Get the current resolved effect params (from active scene + Zigbee overrides)
EffectParams zigbee_light_get_params();
```

- [ ] **Step 2: Write zigbee_light.cpp (on/off + level only for now)**

```cpp
// lumary-zigbee/src/zigbee_light.cpp
#include "zigbee_light.h"
#include "config.h"
#include "scene_store.h"
#include "Zigbee.h"

static ZigbeeColorDimmableLight s_zbLight(LIGHT_ENDPOINT);

static bool    s_on      = true;
static uint8_t s_level   = 255;
static uint8_t s_scene   = 0;
static bool    s_changed = false;

static void on_light_change(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
    s_on      = state;
    s_level   = level;
    s_changed = true;
}

void zigbee_light_init() {
    s_scene = scene_store_get_active();

    s_zbLight.onLightChange(on_light_change);
    s_zbLight.setManufacturerAndModel("DIY", "LumaryZigbee");

    Zigbee.addEndpoint(&s_zbLight);
    if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee init failed — rebooting");
        ESP.restart();
    }
    Serial.println("Zigbee started — waiting for network join");
}

bool zigbee_light_is_on()      { return s_on; }
uint8_t zigbee_light_get_level() { return s_level; }
uint8_t zigbee_light_get_scene() { return s_scene; }

bool zigbee_light_consume_change() {
    bool changed = s_changed;
    s_changed = false;
    return changed;
}

EffectParams zigbee_light_get_params() {
    EffectParams p;
    scene_store_load(s_scene, &p);
    p.brightness = scale8(p.brightness, s_level);
    return p;
}
```

- [ ] **Step 3: Update main.cpp with Zigbee + effect engine integrated**

```cpp
// lumary-zigbee/src/main.cpp
#include <Arduino.h>
#include "config.h"
#include "led_driver.h"
#include "effects.h"
#include "scene_store.h"
#include "zigbee_light.h"
#include "esp_task_wdt.h"

static CRGBW     s_leds[SK6812_NUM_LEDS];
static uint32_t  s_effect_start = 0;
static uint32_t  s_last_frame   = 0;

void setup() {
  Serial.begin(115200);
  led_driver_init();
  scene_store_init();
  zigbee_light_init();

  // Hardware watchdog — reset if loop stalls > WDT_TIMEOUT_MS
  esp_task_wdt_config_t wdt_cfg = {
    .timeout_ms = WDT_TIMEOUT_MS,
    .trigger_panic = true
  };
  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL);  // watch main task

  s_effect_start = millis();
}

void loop() {
  esp_task_wdt_reset();  // feed the watchdog

  if (zigbee_light_consume_change()) {
    s_effect_start = millis();  // reset effect timer on state change
  }

  uint32_t now = millis();
  if (now - s_last_frame < 16) return;
  s_last_frame = now;

  const EffectParams p      = zigbee_light_get_params();
  const bool         on     = zigbee_light_is_on();
  const uint8_t      etype  = (uint8_t)p.type;

  if (etype < EFFECT_COUNT) {
    kEffects[etype].fn(now - s_effect_start, p, s_leds, on);
    led_driver_show(s_leds, SK6812_NUM_LEDS);
  }
}
```

- [ ] **Step 4: Flash and pair with Zigbee2MQTT**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload && pio device monitor
```

In Zigbee2MQTT:
1. Enable permit join
2. Power on the ESP32-H2 — it will attempt to join
3. Expected serial: `Zigbee started — waiting for network join` then `ZB joined network`
4. In Z2M dashboard: device appears as "DIY LumaryZigbee"
5. Test on/off toggle from Z2M dashboard — verify light responds

- [ ] **Step 5: Bind Inovelli switch to light**

In Zigbee2MQTT:
1. Go to device page for the Inovelli Blue Series switch
2. Bind tab → Source Endpoint: `2` → Target: `LumaryZigbee` → Endpoint: `1`
3. Bind clusters: `genOnOff`, `genLevelCtrl`
4. Test: 1× tap up/down toggles light without hub in path

- [ ] **Step 6: Commit**

```bash
git add src/zigbee_light.h src/zigbee_light.cpp src/main.cpp
git commit -m "feat: add Zigbee on/off and level control, bind to Inovelli switch"
```

---

## Task 7: Zigbee Color Control + Scenes Cluster

**Files:**
- Modify: `src/zigbee_light.h`
- Modify: `src/zigbee_light.cpp`

- [ ] **Step 1: Add color + scene callbacks to zigbee_light.h**

Add these declarations after existing ones:

```cpp
// Add to zigbee_light.h:

// Set active scene by Zigbee scene recall command (called by Zigbee callback)
void zigbee_light_set_scene(uint8_t scene_id);

// Add or update a scene from a Zigbee Add Scene command
void zigbee_light_add_scene(uint8_t scene_id, const EffectParams* params);
```

- [ ] **Step 2: Add color temperature + scene callbacks in zigbee_light.cpp**

Add after the existing `on_light_change` callback, before `zigbee_light_init()`:

```cpp
// Add to zigbee_light.cpp:

static uint16_t s_color_temp_mireds = 370; // ~2700K warm default

static void on_color_temp_change(uint16_t color_temp_mireds, uint8_t level) {
    s_color_temp_mireds = color_temp_mireds;
    s_level   = level;
    s_changed = true;
}

void zigbee_light_set_scene(uint8_t scene_id) {
    if (scene_id >= NVS_MAX_SCENES) return;
    s_scene = scene_id;
    scene_store_set_active(scene_id);
    s_effect_start = 0;  // signal main loop to reset effect timer
    s_changed = true;
}

void zigbee_light_add_scene(uint8_t scene_id, const EffectParams* params) {
    scene_store_save(scene_id, params);
}
```

- [ ] **Step 3: Register color temperature callback in zigbee_light_init()**

Add after `s_zbLight.onLightChange(on_light_change);`:

```cpp
// Add inside zigbee_light_init(), after onLightChange registration:
s_zbLight.onColorTemperatureChange(on_color_temp_change);
```

- [ ] **Step 4: Update get_params() to use color temperature for Static White**

Replace the existing `zigbee_light_get_params()` implementation:

```cpp
// Replace zigbee_light_get_params() in zigbee_light.cpp:
EffectParams zigbee_light_get_params() {
    EffectParams p;
    scene_store_load(s_scene, &p);
    // For Static White, map color temp mireds (153=6500K cold, 500=2000K warm) to hue
    if (p.type == EFFECT_STATIC_WHITE) {
        // 153 mireds → hue=255 (cold), 500 mireds → hue=0 (warm)
        uint16_t clamped = s_color_temp_mireds < 153 ? 153 :
                           s_color_temp_mireds > 500 ? 500 : s_color_temp_mireds;
        p.hue = uint8_t(255 - (uint32_t(clamped - 153) * 255 / (500 - 153)));
    }
    p.brightness = scale8(p.brightness, s_level);
    return p;
}
```

- [ ] **Step 5: Flash and verify color temperature and scenes via Z2M**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload
```

In Z2M:
1. Set color temperature slider on the light — verify inner ring shifts warm/cool
2. Send "Add Scene" command with scene_id=1, effect_type=EFFECT_BREATHING (value 4) — verify stored
3. Send "Recall Scene" for scene_id=1 — verify breathing effect starts

- [ ] **Step 6: Bind scene endpoint on Inovelli switch for multi-tap**

In Z2M:
1. Bind tab on Inovelli switch → Source Endpoint: `2`
2. Add binding for cluster `genScenes` to LumaryZigbee endpoint `1`
3. Test: 2× tap up → next scene cycles; 2× tap down → previous scene

- [ ] **Step 7: Commit**

```bash
git add src/zigbee_light.h src/zigbee_light.cpp
git commit -m "feat: add color temperature and Zigbee scenes cluster"
```

---

## Task 8: Zigbee OTA Upgrade Cluster

**Files:**
- Create: `src/zigbee_ota.h`
- Create: `src/zigbee_ota.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Write zigbee_ota.h**

```cpp
// lumary-zigbee/src/zigbee_ota.h
#pragma once

// Initialize Zigbee OTA Upgrade cluster.
// Must be called after Zigbee.begin() in setup().
void zigbee_ota_init();
```

- [ ] **Step 2: Write zigbee_ota.cpp**

```cpp
// lumary-zigbee/src/zigbee_ota.cpp
#include "zigbee_ota.h"
#include "config.h"
#include "Zigbee.h"
#include "esp_ota_ops.h"

// Firmware version — increment this before each OTA release.
// Z2M compares this against the OTA image header; it will only offer
// the update if the image version is higher than this value.
#define FIRMWARE_VERSION  0x00000001

static ZigbeeOTA s_zbOta;

static void on_ota_update(uint8_t progress) {
    // progress: 0–100 percent
    Serial.printf("OTA progress: %u%%\n", progress);
}

void zigbee_ota_init() {
    s_zbOta.setVersion(FIRMWARE_VERSION);
    s_zbOta.onOTAUpdate(on_ota_update);
    Zigbee.addEndpoint(&s_zbOta);
    Serial.printf("OTA cluster ready, firmware version 0x%08X\n", FIRMWARE_VERSION);
}
```

- [ ] **Step 3: Add zigbee_ota_init() call to main.cpp setup()**

In `setup()`, add after `zigbee_light_init()`:

```cpp
// Add to setup() in main.cpp, after zigbee_light_init():
zigbee_ota_init();
```

Add include at top of main.cpp:

```cpp
#include "zigbee_ota.h"
```

- [ ] **Step 4: Download ota_image_tool.py**

```bash
cd /root/lumary-zigbee
curl -L -o ota_image_tool.py \
  https://raw.githubusercontent.com/espressif/esp-zigbee-sdk/main/tools/ota_image_tool.py
chmod +x ota_image_tool.py
```

- [ ] **Step 5: Build firmware and create OTA image**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2
python3 ota_image_tool.py create \
  --manufacturer-code 0x1001 \
  --image-type 0x0001 \
  --file-version 0x00000002 \
  --stack-version 2 \
  --header-string "LumaryZigbee v2" \
  .pio/build/esp32h2/firmware.bin \
  lumary_v2.ota
```

Expected: `lumary_v2.ota` created in project root.

- [ ] **Step 6: Test OTA via Z2M**

1. Copy `lumary_v2.ota` into Z2M's `data/ota/` folder
2. In Z2M `configuration.yaml` verify `ota: true` is set under the `advanced:` key
3. Restart Z2M
4. In Z2M dashboard → device page for LumaryZigbee → "Check for updates"
5. Expected: update offered; click Update; watch serial output show progress
6. Device reboots into new firmware — confirm it rejoins and operates normally

- [ ] **Step 7: Commit**

```bash
git add src/zigbee_ota.h src/zigbee_ota.cpp src/main.cpp ota_image_tool.py
git commit -m "feat: add Zigbee OTA upgrade cluster"
```

---

## Task 9: BLE OTA Fallback

**Files:**
- Create: `src/ble_ota.h`
- Create: `src/ble_ota.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Write ble_ota.h**

```cpp
// lumary-zigbee/src/ble_ota.h
#pragma once

// Poll this in loop(). Starts BLE OTA mode if BOOT button held 5+ seconds.
// Once in BLE OTA mode the device advertises "LumaryOTA" and accepts
// firmware via the NimBLE OTA GATT service.
void ble_ota_poll();
```

- [ ] **Step 2: Write ble_ota.cpp**

```cpp
// lumary-zigbee/src/ble_ota.cpp
#include "ble_ota.h"
#include "config.h"
#include "led_driver.h"
#include "color.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

// NimBLE OTA uses a custom GATT service:
// Service UUID:       fb1e4001-54ae-4a28-9f74-dfccb248601d
// Data characteristic: fb1e4002-54ae-4a28-9f74-dfccb248601d  (WRITE_NO_RESPONSE)
// Control characteristic: fb1e4003-54ae-4a28-9f74-dfccb248601d (NOTIFY)

#define OTA_SERVICE_UUID  "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define OTA_DATA_UUID     "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define OTA_CTRL_UUID     "fb1e4003-54ae-4a28-9f74-dfccb248601d"

static bool              s_ota_mode       = false;
static uint32_t          s_btn_press_start = 0;
static NimBLEServer*     s_server         = nullptr;
static NimBLECharacteristic* s_ctrl_char  = nullptr;
static const esp_partition_t* s_ota_part  = nullptr;
static esp_ota_handle_t  s_ota_handle     = 0;
static bool              s_ota_in_progress = false;

static void blink_blue_status() {
    static CRGBW leds[SK6812_NUM_LEDS];
    static bool  on = false;
    on = !on;
    const CRGBW c = on ? CRGBW{0, 0, 255, 0} : CRGBW{};
    for (int i = 0; i < SK6812_NUM_LEDS; i++) leds[i] = c;
    led_driver_show(leds, SK6812_NUM_LEDS);
}

class OtaDataCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        const std::string& val = pChar->getValue();
        if (!s_ota_in_progress) {
            // First packet — begin OTA
            s_ota_part = esp_ota_get_next_update_partition(nullptr);
            esp_ota_begin(s_ota_part, OTA_SIZE_UNKNOWN, &s_ota_handle);
            s_ota_in_progress = true;
        }
        esp_ota_write(s_ota_handle, val.data(), val.size());

        // Check for end-of-image marker (client sends 0-byte packet to commit)
        if (val.empty()) {
            if (esp_ota_end(s_ota_handle) == ESP_OK &&
                esp_ota_set_boot_partition(s_ota_part) == ESP_OK) {
                s_ctrl_char->setValue("OK");
                s_ctrl_char->notify();
                delay(500);
                esp_restart();
            } else {
                s_ctrl_char->setValue("ERR");
                s_ctrl_char->notify();
                s_ota_in_progress = false;
            }
        }
    }
};

static void start_ble_ota_mode() {
    Serial.println("BLE OTA mode — advertising as LumaryOTA");
    NimBLEDevice::init("LumaryOTA");
    s_server = NimBLEDevice::createServer();

    NimBLEService* svc = s_server->createService(OTA_SERVICE_UUID);
    NimBLECharacteristic* data_char = svc->createCharacteristic(
        OTA_DATA_UUID, NIMBLE_PROPERTY::WRITE_NR);
    data_char->setCallbacks(new OtaDataCallback());

    s_ctrl_char = svc->createCharacteristic(
        OTA_CTRL_UUID, NIMBLE_PROPERTY::NOTIFY);

    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(OTA_SERVICE_UUID);
    adv->start();
    s_ota_mode = true;
}

void ble_ota_poll() {
    if (s_ota_mode) {
        // Flash outer ring blue while in BLE OTA mode
        static uint32_t last_blink = 0;
        if (millis() - last_blink > 500) {
            last_blink = millis();
            blink_blue_status();
        }
        return;
    }

    const bool btn_pressed = (digitalRead(PIN_BLE_OTA_BUTTON) == LOW);
    if (btn_pressed) {
        if (s_btn_press_start == 0) s_btn_press_start = millis();
        if (millis() - s_btn_press_start >= 5000) {
            start_ble_ota_mode();
        }
    } else {
        s_btn_press_start = 0;
    }
}
```

- [ ] **Step 3: Add ble_ota_poll() to main.cpp**

Add include at top of main.cpp:

```cpp
#include "ble_ota.h"
```

In `setup()`, configure BOOT button pin after led_driver_init():

```cpp
// Add to setup() after led_driver_init():
pinMode(PIN_BLE_OTA_BUTTON, INPUT_PULLUP);
```

At the top of `loop()`, before watchdog reset:

```cpp
// Add as first line of loop():
ble_ota_poll();
```

- [ ] **Step 4: Flash, hold BOOT button 5s, verify BLE OTA mode**

```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload && pio device monitor
```

1. Hold the BOOT button for 5+ seconds
2. Expected serial: `BLE OTA mode — advertising as LumaryOTA`
3. Outer ring flashes blue
4. On laptop: `bluetoothctl scan on` — verify "LumaryOTA" appears
5. BLE OTA upload is tested in Phase 6 after install (use ESPHome BLE client or custom script)

- [ ] **Step 5: Commit**

```bash
git add src/ble_ota.h src/ble_ota.cpp src/main.cpp
git commit -m "feat: add NimBLE OTA fallback triggered by BOOT button hold"
```

---

## Task 10: Final Integration + Bench Validation

**Files:**
- Modify: `src/main.cpp` (final cleanup)

- [ ] **Step 1: Final main.cpp — add serial status logging**

Add to `loop()` after the `now - s_last_frame` check, inside the frame block:

```cpp
// Add inside the frame render block in loop(), after led_driver_show():
static uint32_t last_status = 0;
if (now - last_status > 5000) {
    last_status = now;
    const EffectParams dbg = zigbee_light_get_params();
    Serial.printf("on=%d level=%u scene=%u effect=%u hue=%u bri=%u\n",
        zigbee_light_is_on(),
        zigbee_light_get_level(),
        zigbee_light_get_scene(),
        (uint8_t)dbg.type,
        dbg.hue,
        dbg.brightness);
}
```

- [ ] **Step 2: Full validation checklist on bench before install**

Flash final firmware:
```bash
cd /root/lumary-zigbee && pio run -e esp32h2 --target upload
```

Run through each item:
- [ ] 1× tap up/down → light on/off (no hub)
- [ ] Hold up → brightness increases smoothly (no hub)
- [ ] 2× tap up → cycles through scenes (no hub)
- [ ] 2× tap down → cycles scenes in reverse (no hub)
- [ ] Color temperature slider in Z2M → inner ring shifts warm/cool
- [ ] Add Scene command from Z2M → custom scene stored, survives power cycle
- [ ] Power cycle (cut power, restore) → light returns to last scene and state
- [ ] Zigbee OTA: push v2 image via Z2M → firmware updates, rejoins
- [ ] BLE OTA: hold BOOT 5s → BLE mode, outer ring flashes blue
- [ ] Kill Z2M mid-use → switch still controls light directly

- [ ] **Step 3: Commit final state**

```bash
git add src/main.cpp
git commit -m "feat: final integration — full feature parity validation complete"
```

---

## Task 11: Install in Ceiling

- [ ] **Step 1: Measure controller box interior before opening light**

Use calipers or ruler on the light's control box. ESP32-H2 Super Mini is 25×18mm — confirm it fits with some margin for wiring.

- [ ] **Step 2: Verify PSU rail voltage and current**

Before disconnecting anything:
- Measure the 3.3V rail voltage under load (multimeter)
- Check PSU markings for 3.3V current rating
- If rating is ≤150mA, wire a dedicated AMS1117-3.3 from the 5V rail for the ESP32-H2

- [ ] **Step 3: Disconnect CBU board and tap LED signal lines**

1. Cut power to the light at the breaker
2. Open controller box
3. Photograph the original wiring before touching anything
4. Disconnect original CBU board from JST connector
5. Identify signal wires per spec: P16=SK6812 data, P7=WW, P8=CW (verify with multimeter continuity)
6. Solder hookup wires to GPIO 11, GPIO 2, GPIO 3, 3V3, GND on ESP32-H2 Super Mini
7. Connect to corresponding signal lines from JST harness

- [ ] **Step 4: Tuck ESP32-H2 into controller box and reinstall**

1. Place ESP32-H2 in controller box — secure with hot glue or double-sided foam tape
2. Route wires so they don't pinch
3. Close controller box
4. Install light in ceiling per Lumary instructions
5. Restore power at breaker

- [ ] **Step 5: Final in-ceiling validation**

Repeat the validation checklist from Task 10 Step 2 with the light installed. Pay particular attention to:
- Zigbee signal strength in Z2M (RSSI should be > -70 dBm)
- All effects visible through the ceiling trim
- Switch binding still works at normal operating distance

- [ ] **Step 6: Tag release**

```bash
cd /root/lumary-zigbee
git tag v1.0.0
git push origin master --tags
```

---

## Phase 1 Hardware Notes (Reference)

Before starting Task 3 (LED driver), confirm on bench:
1. **SK6812 color order** — test with solid red, verify R channel is red (not green). If colors are swapped, change wire order in `led_driver.cpp` from `{g, r, b, w}` to `{r, g, b, w}` or `{g, r, b, w}` as needed.
2. **Logic level** — measure SK6812 strip supply voltage. If 5V, add 74AHCT125 between GPIO 11 and strip data input before proceeding.
3. **FastLED vs bare SPI** — this firmware uses bare ESP-IDF SPI driver (not FastLED) to drive SK6812, avoiding the RMT/Zigbee conflict entirely. FastLED is not required.
4. **Controller box dimensions** — measure before ordering boards to confirm 25×18mm board fits.

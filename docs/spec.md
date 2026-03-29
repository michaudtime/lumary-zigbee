# Lumary Smart Recessed Light — Zigbee Controller Replacement

**Date:** 2026-03-28
**Status:** Approved

---

## Problem

Lumary RGBAI (RGB + Auxiliary Illumination) 6" recessed lights use a Tuya CBU (BK7231N) WiFi module. Even with local Tuya, reliability is tied to the server being up. Inovelli Blue Series Zigbee switches can't bind directly to WiFi lights, so scenes and local control require the hub to be online.

**Goal:** Replace the WiFi controller with a Zigbee-native one so the Inovelli switch binds directly to the light — hub-independent on/off, dimming, and effects.

---

## Hardware

### Target Light
- Lumary Smart RGBAI Recessed Light, 6"
- **Outer ring:** 36x SK6812 addressable RGBW LEDs (single-wire NZR protocol, similar to WS2812B)
- **Inner ring:** Cold white + warm white LEDs (2x PWM)
- **Controller box:** Separate from light body, connected by wire harness — houses the driver PCB and has space for a replacement board. Interior dimensions TBD — measure before ordering boards.

### Replacement Board
**ESP32-H2 Super Mini** (~$5, ~25×18mm)
- Native IEEE 802.15.4 Zigbee radio (no separate module)
- Built-in BLE (used for OTA fallback)
- Runs on 3.3V (available from existing PSU on driver board)
- Small enough to fit in the existing control box

### Wiring

| ESP32-H2 Pin | Signal | Destination | Notes |
|---|---|---|---|
| GPIO 11 (SPI2 MOSI) | SK6812 data | Outer ring 36-LED strip | SPI used to emulate NZR timing (see below) |
| GPIO 2 | PWM cold white | Inner ring CW | LEDC peripheral |
| GPIO 3 | PWM warm white | Inner ring WW | LEDC peripheral |
| 3.3V | Power | Tap from existing driver board | Verify rail capacity (see Power Budget) |
| GND | Ground | Common ground | |

Original CBU board is disconnected. Signal lines are tapped from the existing JST connector. Existing power supply remains in place.

### SK6812 Data Line — Protocol Note
SK6812 LEDs use a single-wire NZR timed protocol (not multi-wire SPI). On ESP32-H2, the RMT peripheral conflicts with the Zigbee radio and cannot be used. Instead, the SPI2 peripheral drives the data line by encoding each NZR bit as 3 SPI bits (1 = `110`, 0 = `100`) at 2.4 MHz SPI clock, producing a valid 800kHz NZR signal on MOSI. This technique is used in the [ShaunPCcom ESP32-H2 Zigbee LED project](https://github.com/ShaunPCcom/zigbee-LED-ESP32-controller) and is confirmed working.

**Phase 1 deliverables for SK6812:**
- Confirm GPIO 11 produces correct NZR waveform with SPI encoding
- Verify SK6812 color order (RGBW vs GRBW — batch-dependent, set as FastLED compile-time constant)

### Logic Level / Level Shifter
The ESP32-H2 outputs 3.3V logic. SK6812 strips powered at 5V require V_IH ≥ 3.5V (0.7 × 5V), making 3.3V signals marginal. **Phase 1 must verify signal integrity.** If the strip is powered at 3.3V (uncommon) no shifter is needed. If at 5V, add a single 74AHCT125 buffer or N-channel FET level shifter on the data line.

### Power Budget

| Component | Typical | Peak |
|---|---|---|
| ESP32-H2 (MCU active) | 50mA | 100mA |
| ESP32-H2 (Zigbee Tx) | +50mA burst | — |
| Original CBU (replaced) | ~80mA | — |
| **Net change on 3.3V rail** | ~+20mA typical | ~+70mA burst |

The existing 3.3V rail must tolerate the ESP32-H2's peak current including Zigbee Tx bursts. Verify the PSU's 3.3V rail rating before installation. If the rail is marginal, add a small LDO (e.g., AMS1117-3.3) fed from the 5V rail as a dedicated supply for the ESP32-H2.

Note: The SK6812 strip and CW/WW LEDs are driven by the existing higher-voltage rail (not 3.3V) — only the controller logic is on the 3.3V rail.

---

## Software Architecture

**Toolchain:** PlatformIO, Arduino framework, ESP32-H2 target
**Key libraries:** Arduino ESP32 Zigbee (v3.x), FastLED, NimBLE

```
┌─────────────────────────────────────────────┐
│         Zigbee Layer + OTA cluster          │
│   On/Off · Level · Color · Scenes · OTA     │
│       (Arduino ESP32 Zigbee library)        │
│   Runs on background FreeRTOS task          │
├─────────────────────────────────────────────┤
│       BLE Layer (NimBLE OTA fallback)       │
├─────────────────────────────────────────────┤
│              Effect Engine                  │
│  Active effect → renders frame every 16ms  │
│  Triggered by Zigbee scene recall / color   │
├─────────────────────────────────────────────┤
│             LED Driver Layer                │
│   FastLED → SK6812 (SPI2 NZR, 36 LEDs)    │
│   LEDC PWM → CW channel + WW channel       │
└─────────────────────────────────────────────┘
```

### Zigbee Device Type
**Extended Color Light** — exposes standard clusters:
- On/Off cluster
- Level Control cluster (brightness)
- Color Control cluster (RGB + color temperature)
- Scenes cluster (store/recall up to 16 scenes)
- OTA Upgrade cluster (firmware updates via Z2M)

Zigbee endpoint: **1** (standard for primary light endpoint — required for Inovelli Blue Series binding; set Source Endpoint to 2 on the switch, bind to endpoint 1 on the light).

This device type binds natively to the Inovelli Blue Series 2-1 switch.

### Render Loop / Zigbee Coexistence
The Arduino ESP32 Zigbee library runs the Zigbee stack on a background FreeRTOS task. The effect engine runs in `loop()` on the main task. FastLED's `show()` for 36 SK6812 LEDs at 800kHz takes ~1.1ms — well within Zigbee timing tolerances. The hardware watchdog is enabled with a 5-second timeout. If the main loop stalls (e.g., effect function bug), the device resets rather than hanging indefinitely.

### Effect Engine
Each effect is a function with signature:
```cpp
void effectFn(uint32_t elapsed_ms, EffectParams params);
```

Stored in a `const Effect effects[]` lookup table. The active effect index and parameters are stored in NVS so the light restores its last state after a power cut.

Each frame (~16ms):
1. Active effect function updates the FastLED buffer
2. `FastLED.show()` pushes to SK6812 via SPI2 NZR encoding
3. LEDC PWM levels updated for CW/WW

### Built-in Effects

| Scene # | Name | Description |
|---|---|---|
| 1 | Static White | CW/WW blend, brightness from dimmer |
| 2 | Static Color | Solid RGB on outer ring |
| 3 | Warm Gradient | Slow warm→cool sweep across 36 segments |
| 4 | Color Gradient | Multi-color gradient rotating around ring |
| 5 | Breathing | Fade in/out on outer ring |
| 6 | Color Cycle | HSV hue rotation through full spectrum |
| 7 | Chase | Single color chasing around the ring |
| 8 | Nightlight | Dim warm white outer ring only |

### Scene Management
**Zigbee Scenes cluster (primary):** HA/Zigbee2MQTT can send standard Add Scene commands with parameters (effect type, color, brightness, speed). Scenes are stored in NVS. Up to 16 scenes. All stored scenes work from the switch with no hub.

**Scene parameters per scene:**
- Effect type ID (maps to effects table)
- Color (HSV)
- Brightness (0–255)
- Speed/rate (0–255)

**NVS storage schema** (namespace: `lm_light`):

| Key | Type | Description |
|---|---|---|
| `fmt_ver` | u8 | Schema version, default 1. If missing or mismatched, reset to defaults |
| `active_scene` | u8 | Currently active scene index (0–15) |
| `scene_N_type` | u8 | Effect type ID for scene N |
| `scene_N_hue` | u8 | Hue for scene N |
| `scene_N_sat` | u8 | Saturation for scene N |
| `scene_N_bri` | u8 | Brightness for scene N |
| `scene_N_spd` | u8 | Speed for scene N |

On boot, if NVS read fails or `fmt_ver` is absent, fall back to Scene 1 (Static White, full brightness).

> **Note:** ESP-IDF NVS enforces a 15-character key limit. All keys above are within limit. Do not extend the naming scheme without re-checking.

### Switch Binding (Hub-Independent)
The Inovelli Blue Series switch handles all multi-tap logic internally. The light only needs to implement the Zigbee Scenes cluster correctly.

| Switch action | Zigbee command sent by switch | Light behavior |
|---|---|---|
| 1x tap | On/Off (direct binding) | Toggle on/off |
| Hold up/down | Level Control (direct binding) | Brighten/dim |
| 2x tap up | Scene Recall (scene endpoint) | Next scene |
| 2x tap down | Scene Recall (scene endpoint) | Previous scene |
| 3x tap | Scene Recall (specific ID) | Jump to scene (configured in Z2M) |

All commands travel directly over Zigbee from switch to light — no hub in the path.

---

## OTA Updates

### Primary: Zigbee OTA Upgrade cluster
- Standard Zigbee OTA protocol
- Z2M detects firmware in OTA folder, surfaces update in HA dashboard
- Firmware streams over Zigbee mesh in chunks
- Device reboots into new firmware on completion
- Fail-safe: device stays on current firmware if update is interrupted

**Building an OTA image from PlatformIO:**
1. PlatformIO produces a `.bin` file in `.pio/build/esp32h2/`
2. Wrap it in Zigbee OTA image format using Espressif's `ota_image_tool.py` (available at [espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) — not included in Arduino environment by default, download separately)
3. Set manufacturer code and image type in the header — these must match values declared in firmware
4. Drop the `.ota` file into Z2M's `ota/` folder
5. In Z2M `configuration.yaml`, enable `ota: true` — Z2M will offer the update automatically

### Fallback: BLE OTA (NimBLE)
- ESP32-H2 BLE radio used as backup OTA channel
- Uses Espressif's standard NimBLE OTA GATT service
- Flash from laptop via `idf.py` BLE OTA tool or from phone via the ESP BLE Provisioning app
- Activate BLE OTA mode by holding the BOOT button (GPIO 9) on the ESP32-H2 Super Mini for 5 seconds

---

## Development Plan

1. **Phase 1 — Proof of concept:** Bring up ESP32-H2 Super Mini on bench. Drive SK6812 via SPI2 NZR encoding, verify waveform with logic analyzer or oscilloscope. Verify CW/WW PWM. Confirm SK6812 color order. Measure controller box interior dimensions. Check logic level — add level shifter if needed. Verify 3.3V rail current headroom. Confirm FastLED SPI2 backend works correctly under the Arduino ESP32 Zigbee FreeRTOS task model; if not, fall back to bare esp-idf `spi_device` API for LED driving.
2. **Phase 2 — Zigbee basics:** Join network via Z2M, on/off and dimming via switch binding, confirm endpoint 1 binding works with Inovelli switch
3. **Phase 3 — Color + scenes:** Color control cluster, scene storage in NVS with defined schema
4. **Phase 4 — Effects engine:** Implement all 8 effects, scene recall triggers effects, watchdog enabled
5. **Phase 5 — OTA:** Zigbee OTA cluster + BLE OTA fallback, test full update cycle on bench
6. **Phase 6 — Install + validate:** One light installed in ceiling, all features validated in-place

---

## Open Questions / Notes for v2
- Custom PCB (CBU drop-in form factor) if v1 validates the approach
- Additional effects can be added via OTA — no hardware change needed
- BLE OTA companion app for scene editing without HA
- Power-on flash: on power restore, SK6812 will briefly show last color before firmware initializes (~500ms). Acceptable for a ceiling light; can be mitigated in v2 with a data-line pull-down resistor

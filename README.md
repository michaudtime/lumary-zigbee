# Lumary Zigbee Controller

Replaces the Tuya WiFi controller inside a **Lumary 6" RGBAI recessed light** with an **ESP32-H2 Zigbee controller**, enabling:

- Direct Zigbee binding to Inovelli Blue Series switches (hub-independent on/off, dimming, scene control)
- 8 built-in lighting effects with per-scene parameters stored on-device
- Zigbee OTA firmware updates via Zigbee2MQTT
- BLE OTA fallback (hold BOOT button 5s) for when the light is already in the ceiling

## Hardware

| Component | Part |
|---|---|
| Controller | ESP32-H2 Super Mini (~$5, ~25×18mm) |
| Target light | [Lumary Smart RGBAI Recessed Light 6"](https://www.lumarysmart.com/products/lumary-smart-recessed-light-with-gradient-auxiliary-light) |
| Switch | [Inovelli Blue Series 2-1 Zigbee](https://inovelli.com/products/zigbee-matter-blue-series-smart-2-1-on-off-dimmer-switch) |

### Wiring

| ESP32-H2 Pin | Signal | Destination |
|---|---|---|
| GPIO 11 (SPI2 MOSI) | SK6812 data | Outer ring 36-LED strip |
| GPIO 2 | PWM cold white | Inner ring CW |
| GPIO 3 | PWM warm white | Inner ring WW |
| 3.3V | Power | Tap from existing driver board |
| GND | Ground | Common ground |

> **Note:** SK6812 strips powered at 5V require a 74AHCT125 level shifter on the data line. Verify your strip voltage before wiring.

## Why SPI instead of RMT for SK6812?

On ESP32-H2, the RMT peripheral conflicts with the Zigbee radio. Instead, SPI2 is used to bit-encode the single-wire NZR protocol: each NZR bit is encoded as 3 SPI bits (`1 → 110`, `0 → 100`) at 2.4 MHz, producing a valid 800 kHz signal on MOSI.

## Built-in Effects

| # | Name | Description |
|---|---|---|
| 1 | Static White | CW/WW blend via color temp, brightness dimmer-controlled |
| 2 | Static Color | Solid RGB on outer ring |
| 3 | Warm Gradient | Warm→cool sweep rotating around the ring |
| 4 | Color Gradient | Multi-hue gradient rotating around ring |
| 5 | Breathing | Outer ring pulses in/out |
| 6 | Color Cycle | Full HSV hue rotation |
| 7 | Chase | Single lit segment travels around ring |
| 8 | Nightlight | Dim warm white outer ring only |

## Switch Control (Hub-Independent)

| Inovelli action | Behavior |
|---|---|
| 1× tap | On/Off |
| Hold up/down | Brightness |
| 2× tap up | Next scene/effect |
| 2× tap down | Previous scene/effect |

All actions use direct Zigbee binding — no hub required.

## Scene Management

Scenes are stored on-device in NVS (survives power cuts). Up to 16 scenes. When HA/Z2M is available, use standard Zigbee "Add Scene" commands to create or modify scenes with custom parameters (effect type, color, brightness, speed).

## OTA Updates

**Primary:** Zigbee OTA via Zigbee2MQTT — drop `.ota` image in Z2M's `data/ota/` folder, update appears in HA dashboard.

**Fallback:** Hold the BOOT button (GPIO 9) for 5 seconds — device enters BLE OTA mode, outer ring flashes blue, advertises as `LumaryOTA`.

### Building an OTA image

```bash
# Get the tool
curl -L -o ota_image_tool.py \
  https://raw.githubusercontent.com/espressif/esp-zigbee-sdk/main/tools/ota_image_tool.py

# Build firmware
pio run -e esp32h2

# Wrap as OTA image
python3 ota_image_tool.py create \
  --manufacturer-code 0x1001 \
  --image-type 0x0001 \
  --file-version 0x00000002 \
  --stack-version 2 \
  --header-string "LumaryZigbee v2" \
  .pio/build/esp32h2/firmware.bin \
  lumary_v2.ota
```

## Build & Flash

```bash
# Install PlatformIO CLI if needed
pip install platformio

# Build
pio run -e esp32h2

# Flash
pio run -e esp32h2 --target upload

# Serial monitor
pio device monitor
```

## Project Structure

```
src/
  config.h          — Pin definitions, constants
  color.h           — CRGBW struct, HSV conversion, color math
  led_driver.h/.cpp — SPI2 SK6812 driver + LEDC CW/WW PWM
  effect_params.h   — EffectType enum, EffectParams struct
  effects.h/.cpp    — 8 effect implementations + lookup table
  scene_store.h/.cpp — NVS scene persistence
  zigbee_light.h/.cpp — Zigbee Extended Color Light clusters
  zigbee_ota.h/.cpp — Zigbee OTA Upgrade cluster
  ble_ota.h/.cpp    — NimBLE OTA fallback
  main.cpp          — Setup, loop, watchdog
test/
  test_scene_store/ — Native unit tests for scene storage logic
docs/
  spec.md           — Hardware + software design specification
  plan.md           — Implementation plan
```

## Status

- [x] Design spec
- [x] Implementation plan
- [ ] Task 1: Project scaffold
- [ ] Task 2: Color utilities
- [ ] Task 3: LED driver
- [ ] Task 4: Scene storage
- [ ] Task 5: Effects engine
- [ ] Task 6: Zigbee basics
- [ ] Task 7: Zigbee color + scenes
- [ ] Task 8: Zigbee OTA
- [ ] Task 9: BLE OTA fallback
- [ ] Task 10: Integration + validation

## License

MIT

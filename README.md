# Lumary Zigbee Controller

Replaces the Tuya WiFi controller inside a **Lumary 6" RGBAI recessed light** with an **ESP32-H2 Zigbee controller**, enabling:

- Direct Zigbee binding to Inovelli Blue Series switches (hub-independent on/off, dimming, scene control)
- 8 built-in lighting effects with per-scene parameters stored on-device
- Zigbee OTA firmware updates via Zigbee2MQTT
- BLE OTA fallback (hold BOOT button 5s) for when the light is already in the ceiling

## Hardware

| Component | Part |
|---|---|
| Controller | **lumary-brain rev A** — custom ESP32-H2-MINI-1 board, drop-in for the stock `KOK-AH-A172C` (see `hardware/`) |
| Target light | [Lumary Smart RGBAI Recessed Light 6"](https://www.lumarysmart.com/products/lumary-smart-recessed-light-with-gradient-auxiliary-light) |
| Switch | [Inovelli Blue Series 2-1 Zigbee](https://inovelli.com/products/zigbee-matter-blue-series-smart-2-1-on-off-dimmer-switch) |

### The two light sources

The fixture has two independent sources, and the firmware drives them separately:

| Source | Hardware | Driven by |
|---|---|---|
| Outer "gradient" ring | 62-pixel 5 V 3535 **RGBIC** strip, 3 colour bytes/pixel (no white die) | One data line, NZR over SPI |
| Inner white ring | 96 LEDs, 12 series × 4 parallel per colour, 2700 K + 6500 K | Two low-side PWM channels steering the driver's 380 mA |

The external `L-SD8E1` driver supplies **36.63 V constant-current** (inner white anode) and **4.7 V** (ring + logic). The board never sources the white current — it gates and steers it.

### Wiring (rev A board)

| ESP32-H2 Pin | Signal | Destination |
|---|---|---|
| GPIO 11 (SPI2 MOSI) | Ring data → 3.3→4.7 V buffer | `CN1.DIM` (ring DIN) |
| GPIO 4 | PWM cold white → Q1 gate | `CN1.CW-` (6500 K return) |
| GPIO 5 | PWM warm white → Q2 gate | `CN1.WW-` (2700 K return) |
| GPIO 9 | BOOT button | Bench download / BLE OTA |

> **Pin note:** GPIO 2/3/8/9/25 are ESP32-H2 strapping pins. The white PWM channels use GPIO 4/5 on rev A so the MOSFET gate pulldowns can't hold a strapping pin low at reset — the Super Mini prototype's GPIO 2/3 assignment does not carry over.

> **Level shifting:** the ring runs on the 4.7 V rail, so its data line is buffered up from 3.3 V by a `74AHCT1G125` (AHCT accepts 3.3 V as a logic high).

## Why SPI instead of RMT for the ring?

On ESP32-H2, the RMT peripheral conflicts with the Zigbee radio. Instead, SPI2 bit-encodes the single-wire NZR protocol: each NZR bit becomes 3 SPI bits (`1 → 110`, `0 → 100`) at 2.4 MHz, producing a valid 800 kHz signal on MOSI. The encoder lives in `src/pixel_encode.h`, deliberately free of ESP-IDF headers so it can be unit-tested on the host.

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
| 2× tap up | Next effect — **via hub automation**, see below |
| 2× tap down | Previous effect — **via hub automation** |

On/off and dimming use direct Zigbee binding — **no hub required**, and they keep working if the
coordinator is down.

> **Effect stepping is not hub-independent, and cannot be.** The Inovelli's multi-tap events are
> manufacturer-specific and go to the coordinator rather than to a bound light, so nothing the
> light does could make 2× tap work over pure binding. The automation reads the current
> `effect_select` and writes the next one. Not yet verified end to end (plan Task 6.4).

## Selecting an effect

Effect selection rides a **manufacturer-specific cluster (`0xFC00`)**, because nothing standard can
carry it: the Scenes cluster stores colour and level but knows nothing about an effect type, and
the Identify trigger-effects that Z2M's built-in `effect` dropdown drives have no hook in the
Arduino Zigbee library.

Install [`z2m/lumary-brain-revA.js`](z2m/lumary-brain-revA.js) into Z2M's `data/external_converters/`
and restart. That adds an **`effect_select`** control listing the eight effects by name. Without the
converter the light still works normally — the effects are simply unreachable.

Setting a colour or colour temperature **exits the effect** and shows that colour instead;
brightness continues to scale whatever effect is running.

The selected effect persists across power cuts (NVS).

> **Stepping through effects from the wall switch** is done by a hub automation, not by the light:
> read the current `effect_select` and write the next one. The Inovelli's multi-tap events go to
> the coordinator rather than to a bound light, so a hub is in the loop regardless.

Scene *storage* exists — 16 NVS slots, seeded with the defaults in `src/effect_params.h` — but
editing them is **not implemented**. There is no Add Scene support; `scene_store_save()` is only
called to seed defaults at first boot. Changing an effect's colour, speed or brightness
permanently is a future change.

## OTA Updates

**Primary:** Zigbee OTA via Zigbee2MQTT — drop `.ota` image in Z2M's `data/ota/` folder, update appears in HA dashboard.

**Fallback:** Hold the BOOT button (GPIO 9) for 5 seconds — device enters BLE OTA mode, outer ring flashes blue, advertises as `LumaryOTA`.

### Building an OTA image

The device registers an OTA client at startup and queries for an image once it
joins, then hourly. The coordinator only offers images numbered **above** the
running version, so `ZB_FW_VERSION` in `src/config.h` must be bumped and passed
as `--file-version` below — if they disagree, the update silently never appears.

```bash
# Get the tool
curl -L -o ota_image_tool.py \
  https://raw.githubusercontent.com/espressif/esp-zigbee-sdk/main/tools/ota_image_tool.py

# Build firmware (bump ZB_FW_VERSION in src/config.h first)
pio run -e esp32h2

# Wrap as OTA image -- --file-version must equal ZB_FW_VERSION
python3 ota_image_tool.py create \
  --manufacturer-code 0x1001 \
  --image-type 0x0001 \
  --file-version 0x01000001 \
  --stack-version 2 \
  --header-string "LumaryZigbee" \
  .pio/build/esp32h2/firmware.bin \
  lumary.ota
```

## Build & Flash

> **Windows:** run `pio` from **PowerShell or cmd**, not Git Bash. This platform
> version installs its toolchain via `idf_tools.py`, which aborts with
> `ERROR: MSys/Mingw is not supported` under Git Bash. Building also needs
> Windows long-path support enabled (`LongPathsEnabled = 1`).

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
  color.h           — CRGB struct, HSV conversion, color math
  pixel_encode.h    — NZR-over-SPI bit encoder (hardware-free, unit-tested)
  led_driver.h/.cpp — SPI2 ring driver + LEDC CW/WW PWM
  effect_params.h   — EffectType enum, EffectParams struct
  effects.h/.cpp    — 8 effect implementations + lookup table
  scene_store.h/.cpp — NVS scene persistence
  zigbee_light.h/.cpp — Zigbee Extended Color Light clusters
  zigbee_ota.h/.cpp — Zigbee OTA Upgrade cluster
  ble_ota.h/.cpp    — NimBLE OTA fallback
  main.cpp          — Setup, loop, watchdog
test/
  test_pixel_encode/ — Host unit tests for pixel encoding + color math
hardware/
  kicad/            — Board design (build_board.py generates the .kicad_pcb)
  bom.csv, calcs.md, schematic-nets.md
docs/
  spec.md           — Hardware + software design specification
  plan.md           — Implementation plan
  superpowers/      — Board replacement spec, plan, and Phase 0 measurements
```

## Tests

Hardware-independent logic (pixel encoding, colour math) has host-side unit tests:

```bash
scripts\run-native-tests.bat
```

> PlatformIO's `platform = native` expects a gcc toolchain; this script compiles the
> same tests with the MSVC Build Tools that are installed instead. `pio test -e native`
> works as-is on a machine with gcc.

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

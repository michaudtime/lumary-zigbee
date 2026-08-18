# Lumary Zigbee Controller

Replaces the Tuya WiFi controller inside a **Lumary 6" RGBAI recessed light** with an **ESP32-H2 Zigbee controller**, enabling:

- Direct Zigbee binding to Inovelli Blue Series switches (hub-independent on/off, dimming, scene control)
- 6 built-in lighting effects on the accent ring, with per-scene parameters stored on-device
- Zigbee OTA firmware updates via Zigbee2MQTT

## Hardware

| Component | Part |
|---|---|
| Controller | **lumary-brain rev A** — custom ESP32-H2-MINI-1 board, drop-in for the stock `KOK-AH-A172C` (see `hardware/`) |
| Target light | [Lumary Smart RGBAI Recessed Light 6"](https://www.lumarysmart.com/products/lumary-smart-recessed-light-with-gradient-auxiliary-light) |
| Switch | [Inovelli Blue Series 2-1 Zigbee](https://inovelli.com/products/zigbee-matter-blue-series-smart-2-1-on-off-dimmer-switch) |

### The two light sources

The fixture has two independent sources, and the firmware drives them as **two separate Zigbee
endpoints**, so Home Assistant sees two independent light entities rather than one:

| Source | Hardware | Driven by |
|---|---|---|
| Outer "gradient" ring | 62-pixel 5 V 3535 **RGBIC** strip, 3 colour bytes/pixel (no white die) | One data line, NZR over SPI |
| Inner white ring | 96 LEDs, 12 series × 4 parallel per colour, 2700 K + 6500 K | Two low-side PWM channels steering the driver's 380 mA |

The external `L-SD8E1` driver supplies **36.63 V constant-current** (inner white anode) and **4.7 V** (ring + logic). The board never sources the white current — it gates and steers it.

| Entity | Endpoint | Controls |
|---|---|---|
| `light.<name>_downlight` | 1 | on/off, brightness, colour temperature 2700–6500 K |
| `light.<name>_ring` | 2 | on/off, brightness, xy colour, the six effects |

> **Upgrading an already-paired fixture:** adding the second endpoint changes the device
> descriptor, and Zigbee2MQTT caches endpoints from the interview, so an already-paired fixture
> must be **re-interviewed** (Z2M frontend → device → Re-interview, or publish to
> `zigbee2mqtt/bridge/request/device/interview`) before the ring entity works. This has not been
> tested on hardware, so exactly what you'll see beforehand is unconfirmed either way -- expect
> the ring entity to be missing entirely, or present but unresponsive, until the re-interview is
> done. Newly paired fixtures just work.

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

These belong to the **Accent Ring** entity only — the Downlight has no effects, just on/off,
brightness and colour temperature.

| # | Name | Description |
|---|---|---|
| 1 | Warm Gradient | Warm→cool sweep rotating around the ring |
| 2 | Color Gradient | Multi-hue gradient rotating around ring |
| 3 | Breathing | Outer ring pulses in/out |
| 4 | Color Cycle | Full HSV hue rotation |
| 5 | Chase | Single lit segment travels around ring |
| 6 | Nightlight | Dim warm white outer ring only |

> **Changed in this version:** `static_white` and `static_color` are gone from the effect list.
> White is now the separate Downlight entity, and a solid ring colour is `effect: none` with a
> colour set. Automations naming either of the two removed effects need updating. Stored scenes
> are reseeded automatically — the NVS schema version bump discards the old indices rather than
> misreading them.

## Switch Control (Hub-Independent)

| Inovelli action | Behavior |
|---|---|
| 1× tap | On/Off |
| Hold up/down | Brightness |
| 2× tap up | Next effect — **via hub automation**, see below |
| 2× tap down | Previous effect — **via hub automation** |

On/off and dimming use direct Zigbee binding — **no hub required**, and they keep working if the
coordinator is down. With two fixture endpoints, the switch binds to both:

```
Bind from the Inovelli's endpoint 2 (the paddle) to BOTH fixture endpoints,
clusters genOnOff and genLevelCtrl:

  switch ep2 -> fixture ep1   (downlight)
  switch ep2 -> fixture ep2   (accent ring)
```

One tap down turns both sources off; one tap up brings both back at their own levels and colours.
This works because the Inovelli sends discrete `On`/`Off` rather than `Toggle` — verified on the
hardware. Under `Toggle`, two endpoints that had drifted into different states would diverge
further on every tap instead of converging. "Downlight only" is a Home Assistant action rather
than a switch action; remove the second binding if you would rather the ring ignored the switch.

> **Effect stepping is not hub-independent, and cannot be.** The Inovelli's multi-tap events are
> manufacturer-specific and go to the coordinator rather than to a bound light, so nothing the
> light does could make 2× tap work over pure binding. The automation reads the current
> `effect_select` and writes the next one. Not yet verified end to end (plan Task 6.4).

## Selecting an effect

Effect selection rides a **manufacturer-specific cluster (`0xFC00`)**, because nothing standard can
carry it: the Scenes cluster stores colour and level but knows nothing about an effect type, and
the Identify trigger-effects have no hook in the Arduino Zigbee library.

Install [`z2m/lumary-brain-revA.js`](z2m/lumary-brain-revA.js) into Z2M's `data/external_converters/`
and restart. Without the converter the light still works normally — the effects are simply
unreachable.

The converter exposes the effects as the **Accent Ring entity's native Home Assistant effect
list**, so they appear in the effect dropdown of the ring's light card itself rather than as a
separate entity:

```yaml
service: light.turn_on
target: {entity_id: light.lumary_kitchen_ring}
data: {effect: color_cycle}
```

That also means HA scene snapshots capture the running effect, and voice assistants can select one.
The current effect reads back as `state_attr('light.lumary_kitchen_ring', 'effect')`.

| Value | Meaning |
|---|---|
| `warm_gradient` … `nightlight` | One of the six built-in effects, in `EffectType` order |
| `none` | Not running an effect — showing a plain colour |

Setting a colour on the ring **exits the effect** and shows that colour instead, which reads back
as `none`. Selecting `none` does the same thing without changing the colour. Brightness continues
to scale whatever effect is running.

The selected effect persists across power cuts (NVS); `none` deliberately does not, so a power
cycle comes back to the stored effect.

> The Identify trigger-effects that Z2M's stock `effect` dropdown drives (`blink`, `breathe`,
> `okay`, …) are **switched off** in the converter, along with `power_on_behavior`. Both are on by
> default in `light()`, and this firmware implements neither — leaving them on would put controls in
> the light card that quietly do nothing. See `effect: false` / `powerOnBehavior: false` there.

> **Stepping through effects from the wall switch** is done by a hub automation, not by the light:
> read the current effect and set the next one. The Inovelli's multi-tap events go to the
> coordinator rather than to a bound light, so a hub is in the loop regardless.

Scene *storage* exists — 16 NVS slots, seeded with the defaults in `src/effect_params.h` — but
editing them is **not implemented**. There is no Add Scene support; `scene_store_save()` is only
called to seed defaults at first boot. Changing an effect's colour, speed or brightness
permanently is a future change.

## OTA Updates

> **Re-flashing a fixture that was already paired?** See "Upgrading an already-paired fixture"
> above — after this update it needs a Z2M **re-interview** before the ring entity works, or the
> upgrade will look broken even though it succeeded.

**Primary:** Zigbee OTA via Zigbee2MQTT.

Three things must all be true, and each one fails differently:

| Requirement | Symptom when missing |
|---|---|
| Firmware registers an OTA client | Device never asks; nothing in the Z2M log |
| Converter declares `ota: true` | **No `update` entity at all** in Home Assistant |
| Z2M can find the image via an index | Update entity exists; update fails `No image currently available` |

> **Z2M does not discover firmware by scanning a folder.** Copying a `.zigbee` file into `data/ota/`
> accomplishes nothing on its own — Z2M reads an *index* that describes the image, pointed at by
> `ota.zigbee_ota_override_index_location` in `configuration.yaml`. `scripts/gen-ota-index.py`
> generates one; see "Building an OTA image" below.

> **`ota` is a definition property, not a modern extend.** There is no `m.ota()` in
> zigbee-herdsman-converters, and using one makes Z2M reject the entire converter as invalid at load
> time. It goes on the definition object alongside `onEvent`.

**There is no fallback.** A BLE OTA path was planned — `PIN_BLE_OTA_BUTTON` is still defined in
`src/config.h` — but it was never implemented, and nothing references that pin. Do not count on it
for a fixture already in the ceiling: if Zigbee OTA cannot reach a light, the only recovery is
physical access to the USB port. Getting Zigbee OTA verified from an installed location is
consequently the gate on signing off the board (plan task 6.4), not a nice-to-have.

### Building an OTA image

The device registers an OTA client at startup and queries for an image once it
joins, then hourly. The coordinator only offers images numbered **above** the
running version, so the version block in `src/version.h` must be bumped and the
same `ZB_FW_VERSION` passed as `--file-version` below — if they disagree, the
update silently never appears.

> **The tool was renamed and its CLI changed.** Espressif's `tools/ota_image_tool.py`
> is gone; it is now `tools/image_builder_tool/image_builder_tool.py`, it takes short
> flags rather than the old long ones, and the firmware is supplied as a `--tag`
> sub-element rather than a positional argument. There is no `create` subcommand, and
> the output filename is generated for you rather than given. Verified 2026-08-18.

```bash
# Get the tool
mkdir -p build/ota && curl -sSL -o build/ota/image_builder_tool.py   https://raw.githubusercontent.com/espressif/esp-zigbee-sdk/main/tools/image_builder_tool/image_builder_tool.py

# Build firmware (bump the version block in src/version.h first)
pio run -e esp32h2

# Wrap as an OTA image. -m/-i come from src/config.h (ZB_MANUFACTURER_CODE,
# ZB_IMAGE_TYPE); -v must equal ZB_FW_VERSION. Tag 0x0000 is the upgrade image.
cd build/ota && python image_builder_tool.py   -m 0x1001   -i 0x0001   -v 0x02000000   -s 2   -g "LumaryZigbee"   --tag 0x0000 ../../.pio/build/esp32h2/firmware.bin
```

The output is named from the three identifiers rather than chosen — for the values above,
`1001-0001-02000000-ota-file.zigbee`.

Then generate the index Z2M actually reads. It takes the manufacturer code, image type, file version
and header string out of the image itself rather than from arguments, so the index cannot disagree
with the file it describes:

```bash
python scripts/gen-ota-index.py build/ota/1001-0001-02000000-ota-file.zigbee
```

Copy **both** the `.zigbee` image and the generated `index.json` into Z2M's `data/ota/` folder, then
point `configuration.yaml` at the index and restart Z2M:

```yaml
ota:
  zigbee_ota_override_index_location: ota/index.json
```

Both paths are relative to the **Z2M data directory** — the index's own location, and the `url`
inside it naming the image. Verified end to end 2026-08-18 against Z2M 2.13.0: no http(s) URL and no
absolute path is needed for a locally served image.

> **The `url` is relative to the data directory, not to the index that contains it.** With the
> layout above the index lives at `data/ota/index.json` and the image sits beside it, so the `url`
> still needs the `ota/` prefix — `ota/foo.zigbee`, not `foo.zigbee`. `gen-ota-index.py` adds it.
>
> Getting this wrong is worth recognising, because it does not look like a path problem.
> `ota_update/check` reads index *metadata* only, so it reports `update_available: true` and Home
> Assistant offers the update quite happily. Only `ota_update/update` opens the file, and it reports
> the failure as **`No image currently available`** — the same message you get with no index at all.
> The real cause appears only in Z2M's debug log:
>
> ```
> zh:controller:ota: Reading firmware image from 'foo.zigbee'
> ENOENT: no such file or directory, open '/config/zigbee2mqtt/foo.zigbee'
> ```

Z2M caches the last OTA check, so after installing an index it will keep reporting the device as up
to date until it re-checks — on the next device announce, hourly, or immediately if you ask:

```
topic:   zigbee2mqtt/bridge/request/device/ota_update/check
payload: {"id": "Overhead light test"}
```

For a one-off test you can skip the index entirely and name the file in the update request, which is
useful for confirming the image itself is good before troubleshooting configuration:

```
topic:   zigbee2mqtt/bridge/request/device/ota_update/update
payload: {"id": "Overhead light test", "url": "ota/1001-0001-02000000-ota-file.zigbee"}
```

`build/` is git-ignored, so neither the tool, the image, nor the index is committed.

## Build & Flash

> **Re-flashing a fixture that was already paired?** See "Upgrading an already-paired fixture"
> above — after this update it needs a Z2M **re-interview** before the ring entity works, or the
> upgrade will look broken even though it succeeded.

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
  effects.h/.cpp    — 6 effect implementations + lookup table
  scene_store.h/.cpp — NVS scene persistence
  zigbee_light.h/.cpp — Zigbee clusters for both endpoints, and the OTA client
  brightness.h      — CIE 1931 lightness curve, generated tables
  main.cpp          — Setup, loop, watchdog
test/
  test_brightness/   — Host unit tests for the perceptual curve
  test_identify/     — Host unit tests for the identify overlay
  test_light_state/  — Host unit tests for the Zigbee -> fixture translation
  test_pixel_encode/ — Host unit tests for pixel encoding + color math
  test_version/      — Host unit tests for the version block
z2m/
  lumary-brain-revA.js — Z2M external converter
  test/             — Converter tests (stubbed, `node z2m/test/converter.test.mjs`)
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

The Z2M converter has its own suite, covering the effect names, their wire indices and the
exposes Home Assistant discovers from them. It stubs the `zigbee-herdsman-converters` imports,
so it needs nothing installed but Node:

```bash
node z2m/test/converter.test.mjs
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

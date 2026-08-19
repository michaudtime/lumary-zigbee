# Lumary Brain Replacement Board — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **This is a hardware plan.** Software "write a failing test / make it pass" maps to hardware
> "state the expected value / measure or check it." Every task tags an **Owner**:
> **[USER]** = a physical action only Chad can do (measure, probe, click in KiCad GUI, order).
> **[CLAUDE]** = a design artifact Claude generates (calcs, connectivity tables, BOM, config).
> **[BOTH]** = Claude produces content, user verifies against the real part/library.

**Goal:** Produce a fabrication-ready KiCad project for a drop-in ESP32-H2-MINI-1 board that
replaces the stock Tuya controller in the Lumary 6" RGBAI light with zero rewiring.

**Architecture:** Single 4-layer PCB cloning the stock outline + both connectors. Soldered
ESP32-H2-MINI-1. Powers the module from a 4.7 V (or USB-5 V) rail via LDO; sinks/steers the
external driver's 36 V/380 mA constant-current white string on the low side (CW/WW returns);
level-shifts a single-wire data output to 4.7 V for the addressable ring. Schematic is authored
as complete connectivity/BOM tables that get entered in KiCad; layout/routing done in KiCad.

**Tech Stack:** KiCad 8, JLCPCB PCBA (LCSC parts), ESP32-H2-MINI-1, existing PlatformIO firmware.

**Spec:** `docs/superpowers/specs/2026-08-01-brain-replacement-board-design.md`

---

## File / artifact map

| Path | Responsibility |
|---|---|
| `docs/superpowers/research/phase0-measurements.md` | Filled-in reverse-engineering data (the Phase 0 gate) |
| `hardware/calcs.md` | Power, thermal, and current-budget calculations with the measured inputs |
| `hardware/bom.csv` | JLCPCB BOM: designator, value, LCSC part #, footprint |
| `hardware/schematic-nets.md` | Complete connectivity tables (every component pin → net) — the schematic source of truth |
| `hardware/lumary-brain.kicad_pro` + `.kicad_sch` + `.kicad_pcb` | The KiCad project (built from the tables + BOM) |
| `hardware/gerbers/` + `hardware/cpl.csv` | Fab outputs for JLCPCB upload |
| `src/config.h` | Firmware pin remap to the new board |

---

## Phase 0 — Reverse-engineering measurements (GATE)

Nothing in Phase 1+ is trustworthy until this is complete. All results go in one file.

### Task 0.0: Create the measurements file — [CLAUDE]

**Files:** Create `docs/superpowers/research/phase0-measurements.md`

- [ ] **Step 1: Create the results template** with a filled row for the already-known data and blank rows for the rest:

```markdown
# Phase 0 Measurements

## Driver rails (DONE)
- Driver: L-SD8E1, 120VAC/60Hz 0.2A, OUTPUT 35.8V 380mA max (constant current)
- White wire = GND
- Blue wire = 36.63 V  -> V+  (CC string rail)
- Red wire  = 4.7 V    -> 5V+ (logic/ring rail)

## P0.1 Connectors
- Power-in: pins=___  pitch=___mm  JST family=___  order (pin1..N)=___
- CN1:      pins=___  pitch=___mm  JST family=___  order (pin1..N)=___

## P0.3 CN1 pinout (confirm each)
- pin1=___ ... (label: 5V+/WW-/GND/DIM/CW-/+A/-A)  +A=___  -A=___

## P0.4 Outer ring
- pixel chip marking=___  count=___  supply=___V  data protocol=___

## P0.5 White string
- HT7308 sense resistor marking=___  -> I_set=___mA
- CW sub-string series count=___  Vf_total_CW=___V  -> headroom=36.63-Vf=___V
- WW sub-string series count=___  Vf_total_WW=___V  -> headroom=___V
- Max sink dissipation = headroom * 0.38A = ___W

## P0.6 Mechanical
- Board outline (LxW)=___  mounting holes (dia/pos)=___  max height=___mm
- Antenna keepout region relative to fixture opening=___
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/research/phase0-measurements.md
git commit -m "docs: phase 0 measurement template"
```

### Task 0.1: Measure both connectors — [USER]

**Files:** Modify `docs/superpowers/research/phase0-measurements.md` (P0.1)

- [ ] **Step 1:** With calipers, measure pin pitch on the power-in connector and `CN1`. Match pitch to JST family: 1.0 mm→SH, 1.25 mm→GH, 1.5 mm→ZH, 2.0 mm→PH. Record pin counts.
- [ ] **Step 2:** Verify: pitch is one of {1.0, 1.25, 1.5, 2.0} mm ±0.05. If it's something else, photograph the connector and flag — it may be a less common family and affects sourcing (risk R2).
- [ ] **Step 3:** Fill P0.1. Commit.

### Task 0.2: Trace CN1 pinout — [USER]

**Files:** Modify `phase0-measurements.md` (P0.3)

- [ ] **Step 1:** With the board **unpowered**, use a DMM in continuity mode. Map each `CN1` pin to its silkscreen label and to the LED-module harness. Confirm `+A`/`-A`: `+A` should have continuity to the ring's data-in pad; `-A` to ring ground/return.
- [ ] **Step 2:** Verify: exactly one pin reads as ring **data** (goes only to the first pixel's DIN, not to a power plane). If `+A` and `-A` both look like power (both hit the ring's V+/GND), the ring data pin is elsewhere — re-trace. Expected: `+A`=data, `-A`=GND/return.
- [ ] **Step 3:** Fill P0.3. Commit.

### Task 0.3: Identify the outer ring — [USER]

**Files:** Modify `phase0-measurements.md` (P0.4)

- [ ] **Step 1:** Read the marking on one outer-ring pixel under magnification (SK6812 / WS2812B / other). Count total pixels. Note the ring supply pad voltage.
- [ ] **Step 2:** Verify: firmware assumes **36 pixels, SK6812 (RGBW), ~5 V**. If chip ≠ SK6812 or count ≠ 36, note it — firmware timing/LED count must change (risk R5), but board design is unaffected.
- [ ] **Step 3:** Fill P0.4. Commit.

### Task 0.4: Characterize the white string — [USER]

**Files:** Modify `phase0-measurements.md` (P0.5)

- [ ] **Step 1:** Read the `HT7308` current-set (sense) resistor marking on the stock board (e.g. `R680`=0.68 Ω). Compute the per-leg current: `I_set = V_ref / R_sense` (HT7308 V_ref ≈ 0.2–0.25 V per datasheet — pull the datasheet to confirm). Sanity-check against the driver's 380 mA total.
- [ ] **Step 2:** Count LEDs in series per color on the strip (`8C18W` prefix likely encodes cool/warm counts — cross-check). Compute `Vf_total` ≈ series_count × ~3.0 V. Headroom = 36.63 − Vf_total. Dissipation = headroom × 0.38 A.
- [ ] **Step 3:** Verify (decision gate for Task 1.2): if dissipation-per-leg **≤ 0.5 W**, linear CC (clone HT7308) is fine. If **> 0.5 W**, flag for switching-sink topology (risk R1).
- [ ] **Step 4:** Fill P0.5. Commit.

### Task 0.5: Capture mechanical outline — [USER]

**Files:** Modify `phase0-measurements.md` (P0.6); add scan image to `docs/superpowers/research/`

- [ ] **Step 1:** Flatbed-scan the stock board at 600 dpi against a ruler (or photograph flat, straight-down, with a ruler in frame) for tracing the outline in KiCad later. Measure overall L×W, mounting-hole diameter/positions, and the tallest component height with calipers.
- [ ] **Step 2:** Verify: MINI-1 (~2.4 mm tall) + chosen JST connectors fit under the measured max-height envelope (risk R4). If not, flag — may need Option 2 (carrier) reconsideration or a right-angle connector.
- [ ] **Step 3:** Fill P0.6, add the scan. Commit.

**GATE:** Do not start Phase 1 until `phase0-measurements.md` has no blank `___` fields.

---

## Phase 1 — Calculations & part selection

### Task 1.1: Power-path calculations — [CLAUDE]

**Files:** Create `hardware/calcs.md`

- [ ] **Step 1: State expected values.** 3.3 V LDO load = ESP32-H2-MINI-1, design headroom 200 mA (peak radio TX + flash). Worst-case LDO dissipation from USB 5 V: `(5.0 − 3.3) × 0.20 = 0.34 W`; from 4.7 V rail: `(4.7 − 3.3) × 0.20 = 0.28 W`. Dropout requirement at 4.7 V in: LDO dropout must be **< 1.4 V at 200 mA** → rules out marginal parts.
- [ ] **Step 2: Write the calc** into `calcs.md` with the numbers above, plus ring current budget: 36 px × up-to-60 mA (SK6812 RGBW full white) = **2.16 A** worst case at 4.7 V — far above the driver's spare capacity, so firmware brightness cap is mandatory (record the safe average target once P0.4 gives the rail's real current limit).
- [ ] **Step 3: Verify** the LDO dissipation (≤0.34 W) is within a SOT-23-5 package's rating (~0.5–0.9 W with copper). PASS if ≤ 0.5 W. Commit.

### Task 1.2: White-driver topology decision — [CLAUDE, gated on P0.5]

**Files:** Modify `hardware/calcs.md`

- [ ] **Step 1:** Read P0.5 dissipation. Apply the rule from Task 0.4 Step 3: ≤0.5 W → **linear CC (clone HT7308)**; >0.5 W → **N-MOSFET + sense-resistor switching sink** (≥60 V FET, e.g. a 100 V logic-level part).
- [ ] **Step 2:** Record the chosen topology + the target per-leg current (= P0.5 `I_set`) and the resulting part list in `calcs.md`.
- [ ] **Step 3:** Verify the chosen low-side device Vds rating ≥ 60 V (CC compliance is 35.8 V; 60 V gives margin). PASS/FAIL. Commit.

### Task 1.3: Select all parts with LCSC numbers — [BOTH]

**Files:** Create `hardware/bom.csv`

- [ ] **Step 1: Draft the BOM** (Claude) with these blocks and candidate LCSC parts:
  - MCU: `ESP32-H2-MINI-1` (JLCPCB module)
  - 3.3 V LDO: low-dropout 3.3 V/≥300 mA (candidate `ME6211C33M5G` — 300 mA, 250 mV dropout; verify stock)
  - Ring data buffer: `74AHCT1G125` powered from 4.7 V (VIH ~2 V accepts 3.3 V, outputs 4.7 V) — matches README's level-shift note
  - White low-side ×2: HT7308-class **or** MOSFET+sense per Task 1.2
  - USB-C receptacle (16-pin) + ESD: `USBLC6-2SC6`
  - Reverse-polarity P-FET + TVS (`SMAJ5.0A`) on the 4.7 V input
  - 2× JST connectors matching P0.1 pitch/pin-count
  - BOOT + EN tact switches; passives (decoupling, series ~200 Ω on ring data, LDO caps)
- [ ] **Step 2: Verify** (user) each LCSC part number is **In Stock** and **Basic or Extended** on JLCPCB (Extended incurs a feeder fee — acceptable). Swap any out-of-stock part. Confirm the ESP32-H2 USB D+/D− GPIO assignment against the ESP32-H2 datasheet.
- [ ] **Step 3: Commit** `bom.csv`.

### Task 1.4: Confirm ESP32-H2 pin assignments — [BOTH]

**Files:** Modify `hardware/calcs.md` (pin-assignment table)

- [ ] **Step 1:** Assign module GPIOs to functions, mirroring current firmware where possible: `PIN_SK6812_DATA=GPIO11` (SPI2 MOSI), `PIN_CW_PWM=GPIO2`, `PIN_WW_PWM=GPIO3`, `PIN_BLE_OTA_BUTTON=GPIO9` (BOOT). Add USB D+/D− (native), EN.
- [ ] **Step 2: Verify** each chosen GPIO is (a) broken out on the MINI-1 module, (b) not a strapping pin conflict, (c) usable for its peripheral. Fix conflicts and note any firmware pin change needed for Phase 5.
- [ ] **Step 3:** Record the final pin table. Commit.

---

## Phase 2 — Schematic (connectivity source of truth)

### Task 2.1: Power block nets — [CLAUDE]

**Files:** Create `hardware/schematic-nets.md`

- [ ] **Step 1:** Write the power block as a connectivity table: input JST (`GND`, `+4V7`, `+36V`) → reverse-polarity P-FET → `+4V7_PROT` → LDO_IN; USB-C `VBUS` → Schottky → LDO_IN (diode-OR); LDO_OUT=`+3V3`; TVS `+4V7_PROT`→GND; decoupling caps on `+3V3` (10 µF + 100 nF) and `+4V7` (10 µF). List every pin→net.
- [ ] **Step 2: Verify** (ERC-by-hand): every net has ≥1 driver and ≥1 load; no output-to-output; `+36V` never touches `+3V3`/USB. PASS/FAIL. Commit.

### Task 2.2: MCU + USB block nets — [CLAUDE]

**Files:** Modify `hardware/schematic-nets.md`

- [ ] **Step 1:** Table for ESP32-H2-MINI-1: `3V3`, `GND` (all grounds), `EN` (10 kΩ pullup + 1 µF + EN button to GND), `IO9`/BOOT (button to GND, pullup), USB `D+`/`D−` → `USBLC6` → USB-C, USB `VBUS`/`GND`/`CC1`/`CC2` (5.1 kΩ each). Note: native USB-Serial-JTAG self-resets for download — **no external auto-reset transistors needed**.
- [ ] **Step 2: Verify** decoupling on module 3V3 (100 nF close + 10 µF bulk), CC resistors present, D+/D− go to the correct H2 GPIOs from Task 1.4. PASS/FAIL. Commit.

### Task 2.3: Ring data output nets — [CLAUDE]

**Files:** Modify `hardware/schematic-nets.md`

- [ ] **Step 1:** Table: `GPIO11` → `74AHCT1G125` input (Vcc=`+4V7`, OE tied active) → series ~200 Ω → `CN1.+A` (ring data). `CN1.5V+`=`+4V7`, `CN1.-A`=`GND`. Decoupling 100 nF on the buffer.
- [ ] **Step 2: Verify** buffer Vcc is 4.7 V (not 3V3), series resistor present, data net reaches `CN1` per P0.3. PASS/FAIL. Commit.

### Task 2.4: White low-side driver nets — [CLAUDE, gated on Task 1.2]

**Files:** Modify `hardware/schematic-nets.md`

- [ ] **Step 1:** Table for the chosen topology. Linear-CC variant: two HT7308-class sinks, each `EN/PWM` from `GPIO2`(CW)/`GPIO3`(WW), sense resistor per P0.5 `I_set`, sink input `CN1.CW-`/`CN1.WW-`. MOSFET variant: gate from GPIO via ~100 Ω, drain=`CN1.CW-`/`WW-`, source→sense resistor→GND, with the current controlled per Task 1.2. `CN1.V+`=`+36V`.
- [ ] **Step 2: Verify** low-side device Vds ≥ 60 V, gate/EN driven from the correct GPIO, sense resistor sets the P0.5 current, `+36V` only touches `CN1.V+` and the sink drains. PASS/FAIL. Commit.

### Task 2.5: Cross-block net review — [CLAUDE]

**Files:** Modify `hardware/schematic-nets.md`

- [ ] **Step 1:** Assemble a master net list (all nets, all connected pins). Check every component from `bom.csv` appears and every pin is assigned.
- [ ] **Step 2: Verify:** power domains isolated (`+36V` / `+4V7` / `+3V3` / USB-`VBUS`), single ground, no floating inputs, ring data present exactly once, both CN1 white returns present. PASS/FAIL. Commit.

### Task 2.6: Build the KiCad schematic — [USER, Claude-guided]

**Files:** Create `hardware/lumary-brain.kicad_pro`, `hardware/lumary-brain.kicad_sch`

- [ ] **Step 1:** New KiCad 8 project `hardware/lumary-brain`. Place every component from `bom.csv`, wire per `schematic-nets.md`. Assign the JLCPCB/LCSC part field to each symbol.
- [ ] **Step 2: Run ERC.** Expected: 0 errors (warnings for intentional no-connects acceptable, annotate them). Fix any net that disagrees with `schematic-nets.md`.
- [ ] **Step 3:** Assign footprints (module, SOT-23-5 LDO, SOT-23 gates, USB-C, JST per P0.1, 0402/0603 passives). Commit the project.

---

## Phase 3 — PCB layout

### Task 3.1: Board outline + connector placement — [USER, Claude-guided]

**Files:** Create `hardware/lumary-brain.kicad_pcb`

- [ ] **Step 1:** Import the P0.5 board scan as a drawing layer; trace the exact stock outline on Edge.Cuts; place mounting holes at measured positions. Set a 4-layer stackup (Sig/GND/PWR/Sig).
- [ ] **Step 2:** Place both JST connectors and USB-C at the stock harness-exit locations so the drop-in fits. Place the MINI-1 with its antenna over a board-edge **keepout** oriented toward the fixture opening (per P0.6).
- [ ] **Step 3: Verify** outline matches the scan within ~0.3 mm and connectors align to the harness exits. Commit.

### Task 3.2: Place remaining parts by block — [USER, Claude-guided]

**Files:** Modify `hardware/lumary-brain.kicad_pcb`

- [ ] **Step 1:** Group placement: LDO + input protection near the power-in JST; white sinks near `CN1` with thermal copper; ring buffer near `CN1`; USB ESD at the USB-C; decoupling hugging the module pins.
- [ ] **Step 2: Verify** no courtyard overlaps; antenna keepout clear of copper/parts. Commit.

### Task 3.3: Route — [USER, Claude-guided]

**Files:** Modify `hardware/lumary-brain.kicad_pcb`

- [ ] **Step 1:** Route power first: `+36V` and CN1 white-return traces sized for ≥0.5 A (≥0.4 mm) with clearance for 60 V; solid GND pour on layer 2; `+4V7`/`+3V3` on the power layer. Then signals (USB D+/D− as a length-matched ~90 Ω differential pair, ring data short).
- [ ] **Step 2: Run DRC** with JLCPCB design rules (min track/space 0.127 mm, min via 0.3/0.45 mm). Expected: 0 errors.
- [ ] **Step 3: Verify** antenna keepout has no copper on any layer; thermal relief on sink pads. Commit.

---

## Phase 4 — Fab outputs

### Task 4.1: Generate Gerbers + drill — [USER, Claude-guided]

**Files:** Create `hardware/gerbers/` (zip)

- [ ] **Step 1:** Plot Gerbers (all copper, mask, silk, Edge.Cuts) + Excellon drill per JLCPCB's KiCad 8 settings; zip them.
- [ ] **Step 2: Verify** in KiCad's Gerber viewer: outline closed, all layers present, no missing pads. Commit the zip.

### Task 4.2: Generate BOM + CPL for PCBA — [USER, Claude-guided]

**Files:** Create `hardware/cpl.csv`; finalize `hardware/bom.csv`

- [ ] **Step 1:** Export the JLCPCB-format BOM (Comment, Designator, Footprint, LCSC#) and CPL/placement (Designator, Mid-X, Mid-Y, Layer, Rotation).
- [ ] **Step 2: Verify** every placed part has an LCSC number and a rotation; upload the Gerber zip + BOM + CPL to JLCPCB's quote tool and confirm it parses with no unmatched parts. Commit.

**Checkpoint:** Order a small batch (e.g. 5) only after a human sanity-review of the JLCPCB preview.

---

## Phase 5 — Firmware updates

> Phase 0 (P0.4) found the outer ring is **62 pixels of 24-bit RGB** (RGBIC), not the
> 36 RGBW the firmware assumes. This phase covers the pin remap **and** that conversion.

### Task 5.1: Update config.h for the new board — [CLAUDE] ✅ DONE (8c98e91)

**Files:** Modify `src/config.h`

- [x] **Step 1:** Update the pin `#define`s to the Task 1.4 final assignments (only if any changed from the current GPIO11/2/3/9). Add a comment block noting target board = `lumary-brain rev A`.
- [x] **Step 2:** Change `SK6812_NUM_LEDS` from `36` to **`62`**, and recompute `SK6812_SPI_BUF_SIZE` for **24-bit** pixels: `62 × 24 × 3 SPI-bits/bit = 4464 bits = 558 bytes` + ~50 reset bytes → set to `608`.
- [x] **Step 3: Verify** by building: `pio run -e esp32h2`. Expected: compiles clean. Commit.

> **Deviations:** buffer set to **648** not 608 — reset padding raised to 90 bytes
> (~300 µs) so WS2812B-V5-class parts (>280 µs latch) are covered, since the strip's
> exact controller is unknown. Also lowered `PWM_FREQ_HZ` 20 kHz → **1 kHz** (the
> external CC driver can't track fast switching; verify in Task 6.3), and renamed
> `SK6812_*`/`CRGBW` → `RING_*`/`CRGB` since the strip is not an SK6812.

### Task 5.2: Convert the outer-ring buffer from RGBW to RGB — [CLAUDE] ✅ DONE (8c98e91)

**Files:** Modify `src/color.h`, `src/led_driver.cpp`, `src/effects.cpp`, `src/main.cpp`

- [x] **Step 1:** In `led_driver_show()`, change the per-pixel encoding from 32-bit GRBW to **24-bit** color order (default GRB; confirm on bring-up per P0.4). The inner CW/WW white stays on its separate LEDC PWM path — do not fold white into the pixel stream.
- [x] **Step 2:** Switch the ring buffer type used in `main.cpp`/`effects.cpp` from `CRGBW` to a 24-bit `CRGB` (drop the W byte). Keep effect math the same; only the output packing changes.
- [x] **Step 3: Verify** build `pio run -e esp32h2` compiles clean. Full colour-order + pixel-count correctness is verified on hardware in Task 6.2. Commit.

> **Additions:** the encoder was extracted to `src/pixel_encode.h` (no ESP-IDF headers)
> and covered by **11 host tests** in `test/test_pixel_encode/` — GRB order, reset
> padding, short-buffer safety, colour math. Colour order is switchable at build time
> with `-DPIXEL_WIRE_ORDER_GRB=0` if bring-up shows red/green swapped. Two effects that
> relied on the missing white die were reworked: `nightlight` now mixes warm white from
> the RGB dice, and `chase` gained a div-by-zero guard.
> Run tests with `scripts\run-native-tests.bat` (PlatformIO's native env needs gcc,
> which isn't installed on this machine; the script uses MSVC Build Tools instead).

---

## Phase 6 — Bring-up & validation (per board)

### Task 6.1: USB-only bring-up — [USER]

- [x] **Step 0:** Boards arrive blank — flash before expecting `boot ok`:
      `pio run -e esp32h2 -t upload --upload-port COMn`. A blank chip enumerates
      on the ROM's USB-Serial-JTAG but boot-loops on `invalid header: 0xffffffff`,
      and the resulting watchdog reset tears USB down, so the port appears and
      vanishes on a ~3.1 s cycle. That is expected on a virgin board, not a fault
      — the tell is the reset reason: `TG0_WDT_HPSYS`/`USB_UART_HPSYS` and **no**
      `BROWN_OUT_RST`.
- [x] **Step 1:** Power the assembled board from USB-C **only** (no fixture). Expected: enumerates as USB-CDC, `pio device monitor` shows `boot ok`.
      **PASS** (2026-08-13, board MAC `74:4d:bd:6b:57:5f`): `boot ok` + `LED driver init ok`,
      35 s with 0 resets and 0 USB drops.
      First attempt panic-looped 41× on `ZB_ESP_NVRAM: Failed to find zb_storage partition`
      (`zb_esp_nvram.c:84`) — `min_spiffs.csv` has no `zb_storage`/`zb_fct`. Fixed by
      switching `board_build.partitions` to `zigbee_zczr.csv`, which matches the
      `ZIGBEE_MODE_ZCZR` build and keeps app0/app1 for OTA. Latent since the 3.3.11
      framework upgrade, which was only verified to build.
- [x] **Step 2: Verify** the module joins a Zigbee network. PASS/FAIL. If FAIL, check 3V3 rail and EN/BOOT first.
      **PASS** (2026-08-13): steering failures stopped on permit-join; device then queried the
      coordinator (`0x0`) for an OTA image and correctly rejected the empty response. OTA client
      registration is therefore already proven — Task 6.4 only needs a real image to offer.

### Task 6.2: Ring rail + data — [USER]

- [ ] **Step 1:** Add 4.7 V to the input (bench supply, current-limited to ~0.5 A). Expected: outer ring lights and animates via the level-shifted data.
      > **The 0.5 A limit is marginal, but not for the reason first assumed.** `main.cpp`
      > clamps every effect to `MAX_BRIGHTNESS` = 24/255 — a rev A *hardware* ceiling set by
      > the 0.2 mm `+4V7` traces, not a bench guard — so the ring cannot be driven to the
      > multi-amp worst case at all. Expect **~0.55 A** for ring + module at the cap, which is
      > slightly *over* a 0.5 A limit: set the supply to ~0.8 A for this task, still well
      > under the ~1.5 A that would cook the trace.
      > Note also that a plain "on" lands in `EFFECT_STATIC_WHITE`, which blanks the ring and
      > drives the CW/WW string instead — so it lights nothing while 36 V is disconnected.
      > To exercise the ring, send a **saturated colour** (`sat` > `WHITE_SAT_THRESHOLD` = 32).
- [x] **Step 2: Verify** ring colors correct and no data glitches. **PASS** (2026-08-15) — red,
      green and blue each render correctly, full ring lit, no glitches. Ring current measured:
      0.122 A at the brightness cap, 0.040 A quiescent (see `hardware/calcs.md`, R3 now closed).
      Two defects found and fixed on 2026-08-15, in this order:
      1. **Wire order** — commanding red lit the ring green. The strip is **RGB**, not the
         WS2812/SK6812-family GRB the encoder defaulted to. Fixed with
         `-DPIXEL_WIRE_ORDER_GRB=0` in `platformio.ini`.
      2. **`T0H` pulse width** — random single pixels showed clean green or blue in ~11% of
         frames. Diagnosed with a two-channel Saleae capture probing `J2.6` and the ring's DIN
         together: **zero bit differences** between the two ends across 470,208 bits, and
         exactly 1488 bits in all 316 frames, which cleared the wiring, the encoder and the
         framing. That left pulse width: 3 SPI bits at 2.4 MHz gives `T0H` 417 ns, right on the
         SK6812 family's 450 ns ceiling, so `0` bits occasionally latched as `1`. Because the
         commanded red was only 23/255, one spurious bit in another channel visually swamped
         it — hence "clean green or blue" rather than a muddy shade. Fixed by moving to 4 SPI
         bits at 3.2 MHz (`T0H` 312 ns, `T1H` 625 ns, same 1.25 µs period). Ring is solid.
      > Two wrong turns worth remembering: the apparent "chasing" was random single-pixel
      > errors at 62 fps, not a travelling shift, and a leading-reset fix aimed at that wrong
      > model changed nothing (it was kept as correct practice, not as the fix).

### Task 6.3: 36 V white string — [USER]

- [ ] **Step 1:** Connect a **current-limited** supply to `+36V` (limit 0.4 A) with the white LED module attached. Sweep CW/WW PWM. Expected: smooth dimming + CCT shift between cool and warm.
      > **Use CC mode, not CV-with-a-limit.** P0.5 measured Vf_string ≈ 36 V against a 36.63 V
      > rail — **~0.6 V of headroom**. A CV supply at 36 V sits on that knife edge: slightly low
      > and the string barely conducts, slightly high and current runs away into the limit. Set
      > the supply to **CC 380 mA with ~37 V compliance**, which is what the L-SD8E1 actually does.
      > **Bench-supply ceiling:** many 30 V units cannot reach 36.63 V at all. If yours tops out
      > below ~37 V, 6.3 must be run from the real L-SD8E1 driver instead.
      > The MCU can run from USB and the ring can stay dark for this task, so a single supply on
      > the 36 V rail is sufficient — 4.7 V is only needed when testing the ring.
- [x] **Step 1** — **PASS** (2026-08-15). Run from the **real L-SD8E1**, not a bench supply: the
      available PSU tops out at 30 V and cannot reach the string's ~36 V forward voltage at all.
      The driver is the better article anyway, being genuinely CC. Setup was power box white →
      `J1` middle (GND), blue → `J1` bottom (+36 V), **red 4.7 V left off** so the ring rail
      stayed dead, module on USB. Both channels lit and both sweeps — CCT end to end, and
      brightness down to zero — were smooth, with no steps, flicker or dropouts.
      > **Watch the wire colours.** They mean different things on the two sides of the board:
      > on the input side red/white/blue are 4.7 V / GND / 36 V, on the output side they are
      > `5V+` / `CW-` / `DIM`. An hour was lost to "disconnect the red wire" being ambiguous.
- [x] **Step 2: Verify** measured leg current ≈ P0.5 `I_set`; low-side device temperature stable
      under 5 min sustained full-brightness. **PASS on thermals** (2026-08-15) — risk **R1 cleared**.
      Worst case deliberately chosen: 2702 K puts `cct` = 0, so `Q2` carries the full 380 mA at
      100% duty while `Q1` is off. Held 15 minutes, measured with a thermal camera:
      | | Temp | Rise over 23.3 °C (74 °F) ambient |
      |---|---|---|
      | `Q2` at 380 mA, 100% duty | 37 °C | 13.7 °C |
      | `U2` LDO — hottest point on the board | 40 °C | 16.7 °C |
      At the predicted ~0.23 W that implies θJA ≈ 60 °C/W, far better than the ~250 °C/W a bare
      SOT-23 sees in free air — the copper pour is doing the work. `U2` runs from USB here (5 V →
      3.3 V); installed it is fed from 4.7 V and dissipates less.
      **Not verified:** leg current was never metered in series, so P0.5's `I_set` is still taken
      on trust from the driver's CC rating.
      **Caveat:** open-air bench at 23 °C. The fixture is a sealed ceiling can, so the deltas hold
      but the absolute temperatures will be higher — re-check during Task 6.4.

### Effect engine — verified 2026-08-15

Not a numbered task, but the effects had never run on hardware and are recorded here.
Exercised via `BENCH_DEMO_MODE` (`main.cpp`), which cycles all eight from `kDefaultParams` at
5 s each with the effect name on serial; four full cycles, zero errors or resets.

**All eight render correctly**, including the two reworked blind after the outer strip turned
out to have no white die: `nightlight`, which now mixes warm white from the RGB dice, and
`chase`, which gained a div-by-zero guard. Both had been written against an assumption and
never tested until now.

This also runs the ring closer to full white than Home Assistant can reach — `warm_gradient`
drives all three dice at brightness 200 — with no flicker or sag from the driver's 4.7 V rail.

Still untested: **NVS scene storage**. The demo mode uses `kDefaultParams` directly and bypasses
`scene_store` entirely, so saving and recalling scenes over Zigbee has never been exercised.

### Task 6.4: In-fixture drop-in test — [USER] — STILL OPEN, unblocked 2026-08-18

- [ ] **Step 1:** Unplug the stock board, plug in the new board using the existing harnesses (no rewiring). Power the real driver. Expected: light works, both rings function.
- [ ] **Step 2: Verify** RF range / Zigbee binding to the Inovelli switch and a Zigbee OTA test succeed from the installed location. PASS/FAIL → sign off rev A.

> **This task was silently blocked until 2026-08-18, and the block was invisible from the bench.**
> `setup()` opened with an unbounded `while (!Serial) delay(10)`. The build sets
> `ARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` is the USB CDC and never becomes true unless a host
> enumerates it — meaning `setup()` never returned on a fixture running from mains alone. Everything
> below that line was dead: `led_driver_init()` never configured the white PWM GPIO, so the L-SD8E1
> saw no gating signal and drove the downlight full on, and `zigbee_light_init()` never started the
> radio. There is no USB host in a ceiling, so the fixture could not have passed this task at all.
>
> Fixed in `ab5dfe9` by bounding the wait to one second, and a standalone cold boot with USB
> unplugged is verified: downlight stays off, device joins.
>
> Every bench session before this one was USB-tethered, which is exactly why nobody caught it. The
> general lesson is worth carrying into this task: **a bench rig that differs from the deployed
> configuration in any powered respect hides precisely the faults that matter.** Sections 1–7 of the
> bench checklist all passed while this was live.

Two caveats carried from Task 6.3, both still open:

- [ ] **Re-check thermals in the sealed can.** 6.3's numbers (Q2 at 37 °C, U2 at 40 °C) were open-air
      bench at 23 °C ambient. The deltas should hold; the absolute temperatures will not.
- [ ] **Meter the leg current in series.** Never actually measured — P0.5's `I_set` is still taken on
      trust from the driver's CC rating.

Step 2's OTA leg also settles a design risk carried from the two-endpoint split: one OTA client is
registered on endpoint 1 while two endpoints now exist, and the Arduino library's OTA support was
written against single-endpoint examples.

> **The OTA path is now wired end to end and offering an update, verified 2026-08-18.** It had never
> been runnable before: the firmware registered a client and queried on every join, but the converter
> declared no `ota` property, so Z2M had nothing to answer with. Three separate things had to be true,
> and each failed differently — no `update` entity at all (converter), `No image currently available`
> (no index), and up-to-date-forever (Z2M caching the previous check). All three are documented in
> README "OTA Updates".
>
> **A full Zigbee OTA completed successfully at the bench, 2026-08-18.** 1.0.0 -> 2.0.0, 839,694
> bytes, `installed_version` now `33554432`, and both light entities came back correctly shaped
> after the forced post-update re-interview. This is the first OTA this project has ever performed.
>
> **Design risk 2 is settled for the transfer itself**: one OTA client registered on endpoint 1
> while two endpoints exist works, and the Arduino library's single-endpoint-example OTA support
> handles the two-endpoint arrangement. What remains for step 2 is the same transfer *from the
> installed location*, which is an RF question rather than a protocol one — watch for progress
> stalling at a fixed percentage rather than failing outright.
>
> Still do the in-ceiling OTA knowing there is no recovery path: the BLE fallback the README used to
> promise was never implemented, so a failed in-ceiling update means pulling the can.
>
> **BLE OTA is now a firm won't-do, not just an unimplemented gap -- decided 2026-08-19.** The two
> mitigations that exist instead: `esp_task_wdt_*` is now actually armed in `main.cpp` (the
> `WDT_TIMEOUT_MS` constant had sat unused since Task 1's scaffold), so a hang reboots the fixture
> rather than sitting there forever, and OTA itself is now proven end to end at the bench. Neither
> replaces physical USB access as the real recovery path; a fixture that can't be reached by Zigbee
> OTA still needs the can pulled. Boot-verified clean at the bench; not yet verified to actually
> recover from a real hang -- that would mean deliberately hanging an installed fixture, which hasn't
> been done.
>
> **Also fixed 2026-08-19, same hardening pass: `scene_store_load()` could return an uninitialized
> value.** Only the `type` key's NVS read was checked; `hue`/`sat`/`bri`/`spd` were read into locals
> with no status check. `scene_store_save()` writes its five keys one at a time before a single
> commit, so a power loss mid-save -- exactly the kind of event a mains-powered ceiling fixture sees
> -- could leave some keys present and others missing, and a later load would copy an indeterminate
> stack value into the returned `EffectParams`. Every read is now checked, with the same
> default-params fallback `type` already had. Neither this nor the watchdog fix is covered by the
> host test suite -- both live in the ESP-IDF-dependent layer the native tests can't reach.
>
> **Resolved, and it is a Home Assistant display issue only.** After the update HA's device registry
> kept reporting `sw_version: 1.0.0`, and a manual re-interview did not clear it -- which raised the
> real possibility that the image had transferred without being booted. Zigbee2MQTT's own interview
> data says otherwise:
>
> ```
> ieee: 0x744dbdfffe6b575f   swBuildId: 2.0.0   dateCode: 20260818
> ```
>
> The device is running 2.0.0. Z2M holds the correct values; only HA's device registry is stale,
> because it did not apply the republished discovery config. An MQTT integration reload or a Z2M
> restart clears it.
>
> **The date code is what settled this**, and it is worth keeping the habit: the previous image
> carried `20260817` and this one `20260818`, so the two builds are distinguishable from the air
> even though they are otherwise functionally identical -- the OTA changed only the version block.
> Without a moving date code there would have been no way to tell a booted new image from an unbooted
> one. `src/version.h` bumps both together, as one unit, which is exactly why that worked.
>
> Note also that `installed_version` in HA is not independent evidence: it reflects what Z2M recorded
> installing, taken from the image metadata, not a fresh read from the device. `swBuildId` and
> `dateCode` in the herdsman database are the real reads.
>
> **Also observed:** a `select.overhead_light_test_effect_ring` entity appeared alongside the
> effect's presence in both lights' `effect_list`. Z2M creates a `select` for the enum expose *and*
> unions it into the light entities. Harmless duplication, and it means the effect is reachable three
> ways. Worth folding into the item 9 effect-dropdown wart rather than treating separately.

Everything else in the bench checklist (`2026-08-18-bench-verification.md`) passed on 2026-08-18 —
sections 1–9 — so this task is the only remaining gate on signing off rev A.

---

## Self-review notes

- **Spec coverage:** §3 measured facts → Phase 0; §4.1 power → 1.1/2.1; §4.2 white → 0.4/1.2/2.4/6.3; §4.3 ring → 0.3/2.3/6.2; §4.4 MCU/USB → 2.2/6.1; §4.5 field control → firmware (tracked, see below); §5 Phase 0 → Phase 0; §6 BOM → 1.3; §7 firmware → Phase 5; §8 validation → Phase 6; §9 workflow → Phases 2–4.
- **Won't do, decided 2026-08-19 (was: deferred):** the firmware power-cycle reset / BLE-OTA-trigger rework (spec §4.5). Zigbee OTA is proven end to end at the bench and `esp_task_wdt_*` is now actually armed, so a hang reboots rather than hanging forever; BLE OTA would only help the narrower case of Zigbee OTA itself being unreachable, and isn't being built for that. See Task 6.4's note.
- **Dependency variables** (`I_set`, `Vf_total`, headroom, connector pitch, pixel count) are all defined by explicit Phase 0 tasks and consumed by name downstream.

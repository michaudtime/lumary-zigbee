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

## Phase 5 — Firmware pin remap

### Task 5.1: Update config.h for the new board — [CLAUDE]

**Files:** Modify `src/config.h`

- [ ] **Step 1:** Update the pin `#define`s to the Task 1.4 final assignments (only if any changed from the current GPIO11/2/3/9). Add a comment block noting the target board = `lumary-brain rev A`.
- [ ] **Step 2: Verify** by building: `pio run -e esp32h2`. Expected: compiles clean.
- [ ] **Step 3: Commit** `src/config.h`.

---

## Phase 6 — Bring-up & validation (per board)

### Task 6.1: USB-only bring-up — [USER]

- [ ] **Step 1:** Power the assembled board from USB-C **only** (no fixture). Expected: enumerates as USB-CDC, `pio device monitor` shows `boot ok`.
- [ ] **Step 2: Verify** the module joins a Zigbee network. PASS/FAIL. If FAIL, check 3V3 rail and EN/BOOT first.

### Task 6.2: Ring rail + data — [USER]

- [ ] **Step 1:** Add 4.7 V to the input (bench supply, current-limited to ~0.5 A). Expected: outer ring lights and animates via the level-shifted data.
- [ ] **Step 2: Verify** ring colors correct and no data glitches. PASS/FAIL.

### Task 6.3: 36 V white string — [USER]

- [ ] **Step 1:** Connect a **current-limited** supply to `+36V` (limit 0.4 A) with the white LED module attached. Sweep CW/WW PWM. Expected: smooth dimming + CCT shift between cool and warm.
- [ ] **Step 2: Verify** measured leg current ≈ P0.5 `I_set`; low-side device temperature stable under 5 min sustained full-brightness. PASS/FAIL (thermal risk R1 gate).

### Task 6.4: In-fixture drop-in test — [USER]

- [ ] **Step 1:** Unplug the stock board, plug in the new board using the existing harnesses (no rewiring). Power the real driver. Expected: light works, both rings function.
- [ ] **Step 2: Verify** RF range / Zigbee binding to the Inovelli switch and a Zigbee OTA test succeed from the installed location. PASS/FAIL → sign off rev A.

---

## Self-review notes

- **Spec coverage:** §3 measured facts → Phase 0; §4.1 power → 1.1/2.1; §4.2 white → 0.4/1.2/2.4/6.3; §4.3 ring → 0.3/2.3/6.2; §4.4 MCU/USB → 2.2/6.1; §4.5 field control → firmware (tracked, see below); §5 Phase 0 → Phase 0; §6 BOM → 1.3; §7 firmware → Phase 5; §8 validation → Phase 6; §9 workflow → Phases 2–4.
- **Deferred (not in this plan):** the firmware power-cycle reset / BLE-OTA-trigger rework (spec §4.5) is a separate firmware effort — flag for its own brainstorm/plan after the board is proven.
- **Dependency variables** (`I_set`, `Vf_total`, headroom, connector pitch, pixel count) are all defined by explicit Phase 0 tasks and consumed by name downstream.

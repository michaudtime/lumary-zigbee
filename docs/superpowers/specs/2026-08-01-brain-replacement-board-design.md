# Lumary "Brain" Replacement Board — Design Spec

**Date:** 2026-08-01
**Status:** Design approved — pending Phase 0 measurements before schematic
**Goal:** A custom PCB that is a true drop-in replacement for the stock Tuya controller
(`KOK-AH-A172C REV:A`) inside the Lumary 6" RGBAI recessed light, running the existing
`lumary-zigbee` ESP32-H2 firmware. One board design, deployed across all identical fixtures.

---

## 1. Objective & success criteria

Replace the stock Tuya WiFi "brain" in every identical Lumary 6" fixture with a custom
ESP32-H2 Zigbee board that **plugs into the existing harnesses with zero rewiring**.

Done means:
- Same board outline, mounting, and both connectors (power-in + `CN1`) as the stock board,
  so it physically swaps in.
- Drives the existing loads correctly: tunable-white inner ring (current-steered CCT) +
  addressable RGB outer "gradient" ring.
- Runs the current firmware with only a pin-map change.
- Manufacturable as a fully-assembled board via JLCPCB PCBA.
- Deliverable: a complete KiCad project → Gerbers + BOM + CPL ready for JLCPCB.

**Non-goals (YAGNI):** universal/multi-fixture controller, bare-chip RF design, on-board AC/DC
conversion (the external `L-SD8E1` driver stays), field-accessible buttons.

---

## 2. Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| Product type | True drop-in for Lumary 6" lights | Many identical fixtures; zero-rewire swap |
| LED load | Mixed: addressable outer ring + analog tunable white inner ring | Measured/confirmed |
| Manufacturing | JLCPCB PCBA (fully assembled) | Consistent units, fine-pitch + certified module usable |
| Architecture | **Option 1** — integrated board with soldered **ESP32-H2-MINI-1** | Best cost/reliability at qty; RF de-risked by pre-certified module; firmware ports easily |
| Programming | USB-C onboard (native USB-CDC) + auto-reset | Keeps current dev workflow; convenient bench iteration |
| Final deliverable | Full KiCad project → JLCPCB | (Physical routing likely finished/verified by a human in KiCad) |

---

## 3. Measured facts (Phase 0 partial — already established)

### External driver `L-SD8E1`
- Input: 120 VAC / 60 Hz / 0.2 A, PF > 0.9
- **Output: 35.8 V, 380 mA (max) — constant-current**, ~13.6 W
- Three output wires (map onto the stock board's power-in connector):

| Wire | Measured | Stock pin | Meaning |
|---|---|---|---|
| White | GND | `GND` | Ground |
| **Blue** | **36.63 V** | `V+` | Constant-current LED-string rail (380 mA to 35.8 V compliance) |
| **Red** | **4.7 V** | `5V+` | Low-power logic + addressable-ring rail |

### Inner white ring — strip `8C18W-12C4B-2835X2-588×7.5mm V1.0`
(The `8C18W` prefix likely encodes the cool/warm LED counts — cross-check against P0.5 series count.)
Tunable-white **current-steering** design, common anode, two low-side cathode returns:

| Strip pad | Role | Maps to `CN1` | Harness wire |
|---|---|---|---|
| `LED+` | Common anode (from `V+`, 36 V) | `V+` | black |
| `65K-` | 6500 K cool-white cathode return | `CW-` | white |
| `27K-` | 2700 K warm-white cathode return | `WW-` | yellow |

The board **does not source** the white current — the driver does. The board **sinks/steers**
the 380 mA on the low side: PWM total on-time → brightness; CW:WW ratio → color temperature.
Likely ~12 white LEDs in series per color (the `12C`), putting string voltage close to 36 V →
near-zero headroom across the low-side regulators → very low heat.

### Outer ring
Addressable RGB "gradient auxiliary" ring on `CN1` `+A`/`-A`, powered from the 4.7 V rail.
Firmware targets SK6812 (36-LED). Exact chip/pixel-count/protocol **not yet confirmed**
(`digital.csv` capture was noise/mis-triggered — unusable). Does not affect board architecture.

---

## 4. Architecture

Single 4-layer PCB, stock outline + both connectors. Brain = soldered ESP32-H2-MINI-1.
Everything downstream reproduces the stock board's behaviour for the LED module.

```
  Driver harness (in)                Replacement board                       LED module (CN1, out)
 ┌──────────────────┐   ┌───────────────────────────────────────────┐   ┌────────────────────────┐
 │ V+  = 36.63V CC ─┼─► │  V+ ──────────────────────────────────────┼─► │ LED+  (white anode)    │
 │ 5V+ = 4.7V      ─┼─► │  4.7V ─┬─[diode-OR]─►[3.3V LDO]─► 3.3V      │   │                        │
 │ GND             ─┼─► │        │              │  ┌────────────────┐│   │                        │
 └──────────────────┘   │  USB 5V┘              └─►│ ESP32-H2-MINI-1││   │                        │
                        │  (bench flash)           └─┬───┬───────┬──┘│   │                        │
                        │              GPIO(CW-PWM)  │   │(WW-PWM)│data│   │                        │
                        │                            ▼   ▼        ▼   │   │ CW-(65K) WW-(27K)      │
                        │              [≥60V CC sink][≥60V CC sink][3.3│   │ +A(data) -A(gnd/ret)   │
                        │                    │           │      →4.7V ├─► │ 4.7V rail for ring     │
                        │                    └───────────┴───buffer]─┼─► │                        │
                        │  USB-C + ESD + auto-reset, BOOT/EN btns     │   │                        │
                        └───────────────────────────────────────────┘   └────────────────────────┘
```

### 4.1 Power stage
- **3.3 V (module):** LDO from a **diode-OR of the 4.7 V rail and USB-VBUS** → boots/flashes on
  USB alone at the bench, runs off 4.7 V installed. 36 V section stays fully isolated from USB.
- **Ring rail:** the 4.7 V rail feeds the outer ring directly (not through the LDO). Current
  budget is limited (driver has only a few spare watts after 13.6 W of white) — firmware
  brightness cap protects it.
- **Input protection:** reverse-polarity (series P-FET or Schottky) + TVS + bulk cap on the
  low-voltage input. Respect creepage/clearance around the 36 V net.

### 4.2 Inner white output (tunable CCT)
- Two **low-side constant-current sinks** on `CW-`/`WW-`, PWM-dimmed from the module's LEDC
  channels. Brightness = total on-time; CCT = cool:warm ratio.
- **Default:** clone the stock **linear CC** (`HT7308`-class) at the measured setpoint (faithful
  behaviour). **Alternative** (only if thermals demand): MOSFET + sense-resistor switching sink.
- **Ratings:** low-side devices rated **≥60 V** (CC source swings to 35.8 V compliance during
  PWM off-time). Thermal copper on sinks (small, headroom is near-zero).

### 4.3 Outer ring output (addressable)
- Module data GPIO (firmware's SPI2-MOSI NZR encoding) → **3.3 V→4.7 V level buffer**
  (`SN74LVC1T45` or `74AHCT1G125`) → `CN1` data pin, series damping resistor ~150–330 Ω.
- Final `+A`/`-A` mapping confirmed in Phase 0 (P0.3).

### 4.4 MCU / RF / programming
- **ESP32-H2-MINI-1** soldered; PCB antenna at board edge with copper keepout, oriented toward
  the fixture opening (away from metal can). Same location the stock 2.4 GHz module used.
- **USB-C** → native USB (D+/D−), ESD protection, DTR/RTS **auto-reset** (EN + IO9).
- `BOOT`(IO9) + `EN` buttons for bench recovery.

### 4.5 Field control (buttons sealed once installed)
- **Zigbee OTA** = primary update path (works installed).
- **Reset / re-pair** = firmware **power-cycle pattern** (flip switch N times); no reachable
  button needed.
- ⚠️ BLE-OTA-on-BOOT-hold becomes unusable once installed → re-trigger via power-cycle pattern
  or drop it. (Firmware decision, tracked separately.)

---

## 5. Phase 0 — remaining reverse-engineering (gate before schematic)

| # | Measure | How | Status |
|---|---|---|---|
| P0.1 | Power-in + `CN1` connector pin count, **pitch**, part family (JST SH/GH/ZH/PH) | Calipers + continuity | TODO |
| P0.2 | Input rail voltages/polarity | DMM | **DONE** — 36.63 V / 4.7 V / GND |
| P0.3 | `CN1` full pin order; confirm `+A`/`-A` = ring data/return | Continuity trace | TODO |
| P0.4 | Outer ring: chip type, pixel count, protocol | Read pixel marking / clean re-capture | TODO (`digital.csv` unusable) |
| P0.5 | White sub-string series count + `HT7308` sense-resistor value → CC setpoint & headroom/heat | Read markings + DMM | TODO |
| P0.6 | Board outline, mounting holes/standoffs, **max component height**, antenna keepout region | Calipers / flatbed scan | TODO |

---

## 6. BOM (JLCPCB-stockable; verify LCSC stock at finalize)

ESP32-H2-MINI-1 · 3.3 V LDO · diode-OR Schottkys · 3.3→4.7 V data buffer · 2× ≥60 V low-side
CC sink stage (`HT7308`-class or MOSFET+sense) · USB-C + ESD array · reverse-polarity FET +
TVS · 2× JST connectors (matched in P0.1) · BOOT/EN buttons · passives.
Estimated **~$5–8 / assembled board** small qty, less at volume.

---

## 7. Firmware impact (small)

- Remap pins in `src/config.h` to the new layout.
- Level-shift target is **4.7 V**, not 5 V.
- Add power-cycle reset / OTA-entry trigger; reconcile BLE-OTA fallback.

---

## 8. Validation ladder

1. **USB-only bring-up:** module boots, USB-CDC serial, Zigbee joins.
2. **+4.7 V rail + outer ring:** ring lights, level-shift verified.
3. **+36 V CC string on a current-limited bench supply:** CW/WW steering, CCT sweep, dimming,
   thermals under sustained load.
4. **In-fixture swap:** true drop-in fit, RF range, Zigbee binding to Inovelli switch, OTA.

---

## 9. Deliverable workflow (→ full KiCad → JLCPCB)

Schematic → assign LCSC parts + footprints → layout (scaffold placement + critical routing;
finish/verify routing + DRC in KiCad) → Gerbers + BOM + CPL → JLCPCB PCBA.

---

## 10. Open questions / risks

- **R1 — White-driver thermals:** depends on P0.5 (series count → headroom). Near-zero headroom
  expected, but confirm before committing linear CC vs switching sink.
- **R2 — Connector sourcing:** exact JST family/pitch must match (P0.1) or the drop-in claim
  fails. Confirm JLCPCB stocks the matching part or plan an alternate.
- **R3 — 4.7 V ring rail current budget:** total ring current at full white may exceed the
  driver's spare capacity; keep the firmware brightness cap and quantify in P0.4.
- **R4 — Board depth vs can:** confirm MINI-1 + connectors fit the stock height envelope (P0.6).
- **R5 — Outer-ring protocol unconfirmed:** re-capture cleanly or read the pixel marking before
  finalizing firmware timing.

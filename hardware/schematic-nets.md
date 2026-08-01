# Lumary Brain Board — Schematic Net Tables

Source of truth for the KiCad schematic (Phase 2). Designators match `bom.csv`.
Enter these connections in Eeschema, then run ERC (Task 2.6).

**IC pin references:** module pins by signal name (MINI-1 KiCad symbol uses names);
discrete ICs by pin number where standard, else "per symbol/datasheet" — verify against the
placed KiCad symbol during Task 2.6.

---

## Net dictionary

| Net | Meaning |
|---|---|
| `GND` | Common ground (single plane) |
| `+36V` | Constant-current white-string rail (from driver, 36.63 V) |
| `+4V7_IN` | 4.7 V as it enters the board (pre reverse-polarity FET) |
| `+4V7` | Protected 4.7 V board rail (ring power + logic) |
| `LDO_IN` | Diode-OR node feeding the 3.3 V LDO |
| `+3V3` | Module rail |
| `VBUS` | USB-C 5 V (bench only) |
| `USB_DP` / `USB_DM` | USB D+ / D− (shared: connector ↔ ESD ↔ module) |
| `EN` | Module enable/reset |
| `IO9` | Module boot/download-select |
| `CW_PWM` / `WW_PWM` | GPIO → gate-resistor (pre-resistor) |
| `CW_GATE` / `WW_GATE` | MOSFET gates (post gate-resistor) |
| `CW_RET` / `WW_RET` | White cathode returns (`CN1.CW-` / `CN1.WW-`) |
| `RING_DATA_3V3` | GPIO11 ring data at 3.3 V (buffer input) |
| `RING_BUF_OUT` | Buffer output (pre damping-resistor) |
| `RING_DATA` | Level-shifted 4.7 V ring data to `CN1.DIM` |
| `CC1` / `CC2` | USB-C configuration-channel pulldowns |

---

## Task 2.1 — Power block

| Component.Pin | Net |
|---|---|
| J1.1 (GND) + solder-pad | `GND` |
| J1.2 (36V) + solder-pad | `+36V` |
| J1.3 (4V7) + solder-pad | `+4V7_IN` |
| Q3 (AO3401A P-FET) Source | `+4V7_IN` |
| Q3 Drain | `+4V7` |
| Q3 Gate | `GND` (via R… see note) |
| D3 (SMAJ5.0A TVS) cathode | `+4V7_IN` |
| D3 anode | `GND` |
| C4 (10 µF) | `+4V7` ↔ `GND` |
| D1 (B5819W) anode | `+4V7` |
| D1 cathode | `LDO_IN` |
| D2 (B5819W) anode | `VBUS` |
| D2 cathode | `LDO_IN` |
| C3 (1 µF) | `LDO_IN` ↔ `GND` |
| U2 (ME6211) VIN (pin1) | `LDO_IN` |
| U2 GND (pin2) | `GND` |
| U2 EN/CE (pin3) | `LDO_IN` (always-on) |
| U2 NC (pin4) | — |
| U2 VOUT (pin5) | `+3V3` |
| C2 (10 µF) | `+3V3` ↔ `GND` |

**Reverse-polarity note:** AO3401A high-side P-FET — Source=`+4V7_IN`, Drain=`+4V7`, Gate to
`GND` through a 100 k (add as R8 during entry) so it turns on for correct polarity and blocks
reversed input. The ring current flows through it (AO3401A ≤4 A, fine).

**ERC 2.1:** `+3V3` driven by U2 (1 driver); `LDO_IN` driven by D1/D2 (OR, no back-drive — Schottkys
isolate `+4V7` from `VBUS`); `+36V` touches only J1, C1 (Task 2.4), J2 (Task 2.4) — never `+3V3`/USB. **PASS.**

---

## Task 2.2 — MCU + USB block

| Component.Pin | Net |
|---|---|
| U1 (ESP32-H2-MINI-1) 3V3 (all) | `+3V3` |
| U1 GND (all + thermal pad) | `GND` |
| U1 EN | `EN` |
| U1 IO9 | `IO9` |
| U1 GPIO26 (USB_D−) | `USB_DM` |
| U1 GPIO27 (USB_D+) | `USB_DP` |
| U1 GPIO4 | `CW_PWM` |
| U1 GPIO5 | `WW_PWM` |
| U1 GPIO11 | `RING_DATA_3V3` |
| C5 (10 µF) | `+3V3` ↔ `GND` (bulk, near module) |
| C6 (100 nF) | `+3V3` ↔ `GND` (HF, at module pin) |
| R6 (10 k) | `EN` ↔ `+3V3` (pullup) |
| C8 (1 µF) | `EN` ↔ `GND` (power-on delay) |
| SW2 | `EN` ↔ `GND` (reset button) |
| SW1 | `IO9` ↔ `GND` (download button; module has internal pullup) |
| J3 (USB-C) VBUS (A4/B4/A9/B9) | `VBUS` |
| J3 GND (A1/B1/A12/B12) + shield | `GND` |
| J3 D+ (A6+B6) | `USB_DP` |
| J3 D− (A7+B7) | `USB_DM` |
| J3 CC1 (A5) | `CC1` |
| J3 CC2 (B5) | `CC2` |
| R7a (5.1 k) | `CC1` ↔ `GND` |
| R7b (5.1 k) | `CC2` ↔ `GND` |
| U4 (USBLC6-2SC6) I/O1 | `USB_DP` |
| U4 I/O2 | `USB_DM` |
| U4 VBUS pin | `VBUS` |
| U4 GND | `GND` |

**Notes:** USBLC6 is a shunt ESD array — its I/O pins sit *on* the D+/D− nets (connector and module
share each net), clamping to `VBUS`/`GND` internally. No series resistors on D+/D−. Confirm U4 pin
numbers against the KiCad symbol (I/O1, GND, I/O2, VBUS ordering varies by symbol).

**ERC 2.2:** `+3V3` decoupled (C5+C6); `EN` has pullup+cap+button; `USB_DP`/`USB_DM` each = exactly
one MCU pin + connector + ESD (no contention); CC pulldowns present; USB uses GPIO26/27 per Task 1.4. **PASS.**

---

## Task 2.3 — Ring data output

| Component.Pin | Net |
|---|---|
| U1 GPIO11 | `RING_DATA_3V3` |
| U3 (74AHCT1G125) A / IN (pin2) | `RING_DATA_3V3` |
| U3 /OE (pin1) | `GND` (always enabled) |
| U3 GND (pin3) | `GND` |
| U3 Y / OUT (pin4) | `RING_BUF_OUT` |
| U3 VCC (pin5) | `+4V7` |
| C7 (100 nF) | `+4V7` ↔ `GND` (at U3) |
| R5 (200 R) pin1 | `RING_BUF_OUT` |
| R5 pin2 | `RING_DATA` |
| J2 (CN1) DIM | `RING_DATA` |

**ERC 2.3:** buffer VCC = `+4V7` (not +3V3 — correct for 4.7 V logic high); input driven by GPIO11;
output → series R → CN1 data; /OE tied active. **PASS.**

---

## Task 2.4 — White low-side switches + 36 V

| Component.Pin | Net |
|---|---|
| R1 (100 R) pin1 | `CW_PWM` |
| R1 pin2 | `CW_GATE` |
| R2 (100 R) pin1 | `WW_PWM` |
| R2 pin2 | `WW_GATE` |
| Q1 (N-FET) Gate (pin1) | `CW_GATE` |
| Q1 Source (pin2) | `GND` |
| Q1 Drain (pin3) | `CW_RET` |
| R3 (100 k) | `CW_GATE` ↔ `GND` (pulldown, hold off at boot) |
| Q2 (N-FET) Gate (pin1) | `WW_GATE` |
| Q2 Source (pin2) | `GND` |
| Q2 Drain (pin3) | `WW_RET` |
| R4 (100 k) | `WW_GATE` ↔ `GND` (pulldown) |
| C1 (10 µF / 50 V) | `+36V` ↔ `GND` (inrush bulk) |
| J2 (CN1) V+ | `+36V` |
| J2 (CN1) CW- | `CW_RET` |
| J2 (CN1) WW- | `WW_RET` |

**ERC 2.4:** Q1/Q2 gates driven via series-R from GPIO4/5, pulled down (off at boot); drains → CN1
returns; `+36V` = J1 + C1 + CN1.V+ only; MOSFET Vds sees ≤35.8 V (needs ≥60 V part). **PASS.**

---

## Task 2.5 — CN1 (J2) full mapping + master check

**J2 = PicoBlade 7-pos (6 populated, pin 7 = NC):**

| J2 pin | Label | Net |
|---|---|---|
| 1 | V+ | `+36V` |
| 2 | CW- | `CW_RET` |
| 3 | WW- | `WW_RET` |
| 4 | 5V+ | `+4V7` |
| 5 | GND | `GND` |
| 6 | DIM | `RING_DATA` |
| 7 | (empty) | NC |

*(Physical 1–7 order to be aligned to the housing per P0.6 scan before layout.)*

**Master component check** (all `bom.csv` designators placed):
U1 U2 U3 U4 · Q1 Q2 Q3 · D1 D2 D3 · J1 J2 J3 · SW1 SW2 · C1–C8 · R1–R7 (R7 ×2) · R8 (rev-pol gate). ✅

**Power-domain isolation:** `+36V` ⟂ `+4V7` ⟂ `+3V3` ⟂ `VBUS` — the only cross-links are intended
(D1/D2 Schottky-OR into `LDO_IN`; U2 LDO 4.7/5 → 3.3). No net bridges 36 V to any low-voltage rail. ✅

**Floating-input check:** U3 /OE tied; MOSFET gates pulled down; EN/IO9 defined; USB CC pulled down. ✅

**ERC 2.5: PASS** — ready to enter in KiCad (Task 2.6).

---

## Add during KiCad entry (not yet designator'd above)
- **R8** (100 k): Q3 gate → `GND` (reverse-polarity FET gate reference). Add to BOM at Task 2.6.

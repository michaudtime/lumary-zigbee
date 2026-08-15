# Phase 0 Measurements

Fill every `___`. When there are no blanks left, Phase 1 (Claude design work) can start.
Spec: `docs/superpowers/specs/2026-08-01-brain-replacement-board-design.md`
Plan: `docs/superpowers/plans/2026-08-01-brain-replacement-board.md`

---

## Driver rails (DONE)
- Driver: **L-SD8E1**, 120 VAC / 60 Hz / 0.2 A, OUTPUT **35.8 V 380 mA max (constant current)**
- White wire = **GND**
- Blue wire  = **36.63 V** → `V+`  (CC white-string rail)
- Red wire   = **4.7 V**   → `5V+` (logic / addressable-ring rail)

---

## P0.1 — Connectors  (Task 0.1)
Measure pitch with calipers → JST family: 1.0 mm=SH, 1.25 mm=GH, 1.5 mm=ZH, 2.0 mm=PH.

- Power-in:  **soldered on stock board (no existing connector).** New board = **3-pin JST-PH 2.0 mm (polarized) + parallel solder pads** (DECIDED — both options on the footprint; user has PH stock). Wires: GND / 36 V / 4.7 V. Pin order on our connector is our choice → keep GND on an end pin, key so 36 V can't swap with 4.7 V.
- CN1:       pins = **7 (6 populated, 1 empty)**   pitch = **1.25 mm**   JST family = **Molex PicoBlade** (friction-only fit rules out JST-GH's latch)   physical pin order (1..7) = `___ (TODO from scan)`

---

## P0.3 — CN1 pinout  (Task 0.2)  — DONE (physical 1..7 order still TODO)
7-position PicoBlade, 6 wires populated, split 3 (inner white) + 3 (outer addressable ring):

| Label | Wire | Role | Board net |
|---|---|---|---|
| `V+`  | Black  | Inner white-string anode (36 V, → strip `LED+`) | `+36V` |
| `CW-` | White  | Cool-white return (→ strip `65K-`) | low-side CC sink CW |
| `WW-` | Yellow | Warm-white return (→ strip `27K-`) | low-side CC sink WW |
| `5V+` | Red    | Outer ring power (4.7 V) | `+4V7` |
| `GND` | Green  | Outer ring ground | `GND` |
| `DIM` | Blue   | **Outer ring DATA** (single-wire addressable; "DIM" is vendor's generic name) | level-shifted data out |
| (7th) | none   | unpopulated | — |

- Still TODO: physical position of each label on the 7-way housing (which end is pin 1, where the empty slot is) — read off the P0.6 scan.
- Confirmation logic: a color *gradient* over a single control wire ⇒ addressable, so `DIM` = data, not analog dim.

---

## P0.4 — Outer (addressable) ring  (Task 0.3)  — DONE
- strip marking = **UT-08-ZC03-01-5V3535RGBIC-3W-1C62B-628x8mm V1.2 20241120**
- type = **5 V, 3535-package, addressable RGBIC** (per-pixel IC); pads **VCC / DIN / GND** (3-wire)
- **pixel count = 62** (firmware currently 36 → MUST change to 62)
- **color format = RGB, 24-bit** (NOT RGBW; firmware uses CRGBW/32-bit → MUST change to RGB)
- data line: `CN1.DIM` (blue) → strip **DIN**, via 3.3→4.7 V level buffer
- construction: 200 Ω (`201`) inter-pixel data series-R + decoupling cap every 3 px (standard)
- protocol: 800 kHz NZR (WS2812/SK6812-class); confirm exact color order (likely GRB) on bring-up
- ⚠️ FIRMWARE (Phase 5): set count=62, switch outer ring to 24-bit RGB, verify color order
- ⚠️ CURRENT (risk R3): 62 RGB px worst-case ≫ 4.7 V rail budget → brightness cap mandatory

---

## P0.5 — White string  (Task 0.4)  — DONE
Strip `8C18W-12C4B-2835X2-588×7.5mm V1.0`; 96 LEDs total, cool/warm interleaved.
Wires: black=`LED+` (anode), white=`65K-` (cool return), yellow=`27K-` (warm return).

- **Topology: 12 series × 4 parallel banks PER COLOR** (`12C4B`); per-bank ballast resistors
  balance the 4 parallels. 48 cool + 48 warm = 96 ✓
- Per-color full current = 380 mA (driver total) → ~95 mA/bank across 4 banks; **sink carries 380 mA**
- Vf_string ≈ 12 × 3.05 V ≈ **36 V** ≈ rail (36.63 V) → **headroom ≈ 0.6 V**
- **Max sink dissipation ≈ 0.6 V × 0.38 A ≈ 0.23 W per channel** (only one color full at a time)
- Driver confirmed **constant-current** (rails to 36.63 V open = compliance; regulates 380 mA loaded).
  Board **steers + PWMs**, does not source current.
- **DECISION (Task 1.2 gate): 0.23 W ≤ 0.5 W → clone HT7308 linear CC low-side sinks.**
  Set CC limit **~420 mA** (above the driver's 380 mA) so the board never limits below the driver
  (full brightness preserved; CC engages only as protection if headroom ever appears).
- Fidelity (DONE): stock uses **two HT7308** (`H7308 J2448D1`, U1=cool, U1A=warm). Sense resistors
  `R5,R6` (ch1) and `R5A,R6A` (ch2), **all `R680`=0.68 Ω, two in parallel per channel ≈ 0.34 Ω**.
  With HT7308 V_ref ≈ 0.13 V → **I ≈ 382 mA/channel** (matches the 380 mA driver). Confirm V_ref vs
  datasheet at BOM step. Our board: use ~0.33 Ω (or 2×0.68 ∥) per channel → ~380–390 mA.

---

## P0.6 — Mechanical  (Task 0.5)  — FINAL (no scan; using envelope + photo assumptions)
No scan will be provided. Design to the measured envelope; outline = rounded rectangle.

- board outline L×W = **63.3 mm × 31.3 mm** ✅ → KiCad outline = rounded rectangle at these dims,
  corner radius ~5 mm (stadium-ish ends, per photos). Design ~0.3–0.5 mm inside nominal for fit slack.
- tallest component height (max envelope) = **6.7 mm** ✅
- fit check = **PASS, huge margin** (our tallest part USB-C ~3.2 mm; ceramic caps only). ✅
- mounting holes = **ASSUMED NONE** — stock board appears retained by the housing + harnesses
  (no screw holes visible in photos). ⚠️ Correct if the fixture actually screws the board down.
- connector placement (from photos) = **power-in (J1) at one short end, CN1 (J2) at the other short
  end**, matching stock harness exits. J2 pin order = stock label order V+/CW-/WW-/5V+/GND/DIM/(NC);
  align pin 1 to the housing from a CN1 photo at layout time. ⚠️ Confirm orientation before fab.
- antenna keepout = MINI-1 PCB antenna at a short **end**, toward the fixture opening; stock 2.4 GHz
  module ran from this area, so RF is low-risk. Keep copper clear under the antenna on all layers.

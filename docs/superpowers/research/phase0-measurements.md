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

## P0.4 — Outer (addressable) ring  (Task 0.3)
Read the marking on one ring pixel under magnification.

- pixel chip marking = `___`   (expected SK6812)
- pixel count = `___`          (firmware assumes 36)
- ring supply voltage = `___` V
- data protocol / notes = `___`

---

## P0.5 — White string  (Task 0.4)
HT7308 datasheet V_ref ≈ `___` V (confirm). I_set = V_ref / R_sense.

- HT7308 sense-resistor marking = `___`  → R_sense = `___` Ω  → **I_set = `___` mA**
- CW (65K, cool) series LED count = `___`   Vf_total_CW ≈ count×3.0 = `___` V   headroom = 36.63 − Vf = `___` V
- WW (27K, warm) series LED count = `___`   Vf_total_WW ≈ count×3.0 = `___` V   headroom = `___` V
- **Max sink dissipation = headroom × 0.38 A = `___` W**
- Decision (Task 1.2 gate): ≤0.5 W → linear CC (clone HT7308); >0.5 W → MOSFET+sense switching sink → chosen = `___`

---

## P0.6 — Mechanical  (Task 0.5)
600 dpi flatbed scan (or straight-down photo) with a ruler, saved next to this file.

- board outline L×W = `___` mm
- mounting holes: diameter = `___` mm, positions = `___`
- tallest component height (max envelope) = `___` mm
- MINI-1 (~2.4 mm) + chosen connectors fit under envelope? = `___`
- antenna keepout region relative to fixture opening = `___`
- scan file = `___`

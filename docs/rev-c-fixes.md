# Rev C fixes

Things found on real hardware that the next board revision must change.

## 1. J2 (CN1 harness) is assembled 180 deg flipped -- `cpl.csv` rotation

**Found:** 2026-11-25, first power-up of the first rev B board (assembled by JLCPCB).

**Symptom:** Board boots, joins Z2M, answers every command (downlight on/off, ring colour,
identify -- all acknowledged, strong LQI) but no LED lights. Power and pin map were fine.

**Cause:** With the stock harness plugged into J2, the wires sat one position too high and in
reverse order. J2 pad 1 (+36V) is the bottom pad (USB-C pointing down) and was empty; the
harness's black V+ wire landed on pad 7 (NC), so the LED strings never got 36 V. Red (5V+) only
worked because it is the middle pin. The `.kicad_pcb` is correct (pad 1 = +36V at the bottom,
rotation 90). The fault is the assembly rotation.

**Why:** For the rev B order, `cpl.csv` was changed to J2 rotation **270** because JLC's 3D
preview looked wrong at 90 (see rev-b-connector plan, "Order status"). The preview was
misleading; 270 assembled the part flipped. The "do not correct the preview" advice for J1/J3
applies equally to J2.

**Rev C change:**
- Set J2 rotation back to **90** in `hardware/kicad/cpl.csv` (matches KiCad). Not yet verified
  on a board -- confirm on the first rev C unit.
- Do not trust JLC's 3D preview to judge J2 orientation. Check pad 1 (+36V, the bottom pad)
  against the pin table in `docs/superpowers/specs/2026-09-21-rev-b-connector-design.md` 3.1.
- Consider a silkscreen "1" / arrow at J2 pin 1 so a flipped part is obvious.

**Rev B workaround:** re-pin the harness on every rev B board, bottom pad up: black (V+), white
(CW-), yellow (WW-), red (5V+), green (GND), blue (DIM), top pad empty. Verified on the bench.
This makes the harness incompatible with the stock Tuya board.

## 2. Downlight flashes on for ~1 s at mains power-up (minor, check first)

**Found:** 2026-11-25, rev B bench, cold boot from the driver with no USB. Board then joins and
behaves normally.

**Context:** R3/R4 (100 k) hold the CW/WW MOSFET gates low, and `led_driver_init()` zeroes the
white duty as the first thing `setup()` does, so only the ROM/bootloader window (~0.3 s by the
comment in `src/main.cpp`) should be ungated. ~1 s observed (by eye) is longer than that.

**Hypothesis (unmeasured):** during reset the ESP32-H2's GPIO4/GPIO5 sit on their weak internal
pull-ups, and against a 100 k pulldown the gate settles around 2 V -- enough to partly turn on a
logic-level N-FET and let the L-SD8E1 drive the string.

**To check:** scope Q1/Q2 gate at power-up on a rev B board. **Likely rev C change:** R3/R4
100 k -> 10 k (gate ~0.3 V during reset; GPIO drive through R1/R2 100 R is unaffected). Confirm
whether rev A fixtures did the same before treating it as a regression.

# Lumary Brain Board — Engineering Calculations

Inputs from `docs/superpowers/research/phase0-measurements.md`.
Rails: `+36V` = 36.63 V constant-current (380 mA), `+4V7` = 4.7 V logic/ring, `GND`.

---

## Task 1.1 — Power-path calculations

### 3.3 V rail (ESP32-H2-MINI-1)
- Design load: **200 mA** headroom (H2 active radio + flash-write bursts; steady draw is lower).
- LDO dissipation:
  - from USB 5 V:  (5.0 − 3.3) × 0.20 = **0.34 W**
  - from +4V7:     (4.7 − 3.3) × 0.20 = **0.28 W**
- Dropout requirement: at +4V7 in, LDO dropout must be **< 1.4 V @ 200 mA** → pick a low-dropout part (rules out marginal AMS1117-class at this headroom).
- Thermal: 0.34 W in SOT-23-5 (θJA ≈ 200 °C/W) → ~68 °C rise worst case → OK with a modest copper pour.
- **VERDICT: PASS** (≤ 0.5 W). LDO from a diode-OR of (+4V7, USB-VBUS) so the board runs installed *and* boots on USB alone at the bench.

### Outer-ring current budget (risk R3)
- 62 px, 3535 RGBIC (RGB). Per-pixel full-white ≈ 3 × ~6 mA ≈ **~18 mA** (3535 is lower-power than 5050); worst-case ≈ 62 × ~20 mA ≈ **1.24 A** at ~5 V.
- The driver's `+4V7` rail spare capacity is **unknown** (input 24 W − 13.6 W white ≈ ~10 W gross, minus efficiency → order of ~1 A, unconfirmed).
- **Conclusion:** full-brightness all-white on 62 px would exceed the rail. **Firmware brightness cap is mandatory** (already present as `BENCH_BRIGHTNESS`). Quantify the safe average once the rail's real limit is measured; keep the cap conservative until then.

### Inner white sink (from P0.5)
- Headroom ≈ 0.6 V, sink current 380 mA → **≈ 0.23 W per channel**, one color full at a time.
- **VERDICT: PASS**, trivial thermals (see Task 1.2).

---

## Task 1.2 — White-driver topology decision

**Gate rule:** dissipation ≤ 0.5 W → linear CC (clone HT7308); > 0.5 W → MOSFET + sense switching sink.

- P0.5 dissipation = **0.23 W ≤ 0.5 W → CHOSEN: clone the stock HT7308-class linear constant-current low-side sinks.**
- Two channels: `CW-` (cool, GPIO2 PWM) and `WW-` (warm, GPIO3 PWM), common anode at `+36V`.
- **CC limit set ≈ 420 mA** — deliberately *above* the driver's 380 mA so the board never limits below the driver (full brightness preserved). The CC function then only acts as protection if voltage headroom ever appears (e.g. a failed parallel bank).
- Brightness = PWM duty on both sinks; color temperature = cool:warm duty ratio.
- **Ratings:** low-side device / regulator must withstand the 35.8 V CC compliance → spec **≥ 60 V**.
- **To confirm at BOM step (Task 1.3):** HT7308 V_ref from datasheet → sense resistor `R_sense = V_ref / 0.42 A`. (Stock board photos hinted `R680` = 0.68 Ω; verify against datasheet math.)

**VERDICT:** low-side device Vds ≥ 60 V required; topology = linear CC clone. PASS.

---

## Open numeric items (not blocking Phase 1)
- Measure the `+4V7` rail's real current limit → set the firmware brightness cap precisely (risk R3).
- Confirm HT7308 V_ref (datasheet) → final sense-resistor value (Task 1.3).

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
- **MEASURED 2026-08-15** (rev A, bench supply on `+4V7`, module powered from USB so excluded):
  - 62 px dark, quiescent: **0.040 A** (~0.65 mA/px)
  - 62 px at `(24,14,14)` (`MAX_BRIGHTNESS` 24, sat 40%): **0.122 A**
  - → LED current 0.082 A for 52 of 765 channel-units, i.e. **~1.5 mA per unit across the ring**
  - → extrapolated full white at full brightness: **1.21 A**, against the 1.24 A predicted above.
    The per-pixel 3535 estimate is **confirmed to within 3%**.
  - → full white at the current cap of 24: **~0.15 A**, not the ~0.55 A previously assumed.
  - **Implication:** against the 0.74 A trace limit the cap could rise to roughly 100 on paper.
    Not acted on — the measurement is open-air, the fixture is a sealed can, and rev B should
    fix the trace width rather than spend the margin. Revisit after plan Task 6.4.

### Inner white sink (from P0.5)
- Headroom ≈ 0.6 V, sink current 380 mA → **≈ 0.23 W per channel**, one color full at a time.
- **VERDICT: PASS**, trivial thermals (see Task 1.2).

---

## Task 1.2 — White-driver topology decision

**REVISED (approved):** the external driver is a confirmed **constant-current** source (380 mA),
so the board does **not** need to regulate current — it only needs to **gate** each channel.
The stock board used linear CC (`HT7308`) because it did *analog* dimming via the `A08G` op-amp;
we dim **digitally by PWM from the ESP32**, so simple switches suffice.

- **CHOSEN: 2× logic-level N-MOSFET low-side switches** (`Q1`=CW/GPIO2, `Q2`=WW/GPIO3), common
  anode at `+36V`. No current-regulation, no sense resistors — the driver sets the 380 mA.
- Brightness = PWM duty on both switches; color temperature = cool:warm duty ratio.
- **+ 36 V bulk cap `C1` (10 µF/50 V):** absorbs turn-on inrush when a channel switches (mirrors
  the stock 4.7 µF/50 V caps). When both channels are off, the CC driver rails to its 35.8 V
  compliance — this cap holds the rail, and the MOSFETs must block it.
- **Ratings:** MOSFET Vds must withstand the 35.8 V compliance → spec **≥ 60 V**, logic-level
  (fully on at 3.3 V gate), Id ≥ 0.5 A (carries the full 380 mA when on).
- Thermal: MOSFET on-state loss = I²·Rds(on) ≈ 0.38² × ~0.1 Ω ≈ **1.5 mW** — negligible.

**Firmware note (Phase 6):** a CC driver may not track 20 kHz output PWM cleanly; plan to lower
`PWM_FREQ_HZ` to ~500 Hz–2 kHz during bring-up and verify flicker-free low-end dimming.

**VERDICT:** topology = MOSFET low-side switches + 36 V bulk cap; devices ≥ 60 V logic-level. PASS.

---

## Task 1.4 — ESP32-H2 pin assignments

ESP32-H2 **strapping pins = GPIO2, GPIO3, GPIO8, GPIO9, GPIO25**; **USB = GPIO26 (D−), GPIO27 (D+)**.

⚠️ **Conflict found:** the current firmware drives **CW=GPIO2** and **WW=GPIO3** — both are
**strapping pins**. Our MOSFET gate pulldowns would hold them low at reset, risking boot-mode
issues. On the new board we **move CW/WW off the strapping pins**.

| Function | Old (Super Mini fw) | New board | Note |
|---|---|---|---|
| RGBIC ring data | GPIO11 | **GPIO11** | OK (not strapping); SPI2-MOSI via GPIO matrix |
| CW PWM (Q1 gate) | GPIO2 ⚠️ | **GPIO4** | moved off strapping |
| WW PWM (Q2 gate) | GPIO3 ⚠️ | **GPIO5** | moved off strapping |
| BOOT button | GPIO9 | **GPIO9** | strapping = download-mode select (correct use) |
| Reset | — | **EN** pin | dedicated |
| USB D− / D+ | — | **GPIO26 / GPIO27** | native USB-Serial-JTAG |

- **Verify (Task 2.6/layout):** GPIO4, GPIO5, GPIO11 are broken out on MINI-1 and free of analog-only limits.
- **Firmware (Phase 5, Task 5.1):** `PIN_CW_PWM` 2→**4**, `PIN_WW_PWM` 3→**5**; `PIN_SK6812_DATA` stays 11.

## As-built trace widths (rev A) — found after routing

KiCad discarded the `Power` net class when it rewrote `lumary-brain.kicad_pro` on
first open, so **every net on rev A routed at the 0.2 mm Default**, not the 0.5 mm
intended for the power nets. The Gerbers were generated from that board, so this is
what shipped. The class has since been restored in the project file for rev B.

IPC-2221, external layer, 1 oz copper: a 0.2 mm trace carries **0.74 A** at a 10 °C
rise (1.01 A at 20 °C).

| Path | Current | Rise | Drop | Verdict |
|---|---|---|---|---|
| `+36V`, `CW_RET`, `WW_RET` (white string) | 380 mA | 2.2 °C | 37 mV | **fine** — the CC driver fixes this current, it cannot grow |
| `+4V7_IN` at `MAX_BRIGHTNESS` 24 (ring 0.35 A + module 0.2 A) | 550 mA | 5.0 °C | 60 mV | **fine** |
| `+4V7_IN` if brightness were raised to 255 | ~1.5 A | **49 °C** | 164 mV | **not acceptable** |

**Consequence for firmware:** `MAX_BRIGHTNESS` in `main.cpp` is a *hardware* limit on
rev A, not just a USB-bench convenience. The ceiling that keeps `+4V7_IN` inside its
10 °C rise is about **37**; it is currently set to 24, which leaves comfortable margin.
Do not raise it toward 255 on a rev A board.

**Rev B fix:** widen `+4V7`/`+4V7_IN` to 0.5 mm (or pour them). The white path can stay
at 0.2 mm — 380 mA is fixed by the constant-current driver and barely warms the copper.

---

## Open numeric items (not blocking Phase 1)
- Measure the `+4V7` rail's real current limit → set the firmware brightness cap precisely (risk R3).
- Confirm final MOSFET part meets ≥60 V / logic-level / ≥0.5 A and is JLCPCB-stocked (Task 1.3 verify).

# Remove the rev A brightness ceiling — design

**Date:** 2026-08-15
**Status:** approved, ready for implementation plan

## 1. Problem

`main.cpp` clamps every effect's brightness to `MAX_BRIGHTNESS` (24/255) before rendering:

```c
if (p.brightness > MAX_BRIGHTNESS) p.brightness = MAX_BRIGHTNESS;
```

The clamp was added to protect rev A's `+4V7` traces, which routed at 0.2 mm instead of the
intended 0.5 mm because KiCad dropped the Power net class before routing (commit `a1270ba`).
IPC-2221 rates a 0.2 mm external trace in 1 oz copper at 0.74 A for a 10 °C rise.

It has two consequences, one of them unintended:

1. The **outer ring** is limited to 24/255, and because the clamp is a hard limit rather than a
   rescale, brightness 24 through 255 all render identically — the top 90% of the dimmer is dead.
2. The **inner white string** — the fixture's main light source — is limited to ~9% duty. This was
   never the intent. The white current flows 36 V → LEDs → `CW-`/`WW-` → `Q1`/`Q2` → GND and
   **never touches the `+4V7` trace the ceiling exists to protect.**

Consequence 2 is the motivating defect: the fixture cannot currently produce more than a tenth of
its light output.

## 2. Evidence

Bench measurements on 2026-08-15 (see `hardware/calcs.md`) retire the premise the ceiling rests on.

**White path**, Task 6.3 thermal gate — `Q2` at the full 380 mA, 100% duty, 15 minutes:

| | Temp | Rise over 23.3 °C ambient |
|---|---|---|
| `Q2` | 37 °C | 13.7 °C |
| `U2` (LDO, hottest point on the board) | 40 °C | 16.7 °C |

By construction the two white channels can never both run at full: `cw = scale8(level, hue)` and
`ww = scale8(level, 255-hue)` always sum to ≤255. The worst case is one channel at 100%, which is
what was tested. `PWM_RESOLUTION` is `LEDC_TIMER_8_BIT`, so level 255 is genuinely 100% duty.

**Ring path**, three separate runs including a cold start:

| | Estimated when the ceiling was set | Measured |
|---|---|---|
| Ring at brightness 24 | ~0.55 A | 0.12 A |
| Ring at brightness 255 | ~1.2 A | **0.48 A** |
| Trace temperature, any brightness | up to +49 °C | **no measurable rise** |

Current saturates above brightness ~128 (the strip self-limits), and a cold start at 30 °C showed
no surge — 0.480 A at the instant of turn-on, unchanged at 2 minutes.

## 3. Change

Delete the clamp. Keep the history in a comment referencing `hardware/calcs.md`.

Rejected alternatives:

- **Keep `MAX_BRIGHTNESS` at 255.** `uint8_t > 255` is always false — dead code that reads as a
  live safety limit.
- **Build a ring-only ceiling now, set to 255.** Machinery for a requirement that no longer
  exists. If Task 6.4 shows the driver's rail cannot cope, adding a ring-only cap then is a small
  change and would be built against measured numbers rather than a guess.

## 4. Effect

| | Before | After |
|---|---|---|
| White string | 9% max | 100% |
| Ring | 24/255, top 90% of dimmer dead | full range |
| Dimming resolution | 24 steps | 255 steps |

The ring and white string are **mutually exclusive**: only `fx_static_white` drives `CW`/`WW`, and
every other effect calls `white_off()`. So brightness only ever controls one source at a time, and
each now gets the full dimmer range.

## 5. Accepted risks

Recorded here so Task 6.4 checks them rather than discovering them:

1. ~~**The L-SD8E1's spare 4.7 V capacity is unmeasured.**~~ **RETIRED 2026-08-15** by running the
   real service wiring — driver supplying GND, 36 V and 4.7 V, bench PSU disconnected. The ring
   held **steady and flicker-free at full brightness** drawing ~0.48 A, and again at saturation
   25% (~0.55 A), while the module ran from the same rail. No dimming, colour shift or far-end
   fade. The driver carries ring + module without sagging.
2. **Full white on the ring remains untested**, and cannot be reached from Home Assistant at all:
   `sat = 0` always routes to the white string, so the colour path tops out around saturation 25%
   (~83% of full-white current). True full white needs an effect such as `warm_gradient` writing
   near-white pixels, and effects are selected by Zigbee scene commands stored in NVS. It will
   first be exercised whenever scenes are used. Extrapolates to ~0.66 A against the 0.74 A rating;
   expected to be fine given the rail held 0.55 A without complaint and the trace has never warmed
   measurably, but **expected is not measured**.
3. **All measurements were open-air** at 23–29 °C. The fixture is a sealed ceiling can.

## 6. Testing

- **Regression:** the encoder and colour helpers are untouched, so all 35 native tests must pass
  unchanged (`scripts\run-native-tests.bat`).
- **White at full:** drive `color_temp` at brightness 255 and confirm `Q1`/`Q2` thermals still
  match the 6.3 result.
- **Ring at full:** drive a saturated colour at brightness 255 and re-image the `+4V7` trace on
  the **B.Cu** side, where the long 0.2 mm runs are.
- **Rollback:** restore one line.

**Results (2026-08-15, real service wiring — driver supplying all three rails, bench PSU removed):**

| Check | Result |
|---|---|
| Native regression, 35 tests | pass, unchanged |
| White at full (2702 K, 255) | **bright** — the defect fixed; `Q2` 36 °C, `U2` 38 °C |
| Ring at full (sat 40%, 255) | steady, no flicker |
| Ring at sat 25% (~83% of white) | steady, no flicker |

`Q2` at 36 °C against 6.3's 37 °C, and `U2` at 38 °C against 40 °C. `U2` running slightly cooler
is expected: it is now fed from the 4.7 V rail rather than USB's 5 V, dropping 1.4 V instead of
1.7 V and dissipating ~20% less.

## 7. Out of scope

Deliberately not bundled in:

- Gamma correction and per-channel white balance — the ring renders numerically correct colours
  that do not look correct, observed repeatedly on 2026-08-15. Its own change.
- Reporting light state after reboot, so HA does not show a light as on when it is physically off.
- The deferred power-cycle-reset / BLE-OTA-trigger rework (spec §4.5).

# Gamma and the low end — design

**Date:** 2026-08-17
**Status:** approved, ready for implementation

Item 6 of `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`. Taken before items 3 and 9
because it is the last item that is genuinely orthogonal to the two-endpoint split: item 9 balances
the downlight against the ring by eye, and tuning that balance on an uncorrected brightness curve
means tuning it twice.

## 1. Problem

`scale8` feeds both the ring and the 8-bit PWM linearly, so Home Assistant's brightness slider is
non-linear against perceived output: the bottom third does very little and then jumps. The backlog
described the fix as "a gamma LUT on the ring and a minimum duty floor on CW/WW".

### 1.1 The obvious fix makes it worse

A gamma LUT alone regresses the exact region it is meant to repair. With the current 8-bit PWM
(`src/config.h:43`), γ=2.2 maps every input below 15 to duty **0**:

| HA brightness | today (linear) | γ2.2 @ 8-bit | γ2.2 @ 12-bit | CIE L\* @ 8-bit | CIE L\* @ 12-bit |
|---|---|---|---|---|---|
| 1 | 1 | **0** | **0** | **0** | 2 |
| 5 | 5 | **0** | 1 | 1 | 9 |
| 8 | 8 | **0** | 2 | 1 | 14 |
| 13 | 13 | **0** | 6 | 1 | 23 |
| 26 | 26 | 2 | 27 | 3 | 47 |
| 64 | 64 | 12 | 196 | 11 | 182 |
| 128 | 128 | 56 | 899 | 47 | 761 |
| 255 | 255 | 255 | 4095 | 255 | 4095 |

First input that produces any light at all:

| | first non-zero input |
|---|---|
| γ2.2 @ 8-bit | 15 |
| γ2.2 @ 12-bit | 5 |
| CIE L\* @ 8-bit | 5 |
| CIE L\* @ 12-bit | **1** |

> **Correction (post-implementation review):** the "CIE L\* @ 8-bit" row and its "first non-zero
> input: 5" describe the *table's* output, not the ring's drive value. The ring composes the table
> through `scale8(val, scale) = (val * scale) >> 8` (`src/color.h`), where the table entry is the
> `scale` argument. A table entry of 1 is a real, intended floor value, but `(255 * 1) >> 8 == 0` —
> the `>> 8` throws it away for every channel below full value, and even at full value the whole
> `kGamma8[b] == 1` band (`b` in 1..13) composes to zero. First non-zero *drive* on the ring is
> therefore 14, not 5, with a naive `scale8` composition. The implementation avoids this by
> composing with `scale_by_255` (divide by 255) instead of `scale8`'s shift, which is what actually
> gets the ring to brightness 1. See `src/brightness.h`'s `scale_by_255` and the design correction in
> §3.1 below.

So the bottom of the slider is not dead because the curve is wrong. It is dead because **8 bits of
output cannot represent a gamma-corrected low end**. Task 6.3 already swept the white string to zero
duty with no steps, flicker or dropouts, so the hardware is not the limit — quantisation is.

The fix is therefore two coupled changes: a perceptual curve *and* more output resolution beneath
it. The "minimum floor" then falls out as a `max(1, …)` guard rather than a tuned constant.

### 1.2 The curve belongs on the brightness scalar, not on output channels

Because gamma is non-linear, `gamma(a × b) ≠ gamma(a) × b`. Applying the curve to each output
channel independently therefore distorts every *ratio* between channels — and on the white string
that ratio is the colour temperature.

Measured against the intended mix at `level=255, hue=64` (a 191:64 CW/WW split):

| | ww | cw | ratio |
|---|---|---|---|
| intended (linear mix) | 191 | 64 | 2.98 : 1 |
| gamma the total, split linearly | 3067 | 1027 | **2.99 : 1** |
| gamma each leg separately | 1971 | 182 | 10.83 : 1 |

Per-leg gamma would make the fixture drift sharply warm as it dims. The same argument applies to any
fixed colour mix on the ring — `warm_white()`'s {255, 169, 87} and `fx_warm_gradient`'s warm→blue
crossfade both distort the same way.

The correct operation follows from the physics: a pixel's luminous output is linear in its drive
value, so to make a colour appear at some perceptual lightness, every channel is scaled by the same
*gamma-corrected scalar*. That is `drive = original_drive × gamma(brightness)` — the curve applied
once, to the brightness, before it is mixed into either source.

## 2. Decisions

| Decision | Choice | Why |
|---|---|---|
| Curve | CIE 1931 lightness | Its linear segment below L\*=8 is what reaches brightness 1; a plain power law cannot |
| White PWM resolution | 8-bit → 12-bit | Somewhere to put the corrected low end; 1 kHz from the 32 MHz XTAL allows ~14 bits, so 12 has margin |
| Ring resolution | unchanged, 8-bit | Fixed by the LED protocol |
| CCT while dimming | constant | Matches what HA asked for; warm-dim stays a possible deliberate feature, not an artifact |
| Where the curve applies | in the effects, on the composed brightness | The only point that knows the final instantaneous value |

### 2.1 Why not at the driver boundary

One chokepoint in `led_driver_set_cw/ww` and `led_driver_show` would leave the effects untouched,
but by then brightness is already multiplied into the channels — this is exactly the per-channel
case §1.2 rules out.

### 2.2 Why not in `light_state_resolve()`

A single point, already host-tested, no effect changes. But effects modulate brightness *after*
resolve: `fx_breathing` multiplies by a triangle wave (`src/effects.cpp:70`), `fx_warm_gradient` by a
per-pixel spatial ramp (`src/effects.cpp:45`). Curving before those means the envelope rides on an
already-curved value, so the breathing fade rushes at one end and the gradient bunches up. Correct
for plain on/off/dim, wrong for six of the eight effects.

## 3. Design

### 3.1 New module: `src/brightness.h`

Pure, free of ESP-IDF headers, following `pixel_encode.h` / `light_state.h` / `version.h` so the host
tests reach it. Depends on `color.h`; only `effects.cpp` depends on it.

```c
uint8_t  gamma8 (uint8_t brightness);   // ring:  8-bit in -> 8-bit out
uint16_t gamma12(uint8_t brightness);   // white: 8-bit in -> 12-bit out
CRGB     scale_brightness_gamma(CRGB c, uint8_t brightness);
```

Both implement the CIE 1931 lightness curve:

```
L* = brightness / 255 x 100
Y  = L* / 903.3            for L* <= 8      <- linear segment
Y  = ((L* + 16) / 116)^3   for L* > 8
```

`scale_brightness_gamma` lives here rather than in `color.h` so the dependency points one way.

**Two 256-entry lookup tables**, 256 B + 512 B of flash. Not computed at runtime — the ESP32-H2 is
RISC-V with no FPU, so `pow` is soft-float. Not `constexpr`-generated either: `std::pow` is not
constexpr in C++17, which is what the test runner builds with (`scripts/run-native-tests.bat:40`).
A generator script under `scripts/` emits the tables, and the host test recomputes the formula in
`double` and asserts all 512 entries match, so the table cannot silently rot.

**The floor is not a tuned constant.** `gamma12` already yields 2 at brightness 1, so it never
triggers there. `gamma8` yields 0 for brightness 1–4 only, so a `max(1, …)` guard on non-zero input
catches exactly those four — the ring's hardware limit, stated rather than chosen.

> **Correction (post-implementation review):** `gamma8`/`kGamma8` return a *multiplier*, not a drive
> value — composing them into a pixel channel still has to happen, and that composition matters. The
> obvious composition, `scale8(val, scale) = (val * scale) >> 8`, turns a multiplier of 1 into a
> drive of 0 for any channel value, silently discarding the very floor described above: `kGamma8[b]
> == 1` for `b` in 1..13, so all thirteen of those inputs composed to zero via `scale8`, widening the
> ring's dead zone instead of removing it (first light moved from brightness 2 to brightness 14).
> `scale_brightness_gamma` in `src/brightness.h` therefore composes with `scale_by_255(v, s) =
> (v * s) / 255` instead — ordinary integer division rather than a `>> 8` shift — so a multiplier of
> 1 against a full-value channel yields 1, and the table's floor actually reaches the LED. The same
> fix applies to `warm_white_gamma` (`src/brightness.h`), used by `fx_nightlight`, since
> `color.h`'s `warm_white()` has the identical `scale8` exposure.

### 3.2 `config.h` and `led_driver`

- `PWM_RESOLUTION` → `LEDC_TIMER_12_BIT`, new `PWM_DUTY_MAX` = 4095
- `led_driver_set_cw` / `led_driver_set_ww` widen from `uint8_t` to `uint16_t`

`ledc_set_duty` already takes `uint32_t`, so nothing inside changes. The existing `ESP_ERROR_CHECK`
on `ledc_timer_config` (`src/led_driver.cpp:32`) means an invalid resolution/frequency pair panics at
boot rather than misbehaving quietly.

### 3.3 The effects

All eight entries in `kEffects` change; only two need thought. `fx_identify` is a ninth function
outside that table and is untouched.

| Effect | Change |
|---|---|
| `fx_static_color`, `fx_color_gradient`, `fx_breathing`, `fx_color_cycle`, `fx_chase` | rename `scale_brightness` → `scale_brightness_gamma`, one line each |
| `fx_nightlight` | `warm_white(p.brightness)` → `warm_white_gamma(p.brightness)` (see the §3.1 correction — `warm_white(gamma8(…))`, which this section originally specified, carries the same `>>8` bug) |
| `fx_static_white` | the 12-bit split, below |
| `fx_warm_gradient` | rework, below |
| `fx_identify` | untouched — a fixed full-output blink with no brightness input |

**`fx_static_white`** curves the level once, then splits linearly so CCT is exact:

```c
const uint16_t total = gamma12(level);
led_driver_set_ww(uint16_t(uint32_t(total) * (255 - p.hue) / 255));
led_driver_set_cw(uint16_t(uint32_t(total) *        p.hue  / 255));
```

The intermediate is `uint32_t` because `total` reaches 4095 and the multiply would otherwise
overflow 16 bits; the result is capped at `total`, so the narrowing cast back to `uint16_t` is
explicit rather than left to an implicit conversion that `/W3` would flag.

`/255` rather than `scale8`'s `>>8`: the shift maps a full 255 mix to 254/255 of the total, so pure
warm would never quite reach full duty. This is the same off-by-one `scale_level` already works
around (`src/light_state.h:85`). The two coefficients sum to 255, so total output stays constant
across CCT exactly as it does today.

**`fx_warm_gradient`** is the one effect where per-leg gamma would distort a ratio the same way CCT
does — it crossfades a warm colour to blue across the ring with brightness folded into each leg.
Restructured to blend at full intensity and curve once:

```c
const CRGB c = blend(CRGB{200, 100, 0}, CRGB{0, 0, 255}, pos);
leds[i] = scale_brightness_gamma(c, p.brightness);
```

This gives `blend()` (`src/color.h:48`) its first caller — it is currently dead code.

> This changes the rendered gradient slightly **even at full brightness**, because the current
> version is not a true blend. It is the one effect to eyeball on the bench rather than merely check
> dims smoothly.

## 4. Testing

New `test/test_brightness/test_main.cpp`. The runner discovers `test\test_*` automatically
(`scripts/run-native-tests.bat:28`), so no script change is needed.

| Test | Guards against |
|---|---|
| All 512 LUT entries match the CIE formula recomputed in `double` | table rot |
| Monotonic non-decreasing | a step backwards anywhere on the slider |
| `gamma8(255)==255`, `gamma12(255)==4095`, both zero at zero | endpoint drift |
| **Non-zero in → non-zero out, all 255 inputs** | the original bug, and its 8-bit reappearance |
| CCT split holds the mix ratio within ±1 LSB across a (level, hue) grid | §1.2 regression on white |
| `scale_brightness_gamma` preserves channel ratios | someone "simplifying" it back to per-channel gamma |

## 5. Risk

**Frequency is unchanged at 1 kHz**, so the L-SD8E1 sees the same switching rate it already passed
Task 6.3 with. But minimum on-time drops 16×: duty 1/4095 is a 0.24 µs pulse against 3.9 µs today.
The driver's control loop may not respond that fast, in which case the bottom of the 12-bit range
produces no light and brightness 1 is still off — the original bug, relocated.

The bench sweep tests exactly this. If a real threshold exists, the `max(1, …)` guard becomes a
*measured* minimum duty instead. **The floor is provisional by design.**

## 6. Bench verification

All USB-side with the fixture on the bench; no dependency on Task 6.4.

- [ ] Sweep HA brightness 1 → 255: no dead zone at the bottom
- [ ] The perceived ramp is even, with no jump out of the low end
- [ ] CCT holds constant while dimming, checked at 2700 K, 4000 K and 6500 K
- [ ] `fx_warm_gradient` still looks right at full brightness after the rework
- [ ] All eight effects dim smoothly

## 7. Out of scope

- **Temporal dithering** for the ring's bottom four steps. The ring is the accent, not the main
  light; revisit only if it looks bad.
- **Warm-dim.** Possible later as a deliberate opt-in; it is not to arrive as an artifact.
- **Transitions** (item 3). The slew is a separate change and lands after this one.
- **Ring output resolution.** Fixed at 8-bit by the LED protocol; only the white string gains bits.

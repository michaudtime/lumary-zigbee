# Gamma and the Low End Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Home Assistant's brightness slider perceptually even from 1 to 255, with no dead zone at the bottom, on both the outer RGB ring and the inner CW/WW white string.

**Architecture:** A new pure header `src/brightness.h` holds the CIE 1931 lightness curve as two checked-in lookup tables (8-bit for the ring, 12-bit for the white string) plus the CW/WW mix helper. The white PWM widens from 8-bit to 12-bit so the corrected low end has somewhere to live. The curve is applied inside the effects, to the *composed instantaneous brightness*, immediately before it is mixed into a colour — never to individual output channels, because `gamma(a × b) ≠ gamma(a) × b` distorts every channel ratio, and on the white string that ratio is the colour temperature.

**Tech Stack:** C++17, PlatformIO, Arduino ESP32 v3.x on ESP32-H2, ESP-IDF LEDC, Unity (host tests via MSVC).

**Design doc:** `docs/superpowers/specs/2026-08-17-gamma-and-low-end-design.md`

## Global Constraints

- **Branch:** `feat/gamma-and-low-end` (already created, design doc already committed there).
- **Curve:** CIE 1931 lightness, exactly. `L* = brightness / 255 × 100`; `Y = L* / 903.3` for `L* ≤ 8`; `Y = ((L* + 16) / 116)³` for `L* > 8`. The linear segment below `L*=8` is load-bearing — a plain power law cannot reach brightness 1.
- **Floor:** any non-zero brightness must produce a non-zero output. Baked into the tables as `max(1, …)`, not applied at call sites.
- **White PWM:** `LEDC_TIMER_12_BIT`, `PWM_DUTY_MAX` 4095. **Frequency stays at 1000 Hz** — do not change `PWM_FREQ_HZ`.
- **Ring stays 8-bit.** Fixed by the LED protocol. Do not attempt dithering.
- **CCT is constant while dimming.** The curve applies to the brightness scalar, then splits linearly into CW/WW.
- **New headers must stay free of ESP-IDF includes** so `scripts\run-native-tests.bat` can compile them on the host. `brightness.h` may include only `<stdint.h>` and `color.h`.
- **Windows:** run `pio` from **PowerShell or cmd, not Git Bash** — the toolchain installer aborts under MSys. Host tests run from either.
- Do not touch `fx_identify` — it is a fixed full-output blink with no brightness input.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `scripts/gen-gamma-tables.py` | create | Regenerates the two tables; the tests verify its output |
| `src/brightness.h` | create | The curve, both tables, `scale_brightness_gamma`, `white_mix_gamma`. Pure, host-testable |
| `test/test_brightness/test_main.cpp` | create | Host tests for all of the above |
| `src/config.h` | modify | `PWM_RESOLUTION` → 12-bit, add `PWM_DUTY_MAX` |
| `src/led_driver.h` / `.cpp` | modify | CW/WW duty type widens `uint8_t` → `uint16_t` |
| `src/effects.cpp` | modify | All eight effects apply the curve |
| `docs/superpowers/plans/2026-08-17-home-assistant-polish.md` | modify | Mark item 6 done, record bench results |

---

## Task 1: The brightness module

Everything in this task is pure host-side code with no hardware dependency. It must pass `scripts\run-native-tests.bat` before Task 2 begins.

**Files:**
- Create: `scripts/gen-gamma-tables.py`
- Create: `src/brightness.h`
- Test: `test/test_brightness/test_main.cpp`

**Interfaces:**
- Consumes: `CRGB`, `scale_brightness()`, `scale8()` from `src/color.h`
- Produces, for Tasks 2–4:
  - `uint8_t gamma8(uint8_t brightness)` — 8-bit in, 8-bit out
  - `uint16_t gamma12(uint8_t brightness)` — 8-bit in, 12-bit out (0–4095)
  - `CRGB scale_brightness_gamma(CRGB c, uint8_t brightness)`
  - `struct WhiteMix { uint16_t ww, cw; }`
  - `WhiteMix white_mix_gamma(uint8_t level, uint8_t cct)` — `cct` 0 = fully warm, 255 = fully cool

- [ ] **Step 1: Write the generator script**

Create `scripts/gen-gamma-tables.py`:

```python
#!/usr/bin/env python3
"""Regenerates the CIE lightness tables in src/brightness.h.

The tables are checked in rather than computed at runtime (the ESP32-H2 is
RISC-V with no FPU, so pow() is soft-float) and rather than constexpr-generated
(std::pow is not constexpr in C++17, which is what the host test runner uses).

test/test_brightness recomputes this same formula in double and asserts every
entry matches, so the checked-in tables cannot silently drift from this script.

Usage:  python scripts/gen-gamma-tables.py
Then paste each block between the corresponding markers in src/brightness.h.
"""


def cie(brightness, out_max):
    """CIE 1931 lightness. Returns 0 only for an input of 0."""
    if brightness == 0:
        return 0
    lightness = brightness / 255.0 * 100.0
    if lightness <= 8.0:
        luminance = lightness / 903.3
    else:
        luminance = ((lightness + 16.0) / 116.0) ** 3
    return max(1, round(out_max * luminance))


def emit(out_max, per_line, width):
    values = [cie(i, out_max) for i in range(256)]
    lines = []
    for start in range(0, 256, per_line):
        row = ", ".join(f"{v:{width}d}" for v in values[start:start + per_line])
        lines.append(f"    {row},")
    return "\n".join(lines)


print("// --- gamma8 table begin ---")
print(emit(255, 16, 3))
print("// --- gamma8 table end ---")
print()
print("// --- gamma12 table begin ---")
print(emit(4095, 12, 4))
print("// --- gamma12 table end ---")
```

- [ ] **Step 2: Write the failing test**

Create `test/test_brightness/test_main.cpp`:

```cpp
// Native (host) tests for the perceptual brightness curve.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include <math.h>      // lround, pow
#include <stdlib.h>    // labs
#include "brightness.h"

void setUp(void) {}
void tearDown(void) {}

// The reference implementation, recomputed here in double. The tables in
// brightness.h are generated by scripts/gen-gamma-tables.py; this is what stops
// them drifting from the formula they claim to encode.
static uint32_t cie_reference(uint8_t brightness, uint32_t out_max) {
    if (brightness == 0) return 0;
    const double lightness = brightness / 255.0 * 100.0;
    const double luminance = (lightness <= 8.0)
        ? lightness / 903.3
        : pow((lightness + 16.0) / 116.0, 3.0);
    const uint32_t v = (uint32_t)lround(out_max * luminance);
    return v == 0 ? 1 : v;
}

// ── the tables match the formula ──────────────────────────────────────────

void test_gamma8_matches_the_formula(void) {
    for (int i = 0; i < 256; i++) {
        TEST_ASSERT_EQUAL_UINT32(cie_reference((uint8_t)i, 255), gamma8((uint8_t)i));
    }
}

void test_gamma12_matches_the_formula(void) {
    for (int i = 0; i < 256; i++) {
        TEST_ASSERT_EQUAL_UINT32(cie_reference((uint8_t)i, 4095), gamma12((uint8_t)i));
    }
}

// ── curve shape ───────────────────────────────────────────────────────────

void test_both_curves_are_monotonic(void) {
    for (int i = 0; i < 255; i++) {
        TEST_ASSERT_TRUE(gamma8((uint8_t)i)  <= gamma8((uint8_t)(i + 1)));
        TEST_ASSERT_TRUE(gamma12((uint8_t)i) <= gamma12((uint8_t)(i + 1)));
    }
}

void test_endpoints_are_exact(void) {
    TEST_ASSERT_EQUAL_UINT8(0,     gamma8(0));
    TEST_ASSERT_EQUAL_UINT8(255,   gamma8(255));
    TEST_ASSERT_EQUAL_UINT16(0,    gamma12(0));
    TEST_ASSERT_EQUAL_UINT16(4095, gamma12(255));
}

// This is the regression test for the bug the whole change exists to fix:
// linear scaling left the bottom of the slider doing nothing, and a naive
// gamma LUT on 8-bit output would have made brightness 1..14 go fully dark.
void test_no_nonzero_brightness_goes_dark(void) {
    for (int i = 1; i < 256; i++) {
        TEST_ASSERT_GREATER_THAN_UINT8(0,  gamma8((uint8_t)i));
        TEST_ASSERT_GREATER_THAN_UINT16(0, gamma12((uint8_t)i));
    }
}

// ── the CW/WW split holds colour temperature ──────────────────────────────
// Applying the curve per-leg instead of to the total would turn a 2.98:1 mix
// into 10.83:1, so the fixture would drift warm as it dimmed.

void test_white_mix_conserves_the_total(void) {
    for (int level = 1; level < 256; level += 7) {
        for (int cct = 0; cct < 256; cct += 5) {
            const WhiteMix w = white_mix_gamma((uint8_t)level, (uint8_t)cct);
            const uint32_t total = gamma12((uint8_t)level);
            const uint32_t sum   = (uint32_t)w.ww + w.cw;
            TEST_ASSERT_TRUE(sum <= total);
            TEST_ASSERT_TRUE(sum + 2 >= total);
        }
    }
}

void test_white_mix_holds_the_ratio(void) {
    for (int level = 1; level < 256; level += 7) {
        for (int cct = 1; cct < 255; cct += 5) {
            const WhiteMix w = white_mix_gamma((uint8_t)level, (uint8_t)cct);
            // ww/cw should equal (255-cct)/cct. Cross-multiplied to stay in
            // integers: each side truncates by less than 1 LSB, and the two
            // errors are weighted by cct and (255-cct), which sum to 255 --
            // so a bound of 255 here IS the "within 1 LSB" property.
            const int32_t lhs = (int32_t)w.ww * cct;
            const int32_t rhs = (int32_t)w.cw * (255 - cct);
            TEST_ASSERT_TRUE(labs((long)(lhs - rhs)) < 255);
        }
    }
}

void test_white_mix_endpoints_are_pure(void) {
    const WhiteMix warm = white_mix_gamma(255, 0);
    TEST_ASSERT_EQUAL_UINT16(4095, warm.ww);
    TEST_ASSERT_EQUAL_UINT16(0,    warm.cw);

    const WhiteMix cool = white_mix_gamma(255, 255);
    TEST_ASSERT_EQUAL_UINT16(0,    cool.ww);
    TEST_ASSERT_EQUAL_UINT16(4095, cool.cw);
}

void test_white_mix_is_dark_when_off(void) {
    const WhiteMix w = white_mix_gamma(0, 128);
    TEST_ASSERT_EQUAL_UINT16(0, w.ww);
    TEST_ASSERT_EQUAL_UINT16(0, w.cw);
}

// ── the ring keeps its colour while dimming ───────────────────────────────
// Guards against someone "simplifying" this into per-channel gamma later.

void test_scale_brightness_gamma_preserves_channel_ratios(void) {
    const CRGB source = {255, 169, 87};        // the warm_white() mix
    for (int b = 16; b < 256; b += 5) {
        const CRGB out = scale_brightness_gamma(source, (uint8_t)b);
        TEST_ASSERT_TRUE(labs((long)out.r * source.g - (long)out.g * source.r) < 255);
        TEST_ASSERT_TRUE(labs((long)out.g * source.b - (long)out.b * source.g) < 255);
    }
}

void test_scale_brightness_gamma_endpoints(void) {
    const CRGB source = {255, 169, 87};
    const CRGB full   = scale_brightness_gamma(source, 255);
    TEST_ASSERT_EQUAL_UINT8(254, full.r);      // scale8 maps 255 -> 254
    const CRGB dark   = scale_brightness_gamma(source, 0);
    TEST_ASSERT_EQUAL_UINT8(0, dark.r);
    TEST_ASSERT_EQUAL_UINT8(0, dark.g);
    TEST_ASSERT_EQUAL_UINT8(0, dark.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_gamma8_matches_the_formula);
    RUN_TEST(test_gamma12_matches_the_formula);
    RUN_TEST(test_both_curves_are_monotonic);
    RUN_TEST(test_endpoints_are_exact);
    RUN_TEST(test_no_nonzero_brightness_goes_dark);
    RUN_TEST(test_white_mix_conserves_the_total);
    RUN_TEST(test_white_mix_holds_the_ratio);
    RUN_TEST(test_white_mix_endpoints_are_pure);
    RUN_TEST(test_white_mix_is_dark_when_off);
    RUN_TEST(test_scale_brightness_gamma_preserves_channel_ratios);
    RUN_TEST(test_scale_brightness_gamma_endpoints);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `scripts\run-native-tests.bat`

Expected: `--- BUILD FAILED: test_brightness ---` with a compiler error along the lines of `cannot open include file: 'brightness.h'`. Every other suite must still pass.

- [ ] **Step 4: Write `src/brightness.h`**

The two table bodies below are the verbatim output of `scripts/gen-gamma-tables.py`. If you regenerate them, the tests will tell you if anything moved.

```cpp
#pragma once
#include <stdint.h>
#include "color.h"

// Perceptual brightness correction for both light sources.
//
// Home Assistant's brightness slider is perceptually linear; LED output is
// linear in drive value. Without a curve between them the bottom third of the
// slider does very little and then jumps.
//
// The curve is the CIE 1931 lightness function, not a power law:
//
//     L* = brightness / 255 * 100
//     Y  = L* / 903.3            for L* <= 8      <- linear segment
//     Y  = ((L* + 16) / 116)^3   for L* > 8
//
// That linear segment is why this reaches brightness 1 at all. With gamma 2.2
// every input below 15 rounds to zero at 8-bit output -- a naive gamma LUT
// would have made the low end worse than the linear scaling it replaced.
//
// Apply this to the BRIGHTNESS SCALAR, never to individual output channels:
// gamma(a * b) != gamma(a) * b, so a per-channel curve distorts every ratio
// between channels. On the white string that ratio is the colour temperature
// (a 2.98:1 mix becomes 10.83:1, so the fixture drifts warm as it dims); on the
// ring it shifts the hue of any mixed colour.
//
// Tables are generated by scripts/gen-gamma-tables.py and verified entry by
// entry against the formula in test/test_brightness. They are checked in
// because the ESP32-H2 is RISC-V with no FPU (pow() is soft-float) and because
// std::pow is not constexpr in C++17.
//
// `static const` rather than `inline constexpr`: the Arduino ESP32 toolchain's
// default C++ standard varies by framework version, and only effects.cpp
// includes this, so the 768 bytes exist once.

// --- gamma8 table begin ---   (ring: 8-bit in -> 8-bit out)
static const uint8_t kGamma8[256] = {
      0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,
      2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,   3,   3,   4,
      4,   4,   4,   4,   4,   5,   5,   5,   5,   5,   6,   6,   6,   6,   6,   7,
      7,   7,   7,   8,   8,   8,   8,   9,   9,   9,  10,  10,  10,  10,  11,  11,
     11,  12,  12,  12,  13,  13,  13,  14,  14,  15,  15,  15,  16,  16,  17,  17,
     17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  24,  24,  25,
     25,  26,  26,  27,  28,  28,  29,  29,  30,  31,  31,  32,  32,  33,  34,  34,
     35,  36,  37,  37,  38,  39,  39,  40,  41,  42,  43,  43,  44,  45,  46,  47,
     47,  48,  49,  50,  51,  52,  53,  54,  54,  55,  56,  57,  58,  59,  60,  61,
     62,  63,  64,  65,  66,  67,  68,  70,  71,  72,  73,  74,  75,  76,  77,  79,
     80,  81,  82,  83,  85,  86,  87,  88,  90,  91,  92,  94,  95,  96,  98,  99,
    100, 102, 103, 105, 106, 108, 109, 110, 112, 113, 115, 116, 118, 120, 121, 123,
    124, 126, 128, 129, 131, 132, 134, 136, 138, 139, 141, 143, 145, 146, 148, 150,
    152, 154, 155, 157, 159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181,
    183, 185, 187, 189, 191, 193, 196, 198, 200, 202, 204, 207, 209, 211, 214, 216,
    218, 220, 223, 225, 228, 230, 232, 235, 237, 240, 242, 245, 247, 250, 252, 255,
};
// --- gamma8 table end ---

// --- gamma12 table begin ---   (white string: 8-bit in -> 12-bit out)
static const uint16_t kGamma12[256] = {
       0,    2,    4,    5,    7,    9,   11,   12,   14,   16,   18,   20,
      21,   23,   25,   27,   28,   30,   32,   34,   36,   37,   39,   41,
      43,   45,   47,   49,   52,   54,   56,   59,   61,   64,   66,   69,
      72,   75,   77,   80,   83,   87,   90,   93,   96,  100,  103,  107,
     111,  115,  118,  122,  126,  131,  135,  139,  144,  148,  153,  157,
     162,  167,  172,  177,  182,  187,  193,  198,  204,  209,  215,  221,
     227,  233,  239,  246,  252,  259,  265,  272,  279,  286,  293,  300,
     308,  315,  323,  330,  338,  346,  354,  362,  371,  379,  388,  396,
     405,  414,  423,  432,  442,  451,  461,  470,  480,  490,  501,  511,
     521,  532,  543,  553,  564,  576,  587,  598,  610,  622,  634,  646,
     658,  670,  683,  695,  708,  721,  734,  748,  761,  775,  788,  802,
     816,  831,  845,  860,  874,  889,  904,  920,  935,  951,  966,  982,
     999, 1015, 1031, 1048, 1065, 1082, 1099, 1116, 1134, 1152, 1170, 1188,
    1206, 1224, 1243, 1262, 1281, 1300, 1320, 1339, 1359, 1379, 1399, 1420,
    1440, 1461, 1482, 1503, 1525, 1546, 1568, 1590, 1612, 1635, 1657, 1680,
    1703, 1726, 1750, 1774, 1797, 1822, 1846, 1870, 1895, 1920, 1945, 1971,
    1996, 2022, 2048, 2074, 2101, 2128, 2155, 2182, 2209, 2237, 2265, 2293,
    2321, 2350, 2378, 2407, 2437, 2466, 2496, 2526, 2556, 2587, 2617, 2648,
    2679, 2711, 2743, 2774, 2807, 2839, 2872, 2905, 2938, 2971, 3005, 3039,
    3073, 3107, 3142, 3177, 3212, 3248, 3283, 3319, 3356, 3392, 3429, 3466,
    3503, 3541, 3578, 3617, 3655, 3694, 3732, 3772, 3811, 3851, 3891, 3931,
    3972, 4012, 4054, 4095,
};
// --- gamma12 table end ---

inline uint8_t gamma8(uint8_t brightness) {
    return kGamma8[brightness];
}

inline uint16_t gamma12(uint8_t brightness) {
    return kGamma12[brightness];
}

// Curve the brightness, then scale the colour by it linearly, so the pixel's
// hue and saturation survive dimming unchanged.
inline CRGB scale_brightness_gamma(CRGB c, uint8_t brightness) {
    return scale_brightness(c, gamma8(brightness));
}

// 12-bit duties for the inner white string's two channels.
struct WhiteMix {
    uint16_t ww;   // 2700 K
    uint16_t cw;   // 6500 K
};

// Curve the level once, then split it linearly between the two strings, which
// is what keeps colour temperature constant as the fixture dims.
//
// `cct`: 0 = fully warm, 255 = fully cool. The two coefficients sum to 255, so
// total output is constant across colour temperature.
//
// Divides by 255 rather than using scale8's `>> 8`: the shift maps a full 255
// mix to 254/255 of the total, so pure warm would never quite reach full duty.
// Same off-by-one that scale_level() in light_state.h works around.
inline WhiteMix white_mix_gamma(uint8_t level, uint8_t cct) {
    const uint32_t total = gamma12(level);
    return {
        uint16_t(total * (255u - cct) / 255u),
        uint16_t(total * cct / 255u),
    };
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `scripts\run-native-tests.bat`

Expected: `--- test_brightness ---` reports `11 Tests 0 Failures 0 Ignored`, and the run ends with `ALL SUITES PASSED`.

- [ ] **Step 6: Verify the generator agrees with the checked-in tables**

Run: `python scripts/gen-gamma-tables.py`

Expected: the two printed blocks match the tables in `src/brightness.h` exactly. If they do not, the tests in Step 5 would already have failed — but check anyway, because a mismatch means the script and the header have diverged.

- [ ] **Step 7: Commit**

```bash
git add scripts/gen-gamma-tables.py src/brightness.h test/test_brightness/test_main.cpp
git commit -m "feat(fw): CIE lightness curve for both light sources"
```

---

## Task 2: 12-bit white PWM

Widens the white string's duty resolution and puts the curve behind it. Task 1 and Task 2 together make the white string correct; the ring is still linear after this task.

Do not split the resolution change from the `fx_static_white` change — an 8-bit value written into a 12-bit duty register lights the fixture at 1/16 brightness, so a commit with only half of this is a broken build.

**Files:**
- Modify: `src/config.h:42-45`
- Modify: `src/led_driver.h:7-8`
- Modify: `src/led_driver.cpp:58-66`
- Modify: `src/effects.cpp:1-4` (include) and `src/effects.cpp:24-30` (`fx_static_white`)

**Interfaces:**
- Consumes: `white_mix_gamma()`, `WhiteMix` from Task 1
- Produces: `led_driver_set_cw(uint16_t)`, `led_driver_set_ww(uint16_t)` — duty 0–4095

- [ ] **Step 1: Widen the PWM resolution in `src/config.h`**

Replace the `── PWM (inner white string) ──` block (currently lines 37–45) with:

```cpp
// ── PWM (inner white string) ──────────────────────────
// The external L-SD8E1 driver is a 380 mA constant-current source; the board
// only gates it. A CC supply's control loop can't track fast switching, so keep
// the PWM slow enough for it to settle but above the flicker threshold.
// Verify on the bench (Task 6.3) and adjust if low-end dimming shudders.
//
// 12-bit rather than 8-bit so a perceptually corrected low end has somewhere to
// live: at 8 bits the CIE curve puts brightness 1..4 at duty 0, i.e. off. The
// frequency is unchanged, so the driver sees the same switching rate it already
// passed Task 6.3 with -- but minimum on-time drops from 3.9 us to 0.24 us, and
// whether the driver responds at all down there is what the bench sweep checks.
// 1 kHz from the 32 MHz XTAL allows up to ~14 bits, so 12 has margin.
#define PWM_FREQ_HZ           1000
#define PWM_RESOLUTION        LEDC_TIMER_12_BIT
#define PWM_DUTY_MAX          4095
#define PWM_CHANNEL_CW        LEDC_CHANNEL_0
#define PWM_CHANNEL_WW        LEDC_CHANNEL_1
```

- [ ] **Step 2: Widen the driver signatures in `src/led_driver.h`**

Replace lines 7–8:

```cpp
void led_driver_set_cw(uint16_t duty);   // 0..PWM_DUTY_MAX
void led_driver_set_ww(uint16_t duty);   // 0..PWM_DUTY_MAX
```

- [ ] **Step 3: Widen the driver implementations in `src/led_driver.cpp`**

Replace `led_driver_set_cw` and `led_driver_set_ww` (currently lines 58–66):

```cpp
void led_driver_set_cw(uint16_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW);
}

void led_driver_set_ww(uint16_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW);
}
```

`ledc_set_duty` already takes a `uint32_t`, so nothing else in the file changes. `led_driver_off()` passes `0` and needs no edit.

- [ ] **Step 4: Include the new header in `src/effects.cpp`**

Change the include block at the top of the file (lines 1–4) to:

```cpp
#include "effects.h"
#include "brightness.h"
#include "led_driver.h"
#include "identify.h"
#include <string.h>
```

- [ ] **Step 5: Put the curve behind `fx_static_white`**

Replace `fx_static_white` (currently lines 24–30) with:

```cpp
static void fx_static_white(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    // Colour temperature rides on `hue`: 0 = fully warm, 255 = fully cool.
    // The curve applies to the level, and the split that follows is linear --
    // curving each leg separately would drag the colour temperature warm as the
    // fixture dims. See brightness.h.
    const uint8_t level = on ? p.brightness : 0;
    ring_off(leds);
    const WhiteMix w = white_mix_gamma(level, p.hue);
    led_driver_set_ww(w.ww);
    led_driver_set_cw(w.cw);
}
```

- [ ] **Step 6: Verify the firmware builds**

Run from **PowerShell**: `pio run -e esp32h2`

Expected: `SUCCESS`. Warnings about unused variables are acceptable; any error mentioning `led_driver_set_cw` or a narrowing conversion is not.

- [ ] **Step 7: Verify the host tests still pass**

Run: `scripts\run-native-tests.bat`

Expected: `ALL SUITES PASSED`. Nothing in this task touches host-testable code, so a failure here means something was edited by mistake.

- [ ] **Step 8: Commit**

```bash
git add src/config.h src/led_driver.h src/led_driver.cpp src/effects.cpp
git commit -m "feat(fw): 12-bit white PWM with the curve on the level"
```

---

## Task 3: The ring effects

Six mechanical edits. Each replaces a linear brightness multiply with the curved one. `fx_warm_gradient` is deliberately left for Task 4 because it needs restructuring rather than a rename.

**Files:**
- Modify: `src/effects.cpp` — `fx_static_color`, `fx_color_gradient`, `fx_breathing`, `fx_color_cycle`, `fx_chase`, `fx_nightlight`

**Interfaces:**
- Consumes: `scale_brightness_gamma()`, `gamma8()` from Task 1
- Produces: nothing new

- [ ] **Step 1: Curve the five colour effects**

In `src/effects.cpp`, replace `scale_brightness(` with `scale_brightness_gamma(` in exactly these five functions. The composed brightness argument stays as it is — that is the point, the curve goes on the *final* value after each effect has finished modulating it.

`fx_static_color`:
```cpp
    const CRGB c = on ? scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), p.brightness)
                      : CRGB{};
```

`fx_color_gradient`:
```cpp
        leds[i] = scale_brightness_gamma(hsv_to_rgb(hue, p.sat, 255), p.brightness);
```

`fx_breathing` — note the triangle wave stays inside the argument, so the curve sees `brightness × envelope`:
```cpp
    const CRGB c = scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), scale8(p.brightness, half));
```

`fx_color_cycle`:
```cpp
    const CRGB     c      = scale_brightness_gamma(hsv_to_rgb(hue, p.sat, 255), p.brightness);
```

`fx_chase`:
```cpp
    const CRGB     c      = scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), p.brightness);
```

- [ ] **Step 2: Curve the nightlight**

`warm_white()` mixes fixed RGB ratios, so the curve goes on its argument rather than its result — curving the result would distort the mix. Replace `fx_nightlight` (currently lines 95–100):

```cpp
static void fx_nightlight(uint32_t, const EffectParams& p, CRGB* leds, bool on) {
    // These pixels have no white die, so mix a warm white from the RGB dice.
    // The curve goes on the level, not on the mixed colour -- warm_white()
    // scales fixed ratios, and curving its output would shift them.
    const CRGB c = on ? warm_white(gamma8(p.brightness)) : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}
```

- [ ] **Step 3: Verify no linear multiplies were missed**

Run: `git diff src/effects.cpp`

Expected: exactly six changed call sites. Then run:

```bash
grep -n "scale_brightness(" src/effects.cpp
```

Expected: **no matches.** Every remaining brightness multiply in the file should be `scale_brightness_gamma`, `warm_white(gamma8(...))`, or inside `fx_warm_gradient`, which Task 4 handles.

- [ ] **Step 4: Verify the firmware builds**

Run from **PowerShell**: `pio run -e esp32h2`

Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/effects.cpp
git commit -m "feat(fw): curve the ring effects' brightness"
```

---

## Task 4: Rework fx_warm_gradient

Kept separate because it is the only change that alters what the fixture renders at full brightness rather than only how it dims — a reviewer could reasonably accept Task 3 and reject this.

The current version crossfades a warm colour to blue across the ring with brightness folded into each leg (`src/effects.cpp:45-47`). Curving each leg separately would distort the crossfade exactly as per-leg curving would distort colour temperature. Blending at full intensity and curving once is the correct shape.

**Files:**
- Modify: `src/effects.cpp` — `fx_warm_gradient` (currently lines 39–50)

**Interfaces:**
- Consumes: `scale_brightness_gamma()` from Task 1, `blend()` from `src/color.h:48`
- Produces: nothing new

- [ ] **Step 1: Rewrite the effect**

Replace `fx_warm_gradient` in full:

```cpp
static void fx_warm_gradient(uint32_t elapsed_ms, const EffectParams& p, CRGB* leds, bool on) {
    if (!on) { ring_off(leds); white_off(); return; }
    const uint32_t period = speed_to_period_ms(p.speed);
    const uint8_t  offset = (elapsed_ms % period) * 256 / period;
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        const uint8_t pos = (i * 256 / RING_NUM_LEDS + offset) & 0xFF;
        // Blend the two ends at full intensity and curve once. Folding
        // brightness into each end and curving them separately would distort
        // the crossfade the same way it would distort colour temperature.
        const CRGB c = blend(CRGB{200, 100, 0}, CRGB{0, 0, 255}, pos);
        leds[i] = scale_brightness_gamma(c, p.brightness);
    }
    white_off();
}
```

- [ ] **Step 2: Verify `blend()` now has a caller**

Run:

```bash
grep -rn "blend(" src/
```

Expected: the definition at `src/color.h:48` and exactly one call site in `src/effects.cpp`. Before this task `blend()` was dead code.

- [ ] **Step 3: Verify the firmware builds**

Run from **PowerShell**: `pio run -e esp32h2`

Expected: `SUCCESS`.

- [ ] **Step 4: Verify the host tests still pass**

Run: `scripts\run-native-tests.bat`

Expected: `ALL SUITES PASSED`.

- [ ] **Step 5: Commit**

```bash
git add src/effects.cpp
git commit -m "fix(fw): blend warm_gradient's ends before curving, not after"
```

---

## Task 5: Bench verification

Everything so far is unverified on hardware. This task is USB-side only — it does **not** need the fixture in the ceiling and does not depend on board plan Task 6.4.

The one thing that cannot be settled at the host-test level is §5 of the design doc: raising resolution shortens minimum on-time 16×, from 3.9 µs to 0.24 µs. If the L-SD8E1's control loop cannot respond that fast, the bottom of the 12-bit range produces no light and brightness 1 is still off — the original bug, relocated. Step 3 is what finds out.

**Files:**
- Modify: `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`
- Possibly modify: `src/brightness.h` (only if Step 3 finds a hardware floor)

- [ ] **Step 1: Flash the firmware**

Run from **PowerShell**: `pio run -e esp32h2 --target upload`

Expected: upload succeeds, and `pio device monitor` shows `boot ok` followed by `LED driver init ok`.

A panic at `ledc_timer_config` here means 12-bit at 1 kHz was rejected — `ESP_ERROR_CHECK` in `src/led_driver.cpp:32` makes that loud rather than silent. If it happens, drop to `LEDC_TIMER_10_BIT` with `PWM_DUTY_MAX` 1023 and regenerate `kGamma12` by passing `1023` to `emit()` in the generator script.

- [ ] **Step 2: Sweep the white string for evenness**

From Home Assistant, set the light to a plain colour temperature and step brightness through 1, 5, 10, 25, 50, 100, 150, 200, 255.

Expected: a visibly even ramp, with no jump out of the low end and no region where several steps look identical.

- [ ] **Step 3: Check the bottom of the range specifically**

Set brightness to 1, then 2, then 3, then 4, then 5.

Expected: brightness 1 produces **visible dim light**, and each step is brighter than the last.

If brightness 1–N produce no light at all, the driver cannot respond to those on-times. Record the measured N, then change `white_mix_gamma` in `src/brightness.h` to lift non-zero results to that measured minimum duty, and add a test to `test/test_brightness` asserting it. The `max(1, …)` floor is provisional by design — this is the step that would replace it with a measured constant.

- [ ] **Step 4: Confirm colour temperature holds while dimming**

Set 2700 K and sweep brightness 255 → 20. Repeat at 4000 K and 6500 K.

Expected: the light gets dimmer without getting warmer or cooler. Any drift means the split is being curved per-leg somewhere.

- [ ] **Step 5: Check the ring effects**

Select each of the eight effects from Home Assistant's effect dropdown and dim each one.

Expected: all eight dim smoothly. Pay particular attention to `warm_gradient` **at full brightness** — Task 4 changed what it renders, not just how it dims, so compare it against the pre-change look and confirm it is the same or better.

- [ ] **Step 6: Record the results and close out item 6**

In `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`:

1. Change the `## 6. Gamma and the low end` heading to `## 6. Gamma and the low end — DONE, bench-verified 2026-08-17`
2. Replace its body with what was actually built and what the bench showed, following the style of items 4 and 5: the finding that a naive gamma LUT would have regressed the low end, the 8→12-bit change, the measured behaviour at brightness 1, and whether Step 3 found a hardware floor.
3. Update the `## Suggested order` line at the end of the file — item 6 moves into the struck-through group with 4 and 5, leaving **3** as the recommended next item.
4. Update the status line at the top of the file to include item 6.

- [ ] **Step 7: Commit**

```bash
git add docs/superpowers/plans/2026-08-17-home-assistant-polish.md src/brightness.h
git commit -m "docs: record the bench verification of item 6"
```

- [ ] **Step 8: Finish the branch**

Use the `superpowers:finishing-a-development-branch` skill to decide how to integrate `feat/gamma-and-low-end`.

---

## Notes for the implementer

- **One deliberate deviation from the design doc.** §3.3 of the spec shows the CW/WW split written
  inline inside `fx_static_white`. This plan extracts it into `white_mix_gamma()` in `brightness.h`
  instead. The maths is identical; the reason is that §4 of the spec requires a test asserting the
  split holds its ratio, and `effects.cpp` includes `led_driver.h` and so cannot be compiled by the
  host test runner. The split has to be pure to be testable.
- **`brightness.h` cannot include `config.h`,** so `PWM_DUTY_MAX` is not available to it and the
  4095 in `kGamma12` is written out. Do not try to DRY those two together — `config.h` includes
  `driver/ledc.h`, which would drag ESP-IDF into the host build and break every test in
  `test/test_brightness`. If the resolution ever changes, both move together by hand.
- **Do not add dithering.** The ring's bottom four steps are coarse because its output is 8-bit and fixed by the LED protocol. That is recorded as out of scope in the design doc.
- **Do not change `PWM_FREQ_HZ`.** The 1 kHz figure is what Task 6.3 validated against the real driver. Only the resolution changes.
- **Do not move the curve to the driver boundary**, however much tidier one chokepoint looks. `src/brightness.h`'s header comment and §1.2 of the design doc explain why; `test_scale_brightness_gamma_preserves_channel_ratios` and `test_white_mix_holds_the_ratio` will fail if you do.
- **`scale8` maps 255 → 254.** This is pre-existing (`src/color.h:11`) and why `test_scale_brightness_gamma_endpoints` expects 254 rather than 255. Do not "fix" it here — `light_state.h:85` already works around it where it matters, and changing it is a separate change with its own blast radius.

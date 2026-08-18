# Two Light Entities From One Fixture — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Home Assistant two independent light entities under one device — a CW/WW Downlight on Zigbee endpoint 1 and an RGB Accent Ring on endpoint 2 — so the fixture's white downlight and coloured accent ring can run at the same time.

**Architecture:** The combined `LightState` splits into `DownlightState` and `RingState` inside one `FixtureState` container. `WHITE_SAT_THRESHOLD` and `rgb_to_cct()` are deleted rather than reworked: "is this command white or colour?" stops being a question once each source has its own endpoint. Effects become the ring's alone and the list shrinks from eight to six, because `fx_static_white` *is* the Downlight entity once that entity exists.

**Tech Stack:** C++17, PlatformIO, Arduino ESP32 v3.x on ESP32-H2, Arduino Zigbee library, Unity (host tests via MSVC), Node (converter tests), zigbee-herdsman-converters modernExtend.

**Design doc:** `docs/superpowers/specs/2026-08-17-two-endpoint-split-design.md`

## Global Constraints

- **Branch:** `feat/two-endpoints` (already created; the design doc is already committed there).
- **Endpoint 1 = Downlight, endpoint 2 = Accent Ring.** Not the reverse — existing switch bindings target endpoint 1 and must land on the main light, and the OTA client and Basic strings already live there.
- **Downlight capability:** `ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP` only, range `CCT_MIRED_COOL`–`CCT_MIRED_WARM` (154–370 mired). **Ring capability:** `ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION | ZIGBEE_COLOR_CAPABILITY_X_Y`, no colour temperature.
- **Effect list is exactly six, renumbered from 0:** `warm_gradient`, `color_gradient`, `breathing`, `color_cycle`, `chase`, `nightlight`.
- **`NVS_FMT_VER_CURRENT` goes 1 → 2.** This is the entire migration; `scene_store_init()` already reseeds on mismatch.
- **`src/light_state.h` must stay free of ESP-IDF headers** so `scripts\run-native-tests.bat` can compile it on the host. It may include `<stdint.h>`, `color.h` and `effect_params.h` only.
- **Do not change `src/brightness.h`, `src/color.h`, `src/pixel_encode.h`, `src/led_driver.*` or `src/scene_store.*`** beyond the single `NVS_FMT_VER_CURRENT` bump in `src/config.h`.
- **Windows:** run `pio` from **PowerShell or cmd, not Git Bash** — the toolchain installer aborts under MSys. Host tests run from either.
- The firmware must build green at the end of **every** task. No task may leave `pio run -e esp32h2` failing.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `src/light_state.h` | rewrite | Both state structs, both resolves. Pure, host-tested |
| `test/test_light_state/test_main.cpp` | rewrite | Host tests for the above |
| `src/effect_params.h` | modify | Six-entry `EffectType` and `kDefaultParams` |
| `src/effects.h` / `.cpp` | modify | Six effects + `fx_ring_solid`; loses `led_driver.h` |
| `src/config.h` | modify | Two endpoint ids, NVS version bump |
| `src/zigbee_light.h` / `.cpp` | modify | Two endpoints, two callback sets, class hierarchy |
| `src/main.cpp` | modify | Render both sources every frame |
| `z2m/lumary-brain-revA.js` | modify | Two `m.light()` extends, endpoint map |
| `z2m/test/converter.test.mjs` | modify | Two-endpoint coverage |
| `README.md` | modify | Re-interview requirement, both bindings, breaking changes |

---

## Task 1: The two-entity state model

Purely additive. The new types go in alongside the old ones, so the firmware still builds and every existing test still passes. Task 2 does the switchover and deletes the old model.

**Files:**
- Modify: `src/light_state.h` (append a new section; change nothing existing)
- Test: `test/test_light_state/test_main.cpp` (append tests; change nothing existing)

**Interfaces:**
- Consumes: `CRGB` and `rgb_to_hsv()` from `src/color.h`; `EffectParams`, `EffectType` from `src/effect_params.h`; existing `LightMode`, `LIGHT_EFFECT_NONE`, `mireds_to_cct()`, `scale_level()` from `src/light_state.h`
- Produces, for Task 2:
  - `struct DownlightState { bool on; uint8_t level; uint8_t cct; }`
  - `struct RingState { bool on; uint8_t level, hue, sat, scene; LightMode mode; }`
  - `struct FixtureState { DownlightState down; RingState ring; }`
  - `void downlight_state_init(DownlightState*)`, `void ring_state_init(RingState*)`, `void fixture_state_init(FixtureState*)`
  - `uint8_t downlight_level(const DownlightState*)`
  - `void downlight_set_cct(DownlightState*, uint16_t mireds)`
  - `void ring_set_color(RingState*, CRGB)`
  - `void ring_set_scene(RingState*, uint8_t index, uint8_t scene_count)`
  - `void ring_clear_scene(RingState*)`
  - `uint8_t ring_effect_value(const RingState*)`
  - `void ring_next_scene(RingState*, uint8_t scene_count)` / `ring_prev_scene(...)`
  - `EffectParams ring_state_resolve(const RingState*, const EffectParams* scene)`

- [ ] **Step 1: Write the failing tests**

Append to `test/test_light_state/test_main.cpp`, immediately **before** the `int main(` line:

```cpp
// ══ Two-entity model (item 9) ═════════════════════════════════════════════
// Each source is its own endpoint and its own Home Assistant entity, so each
// state carries only the fields its own source has.

// ── the downlight ─────────────────────────────────────────────────────────

void test_downlight_starts_off_at_full_level(void) {
    DownlightState d;
    downlight_state_init(&d);
    TEST_ASSERT_FALSE(d.on);
    TEST_ASSERT_EQUAL_UINT8(255, d.level);
}

void test_downlight_off_renders_dark_whatever_the_level(void) {
    DownlightState d;
    downlight_state_init(&d);
    d.level = 200;
    d.on    = false;
    TEST_ASSERT_EQUAL_UINT8(0, downlight_level(&d));
}

void test_downlight_on_passes_the_level_through(void) {
    DownlightState d;
    downlight_state_init(&d);
    d.level = 200;
    d.on    = true;
    TEST_ASSERT_EQUAL_UINT8(200, downlight_level(&d));
}

// The downlight advertises colour-temperature capability only, so it receives
// mireds directly and never has to infer warmth from an RGB colour.
void test_downlight_maps_mireds_to_the_cw_ww_mix(void) {
    DownlightState d;
    downlight_state_init(&d);
    downlight_set_cct(&d, CCT_MIRED_WARM);
    TEST_ASSERT_EQUAL_UINT8(0, d.cct);
    downlight_set_cct(&d, CCT_MIRED_COOL);
    TEST_ASSERT_EQUAL_UINT8(255, d.cct);
}

// ── the ring: mode transitions ────────────────────────────────────────────

void test_ring_starts_in_scene_mode(void) {
    RingState r;
    ring_state_init(&r);
    TEST_ASSERT_EQUAL(MODE_SCENE, r.mode);
    TEST_ASSERT_EQUAL_UINT8(0, r.scene);
}

void test_ring_colour_command_switches_to_colour_mode(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_color(&r, CRGB{255, 0, 0});
    TEST_ASSERT_EQUAL(MODE_COLOR, r.mode);
    TEST_ASSERT_EQUAL_UINT8(0,   r.hue);
    TEST_ASSERT_EQUAL_UINT8(255, r.sat);
}

void test_ring_selecting_a_scene_switches_back(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_color(&r, CRGB{255, 0, 0});
    ring_set_scene(&r, 3, 6);
    TEST_ASSERT_EQUAL(MODE_SCENE, r.mode);
    TEST_ASSERT_EQUAL_UINT8(3, r.scene);
}

// An out-of-range index arrives over the air, so it is rejected outright
// rather than clamped -- clamping would strand the ring on a scene nobody
// asked for.
void test_ring_out_of_range_scene_is_ignored(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 2, 6);
    ring_set_scene(&r, 99, 6);
    TEST_ASSERT_EQUAL_UINT8(2, r.scene);
}

void test_ring_out_of_range_scene_does_not_leave_colour_mode(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_color(&r, CRGB{0, 255, 0});
    ring_set_scene(&r, 99, 6);
    TEST_ASSERT_EQUAL(MODE_COLOR, r.mode);
}

void test_ring_zero_scene_count_is_ignored(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 0, 0);
    TEST_ASSERT_EQUAL(MODE_SCENE, r.mode);
}

void test_ring_clearing_the_scene_leaves_effect_mode(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 4, 6);
    ring_clear_scene(&r);
    TEST_ASSERT_EQUAL(MODE_COLOR, r.mode);
    TEST_ASSERT_EQUAL_UINT8(4, r.scene);   // index remembered for the power cycle
}

// ── the ring: what the coordinator is told ────────────────────────────────

void test_ring_scene_mode_reports_the_running_effect(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 5, 6);
    TEST_ASSERT_EQUAL_UINT8(5, ring_effect_value(&r));
}

void test_ring_colour_mode_reports_no_effect(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_color(&r, CRGB{0, 0, 255});
    TEST_ASSERT_EQUAL_UINT8(LIGHT_EFFECT_NONE, ring_effect_value(&r));
}

// ── the ring: scene cycling ───────────────────────────────────────────────

void test_ring_next_scene_advances_and_wraps(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 5, 6);
    ring_next_scene(&r, 6);
    TEST_ASSERT_EQUAL_UINT8(0, r.scene);
}

void test_ring_prev_scene_wraps_backwards(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 0, 6);
    ring_prev_scene(&r, 6);
    TEST_ASSERT_EQUAL_UINT8(5, r.scene);
}

// ── the ring: resolve ─────────────────────────────────────────────────────

void test_ring_scene_mode_runs_the_stored_effect(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 1, 6);
    const EffectParams stored = {EffectType(1), 90, 200, 180, 60};
    const EffectParams p      = ring_state_resolve(&r, &stored);
    TEST_ASSERT_EQUAL(EffectType(1), p.type);
    TEST_ASSERT_EQUAL_UINT8(90,  p.hue);
    TEST_ASSERT_EQUAL_UINT8(60,  p.speed);
}

void test_ring_level_dims_the_stored_scene(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 0, 6);
    r.level = 128;
    const EffectParams stored = {EffectType(0), 0, 255, 200, 60};
    const EffectParams p      = ring_state_resolve(&r, &stored);
    TEST_ASSERT_EQUAL_UINT8(scale_level(200, 128), p.brightness);
}

void test_ring_full_level_leaves_the_scene_brightness_intact(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 0, 6);
    r.level = 255;
    const EffectParams stored = {EffectType(0), 0, 255, 200, 60};
    TEST_ASSERT_EQUAL_UINT8(200, ring_state_resolve(&r, &stored).brightness);
}

void test_ring_colour_mode_resolves_to_the_held_colour(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_color(&r, CRGB{255, 0, 0});
    r.level = 120;
    const EffectParams stored = {EffectType(0), 0, 0, 255, 0};
    const EffectParams p      = ring_state_resolve(&r, &stored);
    TEST_ASSERT_EQUAL_UINT8(0,   p.hue);
    TEST_ASSERT_EQUAL_UINT8(255, p.sat);
    TEST_ASSERT_EQUAL_UINT8(120, p.brightness);
}

// `type` is unread in colour mode -- the render loop calls fx_ring_solid
// without consulting it -- but EffectParams is returned by value, and reading
// an indeterminate enum later is undefined behaviour. It must be a valid index.
void test_ring_colour_mode_still_returns_a_valid_effect_index(void) {
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, 4, 6);
    ring_clear_scene(&r);
    const EffectParams stored = {EffectType(0), 0, 0, 255, 0};
    const EffectParams p      = ring_state_resolve(&r, &stored);
    TEST_ASSERT_TRUE(uint8_t(p.type) < 6);
}

// ── the container ─────────────────────────────────────────────────────────

void test_fixture_state_init_sets_both_halves(void) {
    FixtureState f;
    fixture_state_init(&f);
    TEST_ASSERT_FALSE(f.down.on);
    TEST_ASSERT_FALSE(f.ring.on);
    TEST_ASSERT_EQUAL(MODE_SCENE, f.ring.mode);
}

// The whole point of item 9: both sources lit at once, independently.
void test_both_sources_can_be_on_at_once(void) {
    FixtureState f;
    fixture_state_init(&f);
    f.down.on = true;  f.down.level = 255;
    f.ring.on = true;  f.ring.level = 128;
    ring_set_color(&f.ring, CRGB{255, 0, 0});
    downlight_set_cct(&f.down, CCT_MIRED_WARM);

    TEST_ASSERT_EQUAL_UINT8(255, downlight_level(&f.down));
    const EffectParams p = ring_state_resolve(&f.ring, &kDefaultParams[0]);
    TEST_ASSERT_EQUAL_UINT8(255, p.sat);
    TEST_ASSERT_EQUAL_UINT8(128, p.brightness);
}
```

Then add these to `main()`, after the existing `RUN_TEST` lines:

```cpp
    RUN_TEST(test_downlight_starts_off_at_full_level);
    RUN_TEST(test_downlight_off_renders_dark_whatever_the_level);
    RUN_TEST(test_downlight_on_passes_the_level_through);
    RUN_TEST(test_downlight_maps_mireds_to_the_cw_ww_mix);
    RUN_TEST(test_ring_starts_in_scene_mode);
    RUN_TEST(test_ring_colour_command_switches_to_colour_mode);
    RUN_TEST(test_ring_selecting_a_scene_switches_back);
    RUN_TEST(test_ring_out_of_range_scene_is_ignored);
    RUN_TEST(test_ring_out_of_range_scene_does_not_leave_colour_mode);
    RUN_TEST(test_ring_zero_scene_count_is_ignored);
    RUN_TEST(test_ring_clearing_the_scene_leaves_effect_mode);
    RUN_TEST(test_ring_scene_mode_reports_the_running_effect);
    RUN_TEST(test_ring_colour_mode_reports_no_effect);
    RUN_TEST(test_ring_next_scene_advances_and_wraps);
    RUN_TEST(test_ring_prev_scene_wraps_backwards);
    RUN_TEST(test_ring_scene_mode_runs_the_stored_effect);
    RUN_TEST(test_ring_level_dims_the_stored_scene);
    RUN_TEST(test_ring_full_level_leaves_the_scene_brightness_intact);
    RUN_TEST(test_ring_colour_mode_resolves_to_the_held_colour);
    RUN_TEST(test_ring_colour_mode_still_returns_a_valid_effect_index);
    RUN_TEST(test_fixture_state_init_sets_both_halves);
    RUN_TEST(test_both_sources_can_be_on_at_once);
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `scripts\run-native-tests.bat`

Expected: `--- BUILD FAILED: test_light_state ---` with errors naming `DownlightState`, `RingState` and `FixtureState` as undeclared identifiers. Every other suite must still pass.

- [ ] **Step 3: Add the new model to `src/light_state.h`**

Append at the **end** of the file, after `light_state_resolve()`. Change nothing above it.

```cpp
// ══ Two-entity model (item 9) ═════════════════════════════════════════════
// The fixture's two light sources become two Zigbee endpoints and two Home
// Assistant entities, so they can run at once -- white downlight plus coloured
// accent ring, which the single-entity model could not express at all.
//
// Two purpose-built structs rather than two copies of one combined struct: the
// downlight has no hue and the ring has no colour temperature, and a type that
// carries fields its source does not have cannot tell a reader which are live.
//
// Everything the old model needed WHITE_SAT_THRESHOLD for is gone: a command
// arrives AT the ring or AT the downlight, so there is nothing left to infer.

struct DownlightState {
    bool    on;
    uint8_t level;    // Level Control cluster
    uint8_t cct;      // 0 = fully warm (2700K) .. 255 = fully cool (6500K)
};

struct RingState {
    bool      on;
    uint8_t   level;
    uint8_t   hue;
    uint8_t   sat;
    uint8_t   scene;  // index into the scene table
    LightMode mode;   // MODE_SCENE = running an effect, MODE_COLOR = solid colour
};

struct FixtureState {
    DownlightState down;
    RingState      ring;
};

inline void downlight_state_init(DownlightState* s) {
    s->on    = false;
    s->level = 255;
    s->cct   = 128;
}

inline void ring_state_init(RingState* s) {
    s->on    = false;
    s->level = 255;
    s->hue   = 0;
    s->sat   = 0;
    s->scene = 0;
    s->mode  = MODE_SCENE;
}

inline void fixture_state_init(FixtureState* s) {
    downlight_state_init(&s->down);
    ring_state_init(&s->ring);
}

// The downlight has no modes: it is on at a level, or it is off. The render
// loop hands this straight to white_mix_gamma() along with `cct`.
inline uint8_t downlight_level(const DownlightState* s) {
    return s->on ? s->level : 0;
}

// The downlight endpoint advertises colour-temperature capability only, so it
// always receives mireds -- there is no RGB fallback to infer warmth from.
inline void downlight_set_cct(DownlightState* s, uint16_t mireds) {
    s->cct = mireds_to_cct(mireds);
}

inline void ring_set_color(RingState* s, CRGB c) {
    const HSV h = rgb_to_hsv(c);
    s->hue  = h.h;
    s->sat  = h.s;
    s->mode = MODE_COLOR;
}

// Validated here rather than at the Zigbee adapter: the index arrives over the
// air. An out-of-range value is ignored outright, leaving both the scene and
// the mode untouched -- clamping would strand the ring on a scene nobody asked
// for.
inline void ring_set_scene(RingState* s, uint8_t index, uint8_t scene_count) {
    if (scene_count == 0 || index >= scene_count) return;
    s->scene = index;
    s->mode  = MODE_SCENE;
}

// Leaves effect mode without disturbing the colour, which is what the ring
// carries on showing. The firmware side of picking "none" in the dropdown.
inline void ring_clear_scene(RingState* s) {
    s->mode = MODE_COLOR;
}

inline uint8_t ring_effect_value(const RingState* s) {
    return s->mode == MODE_SCENE ? s->scene : LIGHT_EFFECT_NONE;
}

inline void ring_next_scene(RingState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    ring_set_scene(s, uint8_t((s->scene + 1) % scene_count), scene_count);
}

inline void ring_prev_scene(RingState* s, uint8_t scene_count) {
    if (scene_count == 0) return;
    ring_set_scene(s, uint8_t((s->scene + scene_count - 1) % scene_count), scene_count);
}

// Combines the live ring state with the stored parameters of the active scene.
// Unlike the old combined resolve there is no white branch -- the downlight is
// a separate entity that never passes through here.
inline EffectParams ring_state_resolve(const RingState* s, const EffectParams* scene) {
    EffectParams p;
    if (s->mode == MODE_COLOR) {
        // `type` is unread in this branch: the render loop checks `mode` and
        // calls fx_ring_solid without consulting it. It must still hold a valid
        // index -- EffectParams is returned by value, and reading an
        // indeterminate enum later is undefined behaviour. `scene` is always
        // in range, ring_set_scene having rejected anything else.
        p.type       = EffectType(s->scene);
        p.hue        = s->hue;
        p.sat        = s->sat;
        p.brightness = s->level;
        p.speed      = 0;
    } else {
        p            = *scene;
        p.brightness = scale_level(scene->brightness, s->level);
    }
    return p;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `scripts\run-native-tests.bat`

Expected: `--- test_light_state ---` reports `57 Tests 0 Failures 0 Ignored`, and the run ends with `ALL SUITES PASSED`.

- [ ] **Step 5: Verify the firmware still builds**

Run from **PowerShell**: `pio run -e esp32h2`

Expected: `SUCCESS`. This task is additive, so nothing should have broken.

- [ ] **Step 6: Commit**

```bash
git add src/light_state.h test/test_light_state/test_main.cpp
git commit -m "feat(fw): two-entity state model, alongside the old one"
```

---

## Task 2: The switchover

The big one, and unavoidably atomic: a type migration cannot be split across commits without breaking the build. Every piece is given verbatim below.

At the end of this task the firmware has two endpoints, the old combined model is gone, and the effect list is six long.

**Files:**
- Modify: `src/light_state.h` (delete the old model)
- Modify: `src/effect_params.h` (six entries)
- Modify: `src/effects.h`, `src/effects.cpp`
- Modify: `src/config.h` (endpoint ids, NVS version)
- Modify: `src/zigbee_light.h`, `src/zigbee_light.cpp`
- Modify: `src/main.cpp`
- Test: `test/test_light_state/test_main.cpp` (replace wholesale)

**Interfaces:**
- Consumes: everything Task 1 produced; `white_mix_gamma(uint8_t level, uint8_t cct) -> WhiteMix{uint16_t ww, cw}` and `scale_brightness_gamma(CRGB, uint8_t)` from `src/brightness.h`
- Produces: `const FixtureState* zigbee_light_state()`; `DOWNLIGHT_ENDPOINT` 1 and `RING_ENDPOINT` 2

- [ ] **Step 1: Shrink the effect list in `src/effect_params.h`**

Replace the `EffectType` enum and `kDefaultParams` (leave `EffectParams` itself untouched):

```cpp
// Effects belong to the ring. `static_white` used to be index 0; it is now the
// Downlight entity on endpoint 1, not an effect. `static_color` used to be
// index 1; it is now `effect: none` with a colour set, rendered by
// fx_ring_solid, which is deliberately not in this table.
enum EffectType : uint8_t {
    EFFECT_WARM_GRADIENT  = 0,
    EFFECT_COLOR_GRADIENT = 1,
    EFFECT_BREATHING      = 2,
    EFFECT_COLOR_CYCLE    = 3,
    EFFECT_CHASE          = 4,
    EFFECT_NIGHTLIGHT     = 5,
    EFFECT_COUNT          = 6
};

struct EffectParams {
    EffectType type;
    uint8_t    hue;
    uint8_t    sat;
    uint8_t    brightness;
    uint8_t    speed;
};

static const EffectParams kDefaultParams[EFFECT_COUNT] = {
    {EFFECT_WARM_GRADIENT,  8, 220, 200,  60},
    {EFFECT_COLOR_GRADIENT, 0, 255, 200,  60},
    {EFFECT_BREATHING,      0, 255, 255,  80},
    {EFFECT_COLOR_CYCLE,    0, 255, 200, 100},
    {EFFECT_CHASE,          0, 255, 255, 120},
    {EFFECT_NIGHTLIGHT,     8, 180,  50,   0},
};
```

- [ ] **Step 2: Declare `fx_ring_solid` in `src/effects.h`**

Add below the `fx_identify` declaration:

```cpp
// The ring showing a plain colour rather than running an effect -- what
// MODE_COLOR renders. Deliberately NOT in kEffects, for the same reason
// fx_identify is not: that table is positionally indexed by both Home
// Assistant's effect_list and the NVS scene store, and "solid colour" is
// `effect: none`, not a selectable effect.
void fx_ring_solid(const EffectParams& p, CRGB* leds, bool light_on);
```

- [ ] **Step 3: Rework `src/effects.cpp`**

Four changes:

1. Delete `#include "led_driver.h"` from the include block. The file no longer touches the white string at all, so it becomes pure buffer-filling with no hardware dependency.
2. Delete the `white_off()` helper and **all seven calls to it**.
3. Delete `fx_static_white` entirely.
4. Rename `fx_static_color` to `fx_ring_solid`, drop its `static` and its unused first parameter, and remove it from `kEffects`.

The new `fx_ring_solid`:

```cpp
void fx_ring_solid(const EffectParams& p, CRGB* leds, bool light_on) {
    const CRGB c = light_on ? scale_brightness_gamma(hsv_to_rgb(p.hue, p.sat, 255), p.brightness)
                            : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
}
```

The new table, six entries, order matching `EffectType`:

```cpp
const Effect kEffects[EFFECT_COUNT] = {
    {"Warm Gradient",  fx_warm_gradient},
    {"Color Gradient", fx_color_gradient},
    {"Breathing",      fx_breathing},
    {"Color Cycle",    fx_color_cycle},
    {"Chase",          fx_chase},
    {"Nightlight",     fx_nightlight},
};
```

Also update the file's header comment, which currently claims each effect sets both sources:

```cpp
// The outer RGB ring's effects. Each fills the `leds` buffer and nothing else:
// the inner CW/WW white string is a separate Zigbee endpoint and a separate
// Home Assistant entity, rendered directly by the render loop, so no effect
// touches it. That is why this file has no hardware dependency.
```

- [ ] **Step 4: Update `src/config.h`**

Replace `#define LIGHT_ENDPOINT 1` with:

```cpp
// Two endpoints, two Home Assistant light entities. The downlight keeps
// endpoint 1: existing switch bindings target it and should land on the main
// light, and the OTA client and Basic strings already live there.
#define DOWNLIGHT_ENDPOINT    1
#define RING_ENDPOINT         2
```

And bump the NVS schema version, which is the entire scene migration:

```cpp
#define NVS_FMT_VER_CURRENT   2   // 1 -> 2: effect indices renumbered, 8 -> 6
```

- [ ] **Step 5: Update the render loop in `src/main.cpp`**

Change the state pointer type and the render block. Replace the `#else` branch of the `BENCH_DEMO_MODE` block (currently reading `const LightState* s = zigbee_light_state();` through the scene reload) with:

```cpp
    const FixtureState* s = zigbee_light_state();

    // Scene params live in NVS; only re-read them when the scene actually
    // changes, and restart the animation clock so effects begin from frame 0.
    if (s->ring.scene != shown_scene) {
        shown_scene  = s->ring.scene;
        effect_start = now;
        scene_store_load(shown_scene, &scene);
    }

    EffectParams p = ring_state_resolve(&s->ring, &scene);
    const bool on  = s->ring.on;
```

Replace the NVS corruption guard. It currently falls back to `EFFECT_STATIC_WHITE`, which no longer exists:

```cpp
    if (p.type >= EFFECT_COUNT) p.type = EFFECT_WARM_GRADIENT;   // NVS corruption guard
```

Replace the render block at the end of `loop()`:

```cpp
    if (identifying) {
        // Identify is a whole-fixture "which can is this": the ring blinks blue
        // and the downlight goes dark for the duration, because a lit downlight
        // washes the blue out. Neither state is touched, so the next frame
        // after the deadline resumes both by itself.
        fx_identify(now, leds);
        led_driver_set_cw(0);
        led_driver_set_ww(0);
    } else {
#if BENCH_DEMO_MODE
        kEffects[p.type].fn(now - effect_start, p, leds, on);
#else
        if (s->ring.mode == MODE_COLOR) fx_ring_solid(p, leds, on);
        else                            kEffects[p.type].fn(now - effect_start, p, leds, on);

        const WhiteMix w = white_mix_gamma(downlight_level(&s->down), s->down.cct);
        led_driver_set_ww(w.ww);
        led_driver_set_cw(w.cw);
#endif
    }
    led_driver_show(leds, RING_NUM_LEDS);
```

Add `#include "brightness.h"` to the include block — `white_mix_gamma` and `WhiteMix` live there.

> **Why the `#if` inside the else:** `BENCH_DEMO_MODE` compiles out `zigbee_light_*` entirely, so `s` does not exist in that build. Demo mode cycles `kDefaultParams` on the ring only, which is what it already did.

- [ ] **Step 6: Update `src/zigbee_light.h`**

Two edits. Change the state accessor's return type:

```cpp
// Live state, updated from Zigbee callbacks. One object, two independent
// halves -- see FixtureState in light_state.h.
const FixtureState* zigbee_light_state();
```

And correct the `zigbee_light_set_effect` comment, which says "eight effects":

```cpp
// Selects one of the six ring effects and persists it to NVS. Normally driven
// from the coordinator sending LUMARY_CMD_SET_EFFECT on RING_ENDPOINT; exposed
// for a future local button. Out-of-range indices are ignored, not clamped.
```

- [ ] **Step 7: Rework `src/zigbee_light.cpp`**

Replace the single `LumaryLight` class with a three-class hierarchy so the raw `esp_zb` plumbing is written once. Keep every existing comment body — the reasoning in them is still correct — moving each to whichever class now owns it.

```cpp
// Shared plumbing for both light endpoints. The Arduino wrapper has no
// cluster-building API, so this reaches _cluster_list (protected on ZigbeeEP)
// and uses the raw esp_zb calls -- the same pattern the base class itself uses.
class LumaryEndpoint : public ZigbeeColorDimmableLight {
public:
    explicit LumaryEndpoint(uint8_t endpoint) : ZigbeeColorDimmableLight(endpoint) {}

    // Push this endpoint's true state to the coordinator. Nothing else does
    // this after a reboot, so Z2M keeps showing whatever it last saw --
    // typically "on" for a light that came back off.
    //
    // Deliberately not built on setLightState()/setLightLevel(): those no-op
    // when the value is unchanged (so at boot, where off == off, they would
    // push nothing at all), and when the value HAS changed they re-enter our
    // own light-changed callback, which would drag the light into MODE_COLOR.
    void publishState(bool on, uint8_t level) {
        uint8_t on_val = on ? 1 : 0;
        setAttr(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &on_val);
        setAttr(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID, &level);
        reportAttr(ESP_ZB_ZCL_CLUSTER_ID_ON_OFF,
                   ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID);
        reportAttr(ESP_ZB_ZCL_CLUSTER_ID_LEVEL_CONTROL,
                   ESP_ZB_ZCL_ATTR_LEVEL_CONTROL_CURRENT_LEVEL_ID);
    }

protected:
    // ZCL character strings are length-prefixed, not null-terminated: byte 0 is
    // the length. Same encoding the base class does by hand for manufacturer
    // and model.
    void addBasicStringAttr(uint16_t attr_id, const char* value) {
        // Move the body verbatim from the current LumaryLight::addBasicStringAttr
        // (src/zigbee_light.cpp:81-102). It is unchanged -- only its owning
        // class moves. Keep its comment; the length-prefix explanation still
        // applies.
    }

    void setAttr(uint16_t cluster, uint16_t attr, void* value) {
        esp_zb_zcl_set_attribute_val(_endpoint, cluster,
                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                     attr, value, false);
    }

    void reportAttr(uint16_t cluster, uint16_t attr) {
        // Move the body verbatim from the current LumaryLight::reportAttr
        // (src/zigbee_light.cpp:110-130). Unchanged -- note it already uses
        // `_endpoint` for the source, so it is correct for both endpoints
        // without edit. Keep its comment about addressing the coordinator
        // explicitly rather than relying on the binding table.
    }
};

// Endpoint 1: the inner CW/WW white string. Carries the device-level furniture
// -- Basic strings and the OTA client -- because it is endpoint 1.
class LumaryDownlight : public LumaryEndpoint {
public:
    explicit LumaryDownlight(uint8_t endpoint) : LumaryEndpoint(endpoint) {
        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, FW_VERSION_STRING);
        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, FW_DATE_CODE);
    }
};

// Endpoint 2: the outer RGB ring, plus the manufacturer-specific cluster
// carrying the effect index.
//
// The attribute is READ-ONLY on purpose. Selection comes in as a command
// instead, because zbAttributeSet is private in the base class: a subclass may
// override it but cannot call it, so intercepting attribute writes would strand
// on/off, level and colour with no handler at all.
class LumaryRing : public LumaryEndpoint {
public:
    explicit LumaryRing(uint8_t endpoint) : LumaryEndpoint(endpoint) {
        uint8_t effect = 0;
        esp_zb_attribute_list_t* custom = esp_zb_zcl_attr_list_create(LUMARY_CLUSTER_ID);
        esp_zb_custom_cluster_add_custom_attr(custom, LUMARY_ATTR_EFFECT,
                                              ESP_ZB_ZCL_ATTR_TYPE_U8,
                                              ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
                                              &effect);
        esp_zb_cluster_list_add_custom_cluster(_cluster_list, custom,
                                               ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    }

    void publishState(bool on, uint8_t level, uint8_t effect) {
        LumaryEndpoint::publishState(on, level);
        setAttr(LUMARY_CLUSTER_ID, LUMARY_ATTR_EFFECT, &effect);
        // The effect attribute is deliberately NOT reported. esp_zb rejects it
        // with ESP_ERR_NOT_SUPPORTED unless the attribute carries
        // ESP_ZB_ZCL_ATTR_ACCESS_REPORTING -- and adding that flag to a custom
        // cluster attribute makes Zigbee.begin() hang before it ever starts the
        // stack (bisected on hardware 2026-08-15). The value is still written
        // above, so a READ returns the truth, which is what the Z2M converter's
        // convertGet uses.
    }
};

static LumaryDownlight s_down(DOWNLIGHT_ENDPOINT);
static LumaryRing      s_ring(RING_ENDPOINT);
```

Replace `static LightState s_state;` with `static FixtureState s_state;`.

The colour-change detection moves to the ring only — the downlight has no colour capability:

```cpp
// The ring endpoint reports state, level and colour together on every change,
// so the only way to tell a colour command from a plain dim is to compare
// against the last colour we saw. Without this, nudging the brightness would
// kick the ring out of whatever scene it was running.
static uint8_t s_ring_last_r = 255, s_ring_last_g = 255, s_ring_last_b = 255;
```

The callbacks. Each endpoint gets its own — the library registers per endpoint object and the callback carries no endpoint id:

```cpp
// ── endpoint 1: the downlight ─────────────────────────────────────────────
// Colour-temperature capability only, so this is the only light-change
// callback it needs. No RGB callback, and no rgb_to_cct fallback: the
// coordinator can only express this endpoint's colour as mireds.
static void on_downlight_change_temp(bool state, uint8_t level, uint16_t mireds) {
    s_state.down.on    = state;
    s_state.down.level = level;
    downlight_set_cct(&s_state.down, mireds);
}

// ── endpoint 2: the ring ──────────────────────────────────────────────────
static void on_ring_change_rgb(bool state, uint8_t r, uint8_t g, uint8_t b, uint8_t level) {
    s_state.ring.on    = state;
    s_state.ring.level = level;
    if (r != s_ring_last_r || g != s_ring_last_g || b != s_ring_last_b) {
        s_ring_last_r = r;
        s_ring_last_g = g;
        s_ring_last_b = b;
        const LightMode was = s_state.ring.mode;
        ring_set_color(&s_state.ring, CRGB{r, g, b});   // moves out of scene mode
        if (was == MODE_SCENE) publish_effect_attr();   // ...so stop naming one
    }
}
```

Delete `on_light_change_temp` and `on_light_change_rgb`.

`zigbee_light_init()` becomes:

```cpp
void zigbee_light_init() {
    fixture_state_init(&s_state);
    s_state.ring.scene = scene_store_get_active();

    // ── endpoint 1: downlight ──
    s_down.onLightChangeTemp(on_downlight_change_temp);
    s_down.onIdentify(on_identify);
    s_down.setManufacturerAndModel("Lumary", "LumaryBrainRevA");
    s_down.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
    if (!s_down.setLightColorTemperatureRange(CCT_MIRED_COOL, CCT_MIRED_WARM)) {
        log_e("Failed to publish colour temperature range");
    }

    // Zigbee OTA, on endpoint 1 only -- one client per device. The coordinator
    // only offers images numbered above the running version, so ZB_FW_VERSION
    // must match the .ota image's --file-version.
    if (!s_down.addOTAClient(ZB_FW_VERSION, ZB_FW_VERSION_DL, ZB_HW_VERSION,
                             ZB_MANUFACTURER_CODE, ZB_IMAGE_TYPE)) {
        log_e("Failed to add OTA client");
    }

    // ── endpoint 2: accent ring ──
    // No colour temperature: the ring has no white die, and advertising CCT
    // would put a control in Home Assistant that lies.
    s_ring.onLightChangeRgb(on_ring_change_rgb);
    s_ring.onCustomClusterCommand(on_custom_command);
    s_ring.setManufacturerAndModel("Lumary", "LumaryBrainRevA");
    s_ring.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION
                                   | ZIGBEE_COLOR_CAPABILITY_X_Y);

    Zigbee.addEndpoint(&s_down);
    Zigbee.addEndpoint(&s_ring);

    // Router, not end device: these are mains-powered ceiling fixtures, so each
    // one should extend the mesh for the others.
    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        // Deliberately not fatal. A light that can't reach the network must
        // still turn on locally, so carry on rendering and let the caller retry.
        log_e("Zigbee failed to start; continuing with local control only");
        return;
    }
    log_i("Zigbee started, waiting for network");
}
```

The join-time publish covers both endpoints:

```cpp
        const uint8_t effect = ring_effect_value(&s_state.ring);
        s_down.publishState(s_state.down.on, s_state.down.level);
        s_ring.publishState(s_state.ring.on, s_state.ring.level, effect);
        log_i("Published state: downlight on=%d level=%u / ring on=%d level=%u effect=%u",
              s_state.down.on, s_state.down.level,
              s_state.ring.on, s_state.ring.level, effect);
```

The remaining function bodies, updated for the new state and endpoint:

```cpp
const FixtureState* zigbee_light_state() {
    return &s_state;
}

static void publish_effect_attr() {
    uint8_t value = ring_effect_value(&s_state.ring);
    esp_zb_zcl_set_attribute_val(RING_ENDPOINT, LUMARY_CLUSTER_ID,
                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 LUMARY_ATTR_EFFECT, &value, false);
}

static void apply_effect(uint8_t index) {
    if (index == LIGHT_EFFECT_NONE) {
        // "None" in the Home Assistant dropdown: stop the effect and hold the
        // colour already on show. Deliberately NOT persisted -- the stored
        // active scene is what a power cycle should come back to.
        ring_clear_scene(&s_state.ring);
        publish_effect_attr();
        log_i("Effect cleared; holding the current colour");
        return;
    }

    ring_set_scene(&s_state.ring, index, EFFECT_COUNT);
    if (s_state.ring.scene != index) {
        log_w("Ignored out-of-range effect %u", index);
        return;
    }
    scene_store_set_active(s_state.ring.scene);
    publish_effect_attr();
    zigbee_light_report();
    log_i("Effect %u selected", s_state.ring.scene);
}

void zigbee_light_report() {
    s_ring.setLightState(s_state.ring.on);
    s_ring.setLightLevel(s_state.ring.level);
}
```

- [ ] **Step 8: Remove the old model from `src/light_state.h`**

Delete, in this order:

1. `#define WHITE_SAT_THRESHOLD 32` and its two comment lines
2. `rgb_to_cct()` and its two comment lines
3. `struct LightState`
4. `light_state_init`, `light_state_set_color`, `light_state_set_cct`, `light_state_set_scene`, `light_state_clear_scene`, `light_state_effect_value`, `light_state_next_scene`, `light_state_prev_scene`, `light_state_resolve`

Keep: `CCT_MIRED_WARM`/`CCT_MIRED_COOL`, `LIGHT_EFFECT_NONE`, `enum LightMode`, `struct HSV`, `rgb_to_hsv()`, `mireds_to_cct()`, `scale_level()`, and everything Task 1 appended.

Update the file's header comment, which describes the old routing:

```cpp
// Translates what Zigbee tells us into what the fixture should actually do.
// The fixture has two light sources -- the 62-pixel RGB ring and the inner
// CW/WW white string -- and each is its own Zigbee endpoint and its own Home
// Assistant entity, so each has its own state here and they run independently.
//
// Deliberately free of ESP-IDF headers so it can be unit-tested on the host;
// zigbee_light.cpp is the thin adapter that drives it. See test/test_light_state.
```

- [ ] **Step 9: Prune the obsolete tests**

In `test/test_light_state/test_main.cpp`, delete these test functions **and their `RUN_TEST` lines** — each covers a function that no longer exists:

```
test_neutral_white_is_mid_colour_temperature
test_red_heavy_white_reads_warm
test_blue_heavy_white_reads_cool
test_setting_cct_renders_on_the_white_string
test_setting_cct_leaves_scene_mode
test_starts_in_scene_mode
test_setting_a_colour_switches_to_colour_mode
test_recalling_a_scene_switches_back_to_scene_mode
test_out_of_range_scene_is_ignored
test_out_of_range_scene_does_not_leave_colour_mode
test_zero_scene_count_is_ignored
test_scene_mode_reports_the_running_effect
test_colour_mode_reports_no_effect
test_colour_temperature_also_reports_no_effect
test_clearing_the_scene_leaves_effect_mode
test_clearing_the_scene_keeps_the_colour
test_clearing_the_scene_remembers_the_index
test_cleared_scene_renders_the_held_colour
test_next_scene_advances_and_wraps
test_prev_scene_wraps_backwards
test_cycling_scenes_leaves_colour_mode
test_saturated_colour_renders_on_the_ring
test_near_white_colour_renders_on_the_white_string
test_scene_mode_runs_the_stored_effect
test_zigbee_level_dims_the_stored_scene
test_full_level_leaves_the_scene_brightness_intact
```

Keep the four `rgb_to_hsv` tests, the four `mireds_to_cct` tests, `test_none_is_not_a_valid_scene_index`, and everything Task 1 added. Also delete the `// ── colour temperature inference ──` section header, whose whole section is going.

Add one test locking in the new list length:

```cpp
void test_the_effect_table_is_six_ring_effects(void) {
    TEST_ASSERT_EQUAL_UINT8(6, EFFECT_COUNT);
    for (int i = 0; i < EFFECT_COUNT; i++) {
        TEST_ASSERT_TRUE(uint8_t(kDefaultParams[i].type) < EFFECT_COUNT);
        TEST_ASSERT_EQUAL_UINT8(i, uint8_t(kDefaultParams[i].type));   // table order == enum order
    }
}
```

with `RUN_TEST(test_the_effect_table_is_six_ring_effects);` in `main()`.

- [ ] **Step 10: Run the host tests**

Run: `scripts\run-native-tests.bat`

Expected: `--- test_light_state ---` reports `32 Tests 0 Failures 0 Ignored`, and the run ends with `ALL SUITES PASSED`.

- [ ] **Step 11: Verify the firmware builds**

Run from **PowerShell**: `pio run -e esp32h2`

Expected: `SUCCESS`. Any error naming `LightState`, `WHITE_SAT_THRESHOLD`, `rgb_to_cct`, `EFFECT_STATIC_WHITE` or `EFFECT_STATIC_COLOR` means a reference was missed — grep for it and fix.

- [ ] **Step 12: Verify nothing references the deleted names**

Run:

```bash
grep -rn "LightState\|WHITE_SAT_THRESHOLD\|rgb_to_cct\|EFFECT_STATIC_\|LIGHT_ENDPOINT\|white_off" src/ test/
```

Expected: **no matches.** `FixtureState`, `DownlightState` and `RingState` do not contain the substring `LightState`.

- [ ] **Step 13: Commit**

```bash
git add src/ test/
git commit -m "feat(fw): two endpoints, two independent light entities"
```

---

## Task 3: The converter

**Files:**
- Modify: `z2m/lumary-brain-revA.js`
- Test: `z2m/test/converter.test.mjs`

**Interfaces:**
- Consumes: `RING_ENDPOINT` = 2 and the six-entry effect order from Task 2
- Produces: nothing consumed by later tasks

- [ ] **Step 1: Renumber the effect map**

In `z2m/lumary-brain-revA.js`, replace the `EFFECTS` constant. Index order must match `EffectType` in `src/effect_params.h`:

```js
// Index order must match EffectType in src/effect_params.h. The names are the
// firmware's kEffects[].name, lowercased. `none` leads because that is the
// order Home Assistant renders the dropdown in.
//
// `static_white` and `static_color` were removed when the fixture split into
// two entities: white is now the Downlight entity on endpoint 1, and a solid
// ring colour is `none` with a colour set.
const EFFECTS = {
    none: EFFECT_NONE,
    warm_gradient: 0,
    color_gradient: 1,
    breathing: 2,
    color_cycle: 3,
    chase: 4,
    nightlight: 5,
};
```

- [ ] **Step 2: Split the light into two endpoints**

Replace the single `m.light({...})` entry in `extend` with two, and add the endpoint map and `meta` at the definition's top level (alongside `zigbeeModel`, `model`, `vendor`):

```js
    // Two endpoints, two Home Assistant light entities under one device. The
    // downlight keeps endpoint 1 so existing switch bindings land on the main
    // light; the ring is the new endpoint 2.
    endpoint: (device) => ({downlight: 1, ring: 2}),
    meta: {multiEndpoint: true},
```

and in `extend`:

```js
        // Endpoint 1: the inner CW/WW white string. Colour temperature only --
        // it has no colour dice. Mireds match CCT_MIRED_COOL/WARM in
        // src/light_state.h: 6500 K and 2700 K.
        m.light({
            endpointNames: ['downlight'],
            colorTemp: {range: [154, 370], startup: false},
            color: false,
            effect: false,
            powerOnBehavior: false,
        }),
        // Endpoint 2: the outer RGB ring. No colour temperature -- it has no
        // white die, and advertising CCT would expose a control that lies.
        m.light({
            endpointNames: ['ring'],
            color: {modes: ['xy']},
            effect: false,
            powerOnBehavior: false,
        }),
```

The `effect: false` / `powerOnBehavior: false` reasoning in the existing comment block still applies to both and should be kept above them.

- [ ] **Step 3: Point the effect converters at endpoint 2**

The effect cluster lives on the ring. Update `onEvent` to read from endpoint 2:

```js
const onEvent = async (event) => {
    if (event?.type !== 'deviceAnnounce' && event?.type !== 'start') return;
    // The effect cluster is on endpoint 2 -- the ring owns the effects.
    const endpoint = event?.data?.device?.getEndpoint?.(2);
    if (!endpoint) return;
    try {
        await endpoint.read('lumary', ['effect']);
    } catch {
        // Out of range or not joined yet; the next announce retries.
    }
};
```

Add `endpoint: 'ring'` to the `effect` expose so Home Assistant attaches it to the ring entity:

```js
    exposes: [
        e
            .enum('effect', ea.ALL, Object.keys(EFFECTS))
            .withEndpoint('ring')
            .withDescription(
                'Which built-in effect the accent ring runs. Setting a colour on the ' +
                'ring exits the effect and shows that colour instead, which reads back ' +
                'as `none`; selecting `none` does the same thing without changing the ' +
                'colour. The downlight is a separate entity and has no effects.',
            ),
    ],
```

- [ ] **Step 4: Update the converter tests**

In `z2m/test/converter.test.mjs`:

Replace the expected effect values (currently the eight-name list) with:

```js
    assert.deepEqual(effect.values, [
        'none', 'warm_gradient', 'color_gradient', 'breathing',
        'color_cycle', 'chase', 'nightlight',
    ]);
```

Replace `const lightArgs = calls.find((c) => c.fn === 'light').args;` with two lookups, and update the assertions that used it:

```js
const lightCalls  = calls.filter((c) => c.fn === 'light');
const downArgs    = lightCalls.find((c) => c.args.endpointNames?.includes('downlight')).args;
const ringArgs    = lightCalls.find((c) => c.args.endpointNames?.includes('ring')).args;
```

Add these tests:

```js
test('exposes exactly two lights, one per endpoint', () => {
    assert.equal(lightCalls.length, 2);
});

test('the downlight carries colour temperature and no colour', () => {
    assert.deepEqual(downArgs.colorTemp.range, [154, 370]);
    assert.equal(downArgs.colorTemp.startup, false);
    assert.equal(downArgs.color, false);
});

test('the ring carries colour and no colour temperature', () => {
    assert.deepEqual(ringArgs.color, {modes: ['xy']});
    assert.equal(ringArgs.colorTemp, undefined);
});

test('both lights switch off the dead stock controls', () => {
    for (const args of [downArgs, ringArgs]) {
        assert.equal(args.effect, false);
        assert.equal(args.powerOnBehavior, false);
    }
});

test('the endpoint map names both endpoints', () => {
    assert.deepEqual(def.endpoint({}), {downlight: 1, ring: 2});
    assert.equal(def.meta.multiEndpoint, true);
});
```

Update the two `tzEffect` wire-value assertions, whose indices moved: `color_cycle` was 5 and is now 3, `chase` was 6 and is now 4.

```js
    assert.deepEqual(sent, {cluster: 'lumary', cmd: 'setEffect', payload: {effect: 3}});
    assert.deepEqual(res, {state: {effect: 'color_cycle'}});
```

```js
    assert.deepEqual(fz.convert({}, {data: {effect: 4}}), {effect: 'chase'});
```

- [ ] **Step 5: Run the converter tests**

Run: `node z2m/test/converter.test.mjs`

Expected: every test passes, no failures reported.

> If the stub in `z2m/test/stubs/modernExtend.mjs` records only the last `light()` call rather than appending each one, `lightCalls` will have length 1 and the two-endpoint tests fail. Fix the stub to push every call onto `calls` — it already does this for other functions, so follow that pattern.

- [ ] **Step 6: Commit**

```bash
git add z2m/
git commit -m "feat(z2m): two light entities, effects on the ring endpoint"
```

---

## Task 4: Documentation

The behaviour changes here are breaking, and two of them are invisible until someone hits them. This task is what stops that.

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`

- [ ] **Step 1: Document the two entities in `README.md`**

Update the "The two light sources" section to say that each is now its own entity, and add a table:

| Entity | Endpoint | Controls |
|---|---|---|
| `light.<name>_downlight` | 1 | on/off, brightness, colour temperature 2700–6500 K |
| `light.<name>_ring` | 2 | on/off, brightness, xy colour, the six effects |

- [ ] **Step 2: Add the re-interview requirement, prominently**

This affects every fixture already in service, so it belongs near the top of the install instructions, not in a footnote:

> **Upgrading an already-paired fixture:** adding the second endpoint changes the device descriptor, and Zigbee2MQTT caches endpoints from the interview. A fixture paired before this firmware will show only the downlight until it is **re-interviewed** (Z2M frontend → device → Re-interview, or publish to `zigbee2mqtt/bridge/request/device/interview`). Newly paired fixtures just work.

- [ ] **Step 3: Document both bindings**

Replace the switch-binding instructions with both, noting the source endpoint:

```
Bind from the Inovelli's endpoint 2 (the paddle) to BOTH fixture endpoints,
clusters genOnOff and genLevelCtrl:

  switch ep2 -> fixture ep1   (downlight)
  switch ep2 -> fixture ep2   (accent ring)
```

Add the behaviour note: one tap down turns both sources off, one tap up brings both back at their own levels and colours. This works because the Inovelli sends discrete `On`/`Off` rather than `Toggle` — verified on the hardware. "Downlight only" is a Home Assistant action rather than a switch action; remove the second binding if you would rather the ring ignored the switch.

- [ ] **Step 4: Document the breaking effect changes**

Update the effects table to six entries and add:

> **Changed in this version:** `static_white` and `static_color` are gone from the effect list. White is now the separate Downlight entity, and a solid ring colour is `effect: none` with a colour set. Automations naming either of the two removed effects need updating. Stored scenes are reseeded automatically — the NVS schema version bump discards the old indices rather than misreading them.

- [ ] **Step 5: Close out item 9 in the backlog**

In `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`:

1. Change the item 9 heading to `## 9. One light entity or two — DONE, two endpoints shipped`
2. Replace its body with what was built, following the style of items 4 and 5: the effect-list consequence, the binding decision and the Inovelli On/Off finding, and the re-interview requirement.
3. Update the status line at the top of the file to include item 9.
4. Update the `## Suggested order` line: item 9 is done, so **10** is unblocked and **3**, **7** and **11** are next.

- [ ] **Step 6: Commit**

```bash
git add README.md docs/
git commit -m "docs: two light entities, re-interview requirement, both bindings"
```

---

## Bench verification

Not a task — this needs the fixture and belongs to the human. Nothing above has touched hardware.

- [ ] Flash and confirm the device joins, then **re-interview it in Z2M**
- [ ] Both entities appear: `light.*_downlight` and `light.*_ring`
- [ ] The downlight shows a colour-temperature control and **no** colour wheel (risk 1 in the design doc — `ZigbeeColorDimmableLight` configured `COLOR_TEMP`-only keeps the `HA_COLOR_DIMMABLE_LIGHT` device ID, so this is the check that it still presents correctly)
- [ ] The ring shows a colour wheel and **no** colour temperature slider
- [ ] **Both on at once** — white downlight plus a coloured ring. This is the capability the whole item exists for
- [ ] The effect dropdown is on the ring entity only, and lists six effects plus `none`
- [ ] Setting a ring colour drops `effect` to `none`
- [ ] OTA still offered after the split (risk 2 — one client across two endpoints)
- [ ] Bind both endpoints; one tap down kills both, one tap up restores both at their own levels
- [ ] Power-cycle: the ring returns to its stored effect, reseeded from the new defaults

## Notes for the implementer

- **Do not touch `scale8`, `src/color.h`, or `src/brightness.h`.** The gamma work landed on `main` in `9dc9724` and is verified; `white_mix_gamma` is consumed unchanged.
- **Do not implement power-on behaviour.** The downlight not remembering its level and colour temperature across a power cut is item 2b, deliberately out of scope, and `powerOnBehavior: false` stays in the converter until it is done.
- **`FixtureState` does not contain the substring `LightState`.** Task 2 Step 12's grep relies on that; do not rename the container to something like `FixtureLightState` or the check silently stops working.
- **Endpoint 1 is the downlight.** If you find yourself putting the ring on endpoint 1 because it is "the interesting one", stop — existing switch bindings target endpoint 1 and must land on the main light.

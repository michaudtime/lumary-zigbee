// Native (host) tests for the Zigbee -> fixture translation layer.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include "light_state.h"

void setUp(void) {}
void tearDown(void) {}

// ── rgb_to_hsv ────────────────────────────────────────────────────────────
// Hues use the same 43-per-sextant scale as hsv_to_rgb, so the two round-trip.

void test_pure_red_is_hue_zero_fully_saturated(void) {
    const HSV h = rgb_to_hsv({255, 0, 0});
    TEST_ASSERT_EQUAL_UINT8(0,   h.h);
    TEST_ASSERT_EQUAL_UINT8(255, h.s);
    TEST_ASSERT_EQUAL_UINT8(255, h.v);
}

void test_pure_green_and_blue_land_in_their_sextants(void) {
    TEST_ASSERT_EQUAL_UINT8(85,  rgb_to_hsv({0, 255, 0}).h);
    TEST_ASSERT_EQUAL_UINT8(171, rgb_to_hsv({0, 0, 255}).h);
}

void test_white_has_zero_saturation(void) {
    const HSV h = rgb_to_hsv({255, 255, 255});
    TEST_ASSERT_EQUAL_UINT8(0,   h.s);
    TEST_ASSERT_EQUAL_UINT8(255, h.v);
}

void test_black_is_all_zero(void) {
    const HSV h = rgb_to_hsv({0, 0, 0});
    TEST_ASSERT_EQUAL_UINT8(0, h.s);
    TEST_ASSERT_EQUAL_UINT8(0, h.v);
}

// ── colour temperature from Zigbee (mireds) ───────────────────────────────
// The Colour Control cluster reports colour temperature in mireds
// (1e6 / Kelvin), so warmer light is a LARGER number. The fixture's two white
// strings bound the range: 2700 K (370 mired) and 6500 K (154 mired).

void test_warm_end_maps_to_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0, mireds_to_cct(CCT_MIRED_WARM));      // 2700 K
}

void test_cool_end_maps_to_full(void) {
    TEST_ASSERT_EQUAL_UINT8(255, mireds_to_cct(CCT_MIRED_COOL));    // 6500 K
}

void test_midpoint_maps_to_middle(void) {
    const uint16_t mid = (CCT_MIRED_WARM + CCT_MIRED_COOL) / 2;
    const uint8_t  cct = mireds_to_cct(mid);
    TEST_ASSERT_GREATER_THAN_UINT8(115, cct);
    TEST_ASSERT_LESS_THAN_UINT8(140, cct);
}

void test_out_of_range_mireds_clamp(void) {
    TEST_ASSERT_EQUAL_UINT8(0,   mireds_to_cct(500));   // warmer than the WW string
    TEST_ASSERT_EQUAL_UINT8(255, mireds_to_cct(100));   // cooler than the CW string
}

// The sentinel must never be mistakable for a selectable effect, or "none"
// would round-trip as whatever effect happens to sit at that index.
void test_none_is_not_a_valid_scene_index(void) {
    TEST_ASSERT_GREATER_THAN_UINT8(EFFECT_COUNT, LIGHT_EFFECT_NONE);
    RingState r;
    ring_state_init(&r);
    ring_set_scene(&r, LIGHT_EFFECT_NONE, EFFECT_COUNT);
    TEST_ASSERT_EQUAL(MODE_SCENE, r.mode);       // rejected as out of range
    TEST_ASSERT_EQUAL_UINT8(0, r.scene);
}

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

// Starts from MODE_COLOR rather than the freshly-initialised MODE_SCENE, so a
// call that is wrongly accepted (moving to MODE_SCENE) is distinguishable from
// one that is correctly rejected (staying in MODE_COLOR). Against the
// freshly-initialised default this test could not tell "rejected" from
// "accepted a no-op" -- and with scene_count == 0, the second guard
// (index >= scene_count) also rejects, so deleting the scene_count == 0 check
// entirely would have left the old version of this test green.
void test_ring_zero_scene_count_is_ignored(void) {
    RingState r;
    ring_state_init(&r);
    ring_clear_scene(&r);
    TEST_ASSERT_EQUAL(MODE_COLOR, r.mode);
    ring_set_scene(&r, 0, 0);
    TEST_ASSERT_EQUAL(MODE_COLOR, r.mode);
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

// ── the effect table ──────────────────────────────────────────────────────

void test_the_effect_table_is_six_ring_effects(void) {
    TEST_ASSERT_EQUAL_UINT8(6, EFFECT_COUNT);
    for (int i = 0; i < EFFECT_COUNT; i++) {
        TEST_ASSERT_TRUE(uint8_t(kDefaultParams[i].type) < EFFECT_COUNT);
        TEST_ASSERT_EQUAL_UINT8(i, uint8_t(kDefaultParams[i].type));   // table order == enum order
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pure_red_is_hue_zero_fully_saturated);
    RUN_TEST(test_pure_green_and_blue_land_in_their_sextants);
    RUN_TEST(test_white_has_zero_saturation);
    RUN_TEST(test_black_is_all_zero);
    RUN_TEST(test_warm_end_maps_to_zero);
    RUN_TEST(test_cool_end_maps_to_full);
    RUN_TEST(test_midpoint_maps_to_middle);
    RUN_TEST(test_out_of_range_mireds_clamp);
    RUN_TEST(test_none_is_not_a_valid_scene_index);
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
    RUN_TEST(test_the_effect_table_is_six_ring_effects);
    return UNITY_END();
}

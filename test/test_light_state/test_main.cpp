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

// ── colour temperature inference ──────────────────────────────────────────
// The ring has no white die and Zigbee 3.1.0 gives us no CCT cluster, so the
// warmth of a near-white colour is inferred from its red/blue balance.

void test_neutral_white_is_mid_colour_temperature(void) {
    TEST_ASSERT_EQUAL_UINT8(128, rgb_to_cct({255, 255, 255}));
}

void test_red_heavy_white_reads_warm(void) {
    TEST_ASSERT_LESS_THAN_UINT8(100, rgb_to_cct({255, 169, 87}));   // ~2700K mix
}

void test_blue_heavy_white_reads_cool(void) {
    TEST_ASSERT_GREATER_THAN_UINT8(150, rgb_to_cct({180, 220, 255}));
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

void test_setting_cct_renders_on_the_white_string(void) {
    LightState s;
    light_state_init(&s);
    s.level = 200;
    light_state_set_cct(&s, CCT_MIRED_COOL);
    const EffectParams p = light_state_resolve(&s, &kDefaultParams[0]);
    TEST_ASSERT_EQUAL(EFFECT_STATIC_WHITE, p.type);
    TEST_ASSERT_EQUAL_UINT8(255, p.hue);          // fully cool
    TEST_ASSERT_EQUAL_UINT8(200, p.brightness);
}

void test_setting_cct_leaves_scene_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 4, EFFECT_COUNT);
    light_state_set_cct(&s, CCT_MIRED_WARM);
    TEST_ASSERT_EQUAL(MODE_COLOR, s.mode);        // white is a colour command
    TEST_ASSERT_EQUAL_UINT8(0, s.sat);            // ...with no saturation
}

// ── mode switching ────────────────────────────────────────────────────────

void test_starts_in_scene_mode(void) {
    LightState s;
    light_state_init(&s);
    TEST_ASSERT_EQUAL(MODE_SCENE, s.mode);
    TEST_ASSERT_EQUAL_UINT8(0, s.scene);
}

void test_setting_a_colour_switches_to_colour_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {255, 0, 0});
    TEST_ASSERT_EQUAL(MODE_COLOR, s.mode);
    TEST_ASSERT_EQUAL_UINT8(0,   s.hue);
    TEST_ASSERT_EQUAL_UINT8(255, s.sat);
}

void test_recalling_a_scene_switches_back_to_scene_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {255, 0, 0});
    light_state_set_scene(&s, 3, EFFECT_COUNT);
    TEST_ASSERT_EQUAL(MODE_SCENE, s.mode);
    TEST_ASSERT_EQUAL_UINT8(3, s.scene);
}

// An out-of-range index arrives over the air from anything that can write the
// effect attribute, so it must not be able to strand the light on a scene the
// effect engine cannot render.
void test_out_of_range_scene_is_ignored(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 2, EFFECT_COUNT);
    light_state_set_scene(&s, EFFECT_COUNT, EFFECT_COUNT);   // first invalid index
    TEST_ASSERT_EQUAL_UINT8(2, s.scene);                     // unchanged
    light_state_set_scene(&s, 250, EFFECT_COUNT);
    TEST_ASSERT_EQUAL_UINT8(2, s.scene);
}

void test_out_of_range_scene_does_not_leave_colour_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {255, 0, 0});
    light_state_set_scene(&s, 99, EFFECT_COUNT);
    TEST_ASSERT_EQUAL(MODE_COLOR, s.mode);   // rejected, so still a colour
}

void test_zero_scene_count_is_ignored(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {255, 0, 0});
    light_state_set_scene(&s, 0, 0);
    TEST_ASSERT_EQUAL(MODE_COLOR, s.mode);
}

// ── the effect value reported to the coordinator ──────────────────────────
// Home Assistant's effect list has no null member, so "no effect running" is
// carried as LIGHT_EFFECT_NONE rather than being absent.

void test_scene_mode_reports_the_running_effect(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 5, EFFECT_COUNT);
    TEST_ASSERT_EQUAL_UINT8(5, light_state_effect_value(&s));
}

void test_colour_mode_reports_no_effect(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 5, EFFECT_COUNT);
    light_state_set_color(&s, {255, 0, 0});
    TEST_ASSERT_EQUAL_UINT8(LIGHT_EFFECT_NONE, light_state_effect_value(&s));
}

void test_colour_temperature_also_reports_no_effect(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 2, EFFECT_COUNT);
    light_state_set_cct(&s, CCT_MIRED_WARM);
    TEST_ASSERT_EQUAL_UINT8(LIGHT_EFFECT_NONE, light_state_effect_value(&s));
}

// The sentinel must never be mistakable for a selectable effect, or "none"
// would round-trip as whatever effect happens to sit at that index.
void test_none_is_not_a_valid_scene_index(void) {
    TEST_ASSERT_GREATER_THAN_UINT8(EFFECT_COUNT, LIGHT_EFFECT_NONE);
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, LIGHT_EFFECT_NONE, EFFECT_COUNT);
    TEST_ASSERT_EQUAL(MODE_SCENE, s.mode);       // rejected as out of range
    TEST_ASSERT_EQUAL_UINT8(0, s.scene);
}

// ── clearing the effect ("none" in the dropdown) ──────────────────────────

void test_clearing_the_scene_leaves_effect_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 6, EFFECT_COUNT);
    light_state_clear_scene(&s);
    TEST_ASSERT_EQUAL(MODE_COLOR, s.mode);
    TEST_ASSERT_EQUAL_UINT8(LIGHT_EFFECT_NONE, light_state_effect_value(&s));
}

// "None" stops the effect; it does not change what colour is showing. Anything
// else would make selecting it from Home Assistant a destructive act.
void test_clearing_the_scene_keeps_the_colour(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {0, 255, 0});
    const uint8_t hue = s.hue, sat = s.sat, cct = s.cct;
    light_state_set_scene(&s, 4, EFFECT_COUNT);
    light_state_clear_scene(&s);
    TEST_ASSERT_EQUAL_UINT8(hue, s.hue);
    TEST_ASSERT_EQUAL_UINT8(sat, s.sat);
    TEST_ASSERT_EQUAL_UINT8(cct, s.cct);
}

// The stored index survives, so re-selecting the same effect -- and a reboot,
// which comes back in MODE_SCENE -- returns to where it left off.
void test_clearing_the_scene_remembers_the_index(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 7, EFFECT_COUNT);
    light_state_clear_scene(&s);
    TEST_ASSERT_EQUAL_UINT8(7, s.scene);
    light_state_set_scene(&s, 7, EFFECT_COUNT);
    TEST_ASSERT_EQUAL(MODE_SCENE, s.mode);
    TEST_ASSERT_EQUAL_UINT8(7, light_state_effect_value(&s));
}

void test_cleared_scene_renders_the_held_colour(void) {
    LightState s;
    light_state_init(&s);
    s.level = 200;
    light_state_set_color(&s, {0, 255, 0});
    light_state_set_scene(&s, 4, EFFECT_COUNT);     // breathing takes over
    light_state_clear_scene(&s);                    // ...and is stopped again
    const EffectParams p = light_state_resolve(&s, &kDefaultParams[4]);
    TEST_ASSERT_EQUAL(EFFECT_STATIC_COLOR, p.type);
    TEST_ASSERT_EQUAL_UINT8(85, p.hue);             // still green
}

// ── scene cycling (the switch's double-tap actions) ───────────────────────

void test_next_scene_advances_and_wraps(void) {
    LightState s;
    light_state_init(&s);
    light_state_next_scene(&s, 3);
    TEST_ASSERT_EQUAL_UINT8(1, s.scene);
    light_state_next_scene(&s, 3);
    light_state_next_scene(&s, 3);
    TEST_ASSERT_EQUAL_UINT8(0, s.scene);          // wrapped past the last
}

void test_prev_scene_wraps_backwards(void) {
    LightState s;
    light_state_init(&s);
    light_state_prev_scene(&s, 3);
    TEST_ASSERT_EQUAL_UINT8(2, s.scene);          // 0 -> last
}

void test_cycling_scenes_leaves_colour_mode(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_color(&s, {255, 0, 0});
    light_state_next_scene(&s, 8);
    TEST_ASSERT_EQUAL(MODE_SCENE, s.mode);
}

// ── resolve: state + stored scene -> what the effect engine runs ──────────

void test_saturated_colour_renders_on_the_ring(void) {
    LightState s;
    light_state_init(&s);
    s.level = 200;
    light_state_set_color(&s, {0, 255, 0});
    const EffectParams p = light_state_resolve(&s, &kDefaultParams[0]);
    TEST_ASSERT_EQUAL(EFFECT_STATIC_COLOR, p.type);
    TEST_ASSERT_EQUAL_UINT8(85,  p.hue);
    TEST_ASSERT_EQUAL_UINT8(200, p.brightness);
}

void test_near_white_colour_renders_on_the_white_string(void) {
    LightState s;
    light_state_init(&s);
    s.level = 180;
    light_state_set_color(&s, {255, 250, 245});        // barely saturated
    const EffectParams p = light_state_resolve(&s, &kDefaultParams[0]);
    TEST_ASSERT_EQUAL(EFFECT_STATIC_WHITE, p.type);
    TEST_ASSERT_EQUAL_UINT8(180, p.brightness);
    // fx_static_white reads `hue` as colour temperature; warm-ish here.
    TEST_ASSERT_LESS_THAN_UINT8(140, p.hue);
}

void test_scene_mode_runs_the_stored_effect(void) {
    LightState s;
    light_state_init(&s);
    light_state_set_scene(&s, 6, EFFECT_COUNT);
    const EffectParams scene = {EFFECT_CHASE, 40, 255, 200, 120};
    const EffectParams p = light_state_resolve(&s, &scene);
    TEST_ASSERT_EQUAL(EFFECT_CHASE, p.type);
    TEST_ASSERT_EQUAL_UINT8(40,  p.hue);
    TEST_ASSERT_EQUAL_UINT8(120, p.speed);
}

void test_zigbee_level_dims_the_stored_scene(void) {
    LightState s;
    light_state_init(&s);
    s.level = 128;                                     // half brightness
    const EffectParams scene = {EFFECT_CHASE, 40, 255, 200, 120};
    const EffectParams p = light_state_resolve(&s, &scene);
    TEST_ASSERT_EQUAL_UINT8(100, p.brightness);        // 200 scaled by 128/255
}

void test_full_level_leaves_the_scene_brightness_intact(void) {
    LightState s;
    light_state_init(&s);
    s.level = 255;
    const EffectParams scene = {EFFECT_BREATHING, 0, 255, 200, 80};
    const EffectParams p = light_state_resolve(&s, &scene);
    TEST_ASSERT_EQUAL_UINT8(200, p.brightness);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pure_red_is_hue_zero_fully_saturated);
    RUN_TEST(test_pure_green_and_blue_land_in_their_sextants);
    RUN_TEST(test_white_has_zero_saturation);
    RUN_TEST(test_black_is_all_zero);
    RUN_TEST(test_neutral_white_is_mid_colour_temperature);
    RUN_TEST(test_red_heavy_white_reads_warm);
    RUN_TEST(test_blue_heavy_white_reads_cool);
    RUN_TEST(test_warm_end_maps_to_zero);
    RUN_TEST(test_cool_end_maps_to_full);
    RUN_TEST(test_midpoint_maps_to_middle);
    RUN_TEST(test_out_of_range_mireds_clamp);
    RUN_TEST(test_setting_cct_renders_on_the_white_string);
    RUN_TEST(test_setting_cct_leaves_scene_mode);
    RUN_TEST(test_starts_in_scene_mode);
    RUN_TEST(test_setting_a_colour_switches_to_colour_mode);
    RUN_TEST(test_recalling_a_scene_switches_back_to_scene_mode);
    RUN_TEST(test_out_of_range_scene_is_ignored);
    RUN_TEST(test_out_of_range_scene_does_not_leave_colour_mode);
    RUN_TEST(test_zero_scene_count_is_ignored);
    RUN_TEST(test_scene_mode_reports_the_running_effect);
    RUN_TEST(test_colour_mode_reports_no_effect);
    RUN_TEST(test_colour_temperature_also_reports_no_effect);
    RUN_TEST(test_none_is_not_a_valid_scene_index);
    RUN_TEST(test_clearing_the_scene_leaves_effect_mode);
    RUN_TEST(test_clearing_the_scene_keeps_the_colour);
    RUN_TEST(test_clearing_the_scene_remembers_the_index);
    RUN_TEST(test_cleared_scene_renders_the_held_colour);
    RUN_TEST(test_next_scene_advances_and_wraps);
    RUN_TEST(test_prev_scene_wraps_backwards);
    RUN_TEST(test_cycling_scenes_leaves_colour_mode);
    RUN_TEST(test_saturated_colour_renders_on_the_ring);
    RUN_TEST(test_near_white_colour_renders_on_the_white_string);
    RUN_TEST(test_scene_mode_runs_the_stored_effect);
    RUN_TEST(test_zigbee_level_dims_the_stored_scene);
    RUN_TEST(test_full_level_leaves_the_scene_brightness_intact);
    return UNITY_END();
}

// Native (host) tests for telling a ring colour command apart from a plain
// On/Off or Level command.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include "ring_command.h"

static RingState ring;
static RingSync  sync;

// A fixture as it comes up after a power cycle: off, full level, running the
// stored scene, with the sync baseline at the library's power-up values.
void setUp(void) {
    ring_state_init(&ring);
    ring.scene = 3;
    ring.mode  = MODE_SCENE;
    ring_sync_init(&sync);
}
void tearDown(void) {}

// The colour the library hands back when it dispatches a non-colour command
// through the RGB callback: its own power-up default, not anything commanded.
static const CRGB LIB_DEFAULT = {255, 255, 255};

// ── the reported bug: a power cycle, then the first command ───────────────

void test_first_on_after_a_power_cycle_keeps_the_scene(void) {
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(3, ring.scene);
    TEST_ASSERT_TRUE(ring.on);
}

void test_first_dim_after_a_power_cycle_keeps_the_scene(void) {
    ring_apply_rgb(&ring, &sync, false, LIB_DEFAULT, 128);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(3,   ring.scene);
    TEST_ASSERT_EQUAL_UINT8(128, ring.level);
}

// The ColorMode attribute can be written directly (a scene recall or another
// coordinator), latching the library into HSV without any hue/sat command --
// so the first On/Off after that dispatches through the HSV callback carrying
// the library's default hue 0 / sat 0. Same bug, other callback.
void test_first_on_dispatched_as_hsv_keeps_the_scene(void) {
    ring_apply_hsv(&ring, &sync, true, 0, 0, 255);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(3, ring.scene);
}

void test_a_run_of_on_off_and_dim_never_touches_the_scene(void) {
    ring_apply_rgb(&ring, &sync, true,  LIB_DEFAULT, 255);
    ring_apply_rgb(&ring, &sync, true,  LIB_DEFAULT, 200);
    ring_apply_rgb(&ring, &sync, true,  LIB_DEFAULT, 64);
    ring_apply_rgb(&ring, &sync, false, LIB_DEFAULT, 64);
    ring_apply_rgb(&ring, &sync, true,  LIB_DEFAULT, 64);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(3, ring.scene);
}

// ── the hole the old colour-comparison sentinels left open ────────────────
// hue 0 / sat 0 IS the library's power-up default, so seeding a last-colour
// baseline would swallow a genuine "make the ring white". The state/level test
// gets this right: a colour write leaves both untouched.

void test_a_genuine_first_white_over_hsv_still_replaces_the_scene(void) {
    ring_apply_hsv(&ring, &sync, false, 0, 0, 255);
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(0, ring.hue);
    TEST_ASSERT_EQUAL_UINT8(0, ring.sat);
}

void test_a_genuine_first_white_over_rgb_still_replaces_the_scene(void) {
    ring_apply_rgb(&ring, &sync, false, LIB_DEFAULT, 255);
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(0, ring.sat);
}

void test_a_genuine_first_colour_replaces_the_scene(void) {
    ring_apply_rgb(&ring, &sync, false, CRGB{255, 0, 0}, 255);
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(0,   ring.hue);
    TEST_ASSERT_EQUAL_UINT8(255, ring.sat);
}

// ── ordinary traffic ──────────────────────────────────────────────────────

// Home Assistant sends "on, at this colour" as separate attribute writes, so
// the On lands first and the colour follows with state and level already settled.
void test_an_on_followed_by_a_colour_applies_the_colour(void) {
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    ring_apply_rgb(&ring, &sync, true, CRGB{0, 255, 0}, 255);
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(85, ring.hue);      // green
}

void test_a_dim_after_a_colour_keeps_the_colour(void) {
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT,     255);   // On
    ring_apply_rgb(&ring, &sync, true, CRGB{255, 0, 0}, 255);   // then red
    ring_apply_rgb(&ring, &sync, true, CRGB{255, 0, 0}, 100);   // then dim
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(255, ring.sat);
    TEST_ASSERT_EQUAL_UINT8(100, ring.level);
}

void test_a_dim_after_a_scene_was_selected_keeps_the_scene(void) {
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255);
    ring_set_scene(&ring, 5, EFFECT_COUNT);
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 30);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
    TEST_ASSERT_EQUAL_UINT8(5, ring.scene);
}

void test_every_dispatch_tracks_state_and_level(void) {
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 77);
    TEST_ASSERT_TRUE(ring.on);
    TEST_ASSERT_EQUAL_UINT8(77, ring.level);
    ring_apply_hsv(&ring, &sync, false, 10, 20, 44);
    TEST_ASSERT_FALSE(ring.on);
    TEST_ASSERT_EQUAL_UINT8(44, ring.level);
}

// ── re-seeding after we push values into the library ourselves ────────────
// The OTA-recovery restore drives setLightState()/setLightLevel() with our
// callbacks suppressed, so the library moves without a dispatch. The baseline
// has to move with it, or the next genuine colour command reads as an On/Off.

void test_a_restore_resync_moves_the_baseline(void) {
    ring.on    = true;
    ring.level = 255;
    ring_sync_note(&sync, true, 255);          // what the suppressed re-sync pushed

    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255);
    TEST_ASSERT_EQUAL(MODE_COLOR, ring.mode);  // a real colour command, not an On
}

void test_without_the_resync_the_same_command_would_read_as_an_on(void) {
    ring.on    = true;                         // restored on, baseline left at boot
    ring.level = 255;
    ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255);
    TEST_ASSERT_EQUAL(MODE_SCENE, ring.mode);
}

// ── the return value drives publish_effect_attr() ─────────────────────────

void test_leaving_scene_mode_is_reported(void) {
    TEST_ASSERT_TRUE(ring_apply_rgb(&ring, &sync, false, CRGB{0, 0, 255}, 255));
}

void test_a_non_colour_command_reports_nothing(void) {
    TEST_ASSERT_FALSE(ring_apply_rgb(&ring, &sync, true, LIB_DEFAULT, 255));
}

void test_a_colour_on_a_ring_already_in_colour_mode_reports_nothing(void) {
    ring_apply_rgb(&ring, &sync, false, CRGB{0, 0, 255}, 255);   // leaves scene mode
    TEST_ASSERT_FALSE(ring_apply_rgb(&ring, &sync, false, CRGB{255, 0, 0}, 255));
}

void test_leaving_scene_mode_over_hsv_is_reported(void) {
    TEST_ASSERT_TRUE(ring_apply_hsv(&ring, &sync, false, 128, 200, 255));
    TEST_ASSERT_EQUAL_UINT8(128, ring.hue);
    TEST_ASSERT_EQUAL_UINT8(200, ring.sat);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_on_after_a_power_cycle_keeps_the_scene);
    RUN_TEST(test_first_dim_after_a_power_cycle_keeps_the_scene);
    RUN_TEST(test_first_on_dispatched_as_hsv_keeps_the_scene);
    RUN_TEST(test_a_run_of_on_off_and_dim_never_touches_the_scene);
    RUN_TEST(test_a_genuine_first_white_over_hsv_still_replaces_the_scene);
    RUN_TEST(test_a_genuine_first_white_over_rgb_still_replaces_the_scene);
    RUN_TEST(test_a_genuine_first_colour_replaces_the_scene);
    RUN_TEST(test_an_on_followed_by_a_colour_applies_the_colour);
    RUN_TEST(test_a_dim_after_a_colour_keeps_the_colour);
    RUN_TEST(test_a_dim_after_a_scene_was_selected_keeps_the_scene);
    RUN_TEST(test_every_dispatch_tracks_state_and_level);
    RUN_TEST(test_a_restore_resync_moves_the_baseline);
    RUN_TEST(test_without_the_resync_the_same_command_would_read_as_an_on);
    RUN_TEST(test_leaving_scene_mode_is_reported);
    RUN_TEST(test_a_non_colour_command_reports_nothing);
    RUN_TEST(test_a_colour_on_a_ring_already_in_colour_mode_reports_nothing);
    RUN_TEST(test_leaving_scene_mode_over_hsv_is_reported);
    return UNITY_END();
}

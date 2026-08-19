// Native (host) tests for the data-driven effect renderer.
// Run: pio test -e native   (or scripts\run-native-tests.bat)
#include <unity.h>
#include <string.h>
#include "recipe_render.h"

void setUp(void) {}
void tearDown(void) {}

static CRGB leds[RING_NUM_LEDS];

static int lit_count(void) {
    int n = 0;
    for (int i = 0; i < RING_NUM_LEDS; i++)
        if (leds[i].r || leds[i].g || leds[i].b) n++;
    return n;
}

static bool same_pixel(CRGB a, CRGB b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// A minimal valid recipe, for tests that vary one field at a time.
static EffectRecipe base_recipe(void) {
    EffectRecipe r;
    memset(&r, 0, sizeof(r));
    r.version        = RECIPE_VERSION;
    r.palette_kind   = PALETTE_SOLID;
    r.palette_interp = INTERP_BLEND;
    r.stop_count     = 1;
    r.stops[0]       = {0, 255, 255};
    r.spatial        = SPATIAL_UNIFORM;
    r.repeat         = 1;
    r.motion         = MOTION_STILL;
    r.speed          = 128;
    r.envelope       = ENV_NONE;
    r.brightness     = 255;
    return r;
}

// ── the wire format ───────────────────────────────────────────────────────

// EffectRecipe is written to NVS and sent over the air verbatim, so its size
// is a contract with the stored data and with the designer, not an
// implementation detail. If this fails, RECIPE_VERSION needs bumping and the
// payload budget needs rechecking -- do not just update the number.
void test_recipe_size_is_the_wire_format(void) {
    TEST_ASSERT_EQUAL_UINT32(27, (uint32_t)sizeof(EffectRecipe));
}

void test_every_builtin_recipe_is_valid(void) {
    for (unsigned i = 0; i < RECIPE_BUILTIN_COUNT; i++) {
        TEST_ASSERT_TRUE_MESSAGE(recipe_is_valid(kDefaultRecipes[i]),
                                 "a shipped default failed validation");
    }
}

// Every rejection below is a byte that arrives straight off the air.
void test_validation_rejects_unknown_version(void) {
    EffectRecipe r = base_recipe();
    r.version = RECIPE_VERSION + 1;
    TEST_ASSERT_FALSE(recipe_is_valid(r));
}

void test_validation_rejects_out_of_range_enums(void) {
    EffectRecipe r = base_recipe();
    r.palette_kind = PALETTE_KIND_COUNT;   TEST_ASSERT_FALSE(recipe_is_valid(r));
    r = base_recipe(); r.palette_interp = INTERP_COUNT;  TEST_ASSERT_FALSE(recipe_is_valid(r));
    r = base_recipe(); r.spatial        = SPATIAL_COUNT; TEST_ASSERT_FALSE(recipe_is_valid(r));
    r = base_recipe(); r.motion         = MOTION_COUNT;  TEST_ASSERT_FALSE(recipe_is_valid(r));
    r = base_recipe(); r.envelope       = ENV_COUNT;     TEST_ASSERT_FALSE(recipe_is_valid(r));
}

// stop_count indexes stops[]; an out-of-range value would read past the array.
void test_validation_rejects_bad_stop_count(void) {
    EffectRecipe r = base_recipe();
    r.stop_count = 0;                     TEST_ASSERT_FALSE(recipe_is_valid(r));
    r.stop_count = RECIPE_MAX_STOPS + 1;  TEST_ASSERT_FALSE(recipe_is_valid(r));
    r.stop_count = RECIPE_MAX_STOPS;      TEST_ASSERT_TRUE(recipe_is_valid(r));
}

void test_validation_rejects_bad_repeat(void) {
    EffectRecipe r = base_recipe();
    r.repeat = 0;   TEST_ASSERT_FALSE(recipe_is_valid(r));
    r.repeat = 17;  TEST_ASSERT_FALSE(recipe_is_valid(r));
    r.repeat = 16;  TEST_ASSERT_TRUE(recipe_is_valid(r));
}

// ── the arithmetic the JavaScript twin has to reproduce ───────────────────

// fx_hash is pinned by value, not by property. The designer's simulator
// reimplements it in a language with no native uint32 arithmetic -- `*`
// overflows into doubles and shifts are signed -- so these constants are the
// contract the JS version has to meet with Math.imul and `>>> 0`.
void test_fx_hash_is_pinned_to_known_values(void) {
    // Zero is a fixed point of this hash family, which is why the sparkle
    // renderer hashes `i + 1` rather than `i` -- pixel 0 would otherwise get a
    // degenerate phase.
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, fx_hash(0));
    TEST_ASSERT_EQUAL_UINT32(0x688990C0u, fx_hash(1));
    TEST_ASSERT_EQUAL_UINT32(0xD1132181u, fx_hash(2));
    TEST_ASSERT_EQUAL_UINT32(0x08718688u, fx_hash(62));
    TEST_ASSERT_EQUAL_UINT32(0x6768824Au, fx_hash(0xFFFFFFFFu));
}

// 255 must be an exact identity, or a recipe at full everything would come out
// one count short -- the same off-by-one scale_level() works around.
void test_combine_is_exact_at_full_scale(void) {
    TEST_ASSERT_EQUAL_UINT8(255, recipe_combine(255, 255, 255, 255));
    TEST_ASSERT_EQUAL_UINT8(128, recipe_combine(128, 255, 255, 255));
    TEST_ASSERT_EQUAL_UINT8(0,   recipe_combine(255, 255, 255, 0));
}

void test_circular_distance_takes_the_short_way(void) {
    TEST_ASSERT_EQUAL_UINT8(0,   recipe_circular_distance(0, 0));
    TEST_ASSERT_EQUAL_UINT8(10,  recipe_circular_distance(10, 0));
    TEST_ASSERT_EQUAL_UINT8(1,   recipe_circular_distance(0, 255));   // across the wrap
    TEST_ASSERT_EQUAL_UINT8(128, recipe_circular_distance(0, 128));   // the far side
    TEST_ASSERT_EQUAL_UINT8(6,   recipe_circular_distance(250, 0));
}

// ── rendering ─────────────────────────────────────────────────────────────

void test_light_off_blanks_the_ring(void) {
    memset(leds, 0xFF, sizeof(leds));
    recipe_render(kDefaultRecipes[0], 1234, 255, false, leds, RING_NUM_LEDS);
    TEST_ASSERT_EQUAL_INT(0, lit_count());
}

void test_uniform_spatial_lights_every_pixel_the_same(void) {
    EffectRecipe r = base_recipe();
    recipe_render(r, 500, 255, true, leds, RING_NUM_LEDS);
    TEST_ASSERT_EQUAL_INT(RING_NUM_LEDS, lit_count());
    for (int i = 1; i < RING_NUM_LEDS; i++)
        TEST_ASSERT_TRUE_MESSAGE(same_pixel(leds[0], leds[i]), "uniform ring is not uniform");
}

// Chase is a segment of one pixel, and the direction it travels is now a
// property of the recipe rather than an accident of how each effect was
// written -- the two hand-written effects disagreed about it.
void test_segment_lights_a_narrow_band_that_travels_forward(void) {
    const EffectRecipe& chase = kDefaultRecipes[4];
    int first = -1, later = -1;
    recipe_render(chase, 0, 255, true, leds, RING_NUM_LEDS);
    for (int i = 0; i < RING_NUM_LEDS; i++) if (leds[i].r) { first = i; break; }
    TEST_ASSERT_TRUE_MESSAGE(lit_count() <= 2, "chase lit more than a narrow band");
    TEST_ASSERT_EQUAL_INT(0, first);

    recipe_render(chase, 600, 255, true, leds, RING_NUM_LEDS);
    for (int i = 0; i < RING_NUM_LEDS; i++) if (leds[i].r) { later = i; break; }
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(first, later, "chase did not travel forward");
}

// The one-byte flag that made keyframe sequences unnecessary: with no
// interpolation, N stops are N discrete colours rather than a gradient.
void test_step_interpolation_produces_only_the_stop_colours(void) {
    EffectRecipe r = base_recipe();
    r.palette_kind   = PALETTE_STOPS;
    r.palette_interp = INTERP_STEP;
    r.stop_count     = 2;
    r.stops[0]       = {0,   255, 255};    // red
    r.stops[1]       = {160, 255, 255};    // blue
    r.spatial        = SPATIAL_GRADIENT;

    recipe_render(r, 0, 255, true, leds, RING_NUM_LEDS);

    // Every pixel is one of exactly two colours -- no blended intermediates.
    int distinct = 0;
    CRGB seen[4];
    for (int i = 0; i < RING_NUM_LEDS; i++) {
        bool found = false;
        for (int k = 0; k < distinct; k++) if (same_pixel(seen[k], leds[i])) found = true;
        if (!found) {
            TEST_ASSERT_LESS_THAN_INT_MESSAGE(4, distinct, "step palette blended");
            seen[distinct++] = leds[i];
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, distinct, "two stepped stops should give two colours");
}

void test_blend_interpolation_produces_intermediates(void) {
    EffectRecipe r = base_recipe();
    r.palette_kind   = PALETTE_STOPS;
    r.palette_interp = INTERP_BLEND;
    r.stop_count     = 2;
    r.stops[0]       = {0,   255, 255};
    r.stops[1]       = {160, 255, 255};
    r.spatial        = SPATIAL_GRADIENT;

    recipe_render(r, 0, 255, true, leds, RING_NUM_LEDS);

    int distinct = 0;
    for (int i = 1; i < RING_NUM_LEDS; i++) if (!same_pixel(leds[i], leds[i-1])) distinct++;
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(10, distinct, "blended gradient looks stepped");
}

// Depth and shape are independent controls: depth 0 must leave the effect
// unmodulated no matter which envelope is selected.
void test_envelope_depth_zero_does_not_modulate(void) {
    EffectRecipe r = base_recipe();
    r.envelope       = ENV_BREATHE;
    r.envelope_depth = 0;
    r.envelope_speed = 128;

    recipe_render(r, 0, 255, true, leds, RING_NUM_LEDS);
    const CRGB at_zero = leds[0];
    for (uint32_t t = 0; t < 4000; t += 137) {
        recipe_render(r, t, 255, true, leds, RING_NUM_LEDS);
        TEST_ASSERT_TRUE_MESSAGE(same_pixel(at_zero, leds[0]), "depth 0 still modulated");
    }
}

void test_breathing_at_full_depth_spans_dark_to_bright(void) {
    const EffectRecipe& breathing = kDefaultRecipes[2];
    int lo = 999, hi = -1;
    for (uint32_t t = 0; t < 6000; t += 20) {
        recipe_render(breathing, t, 255, true, leds, RING_NUM_LEDS);
        if (leds[0].r < lo) lo = leds[0].r;
        if (leds[0].r > hi) hi = leds[0].r;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lo, "breathing never reached its trough");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(200, hi, "breathing never reached its peak");
}

void test_brightness_zero_is_fully_dark(void) {
    EffectRecipe r = base_recipe();
    r.brightness = 0;
    recipe_render(r, 500, 255, true, leds, RING_NUM_LEDS);
    TEST_ASSERT_EQUAL_INT(0, lit_count());
}

// Same inputs, same frame -- the property the golden vectors depend on, and
// the one a stateful PRNG would have broken.
void test_rendering_is_deterministic(void) {
    CRGB first[RING_NUM_LEDS];
    EffectRecipe r = base_recipe();
    r.spatial = SPATIAL_SPARKLE;
    r.motion  = MOTION_ROTATE;

    recipe_render(r, 4321, 200, true, first, RING_NUM_LEDS);
    for (int pass = 0; pass < 3; pass++) {
        recipe_render(r, 4321, 200, true, leds, RING_NUM_LEDS);
        for (int i = 0; i < RING_NUM_LEDS; i++)
            TEST_ASSERT_TRUE_MESSAGE(same_pixel(first[i], leds[i]), "render is not deterministic");
    }
}

// ── the bench finding this renderer was built to fix ──────────────────────

// The bench found effects collapsing to black below roughly brightness 20
// (2026-08-18, section 7): a gradient's mid-tone pixels multiplied by a small
// gamma multiplier truncated to zero, so most of the ring went dark while a
// solid colour stayed lit.
//
// Measured on the old hand-written warm gradient, at the effect's own
// brightness: 0 of 62 pixels lit at 8 and 12, and 53 of 62 at 16 and 20 -- a
// ring with holes in it. The recipe renderer combines its four perceptual
// factors before curving and rounds the final scale instead of truncating,
// which is what keeps the whole ring lit this far down.
void test_gradient_does_not_collapse_at_the_low_end(void) {
    EffectRecipe r = kDefaultRecipes[0];          // Warm Gradient
    const uint8_t levels[] = {8, 12, 16, 20, 24, 32};
    for (unsigned k = 0; k < sizeof(levels); k++) {
        r.brightness = levels[k];
        recipe_render(r, 500, 255, true, leds, RING_NUM_LEDS);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RING_NUM_LEDS, lit_count(),
                                      "a gradient pixel fell off the bottom of the range");
    }
}

// The floor has to hold for solid colours too, and all the way to 1.
void test_no_nonzero_brightness_renders_black(void) {
    EffectRecipe r = base_recipe();
    for (int b = 1; b < 256; b++) {
        r.brightness = (uint8_t)b;
        recipe_render(r, 0, 255, true, leds, RING_NUM_LEDS);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RING_NUM_LEDS, lit_count(),
                                      "a non-zero brightness rendered fully dark");
    }
}

// The entity's level scales a designed effect without editing it, and must not
// reintroduce a truncation that darkens the recipe at full level.
void test_entity_level_255_leaves_the_recipe_untouched(void) {
    EffectRecipe r = base_recipe();
    r.brightness = 200;
    recipe_render(r, 0, 255, true, leds, RING_NUM_LEDS);
    const CRGB at_full = leds[0];

    r.brightness = 200;
    recipe_render(r, 0, 128, true, leds, RING_NUM_LEDS);
    TEST_ASSERT_LESS_THAN_UINT8_MESSAGE(at_full.r, leds[0].r, "half level did not dim");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_recipe_size_is_the_wire_format);
    RUN_TEST(test_every_builtin_recipe_is_valid);
    RUN_TEST(test_validation_rejects_unknown_version);
    RUN_TEST(test_validation_rejects_out_of_range_enums);
    RUN_TEST(test_validation_rejects_bad_stop_count);
    RUN_TEST(test_validation_rejects_bad_repeat);
    RUN_TEST(test_fx_hash_is_pinned_to_known_values);
    RUN_TEST(test_combine_is_exact_at_full_scale);
    RUN_TEST(test_circular_distance_takes_the_short_way);
    RUN_TEST(test_light_off_blanks_the_ring);
    RUN_TEST(test_uniform_spatial_lights_every_pixel_the_same);
    RUN_TEST(test_segment_lights_a_narrow_band_that_travels_forward);
    RUN_TEST(test_step_interpolation_produces_only_the_stop_colours);
    RUN_TEST(test_blend_interpolation_produces_intermediates);
    RUN_TEST(test_envelope_depth_zero_does_not_modulate);
    RUN_TEST(test_breathing_at_full_depth_spans_dark_to_bright);
    RUN_TEST(test_brightness_zero_is_fully_dark);
    RUN_TEST(test_rendering_is_deterministic);
    RUN_TEST(test_gradient_does_not_collapse_at_the_low_end);
    RUN_TEST(test_no_nonzero_brightness_renders_black);
    RUN_TEST(test_entity_level_255_leaves_the_recipe_untouched);
    return UNITY_END();
}

// Regenerates tools/designer/golden-vectors.json from the firmware renderer.
//
// The designer's simulator reimplements src/recipe_render.h in JavaScript so a
// user can see an effect before it reaches a ceiling they cannot watch while
// designing. Two implementations of one renderer drift, and a drifting preview
// is worse than no preview -- it lies with confidence.
//
// This dumps frames rendered by the real firmware code; tools/designer/test/
// render.test.mjs asserts the JavaScript reproduces them byte for byte. The
// recipe is emitted as its 27 wire bytes rather than as named fields, so the
// vectors pin the struct layout and the decoder as well as the arithmetic.
//
// Build and run from the repo root:
//
//     g++ -std=gnu++17 -I src -o build/gen-golden scripts/gen-golden-vectors.cpp
//     ./build/gen-golden > tools/designer/golden-vectors.json
//
// Cases are chosen for the places the two implementations are most likely to
// disagree, not for the ones that look best: the hash-driven paths, and the
// bottom of the brightness range where the collapse finding lives.

#include "recipe_render.h"
#include <cstdio>
#include <cstring>
#include <string>

static std::string hex_bytes(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        out += d[p[i] >> 4];
        out += d[p[i] & 0xF];
    }
    return out;
}

static EffectRecipe base(void) {
    EffectRecipe r;
    memset(&r, 0, sizeof(r));
    r.version = RECIPE_VERSION;
    r.palette_kind = PALETTE_SOLID;
    r.palette_interp = INTERP_BLEND;
    r.stop_count = 1;
    r.stops[0] = {0, 255, 255};
    r.spatial = SPATIAL_UNIFORM;
    r.repeat = 1;
    r.motion = MOTION_STILL;
    r.speed = 128;
    r.envelope = ENV_NONE;
    r.brightness = 255;
    return r;
}

struct Case {
    const char*  name;
    EffectRecipe recipe;
    uint8_t      level;
};

int main(void) {
    Case cases[24];
    int  n = 0;

    // The six built-ins at full level: the effects the fixture already ships.
    static const char* builtin_names[] = {
        "warm_gradient", "color_gradient", "breathing",
        "color_cycle",   "chase",          "nightlight",
    };
    for (unsigned i = 0; i < RECIPE_BUILTIN_COUNT; i++)
        cases[n++] = {builtin_names[i], kDefaultRecipes[i], 255};

    // The bottom of the range, where the bench found effects collapsing to
    // black. A simulator that flatters the design here is worse than useless:
    // with no live preview this is the only place the user can discover it.
    for (uint8_t b : {8, 16, 24}) {
        EffectRecipe r = kDefaultRecipes[0];
        r.brightness = b;
        static char names[3][32];
        static int  k = 0;
        snprintf(names[k], sizeof(names[k]), "warm_gradient_bri_%u", b);
        cases[n++] = {names[k++], r, 255};
    }

    // Entity level scaling on top of the recipe's own brightness.
    cases[n++] = {"warm_gradient_level_64", kDefaultRecipes[0], 64};

    // Stepped palette: the one-byte flag that made keyframes unnecessary.
    {
        EffectRecipe r = base();
        r.palette_kind = PALETTE_STOPS;
        r.palette_interp = INTERP_STEP;
        r.stop_count = 2;
        r.stops[0] = {0, 255, 255};      // red
        r.stops[1] = {160, 255, 255};    // blue
        r.spatial = SPATIAL_UNIFORM;
        r.motion = MOTION_ROTATE;
        r.speed = 200;
        cases[n++] = {"police_stepped", r, 255};
    }

    // Four stepped stops laid around the ring -- hard-edged colour blocks.
    {
        EffectRecipe r = base();
        r.palette_kind = PALETTE_STOPS;
        r.palette_interp = INTERP_STEP;
        r.stop_count = 4;
        r.stops[0] = {0, 255, 255};
        r.stops[1] = {64, 255, 200};
        r.stops[2] = {128, 255, 255};
        r.stops[3] = {200, 200, 180};
        r.spatial = SPATIAL_GRADIENT;
        r.motion = MOTION_ROTATE;
        r.speed = 40;
        cases[n++] = {"team_colours", r, 255};
    }

    // Segment with a tail: exercises the falloff ramp and the per-pixel
    // palette sampling inside the band.
    {
        EffectRecipe r = base();
        r.palette_kind = PALETTE_STOPS;
        r.stop_count = 2;
        r.stops[0] = {30, 255, 255};
        r.stops[1] = {30, 255, 0};
        r.spatial = SPATIAL_SEGMENT;
        r.span = 12;
        r.falloff = 40;
        r.motion = MOTION_ROTATE;
        r.speed = 150;
        cases[n++] = {"comet", r, 255};
    }

    // Two chasers, to pin the `repeat` arithmetic.
    {
        EffectRecipe r = base();
        r.spatial = SPATIAL_SEGMENT;
        r.span = 8;
        r.falloff = 16;
        r.repeat = 2;
        r.motion = MOTION_ROTATE;
        r.speed = 160;
        cases[n++] = {"twin_pulse", r, 255};
    }

    // Bounce, which is the only motion mode that reverses mid-period.
    {
        EffectRecipe r = base();
        r.spatial = SPATIAL_SEGMENT;
        r.span = 10;
        r.falloff = 20;
        r.motion = MOTION_BOUNCE;
        r.speed = 100;
        cases[n++] = {"bounce", r, 255};
    }

    // == the hash-driven paths ==
    // JavaScript has no native uint32 arithmetic, so these two are where a
    // ported renderer is most likely to look plausible and be wrong.
    {
        EffectRecipe r = base();
        r.palette_kind = PALETTE_HUE_RAMP;
        r.spatial = SPATIAL_SPARKLE;
        r.speed = 180;
        cases[n++] = {"sparkle", r, 255};
    }
    {
        EffectRecipe r = base();
        r.stops[0] = {21, 200, 255};
        r.spatial = SPATIAL_UNIFORM;
        r.envelope = ENV_NOISE;
        r.envelope_depth = 160;
        r.envelope_speed = 190;
        r.brightness = 180;
        cases[n++] = {"candle", r, 255};
    }

    // The remaining envelope shapes.
    for (int e = ENV_PULSE; e <= ENV_SAW; e++) {
        EffectRecipe r = base();
        r.envelope = uint8_t(e);
        r.envelope_depth = 200;
        r.envelope_speed = 120;
        cases[n++] = {e == ENV_PULSE ? "envelope_pulse" : "envelope_saw", r, 255};
    }

    // Frame times chosen to land at different points in a period, including 0
    // and a time past several wraps.
    static const uint32_t times[] = {0, 137, 1500, 4321, 9999};

    CRGB leds[RING_NUM_LEDS];
    printf("{\n");
    printf("  \"_comment\": \"Generated by scripts/gen-golden-vectors.cpp. "
           "Do not edit by hand -- regenerate.\",\n");
    printf("  \"recipe_version\": %d,\n", RECIPE_VERSION);
    printf("  \"recipe_bytes\": %u,\n", (unsigned)sizeof(EffectRecipe));
    printf("  \"ring_pixels\": %d,\n", RING_NUM_LEDS);
    printf("  \"cases\": [\n");

    for (int c = 0; c < n; c++) {
        uint8_t raw[sizeof(EffectRecipe)];
        memcpy(raw, &cases[c].recipe, sizeof(raw));

        printf("    {\n");
        printf("      \"name\": \"%s\",\n", cases[c].name);
        printf("      \"level\": %u,\n", cases[c].level);
        printf("      \"bytes\": \"%s\",\n", hex_bytes(raw, sizeof(raw)).c_str());
        printf("      \"frames\": [\n");

        const int frame_count = (int)(sizeof(times) / sizeof(times[0]));
        for (int f = 0; f < frame_count; f++) {
            recipe_render(cases[c].recipe, times[f], cases[c].level, true,
                          leds, RING_NUM_LEDS);
            uint8_t flat[RING_NUM_LEDS * 3];
            for (int i = 0; i < RING_NUM_LEDS; i++) {
                flat[i * 3 + 0] = leds[i].r;
                flat[i * 3 + 1] = leds[i].g;
                flat[i * 3 + 2] = leds[i].b;
            }
            printf("        {\"t\": %u, \"px\": \"%s\"}%s\n",
                   times[f], hex_bytes(flat, sizeof(flat)).c_str(),
                   f + 1 < frame_count ? "," : "");
        }
        printf("      ]\n");
        printf("    }%s\n", c + 1 < n ? "," : "");
    }

    printf("  ]\n}\n");
    return 0;
}

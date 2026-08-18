#pragma once
#include <stdint.h>

// A ring effect described as DATA rather than as a C function.
//
// The six built-in effects were never six ideas -- they were one idea with six
// settings. Every one of them is a corner of the same four-axis space:
//
//     palette   x   spatial mapping   x   motion   x   envelope
//
// so `EffectRecipe` describes a point in that space and recipe_render.h walks
// it. That is what lets a user design an effect in a browser and push the
// result to the fixture: the firmware never learns a new effect, it just reads
// a different 27 bytes.
//
// Deliberately free of ESP-IDF headers, and of config.h -- see ring_geometry.h.
// The whole struct is host-testable, and has to be: the designer's simulator
// reimplements this renderer in JavaScript, and golden vectors generated from
// the host build are what stop the two drifting apart.
//
// FIELD ORDER AND SIZE ARE WIRE FORMAT. This struct is written to NVS and sent
// over the air verbatim. Adding a field means bumping RECIPE_VERSION and
// teaching recipe_is_valid() about the old shape -- never silently reusing a
// byte.

#define RECIPE_VERSION      1
#define RECIPE_MAX_STOPS    4

// ── palette: what colours the effect is made of ───────────────────────────
enum PaletteKind : uint8_t {
    PALETTE_SOLID    = 0,   // stops[0] everywhere
    PALETTE_STOPS    = 1,   // stop_count colours, cyclic
    PALETTE_HUE_RAMP = 2,   // a full 256-step hue sweep from stops[0].h
    PALETTE_KIND_COUNT
};

// The highest-value byte in the struct, and it was found by writing out the
// preset gallery rather than by designing the format.
//
// BLEND makes four stops a gradient. STEP makes the same four stops a
// SEQUENCE -- four colours held and switched with no interpolation -- which is
// most of what keyframes were wanted for, at a cost of one byte and no
// variable-length data. Police lights are two stepped stops rotating; team
// colours are four. What STEP still cannot do is unequal hold times, per-step
// fade durations, or more than RECIPE_MAX_STOPS colours.
enum PaletteInterp : uint8_t {
    INTERP_BLEND = 0,
    INTERP_STEP  = 1,
    INTERP_COUNT
};

// ── spatial: how the palette maps across the 62 pixels ────────────────────
enum Spatial : uint8_t {
    SPATIAL_UNIFORM  = 0,   // every pixel the same colour -- the whole ring is one sample
    SPATIAL_GRADIENT = 1,   // the palette laid out around the ring
    SPATIAL_SEGMENT  = 2,   // a lit band of `span` pixels, the rest dark
    SPATIAL_SPARKLE  = 3,   // per-pixel twinkle at independent phases
    SPATIAL_COUNT
};

// ── motion: how the pattern moves over time ───────────────────────────────
enum Motion : uint8_t {
    MOTION_STILL  = 0,
    MOTION_ROTATE = 1,      // travels one full cycle per period, wrapping
    MOTION_BOUNCE = 2,      // sweeps out and back within the period
    MOTION_COUNT
};

// ── envelope: brightness modulation over time ─────────────────────────────
enum Envelope : uint8_t {
    ENV_NONE    = 0,
    ENV_BREATHE = 1,        // symmetric triangle -- the built-in Breathing effect
    ENV_PULSE   = 2,        // fast attack, slow decay
    ENV_SAW     = 3,        // ramp up, hard reset
    ENV_NOISE   = 4,        // seeded pseudo-random flicker, for candle
    ENV_COUNT
};

// A palette stop. `v` is NOT baked into the colour: it is carried through the
// renderer as a perceptual weight and folded in with brightness and the
// envelope at the single point where the gamma curve is applied. Baking it in
// here would pre-scale the pixel and reintroduce exactly the double-scaling
// behind the bench's low-end collapse finding (2026-08-18, section 7).
struct RecipeStop {
    uint8_t h, s, v;
};

struct EffectRecipe {
    uint8_t    version;         // RECIPE_VERSION; rejected outright if unknown
    uint8_t    palette_kind;    // PaletteKind
    uint8_t    palette_interp;  // PaletteInterp
    uint8_t    stop_count;      // 1..RECIPE_MAX_STOPS
    RecipeStop stops[RECIPE_MAX_STOPS];

    uint8_t    spatial;         // Spatial
    uint8_t    span;            // SEGMENT: lit width, in 1/255 of the ring
    uint8_t    falloff;         // SEGMENT: tail length, 0 = hard edge
    uint8_t    repeat;          // copies of the pattern around the ring, 1..16

    uint8_t    motion;          // Motion
    uint8_t    direction;       // 0 = forward, 1 = reversed
    uint8_t    speed;           // 0 = slowest, 255 = fastest

    uint8_t    envelope;        // Envelope
    uint8_t    envelope_depth;  // 0 = no modulation, 255 = full swing to black
    uint8_t    envelope_speed;

    uint8_t    brightness;      // the recipe's own level, before the entity's
};

// 27 bytes. Kept small on purpose: it has to fit in one ZCL command alongside
// a slot index, and the payload budget on an 802.15.4 frame is roughly 60-70
// bytes after headers.
static_assert(sizeof(EffectRecipe) == 27, "EffectRecipe is wire format -- "
              "size changed, bump RECIPE_VERSION and check the payload budget");

// Validated before anything is stored or rendered, because a recipe arrives
// straight off the air. Rejects rather than clamps, matching ring_set_scene():
// a recipe that means something other than what was sent is worse than one
// that is refused, and the caller can say so.
inline bool recipe_is_valid(const EffectRecipe& r) {
    if (r.version != RECIPE_VERSION)                 return false;
    if (r.palette_kind   >= PALETTE_KIND_COUNT)      return false;
    if (r.palette_interp >= INTERP_COUNT)            return false;
    if (r.stop_count == 0 || r.stop_count > RECIPE_MAX_STOPS) return false;
    if (r.spatial  >= SPATIAL_COUNT)                 return false;
    if (r.motion   >= MOTION_COUNT)                  return false;
    if (r.envelope >= ENV_COUNT)                     return false;
    if (r.repeat == 0 || r.repeat > 16)              return false;
    return true;
}

// ── the six built-ins, as recipes ─────────────────────────────────────────
// These seed the NVS store and are what Home Assistant's effect_list names.
// Reimplementing them on the recipe engine BEFORE any custom slot exists is
// the point: it proves the format is expressive enough against known-good
// output, with nothing else changing.
//
// They reproduce the old hand-written effects closely but not bit-identically.
// The old functions blended in RGB and picked colours as literal channel
// triples; stops are HSV here, and the round trip moves a channel by a count
// or two (warm gradient's {200,100,0} becomes hue 21 at v 200, which renders
// {255,126,0} scaled by 200 rather than {200,100,0}). Chasing exact equality
// would mean carrying an RGB stop format for the sake of six frozen presets.
static const EffectRecipe kDefaultRecipes[] = {
    // Warm Gradient -- amber to blue, sweeping round the ring.
    {RECIPE_VERSION, PALETTE_STOPS, INTERP_BLEND, 2,
     {{21, 255, 200}, {170, 255, 255}, {0,0,0}, {0,0,0}},
     SPATIAL_GRADIENT, 0, 0, 1,
     MOTION_ROTATE, 0, 60,
     ENV_NONE, 0, 0,
     200},

    // Color Gradient -- the full hue circle laid around the ring, rotating.
    {RECIPE_VERSION, PALETTE_HUE_RAMP, INTERP_BLEND, 1,
     {{0, 255, 255}, {0,0,0}, {0,0,0}, {0,0,0}},
     SPATIAL_GRADIENT, 0, 0, 1,
     MOTION_ROTATE, 0, 60,
     ENV_NONE, 0, 0,
     200},

    // Breathing -- one colour, whole ring, pulsing in and out.
    {RECIPE_VERSION, PALETTE_SOLID, INTERP_BLEND, 1,
     {{0, 255, 255}, {0,0,0}, {0,0,0}, {0,0,0}},
     SPATIAL_UNIFORM, 0, 0, 1,
     MOTION_STILL, 0, 0,
     ENV_BREATHE, 255, 80,
     255},

    // Color Cycle -- whole ring one colour, the colour walking the hue circle.
    {RECIPE_VERSION, PALETTE_HUE_RAMP, INTERP_BLEND, 1,
     {{0, 255, 255}, {0,0,0}, {0,0,0}, {0,0,0}},
     SPATIAL_UNIFORM, 0, 0, 1,
     MOTION_ROTATE, 0, 100,
     ENV_NONE, 0, 0,
     200},

    // Chase -- a single lit pixel travelling round. span 4/255 of 62 px = 1 px.
    {RECIPE_VERSION, PALETTE_SOLID, INTERP_BLEND, 1,
     {{0, 255, 255}, {0,0,0}, {0,0,0}, {0,0,0}},
     SPATIAL_SEGMENT, 4, 0, 1,
     MOTION_ROTATE, 0, 120,
     ENV_NONE, 0, 0,
     255},

    // Nightlight -- dim warm white, still. Mixed from the RGB dice; the ring
    // has no white die.
    {RECIPE_VERSION, PALETTE_SOLID, INTERP_BLEND, 1,
     {{21, 180, 255}, {0,0,0}, {0,0,0}, {0,0,0}},
     SPATIAL_UNIFORM, 0, 0, 1,
     MOTION_STILL, 0, 0,
     ENV_NONE, 0, 0,
     50},
};

#define RECIPE_BUILTIN_COUNT (sizeof(kDefaultRecipes) / sizeof(kDefaultRecipes[0]))

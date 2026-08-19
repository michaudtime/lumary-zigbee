// The starter gallery.
//
// Nobody should meet this tool as a blank panel of fifteen sliders. These are
// read-only starting points: pick one, tweak it, name it, push it. Between
// them they also teach the parameter space by example -- the fastest way to
// understand what `falloff` does is to load Comet and drag it.
//
// Writing this list out was also how the recipe format got validated. Fourteen
// of the fifteen turned out to be expressible on the four axes, and the three
// fields that got them there -- falloff, repeat, palette_interp -- cost three
// bytes between them. That is the evidence behind shipping recipes alone and
// treating keyframe sequences as genuinely deferred rather than imminent. See
// docs/superpowers/specs/2026-08-18-custom-effects-brainstorm.md section 2.1.
//
// The first six are the fixture's built-in effects and must stay in step with
// kDefaultRecipes[] in src/effect_recipe.h. The golden vectors cover all six,
// so a drift here fails the test suite rather than shipping quietly.

import {
    RECIPE_VERSION,
    PALETTE_SOLID, PALETTE_STOPS, PALETTE_HUE_RAMP,
    INTERP_BLEND, INTERP_STEP,
    SPATIAL_UNIFORM, SPATIAL_GRADIENT, SPATIAL_SEGMENT, SPATIAL_SPARKLE,
    MOTION_STILL, MOTION_ROTATE, MOTION_BOUNCE,
    ENV_NONE, ENV_BREATHE, ENV_PULSE, ENV_SAW, ENV_NOISE,
} from './recipe.mjs';

const stop = (h, s, v) => ({h, s, v});
const NO_STOP = stop(0, 0, 0);

// Fills in the parts almost every preset leaves at their defaults, so each
// entry below shows only what makes it what it is.
//
// Named makeRecipe rather than recipe because the standalone build flattens
// every module into one scope, where `recipe` is the editor's working copy.
// scripts/build-designer-single.py fails loudly on a collision like that, but
// not colliding in the first place is better.
function makeRecipe(fields) {
    const stops = (fields.stops ?? [stop(0, 255, 255)]).slice(0, 4);
    return {
        version:        RECIPE_VERSION,
        palette_kind:   PALETTE_SOLID,
        palette_interp: INTERP_BLEND,
        stop_count:     stops.length,
        stops:          [...stops, NO_STOP, NO_STOP, NO_STOP].slice(0, 4),
        spatial:        SPATIAL_UNIFORM,
        span:           0,
        falloff:        0,
        repeat:         1,
        motion:         MOTION_STILL,
        direction:      0,
        speed:          128,
        envelope:       ENV_NONE,
        envelope_depth: 0,
        envelope_speed: 0,
        brightness:     255,
        ...fields,
        stops:          [...stops, NO_STOP, NO_STOP, NO_STOP].slice(0, 4),
        stop_count:     stops.length,
    };
}

export const PRESETS = [
    // ── the six the fixture already ships ─────────────────────────────────
    {
        name: 'Warm Gradient',
        builtin: true,
        note: 'Amber to blue, sweeping round the ring.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(21, 255, 200), stop(170, 255, 255)],
            spatial: SPATIAL_GRADIENT, motion: MOTION_ROTATE, speed: 60,
            brightness: 200,
        }),
    },
    {
        name: 'Color Gradient',
        builtin: true,
        note: 'The whole hue circle laid around the ring, rotating.',
        recipe: makeRecipe({
            palette_kind: PALETTE_HUE_RAMP,
            spatial: SPATIAL_GRADIENT, motion: MOTION_ROTATE, speed: 60,
            brightness: 200,
        }),
    },
    {
        name: 'Breathing',
        builtin: true,
        note: 'One colour, whole ring, pulsing in and out.',
        // speed 0 rather than the helper's default: motion is STILL here, so
        // the field is unread -- but it is still a wire byte, and it has to
        // match kDefaultRecipes[] exactly or the gallery ships a "Breathing"
        // that encodes differently from the one the fixture stores. The same
        // trap light_state.h documents for EffectParams::type under MODE_COLOR.
        recipe: makeRecipe({
            speed: 0,
            envelope: ENV_BREATHE, envelope_depth: 255, envelope_speed: 80,
        }),
    },
    {
        name: 'Color Cycle',
        builtin: true,
        note: 'The whole ring one colour, walking the hue circle.',
        recipe: makeRecipe({
            palette_kind: PALETTE_HUE_RAMP,
            motion: MOTION_ROTATE, speed: 100, brightness: 200,
        }),
    },
    {
        name: 'Chase',
        builtin: true,
        note: 'A single lit pixel travelling round.',
        recipe: makeRecipe({
            spatial: SPATIAL_SEGMENT, span: 4,
            motion: MOTION_ROTATE, speed: 120,
        }),
    },
    {
        name: 'Nightlight',
        builtin: true,
        note: 'Dim warm white, still. Mixed from the RGB dice.',
        recipe: makeRecipe({
            speed: 0,                       // unread under MOTION_STILL; see Breathing
            stops: [stop(21, 180, 255)], brightness: 50,
        }),
    },

    // ── derived starters ──────────────────────────────────────────────────
    {
        name: 'Comet',
        note: 'A bright head with a fading tail. Drag falloff to lengthen it.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(30, 255, 255), stop(30, 255, 0)],
            spatial: SPATIAL_SEGMENT, span: 12, falloff: 40,
            motion: MOTION_ROTATE, speed: 150,
        }),
    },
    {
        name: 'Slow Sunset',
        note: 'Amber through magenta to indigo, drifting slowly.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(16, 255, 255), stop(224, 220, 200), stop(170, 255, 150)],
            spatial: SPATIAL_GRADIENT, motion: MOTION_ROTATE, speed: 20,
            brightness: 220,
        }),
    },
    {
        name: 'Twin Pulse',
        note: 'Two chasers opposite each other. `repeat` is what doubles them.',
        recipe: makeRecipe({
            spatial: SPATIAL_SEGMENT, span: 8, falloff: 16, repeat: 2,
            motion: MOTION_ROTATE, speed: 160,
        }),
    },
    {
        name: 'Ocean Drift',
        note: 'Teals and blues, drifting, with a slow swell over the top.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(120, 255, 255), stop(150, 230, 200), stop(170, 255, 255)],
            spatial: SPATIAL_GRADIENT, motion: MOTION_ROTATE, speed: 30,
            envelope: ENV_BREATHE, envelope_depth: 90, envelope_speed: 20,
            brightness: 220,
        }),
    },
    {
        name: 'Police',
        note: 'Red and blue, alternating hard. Stepping is what stops it blending.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS, palette_interp: INTERP_STEP,
            stops: [stop(0, 255, 255), stop(160, 255, 255)],
            motion: MOTION_ROTATE, speed: 200,
        }),
    },
    {
        name: 'Team Colours',
        note: 'Up to four colours in sequence. Swap the stops for your own.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS, palette_interp: INTERP_STEP,
            stops: [stop(0, 255, 255), stop(64, 255, 200),
                    stop(128, 255, 255), stop(200, 200, 180)],
            spatial: SPATIAL_GRADIENT, motion: MOTION_ROTATE, speed: 40,
        }),
    },
    {
        name: 'Sparkle',
        note: 'Every pixel twinkles on its own phase.',
        recipe: makeRecipe({
            palette_kind: PALETTE_HUE_RAMP,
            spatial: SPATIAL_SPARKLE, speed: 180,
        }),
    },
    {
        name: 'Candle',
        note: 'Warm, with a noise envelope so it flickers instead of strobing.',
        recipe: makeRecipe({
            stops: [stop(21, 200, 255)],
            envelope: ENV_NOISE, envelope_depth: 160, envelope_speed: 190,
            brightness: 180,
        }),
    },
    {
        name: 'Heartbeat',
        note: 'Fast attack, slow decay. The pulse envelope, at depth.',
        recipe: makeRecipe({
            stops: [stop(250, 255, 255)],
            envelope: ENV_PULSE, envelope_depth: 230, envelope_speed: 150,
        }),
    },
    {
        name: 'Sweep',
        note: 'A band running out and back rather than round and round.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(100, 255, 255), stop(140, 255, 120)],
            spatial: SPATIAL_SEGMENT, span: 30, falloff: 30,
            motion: MOTION_BOUNCE, speed: 90,
        }),
    },
    {
        name: 'Rise',
        note: 'A saw envelope: swells, then resets. Slow it right down for a sunrise.',
        recipe: makeRecipe({
            palette_kind: PALETTE_STOPS,
            stops: [stop(10, 255, 255), stop(30, 200, 255)],
            spatial: SPATIAL_GRADIENT,
            envelope: ENV_SAW, envelope_depth: 210, envelope_speed: 40,
            brightness: 230,
        }),
    },
];

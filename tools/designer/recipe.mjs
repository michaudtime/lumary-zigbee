// The effect renderer, in JavaScript. A line-for-line port of
// src/recipe_render.h so the designer can show what the fixture will do.
//
// == Why this file is written the way it is ==
//
// Every operation here is integer arithmetic done the way C does it, because
// the browser and the firmware have to agree EXACTLY. The user designs against
// this simulator and pushes the result to a ceiling they cannot watch, so a
// preview that is close is a preview that lies.
//
// Two hazards drove the style, and both are covered by golden-vectors.json:
//
//   1. JavaScript has no uint32. `*` overflows into doubles and `<<`/`>>` are
//      signed, so fxHash uses Math.imul for every multiply and `>>> 0` after
//      every step. Getting this subtly wrong produces sparkle and candle
//      patterns that look plausible and are not what the light will do.
//
//   2. JavaScript division is floating point. Every `/` in the C is integer
//      division, so every one here is wrapped in Math.floor or `| 0`. A single
//      missed truncation shifts the low end by a count, which is exactly where
//      the ring has no resolution to spare.
//
// If you change this file, regenerate nothing -- change src/recipe_render.h
// first, regenerate the vectors, and make this match.

import {GAMMA8} from './gamma.mjs';

export const RECIPE_VERSION   = 1;
export const RECIPE_MAX_STOPS = 4;
export const RECIPE_BYTES     = 27;
export const RING_NUM_LEDS    = 62;

export const PALETTE_SOLID = 0, PALETTE_STOPS = 1, PALETTE_HUE_RAMP = 2;
export const INTERP_BLEND = 0, INTERP_STEP = 1;
export const SPATIAL_UNIFORM = 0, SPATIAL_GRADIENT = 1,
             SPATIAL_SEGMENT = 2, SPATIAL_SPARKLE = 3;
export const MOTION_STILL = 0, MOTION_ROTATE = 1, MOTION_BOUNCE = 2;
export const ENV_NONE = 0, ENV_BREATHE = 1, ENV_PULSE = 2,
             ENV_SAW = 3, ENV_NOISE = 4;

// ── the arithmetic primitives, matched to C ───────────────────────────────

const u8 = (v) => v & 0xff;

// C: (uint16_t(val) * scale) >> 8. Max 65025, so no overflow either side.
const scale8 = (val, scale) => (val * scale) >> 8;

// C: (uint16_t(v) * s) / 255, integer division.
const scaleBy255 = (v, s) => Math.floor((v * s) / 255);

// Chris Wellons' lowbias32. See the hazard note at the top: Math.imul does the
// 32-bit multiply JavaScript's `*` cannot, and `>>> 0` keeps the value unsigned
// after each step. Pinned by value in test/test_recipe on the firmware side.
export function fxHash(x) {
    x = x >>> 0;
    x ^= x >>> 16;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x ^= x >>> 15;
    x = Math.imul(x, 0x846ca68b) >>> 0;
    x ^= x >>> 16;
    return x >>> 0;
}

export const periodMs = (speed) =>
    200 + Math.floor(((10000 - 200) * (255 - speed)) / 255);

export function triangle(t, period) {
    if (period === 0) return 255;
    const half = Math.floor(period / 2);
    if (half === 0) return 255;
    return t < half
        ? u8(Math.floor((t * 255) / half))
        : u8(255 - Math.floor(((t - half) * 255) / half));
}

export function circularDistance(a, b) {
    const d = a - b;
    const m = d < 0 ? -d : d;
    return u8(m > 128 ? 256 - m : m);
}

// C's hsv_to_rgb from src/color.h, including its 43-per-sextant hue scale.
export function hsvToRgb(hue, sat, val) {
    if (sat === 0) return [val, val, val];
    const region = Math.floor(hue / 43);
    const remainder = (hue % 43) * 6;
    const p = scale8(val, 255 - sat);
    const q = scale8(val, 255 - scale8(sat, remainder));
    const t = scale8(val, 255 - scale8(sat, 255 - remainder));
    switch (region) {
        case 0:  return [val, t, p];
        case 1:  return [q, val, p];
        case 2:  return [p, val, t];
        case 3:  return [p, q, val];
        case 4:  return [t, p, val];
        default: return [val, p, q];
    }
}

// C: uint8_t(a + (int(b - a) * amount >> 8)). `>>` is arithmetic in both
// languages, so negative deltas round the same way.
const blendChannel = (a, b, amount) => u8(a + (((b - a) * amount) >> 8));

const blendRgb = (a, b, amount) => [
    blendChannel(a[0], b[0], amount),
    blendChannel(a[1], b[1], amount),
    blendChannel(a[2], b[2], amount),
];

// ── the renderer ──────────────────────────────────────────────────────────

export function paletteAt(r, pos) {
    if (r.palette_kind === PALETTE_SOLID) {
        return {rgb: hsvToRgb(r.stops[0].h, r.stops[0].s, 255), v: r.stops[0].v};
    }
    if (r.palette_kind === PALETTE_HUE_RAMP) {
        return {rgb: hsvToRgb(u8(r.stops[0].h + pos), r.stops[0].s, 255), v: r.stops[0].v};
    }

    const n = r.stop_count;
    const scaled = pos * n;
    const index = (scaled >> 8) & 0xff;
    const frac = scaled & 0xff;

    const a = r.stops[index < n ? index : n - 1];
    if (r.palette_interp === INTERP_STEP) {
        return {rgb: hsvToRgb(a.h, a.s, 255), v: a.v};
    }

    const b = r.stops[(index + 1) % n];
    const ca = hsvToRgb(a.h, a.s, 255);
    const cb = hsvToRgb(b.h, b.s, 255);
    return {rgb: blendRgb(ca, cb, frac), v: u8(a.v + (((b.v - a.v) * frac) >> 8))};
}

export function phaseAt(r, elapsedMs) {
    if (r.motion === MOTION_STILL) return 0;
    const period = periodMs(r.speed);
    const t = elapsedMs % period;
    const raw = r.motion === MOTION_BOUNCE
        ? triangle(t, period)
        : u8(Math.floor((t * 256) / period));
    return r.direction ? u8(255 - raw) : raw;
}

export function envelopeAt(r, elapsedMs) {
    if (r.envelope === ENV_NONE || r.envelope_depth === 0) return 255;

    const period = periodMs(r.envelope_speed);
    const t = elapsedMs % period;
    let raw;

    switch (r.envelope) {
        case ENV_BREATHE:
            raw = triangle(t, period);
            break;
        case ENV_PULSE: {
            const attack = Math.floor(period / 8);
            raw = t < attack && attack > 0
                ? u8(Math.floor((t * 255) / attack))
                : u8(255 - Math.floor(((t - attack) * 255) / (period - attack)));
            break;
        }
        case ENV_SAW:
            raw = u8(Math.floor((t * 255) / period));
            break;
        case ENV_NOISE: {
            const step = Math.floor(elapsedMs / period);
            const a = fxHash(step) & 0xff;
            const b = fxHash(step + 1) & 0xff;
            const mix = u8(Math.floor((t * 255) / period));
            raw = u8(a + (((b - a) * mix) >> 8));
            break;
        }
        default:
            raw = 255;
            break;
    }

    return u8(255 - scaleBy255(r.envelope_depth, u8(255 - raw)));
}

// Four perceptual factors, folded in two rounded halves because 255^4 does not
// fit in the 32 bits the C version has to work in.
export function combine(a, b, c, d) {
    const ab = Math.floor((a * b + 127) / 255);
    const cd = Math.floor((c * d + 127) / 255);
    return u8(Math.floor((ab * cd + 127) / 255));
}

// Curve once, scale once, ROUNDING rather than truncating. This is what keeps
// mid-gradient pixels alive at the bottom of the range.
export function scaleGamma(rgb, perceptual) {
    const g = GAMMA8[perceptual];
    return [
        Math.floor((rgb[0] * g + 127) / 255),
        Math.floor((rgb[1] * g + 127) / 255),
        Math.floor((rgb[2] * g + 127) / 255),
    ];
}

// Renders one frame. Returns a Uint8Array of count*3 bytes, RGB per pixel --
// the same layout the golden vectors record.
export function render(r, elapsedMs, level, lightOn, count = RING_NUM_LEDS) {
    const out = new Uint8Array(count * 3);
    if (!lightOn) return out;

    const phase = phaseAt(r, elapsedMs);
    const env = envelopeAt(r, elapsedMs);
    const base = scaleBy255(r.brightness, level);
    const repeat = r.repeat ? r.repeat : 1;

    const half = Math.floor(r.span / 2);
    const reach = half + r.falloff;

    for (let i = 0; i < count; i++) {
        const ringPos = u8(Math.floor((i * 256) / count));
        let pos = 0;
        let weight = 0;

        switch (r.spatial) {
            case SPATIAL_UNIFORM:
                pos = phase;
                weight = 255;
                break;

            case SPATIAL_GRADIENT:
                pos = u8(ringPos * repeat + 256 - phase);
                weight = 255;
                break;

            case SPATIAL_SEGMENT: {
                const local = u8(ringPos * repeat);
                const d = circularDistance(local, phase);
                if (d > reach) {
                    pos = 0;
                    weight = 0;
                } else if (d <= half || reach === 0) {
                    pos = u8(Math.floor((d * 255) / (reach ? reach : 1)));
                    weight = 255;
                } else {
                    pos = u8(Math.floor((d * 255) / reach));
                    weight = u8(255 - Math.floor(((d - half) * 255) / (reach - half)));
                }
                break;
            }

            case SPATIAL_SPARKLE:
            default: {
                const h = fxHash(i + 1);
                const offset = h & 0xff;
                const period = periodMs(r.speed);
                const t = (elapsedMs + Math.floor((offset * period) / 256)) % period;
                pos = (h >>> 8) & 0xff;
                weight = triangle(t, period);
                break;
            }
        }

        if (weight === 0) continue;

        const s = paletteAt(r, pos);
        const px = scaleGamma(s.rgb, combine(base, env, weight, s.v));
        out[i * 3 + 0] = px[0];
        out[i * 3 + 1] = px[1];
        out[i * 3 + 2] = px[2];
    }

    return out;
}

// ── the wire format ───────────────────────────────────────────────────────
// The same 27 bytes the firmware stores in NVS and receives over the air. The
// golden vectors carry recipes in this encoding, so the round trip is tested
// alongside the arithmetic rather than separately.

export function decode(bytes) {
    if (bytes.length !== RECIPE_BYTES) {
        throw new Error(`recipe must be ${RECIPE_BYTES} bytes, got ${bytes.length}`);
    }
    const stops = [];
    for (let k = 0; k < RECIPE_MAX_STOPS; k++) {
        stops.push({h: bytes[4 + k * 3], s: bytes[5 + k * 3], v: bytes[6 + k * 3]});
    }
    return {
        version:        bytes[0],
        palette_kind:   bytes[1],
        palette_interp: bytes[2],
        stop_count:     bytes[3],
        stops,
        spatial:        bytes[16],
        span:           bytes[17],
        falloff:        bytes[18],
        repeat:         bytes[19],
        motion:         bytes[20],
        direction:      bytes[21],
        speed:          bytes[22],
        envelope:       bytes[23],
        envelope_depth: bytes[24],
        envelope_speed: bytes[25],
        brightness:     bytes[26],
    };
}

export function encode(r) {
    const b = new Uint8Array(RECIPE_BYTES);
    b[0] = r.version ?? RECIPE_VERSION;
    b[1] = r.palette_kind;
    b[2] = r.palette_interp;
    b[3] = r.stop_count;
    for (let k = 0; k < RECIPE_MAX_STOPS; k++) {
        const s = r.stops[k] ?? {h: 0, s: 0, v: 0};
        b[4 + k * 3] = s.h;
        b[5 + k * 3] = s.s;
        b[6 + k * 3] = s.v;
    }
    b[16] = r.spatial;
    b[17] = r.span;
    b[18] = r.falloff;
    b[19] = r.repeat;
    b[20] = r.motion;
    b[21] = r.direction;
    b[22] = r.speed;
    b[23] = r.envelope;
    b[24] = r.envelope_depth;
    b[25] = r.envelope_speed;
    b[26] = r.brightness;
    return b;
}

// Mirrors recipe_is_valid() in src/effect_recipe.h. The firmware rejects rather
// than clamps, so the designer refuses to push what the light would refuse to
// store -- and can say why while the user is still looking at it.
export function validate(r) {
    if (r.version !== RECIPE_VERSION) return `unknown recipe version ${r.version}`;
    if (r.palette_kind > PALETTE_HUE_RAMP) return 'palette kind out of range';
    if (r.palette_interp > INTERP_STEP) return 'palette interpolation out of range';
    if (r.stop_count < 1 || r.stop_count > RECIPE_MAX_STOPS) return 'stop count out of range';
    if (r.spatial > SPATIAL_SPARKLE) return 'spatial mode out of range';
    if (r.motion > MOTION_BOUNCE) return 'motion mode out of range';
    if (r.envelope > ENV_NOISE) return 'envelope out of range';
    if (r.repeat < 1 || r.repeat > 16) return 'repeat out of range';
    return null;
}

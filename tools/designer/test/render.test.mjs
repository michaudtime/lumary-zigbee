// Asserts the JavaScript renderer reproduces the firmware's output exactly.
//
//     node tools/designer/test/render.test.mjs
//
// Zero dependencies, matching z2m/test/converter.test.mjs.
//
// This is the file that makes the designer trustworthy. The user designs an
// effect in a browser and pushes it to a ceiling they cannot watch while
// designing, so the simulator is the only feedback loop before the fixture. If
// these two renderers drift, the preview lies -- confidently, and about the
// low end first, which is where it matters most.
//
// The vectors in golden-vectors.json are generated from src/recipe_render.h by
// scripts/gen-golden-vectors.cpp. Regenerate them when the firmware renderer
// changes; never edit either side to make this pass without understanding
// which one is wrong.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

import {
    render, decode, encode, validate, fxHash,
    RECIPE_BYTES, RECIPE_VERSION,
} from '../recipe.mjs';
import {PRESETS} from '../presets.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const golden = JSON.parse(
    readFileSync(path.join(here, '..', 'golden-vectors.json'), 'utf8'),
);

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        passed++;
        console.log(`  ok   ${name}`);
    } catch (err) {
        failed++;
        console.log(`  FAIL ${name}`);
        console.log(`       ${err.message.split('\n')[0]}`);
    }
}

const fromHex = (s) =>
    Uint8Array.from(s.match(/../g).map((h) => parseInt(h, 16)));

// Reports the first disagreeing pixel rather than dumping 62 of them: the
// index and the two triples are what identify which branch of the renderer
// went wrong.
function assertPixelsMatch(actual, expected, label) {
    assert.equal(actual.length, expected.length, `${label}: wrong buffer length`);
    for (let i = 0; i < expected.length; i += 3) {
        if (
            actual[i] !== expected[i] ||
            actual[i + 1] !== expected[i + 1] ||
            actual[i + 2] !== expected[i + 2]
        ) {
            const px = i / 3;
            assert.fail(
                `${label}: pixel ${px} is ` +
                `[${actual[i]},${actual[i + 1]},${actual[i + 2]}], ` +
                `firmware says [${expected[i]},${expected[i + 1]},${expected[i + 2]}]`,
            );
        }
    }
}

console.log('\nthe vectors themselves');

test('the golden file matches this renderer\'s constants', () => {
    assert.equal(golden.recipe_version, RECIPE_VERSION,
        'recipe version changed -- the simulator needs updating, not the vectors');
    assert.equal(golden.recipe_bytes, RECIPE_BYTES);
    assert.equal(golden.ring_pixels, 62);
});

test('there are vectors to check', () => {
    assert.ok(golden.cases.length >= 15, 'suspiciously few cases');
    for (const c of golden.cases) assert.ok(c.frames.length > 0, `${c.name} has no frames`);
});

console.log('\nthe wire format');

test('every recipe decodes and re-encodes to the same bytes', () => {
    for (const c of golden.cases) {
        const bytes = fromHex(c.bytes);
        assert.equal(bytes.length, RECIPE_BYTES, `${c.name}: wrong byte count`);
        assert.deepEqual(
            Array.from(encode(decode(bytes))), Array.from(bytes),
            `${c.name}: encode(decode(x)) !== x`,
        );
    }
});

test('every recipe the firmware emitted passes the simulator\'s validation', () => {
    for (const c of golden.cases) {
        assert.equal(validate(decode(fromHex(c.bytes))), null, `${c.name} was rejected`);
    }
});

console.log('\nthe hash, ported to a language without uint32');

test('fxHash matches the values pinned in test/test_recipe', () => {
    assert.equal(fxHash(0) >>> 0, 0x00000000);
    assert.equal(fxHash(1) >>> 0, 0x688990c0);
    assert.equal(fxHash(2) >>> 0, 0xd1132181);
    assert.equal(fxHash(62) >>> 0, 0x08718688);
    assert.equal(fxHash(0xffffffff) >>> 0, 0x6768824a);
});

console.log('\nrendering, against the firmware\'s own output');

for (const c of golden.cases) {
    test(`${c.name} matches the firmware on every frame`, () => {
        const recipe = decode(fromHex(c.bytes));
        for (const frame of c.frames) {
            const actual = render(recipe, frame.t, c.level, true);
            assertPixelsMatch(actual, fromHex(frame.px), `${c.name} @ t=${frame.t}`);
        }
    });
}

console.log('\nthe preset gallery');

test('every preset is valid and survives a round trip', () => {
    for (const p of PRESETS) {
        assert.equal(validate(p.recipe), null, `${p.name} was rejected: ${validate(p.recipe)}`);
        const bytes = encode(p.recipe);
        assert.deepEqual(Array.from(encode(decode(bytes))), Array.from(bytes),
            `${p.name} does not survive encode/decode`);
    }
});

// The gallery's first six ARE the fixture's built-in effects, and the golden
// vectors were generated from the firmware's kDefaultRecipes[]. Comparing the
// two byte for byte is what stops the editor quietly offering a "Chase" that
// is not the Chase the light runs.
test('the built-in presets are byte-identical to the firmware defaults', () => {
    const builtins = PRESETS.filter((p) => p.builtin);
    assert.equal(builtins.length, 6, 'expected six built-in presets');

    for (let i = 0; i < builtins.length; i++) {
        const fromFirmware = fromHex(golden.cases[i].bytes);
        const fromGallery = encode(builtins[i].recipe);
        assert.deepEqual(
            Array.from(fromGallery), Array.from(fromFirmware),
            `preset "${builtins[i].name}" has drifted from the firmware default ` +
            `"${golden.cases[i].name}"`,
        );
    }
});

test('every preset renders without throwing', () => {
    for (const p of PRESETS) {
        for (const t of [0, 137, 4321]) {
            const out = render(p.recipe, t, 255, true);
            assert.equal(out.length, 62 * 3, `${p.name} rendered the wrong buffer`);
        }
    }
});

console.log('\nbehaviour the vectors do not cover');

test('a light that is off renders nothing at all', () => {
    const recipe = decode(fromHex(golden.cases[0].bytes));
    const out = render(recipe, 1234, 255, false);
    assert.ok(out.every((v) => v === 0), 'ring was not blank with the light off');
});

test('validation rejects what the firmware would reject', () => {
    const good = decode(fromHex(golden.cases[0].bytes));
    assert.equal(validate(good), null);
    assert.match(validate({...good, version: 99}), /version/);
    assert.match(validate({...good, stop_count: 0}), /stop count/);
    assert.match(validate({...good, stop_count: 5}), /stop count/);
    assert.match(validate({...good, repeat: 0}), /repeat/);
    assert.match(validate({...good, repeat: 17}), /repeat/);
    assert.match(validate({...good, spatial: 9}), /spatial/);
});

test('decode refuses a payload of the wrong length', () => {
    assert.throws(() => decode(new Uint8Array(RECIPE_BYTES - 1)), /27 bytes/);
    assert.throws(() => decode(new Uint8Array(RECIPE_BYTES + 1)), /27 bytes/);
});

console.log(`\n${passed} passed, ${failed} failed\n`);
process.exit(failed === 0 ? 0 : 1);

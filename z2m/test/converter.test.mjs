// Tests for z2m/lumary-brain-revA.js, run with nothing installed: ./loader.mjs
// swaps the zigbee-herdsman-converters imports for the stubs in ./stubs.
//
//     node z2m/test/converter.test.mjs
//
// The converter is the one piece of this project that needs maintenance across
// Z2M upgrades, and a typo in it is only visible after restarting Z2M and
// re-pairing. These checks pin the parts the firmware and Home Assistant both
// depend on: the effect names, the wire indices, and the fact that the expose
// is called `effect` -- which is what puts the dropdown in the light card
// rather than in a select entity of its own.
import assert from 'node:assert/strict';
import {register} from 'node:module';

register('./loader.mjs', import.meta.url);

const def = (await import('../lumary-brain-revA.js')).default;
const {calls} = await import('./stubs/modernExtend.mjs');
const {light_color_colortemp} = await import('./stubs/toZigbee.mjs');

let passed = 0;
let failed = 0;

async function test(name, fn) {
    try {
        await fn();
        console.log(`  ok    ${name}`);
        passed++;
    } catch (err) {
        console.log(`  FAIL  ${name}\n        ${err.message.split('\n').join('\n        ')}`);
        failed++;
    }
}

// ── the expose Home Assistant reads ───────────────────────────────────────
// Z2M's HA discovery collects every enum expose named `effect` and folds their
// values into the light's effect_list. Under any other name it stays a select.

const effect = def.exposes.find((x) => x.name === 'effect');

await test('exposes an enum named exactly `effect`', () => {
    assert.ok(effect, 'no expose named `effect`');
    assert.equal(effect.type, 'enum');
});

await test('effect is readable, not write-only like the Identify preset', () => {
    assert.equal(effect.access, 0b111);
});

await test('effect list is `none` plus the six firmware effects, none first', () => {
    assert.deepEqual(effect.values, [
        'none', 'warm_gradient', 'color_gradient', 'breathing',
        'color_cycle', 'chase', 'nightlight',
    ]);
});

await test('effect expose is attached to the ring endpoint', () => {
    assert.equal(effect.endpoint, 'ring');
});

await test('effect expose property is postfixed to `effect_ring`', () => {
    // withEndpoint('ring') rewrites `property` the same way real ZHC does
    // (see stubs/exposes.mjs) -- this is the key Home Assistant actually
    // reads, and the one fzEffect must publish under (item 2).
    assert.equal(effect.property, 'effect_ring');
});

await test('the old effect_select expose is gone', () => {
    assert.equal(def.exposes.find((x) => x.name === 'effect_select'), undefined);
});

// ── light() must not contribute a second, dead effect list ────────────────

const lightCalls  = calls.filter((c) => c.fn === 'light');
const downArgs    = lightCalls.find((c) => c.args.endpointNames?.includes('downlight')).args;
const ringArgs    = lightCalls.find((c) => c.args.endpointNames?.includes('ring')).args;

await test('exposes exactly two lights, one per endpoint', () => {
    assert.equal(lightCalls.length, 2);
});

await test('the downlight carries colour temperature and no colour', () => {
    assert.deepEqual(downArgs.colorTemp.range, [154, 370]);
    assert.equal(downArgs.colorTemp.startup, false);
    assert.equal(downArgs.color, false);
});

await test('the ring carries colour and no colour temperature', () => {
    assert.deepEqual(ringArgs.color, {modes: ['xy']});
    assert.equal(ringArgs.colorTemp, undefined);
});

await test('both lights switch off the dead stock controls', () => {
    for (const args of [downArgs, ringArgs]) {
        assert.equal(args.effect, false);
        assert.equal(args.powerOnBehavior, false);
    }
});

await test('the endpoint map names both endpoints, with a default', () => {
    assert.deepEqual(def.endpoint({}), {downlight: 1, ring: 2, default: 1});
    assert.equal(def.meta.multiEndpoint, true);
});

// ── identify ──────────────────────────────────────────────────────────────
// The commissioning button. Verified against zigbee-herdsman-converters
// 26.90.0: m.identify() exposes an enum named `identify`, not `effect`, so it
// cannot be unioned into the light's effect_list the way the stock Identify
// trigger-effects were.

await test('the converter asks for identify', () => {
    assert.ok(calls.find((c) => c.fn === 'identify'), 'm.identify() was never called');
});

// ── OTA ───────────────────────────────────────────────────────────────────────
// The firmware registers an OTA client on endpoint 1 and queries for an image
// on first join (`addOTAClient` / `requestOTAUpdate` in src/zigbee_light.cpp).
// Without m.ota() here the device asks and Z2M has nothing to answer with, so
// the whole OTA path is dead from the coordinator side while looking healthy
// from the firmware side -- exactly the state this repo was in until 2026-08-18.

await test('the converter asks for OTA', () => {
    assert.ok(calls.find((c) => c.fn === 'ota'), 'm.ota() was never called');
});

await test('identify does not contribute a second `effect` expose', () => {
    // Collect exposes from both the static array and all extend entries.
    // extend entries' exposes may be absent, an array, or a function.
    const allExposes = [...(def.exposes ?? [])];
    for (const entry of def.extend ?? []) {
        let exposes = entry.exposes;
        if (typeof exposes === 'function') {
            exposes = exposes({}, {});
        }
        if (Array.isArray(exposes)) {
            allExposes.push(...exposes);
        }
    }
    const effects = allExposes.filter((x) => x.name === 'effect');
    assert.equal(effects.length, 1, `expected 1 expose named 'effect', got ${effects.length}`);
});

// ── selecting an effect ───────────────────────────────────────────────────

const tzEffect = def.toZigbee.find((c) => c.key.length === 1 && c.key[0] === 'effect');

await test('the effect converter is definition-level, so it beats light()\'s', () => {
    assert.ok(tzEffect, 'no toZigbee converter keyed on `effect`');
});

await test('selecting an effect sends setEffect with the firmware index', async () => {
    let sent;
    const entity = {command: async (cluster, cmd, payload) => { sent = {cluster, cmd, payload}; }};
    const res = await tzEffect.convertSet(entity, 'effect', 'color_cycle', {});
    assert.deepEqual(sent, {cluster: 'lumary', cmd: 'setEffect', payload: {effect: 3}});
    assert.deepEqual(res, {state: {effect: 'color_cycle'}});
});

await test('selecting `none` sends the 0xFF sentinel (LIGHT_EFFECT_NONE)', async () => {
    let sent;
    const entity = {command: async (cluster, cmd, payload) => { sent = payload; }};
    const res = await tzEffect.convertSet(entity, 'effect', 'none', {});
    assert.equal(sent.effect, 0xff);
    assert.deepEqual(res, {state: {effect: 'none'}});
});

await test('an unknown effect name is rejected rather than sent', async () => {
    let called = false;
    const entity = {command: async () => { called = true; }};
    await assert.rejects(() => tzEffect.convertSet(entity, 'effect', 'disco', {}), /unknown effect/);
    assert.equal(called, false, 'nothing should reach the device');
});

await test('reading the effect reads the custom cluster attribute', async () => {
    let read;
    await tzEffect.convertGet({read: async (cluster, attrs) => { read = {cluster, attrs}; }}, 'effect', {});
    assert.deepEqual(read, {cluster: 'lumary', attrs: ['effect']});
});

// ── reading state back ────────────────────────────────────────────────────

const fz = def.fromZigbee.find((c) => c.cluster === 'lumary');

// Published under `effect_ring`, not `effect`: withEndpoint('ring') rewrites
// the expose's property to that, and Home Assistant reads value_json under
// the exact property name the expose declares (item 2).

await test('an effect report maps the index back to its name, keyed `effect_ring`', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 4}}), {effect_ring: 'chase'});
});

await test('0xFF reads back as `none`, keyed `effect_ring`', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 0xff}}), {effect_ring: 'none'});
});

await test('an unrecognised index passes through rather than becoming undefined', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 99}}), {effect_ring: 99});
});

await test('a report carrying no effect field is ignored', () => {
    assert.equal(fz.convert({}, {data: {}}), undefined);
});

// ── the staleness fix ─────────────────────────────────────────────────────
// A colour command takes the fixture out of effect mode. The firmware knows,
// but cannot report it, so the converter mirrors the same transition.

const tzColor = def.toZigbee.find((c) => c.key.includes('color'));

await test('setting a colour drives effect back to `none`', async () => {
    const res = await tzColor.convertSet({}, 'color', {x: 0.5, y: 0.4}, {});
    assert.equal(res.state.effect, 'none');
});

await test('...without dropping what the delegate returned', async () => {
    const res = await tzColor.convertSet({}, 'color', {x: 0.5, y: 0.4}, {});
    assert.deepEqual(res.state.color, {x: 0.5, y: 0.4});
    assert.equal(res.readAfterWriteTime, 100);
});

// color_temp belongs to the downlight now, which has no effects, and the
// firmware deliberately does not clear the ring's effect attribute on a
// colour-temperature write -- so this converter must no longer claim that key
// (item 3). Z2M dispatches convertSet/convertGet to a converter by matching
// the key being set against `key`, so narrowing the list is what stops this
// wrapper from being invoked for color_temp at all in production.
await test('color_temp no longer clears the ring effect: only `color` is claimed', () => {
    assert.deepEqual(tzColor.key, ['color']);
});

await test('colour reads still delegate', async () => {
    light_color_colortemp.lastGet = undefined;
    await tzColor.convertGet({}, 'color', {});
    assert.equal(light_color_colortemp.lastGet, 'color');
});

// ── rejoin read-back ──────────────────────────────────────────────────────
// The attribute cannot be reported, so a reboot would otherwise leave Z2M
// showing whatever it last saw.

await test('deviceAnnounce reads the effect attribute back', async () => {
    let read;
    const device = {getEndpoint: () => ({read: async (cluster, attrs) => { read = {cluster, attrs}; }})};
    await def.onEvent({type: 'deviceAnnounce', data: {device}});
    assert.deepEqual(read, {cluster: 'lumary', attrs: ['effect']});
});

await test('an unreachable device on rejoin does not throw', async () => {
    const device = {getEndpoint: () => ({read: async () => { throw new Error('MAC no ack'); }})};
    await def.onEvent({type: 'deviceAnnounce', data: {device}});
});

await test('an unexpected event shape degrades to a no-op', async () => {
    await def.onEvent({type: 'stop', data: {ieeeAddr: '0x1'}});
    await def.onEvent(undefined);
    await def.onEvent({type: 'deviceAnnounce', data: {}});
});

// ── still agrees with the firmware ────────────────────────────────────────

await test('custom cluster is 0xFC00, attribute 0x0000, command 0x00', () => {
    const {name, definition} = calls.find((c) => c.fn === 'deviceAddCustomCluster');
    assert.equal(name, 'lumary');
    assert.equal(definition.ID, 0xfc00);
    assert.equal(definition.attributes.effect.ID, 0x0000);
    assert.equal(definition.commands.setEffect.ID, 0x00);
});

await test('the model still matches setManufacturerAndModel in the firmware', () => {
    assert.deepEqual(def.zigbeeModel, ['LumaryBrainRevA']);
    assert.equal(def.vendor, 'Lumary');
});

console.log(`\n${passed + failed} checks, ${failed} failure${failed === 1 ? '' : 's'}`);
console.log(failed ? 'FAILED' : 'OK');
process.exit(failed ? 1 : 0);

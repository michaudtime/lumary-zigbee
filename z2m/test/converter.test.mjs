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

await test('effect list is `none` plus the eight firmware effects, none first', () => {
    assert.deepEqual(effect.values, [
        'none', 'static_white', 'static_color', 'warm_gradient',
        'color_gradient', 'breathing', 'color_cycle', 'chase', 'nightlight',
    ]);
});

await test('the old effect_select expose is gone', () => {
    assert.equal(def.exposes.find((x) => x.name === 'effect_select'), undefined);
});

// ── light() must not contribute a second, dead effect list ────────────────

const lightArgs = calls.find((c) => c.fn === 'light').args;

await test('light() effect is off (else HA unions in 6 dead Identify effects)', () => {
    assert.equal(lightArgs.effect, false);
});

await test('light() powerOnBehavior is off (no StartUpOnOff in firmware yet)', () => {
    assert.equal(lightArgs.powerOnBehavior, false);
});

await test('colour temperature range still matches CCT_MIRED_COOL/WARM', () => {
    assert.deepEqual(lightArgs.colorTemp.range, [154, 370]);
});

await test('light() colorTemp.startup is off (no StartUpColorTemperature either)', () => {
    // Asking for colorTemp makes light() add a `color_temp_startup` control
    // backed by Colour 0x4010, which the firmware answers with
    // UNSUPPORTED_ATTRIBUTE -- observed in the Z2M log against the fixture.
    assert.equal(lightArgs.colorTemp.startup, false);
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
    assert.deepEqual(sent, {cluster: 'lumary', cmd: 'setEffect', payload: {effect: 5}});
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

await test('an effect report maps the index back to its name', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 6}}), {effect: 'chase'});
});

await test('0xFF reads back as `none`', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 0xff}}), {effect: 'none'});
});

await test('an unrecognised index passes through rather than becoming undefined', () => {
    assert.deepEqual(fz.convert({}, {data: {effect: 99}}), {effect: 99});
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

await test('setting a colour temperature does the same', async () => {
    const res = await tzColor.convertSet({}, 'color_temp', 300, {});
    assert.equal(res.state.effect, 'none');
    assert.equal(res.state.color_temp, 300);
});

await test('colour reads still delegate', async () => {
    light_color_colortemp.lastGet = undefined;
    await tzColor.convertGet({}, 'color_temp', {});
    assert.equal(light_color_colortemp.lastGet, 'color_temp');
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

// Zigbee2MQTT external converter for the lumary-brain rev A controller.
//
// Install: copy to Z2M's `data/external_converters/` and restart Z2M. Written
// against Z2M 2.13.0 / zigbee-herdsman-converters 26.90.0 (ESM, modernExtend).
//
// Without this, Z2M generates a definition from the standard clusters and the
// light works fine -- but cluster 0xFC00 is unknown to it, so the eight effects
// stay unreachable.
//
// The firmware side is documented in
// docs/superpowers/specs/2026-08-15-effect-selection-design.md.

import {Zcl} from 'zigbee-herdsman';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';
import * as tz from 'zigbee-herdsman-converters/converters/toZigbee';

const e = exposes.presets;
const ea = exposes.access;

// Wire value for "not running an effect". Must match LIGHT_EFFECT_NONE in
// src/light_state.h. Home Assistant's effect list has no null member -- an
// effect is just a string from `effect_list` -- so "no effect" has to be a
// value like any other, both in the dropdown and on the air.
const EFFECT_NONE = 0xff;

// Index order must match EffectType in src/effect_params.h. The names are the
// firmware's kEffects[].name, lowercased. `none` leads because that is the
// order Home Assistant renders the dropdown in.
//
// `static_white` and `static_color` were removed when the fixture split into
// two entities: white is now the Downlight entity on endpoint 1, and a solid
// ring colour is `none` with a colour set.
const EFFECTS = {
    none: EFFECT_NONE,
    warm_gradient: 0,
    color_gradient: 1,
    breathing: 2,
    color_cycle: 3,
    chase: 4,
    nightlight: 5,
};

const nameFor = (index) => Object.keys(EFFECTS).find((k) => EFFECTS[k] === index);

// Named `effect` rather than something of our own, because that exact name is
// what makes this a first-class light control instead of a side entity: Z2M's
// Home Assistant discovery collects every enum expose named `effect` and folds
// their values into the light's `effect_list`, which is what puts the dropdown
// inside the light card and makes `light.turn_on(effect: ...)`, scene capture
// and voice control work. Under any other name it is only ever a `select`.
const fzEffect = {
    cluster: 'lumary',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.effect === undefined) return;
        // The key must be `effect_ring`, not `effect`: the expose below is
        // `.withEndpoint('ring')`, and real zigbee-herdsman-converters
        // rewrites an endpoint-scoped expose's `property` from `effect` to
        // `effect_${endpoint}` (the same rewrite postfixWithEndpointName does
        // at read time). Home Assistant reads value_json.effect_ring, so
        // returning `effect` here means every device-originated read --
        // including the rejoin read below -- writes a property nothing
        // consumes. Hardcoded rather than computed via
        // postfixWithEndpointName: that helper resolves the endpoint name
        // from msg.endpoint against the definition's endpoint map, which
        // would need a much heavier stub to test, and this cluster only ever
        // exists on the ring endpoint -- there is nothing to actually
        // compute.
        return {effect_ring: nameFor(msg.data.effect) ?? msg.data.effect};
    },
};

const tzEffect = {
    key: ['effect'],
    convertSet: async (entity, key, value, meta) => {
        const index = EFFECTS[value];
        if (index === undefined) {
            throw new Error(`effect: unknown effect '${value}'`);
        }
        // Selection is a command rather than an attribute write -- see the spec;
        // the attribute is read-only and reports what is currently running.
        await entity.command('lumary', 'setEffect', {effect: index}, {});
        return {state: {effect: value}};
    },
    convertGet: async (entity) => {
        await entity.read('lumary', ['effect']);
    },
};

// Setting a colour or colour temperature takes the fixture out of effect mode,
// so the effect control has to follow it to `none` or it goes on naming an
// effect that stopped running -- Home Assistant showing "chase" over a static
// colour. The firmware does exactly this to its attribute, but that attribute
// cannot be reported (see LumaryLight::publishState), so the device has no way
// to volunteer the change. Mirroring it here keeps HA honest with no round trip.
//
// Definition-level toZigbee converters are matched ahead of extend-provided
// ones and the first match wins, so this takes precedence over the one light()
// installs -- which it then delegates to for the actual colour work.
// Narrowed to `color` only: colour temperature now belongs to the Downlight
// entity (endpoint 1), which has no effects at all, and the firmware
// deliberately does not clear the ring's effect attribute on a colour-
// temperature write (on_downlight_change_temp never calls
// publish_effect_attr()). Keeping color_temp/color_temp_percent here would
// make the converter report `effect: none` for the ring while an effect is
// still running, just because someone changed the downlight's white balance.
const tzColorClearsEffect = {
    key: ['color'],
    convertSet: async (entity, key, value, meta) => {
        const result = await tz.light_color_colortemp.convertSet(entity, key, value, meta);
        return {...result, state: {...result?.state, effect: 'none'}};
    },
    convertGet: async (entity, key, meta) => {
        await tz.light_color_colortemp.convertGet(entity, key, meta);
    },
};

// Same reporting gap, the other symptom: after the light reboots it comes back
// running its stored effect, and Z2M is still showing whatever it last saw. One
// read on rejoin settles it. Written defensively about the event shape so a
// future signature change degrades to a no-op instead of throwing -- ZHC
// 26.90.0 passes a single {type, data} object.
const onEvent = async (event) => {
    if (event?.type !== 'deviceAnnounce' && event?.type !== 'start') return;
    // The effect cluster is on endpoint 2 -- the ring owns the effects.
    const endpoint = event?.data?.device?.getEndpoint?.(2);
    if (!endpoint) return;
    try {
        await endpoint.read('lumary', ['effect']);
    } catch {
        // Out of range or not joined yet; the next announce retries.
    }
};

export default {
    zigbeeModel: ['LumaryBrainRevA'],
    model: 'LumaryBrainRevA',
    vendor: 'Lumary',
    description: 'ESP32-H2 Zigbee controller for Lumary 6" RGBAI recessed light',
    // Two endpoints, two Home Assistant light entities under one device. The
    // downlight keeps endpoint 1 so existing switch bindings land on the main
    // light; the ring is the new endpoint 2.
    endpoint: (device) => ({downlight: 1, ring: 2, default: 1}),
    meta: {multiEndpoint: true},
    extend: [
        // `effect: false` because light() otherwise exposes the standard
        // Identify trigger-effects (blink, breathe, okay, ...) and wires them to
        // genIdentify.triggerEffect, which this firmware has no handler for --
        // the Arduino library surfaces onIdentify() only. Since HA unions every
        // enum expose named `effect` into one list, leaving it on would hand the
        // light card dead entries alongside the real effects.
        //
        // `powerOnBehavior: false` for the same reason: light() exposes it by
        // default, and the firmware does not implement StartUpOnOff (0x4003)
        // yet, so the control would be there and do nothing. The Z2M log has
        // this one confirmed from the bench:
        //
        //     Publish 'set' 'power_on_behavior' to 'Overhead light test'
        //     failed: 'device does not support power on behaviour'
        //
        // For the downlight's colorTemp.startup: asking for colorTemp makes light()
        // add a `color_temp_startup` control backed by StartUpColorTemperature
        // (Colour 0x4010), which the firmware does not implement:
        //
        //     lightingColorCtrl.write({"startUpColorTemperature":370})
        //     failed (Status 'UNSUPPORTED_ATTRIBUTE')
        //
        // Turning it off leaves `color_temp` itself untouched. All three come
        // back on together when power-on behaviour is implemented properly.
        //
        // Endpoint 1: the inner CW/WW white string. Colour temperature only --
        // it has no colour dice. Mireds match CCT_MIRED_COOL/WARM in
        // src/light_state.h: 6500 K and 2700 K.
        m.light({
            endpointNames: ['downlight'],
            colorTemp: {range: [154, 370], startup: false},
            color: false,
            effect: false,
            powerOnBehavior: false,
        }),
        // Endpoint 2: the outer RGB ring. No colour temperature -- it has no
        // white die, and advertising CCT would expose a control that lies.
        m.light({
            endpointNames: ['ring'],
            color: {modes: ['xy']},
            effect: false,
            powerOnBehavior: false,
        }),
        // The commissioning button: blinks the ring blue so you can tell which
        // can in the ceiling you are looking at. Exposes an enum named
        // `identify`, so unlike the stock Identify trigger-effects it cannot be
        // folded into the light's effect_list.
        m.identify(),
        m.deviceAddCustomCluster('lumary', {
            ID: 0xfc00,
            attributes: {
                effect: {ID: 0x0000, type: Zcl.DataType.UINT8},
            },
            commands: {
                setEffect: {
                    ID: 0x00,
                    parameters: [{name: 'effect', type: Zcl.DataType.UINT8}],
                },
            },
            commandsResponse: {},
        }),
    ],
    fromZigbee: [fzEffect],
    toZigbee: [tzEffect, tzColorClearsEffect],
    onEvent,
    exposes: [
        e
            .enum('effect', ea.ALL, Object.keys(EFFECTS))
            .withEndpoint('ring')
            .withDescription(
                'Which built-in effect the accent ring runs. Setting a colour on the ' +
                'ring exits the effect and shows that colour instead, which reads back ' +
                'as `none`; selecting `none` does the same thing without changing the ' +
                'colour. The downlight is a separate entity and has no effects.',
            ),
    ],
};

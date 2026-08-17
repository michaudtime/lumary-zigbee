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
const EFFECTS = {
    none: EFFECT_NONE,
    static_white: 0,
    static_color: 1,
    warm_gradient: 2,
    color_gradient: 3,
    breathing: 4,
    color_cycle: 5,
    chase: 6,
    nightlight: 7,
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
        return {effect: nameFor(msg.data.effect) ?? msg.data.effect};
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
const tzColorClearsEffect = {
    key: ['color', 'color_temp', 'color_temp_percent'],
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
    const endpoint = event?.data?.device?.getEndpoint?.(1);
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
    extend: [
        // Mireds, matching CCT_MIRED_COOL/WARM in src/config.h: 6500 K and 2700 K.
        //
        // `effect: false` because light() otherwise exposes the standard
        // Identify trigger-effects (blink, breathe, okay, ...) and wires them to
        // genIdentify.triggerEffect, which this firmware has no handler for --
        // the Arduino library surfaces onIdentify() only. Since HA unions every
        // enum expose named `effect` into one list, leaving it on would hand the
        // light card six dead entries alongside the eight real ones.
        //
        // `powerOnBehavior: false` for the same reason: light() exposes it by
        // default, and the firmware does not implement StartUpOnOff (0x4003)
        // yet, so the control would be there and do nothing. The Z2M log has
        // this one confirmed from the bench:
        //
        //     Publish 'set' 'power_on_behavior' to 'Overhead light test'
        //     failed: 'device does not support power on behaviour'
        //
        // `colorTemp.startup: false` is the same defect one layer down --
        // asking for colorTemp at all makes light() add a `color_temp_startup`
        // control backed by StartUpColorTemperature (Colour 0x4010), which the
        // firmware does not implement either:
        //
        //     lightingColorCtrl.write({"startUpColorTemperature":370})
        //     failed (Status 'UNSUPPORTED_ATTRIBUTE')
        //
        // Turning it off leaves `color_temp` itself untouched. All three come
        // back on together when power-on behaviour is implemented properly.
        m.light({
            colorTemp: {range: [154, 370], startup: false},
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
            .withDescription(
                'Which built-in effect the fixture runs. Setting a colour or colour ' +
                'temperature exits the effect and shows that colour instead, which ' +
                'reads back as `none`; selecting `none` does the same thing without ' +
                'changing the colour.',
            ),
    ],
};

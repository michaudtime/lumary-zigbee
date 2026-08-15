// Zigbee2MQTT external converter for the lumary-brain rev A controller.
//
// Install: copy to Z2M's `data/external_converters/` and restart Z2M. Written
// against Z2M 2.13.0 / zigbee-herdsman-converters 26.90.0 (ESM, modernExtend).
//
// Without this, Z2M generates a definition from the standard clusters and the
// light works fine -- but cluster 0xFC00 is unknown to it, so the eight effects
// stay unreachable. This adds an `effect_select` control.
//
// The firmware side is documented in
// docs/superpowers/specs/2026-08-15-effect-selection-design.md.

import {Zcl} from 'zigbee-herdsman';
import * as m from 'zigbee-herdsman-converters/lib/modernExtend';
import * as exposes from 'zigbee-herdsman-converters/lib/exposes';

const e = exposes.presets;
const ea = exposes.access;

// Index order must match EffectType in src/effect_params.h. The names are the
// firmware's kEffects[].name, lowercased.
const EFFECTS = {
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

// Deliberately NOT called `effect`: the light already exposes that for the
// standard Identify trigger-effects (blink, breathe, okay...), which this
// firmware does not implement. Colliding with it would be confusing.
const fzEffectSelect = {
    cluster: 'lumary',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.data.effect === undefined) return;
        return {effect_select: nameFor(msg.data.effect) ?? msg.data.effect};
    },
};

const tzEffectSelect = {
    key: ['effect_select'],
    convertSet: async (entity, key, value, meta) => {
        const index = EFFECTS[value];
        if (index === undefined) {
            throw new Error(`effect_select: unknown effect '${value}'`);
        }
        // Selection is a command rather than an attribute write -- see the spec;
        // the attribute is read-only and reports what is currently running.
        await entity.command('lumary', 'setEffect', {effect: index}, {});
        return {state: {effect_select: value}};
    },
    convertGet: async (entity) => {
        await entity.read('lumary', ['effect']);
    },
};

export default {
    zigbeeModel: ['LumaryBrainRevA'],
    model: 'LumaryBrainRevA',
    vendor: 'Lumary',
    description: 'ESP32-H2 Zigbee controller for Lumary 6" RGBAI recessed light',
    extend: [
        // Mireds, matching CCT_MIRED_COOL/WARM in src/config.h: 6500 K and 2700 K.
        m.light({colorTemp: {range: [154, 370]}, color: {modes: ['xy']}}),
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
    fromZigbee: [fzEffectSelect],
    toZigbee: [tzEffectSelect],
    exposes: [
        e
            .enum('effect_select', ea.ALL, Object.keys(EFFECTS))
            .withDescription(
                'Which built-in effect the fixture runs. Setting a colour or colour ' +
                'temperature exits the effect and shows that colour instead.',
            ),
    ],
};

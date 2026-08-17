# Home Assistant polish — backlog

**Date:** 2026-08-17
**Status:** items 1 and 2 implemented and **verified on hardware** (PR #3, bench-tested
2026-08-17 against the fixture at `0x744dbdfffe6b575f`); item 9 decided; the rest are open

What it takes to make the fixture read as a finished product in Home Assistant rather than a
working prototype. Written after auditing what HA actually sees today, so each item records the
finding as well as the fix — most of the cost here was in the investigation, not the change.

## What HA sees today

| Entity | Source |
|---|---|
| `light.*` — on/off, brightness, colour temp 154–370, xy colour | `m.light()` in the converter |
| `select.*_effect` (disabled by default) + the light's own effect list | custom cluster `0xFC00`, after PR #3 |
| `update.*` | `addOTAClient()` |
| `sensor.*_linkquality` | Z2M default |

No identify button, no power-on behaviour, no firmware version string, no effect parameters.

---

## 1. Effects on the light entity — DONE (PR #3)

Renamed the `effect_select` expose to `effect`, which is what folds it into the light's
`effect_list`. See the amendment in `2026-08-15-effect-selection-design.md`. Two findings worth
keeping:

- **`m.light()` defaults to `effect: true`**, so the converter was already exposing the six
  Identify trigger-effects and wiring them to `genIdentify.triggerEffect`, which this firmware has
  no handler for. HA had a dead effects dropdown the whole time.
- **Z2M's HA discovery unions every enum expose named `effect`** into one `effect_list`, so a
  second one cannot simply be added alongside — the stock one has to be switched off.

Bench-verified 2026-08-17. Before, the light entity carried the dead Identify list:

```
effect_list: [blink, breathe, okay, channel_change,
              finish_effect, stop_effect, colorloop, stop_colorloop]
```

After deploying the converter it carries the real one, and `select.*_effect_select` and
`select.*_power_on_behavior` are both gone:

```
effect_list: [none, static_white, static_color, warm_gradient,
              color_gradient, breathing, color_cycle, chase, nightlight]
```

`light.turn_on(effect: chase)` from HA reaches the firmware as `apply_effect(): Effect 6 selected`,
and setting `color_temp_kelvin: 4000` afterwards drops `effect` to `none` — so `tzColorClearsEffect`
behaves on real hardware, not just against the stubs.

## 2. Power-on behaviour

`m.light()` defaults to `powerOnBehavior: true`, so a `power_on_behavior` select was being exposed
with no `StartUpOnOff` (OnOff 0x4003) behind it. Asking for `colorTemp` separately adds a
`color_temp_startup` number backed by `StartUpColorTemperature` (Colour 0x4010), which the firmware
does not implement either. PR #3 switches both off rather than leave controls that do nothing
(`powerOnBehavior: false`, `colorTemp.startup: false`).

Both are confirmed against the fixture, not just inferred from the converter — the Z2M log has the
device rejecting each one:

```
Publish 'set' 'power_on_behavior' to 'Overhead light test' failed:
  'device does not support power on behaviour'
Publish 'set' 'color_temp_startup' ... lightingColorCtrl.write({"startUpColorTemperature":370})
  failed (Status 'UNSUPPORTED_ATTRIBUTE')
```

To implement properly: `StartUpOnOff` (0x4003), `StartUpCurrentLevel` (Level 0x4000),
`StartUpColorTemperature` (Colour 0x4010), then re-enable all three in the converter. These are
standard attributes, but the Arduino wrapper may not surface them — likely needs the same raw
`esp_zb_zcl_*` approach the custom cluster already uses, which is proven on this hardware.

Worth doing: this is a ceiling can on a dumb wall switch, and it currently always boots off.

## 3. Transitions

HA's `transition:` parameter and the Inovelli's ramp rate both do nothing — every change snaps at
the next 16 ms frame in `main.cpp`. A local slew from current to target level in the render loop
covers HA, the switch and bind-only operation at once, with no dependency on the Zigbee library.

Probably the single biggest perceived-quality item after gamma.

## 4. Identify

`on_identify()` in `src/zigbee_light.cpp` only logs. It receives the duration, so rendering a
blink is small. Needs `m.identify()` in the converter for the button to exist at all.

Matters when commissioning several fixtures in one ceiling.

## 5. Firmware version in HA

Basic cluster `SWBuildID` (0x4000) and `DateCode` are unset, so the device page reads
"Firmware: unknown" and the update card shows a bare integer. Also reconcile `ZB_FW_VERSION`
(`0x01000000`) against `ZB_FW_VERSION_DL` (`0x01000001`) in `src/config.h` — they disagree.

## 6. Gamma and the low end

`scale8` feeds both the ring and the 8-bit PWM linearly, so the bottom third of HA's brightness
slider does very little and then jumps. Wants a gamma LUT on the ring and a minimum duty floor on
CW/WW so brightness 1 is dim rather than off.

## 7. Effect parameters

Effects run at the hardcoded `kDefaultParams` speeds, and `scene_store_save()` still has no caller
outside seeding — the 16-slot NVS store exists and is unused. Expose `effect_speed` as a number
with `.withCategory('config')`, wire it to the store, and consider per-effect hue for chase,
breathing and colour cycle. The stock Lumary app has a speed slider; this does not.

## 8. Bindings and reporting

`LumaryLight::publishState()` hardcodes dst `0x0000` / ep 1 because bindings were never configured,
and `m.light()` defaults `configureReporting: false`. A `configure()` binding `genOnOff`,
`genLevelCtrl` and `lightingColorCtrl` with min/max reporting intervals (max ~3600 s) would let the
firmware report into the binding table normally, and would give HA self-healing after a dropped
report — which it has none of today.

## 9. One light entity or two — DECIDED: two

`WHITE_SAT_THRESHOLD` in `src/light_state.h` makes colour and white mutually exclusive, so the
fixture's "gradient auxiliary light" selling point — white downlight plus coloured accent ring at
the same time — is unreachable.

**Decision (2026-08-17): a second Zigbee endpoint, so HA gets two light entities under one device
— "Downlight" and "Accent Ring".** This is what a HA user expects from this hardware, and it is the
only option that actually reaches the capability the fixture was bought for. Cost accepted: it
ripples through the firmware, the converter and the Inovelli binding story.

What it implies, to be worked out when this is picked up:

- **Firmware.** `LightState` currently models one fixture; it needs to carry downlight and ring
  independently, and `WHITE_SAT_THRESHOLD` stops being a mode switch. `publishState()` and the
  endpoint registration in `src/zigbee_light.cpp` both become per-endpoint.
- **Converter.** Two `m.light()` extends with `endpointNames`, plus an endpoint map. The effect
  cluster stays on endpoint 1 — effects are a whole-fixture concept, so `effect` should not be
  duplicated per endpoint, or HA will union two identical lists.
- **Inovelli binding.** The switch binds to one endpoint. Downlight is the sane default; the ring
  then only follows via the hub automation, which is a behaviour change worth stating in the README.
- **Item 7** gains a question: are effect parameters per-endpoint or global? Global, presumably,
  for the same reason as `effect` itself.

This unblocks item 10.

## 10. Upstream the converter

Getting the definition into `zigbee-herdsman-converters` removes the
`data/external_converters/` install step entirely — pair it and it works. Requires snake_case
expose names (already the case) and a clean definition. Was blocked on item 9; now blocked only on
item 9 being *implemented*, since the two-endpoint shape is what would be upstreamed.

## 11. Ship the switch blueprint

The README describes the hub automation for 2× tap up/down to step effects, but the repo does not
contain it. Add an HA blueprint under `ha/` so it is a two-click install. Post-PR #3 it reads
`state_attr('light.x', 'effect')` and calls `light.turn_on` with the next name.

---

## Suggested order

Cheap, independent, and most of the visible difference: **4, 5, 6, 3.**
Then **2** and **8**.
Then **9** (now decided: two endpoints), which unblocks **10** and shapes **7** and **11**.

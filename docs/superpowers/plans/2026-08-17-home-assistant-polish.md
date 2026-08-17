# Home Assistant polish — backlog

**Date:** 2026-08-17
**Status:** item 1 implemented (PR #3, unverified on hardware); the rest are open

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

## 2. Power-on behaviour

`m.light()` also defaults to `powerOnBehavior: true`, so a `power_on_behavior` select was being
exposed with no `StartUpOnOff` (OnOff 0x4003) behind it. PR #3 switches it off rather than leave a
control that does nothing.

To implement properly: `StartUpOnOff` (0x4003), `StartUpCurrentLevel` (Level 0x4000),
`StartUpColorTemperature` (Colour 0x4010), then re-enable it in the converter. These are standard
attributes, but the Arduino wrapper may not surface them — likely needs the same raw
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

## 9. One light entity or two

`WHITE_SAT_THRESHOLD` in `src/light_state.h` makes colour and white mutually exclusive, so the
fixture's "gradient auxiliary light" selling point — white downlight plus coloured accent ring at
the same time — is unreachable.

Options: keep one entity; add a second Zigbee endpoint so HA gets two light entities under one
device ("Downlight" / "Accent Ring"); or a config toggle. Option two is what a HA user would expect
from this hardware, but it ripples through the firmware, the converter and the Inovelli binding
story.

**This is a product decision and it shapes the converter, so settle it before item 10.**

## 10. Upstream the converter

Getting the definition into `zigbee-herdsman-converters` removes the
`data/external_converters/` install step entirely — pair it and it works. Requires snake_case
expose names (already the case) and a clean definition. Blocked on item 9.

## 11. Ship the switch blueprint

The README describes the hub automation for 2× tap up/down to step effects, but the repo does not
contain it. Add an HA blueprint under `ha/` so it is a two-click install. Post-PR #3 it reads
`state_attr('light.x', 'effect')` and calls `light.turn_on` with the next name.

---

## Suggested order

Cheap, independent, and most of the visible difference: **4, 5, 6, 3.**
Then **2** and **8**.
Then settle **9**, which unblocks **10** and shapes **7** and **11**.

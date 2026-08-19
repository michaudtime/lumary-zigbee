# Home Assistant polish — backlog

**Date:** 2026-08-17
**Status:** items 1, 2, 4, 5, 6 and 9 implemented and **verified on hardware** (bench-tested
2026-08-17/18 against the fixture at `0x744dbdfffe6b575f`); item 9's downlight-effect-dropdown wart
fixed 2026-08-19; the rest are open

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

## 4. Identify — DONE, bench-verified 2026-08-17

`on_identify()` only logged. Now a render-loop overlay: a deadline set on the Zigbee task, checked
in `loop()`, drawing a blue ring blink with the white string dark. `LightState` is never touched, so
the fixture resumes by itself with no restore path. `m.identify()` in the converter gives HA the
button (`button.*_identify`). See `2026-08-17-identify-and-version-design.md`.

Verified on the fixture: pressing Identify blinks the ring blue for the requested duration and stops
on its own, and the light entity stays `off` throughout — the overlay really is invisible to state.

**Finding worth keeping: `on_identify` fires once per second, not once.** The ZCL `IdentifyTime`
attribute counts down and the Arduino callback fires on every change:

```
on_identify(): Zigbee identify for 3s
on_identify(): Zigbee identify for 2s
on_identify(): Zigbee identify for 1s
on_identify(): Zigbee identify for 0s
```

Each call recomputes `now + time * 1000`, and since `now` advances by the same second that `time`
decrements, they all converge on one absolute deadline. The terminal `0` takes the ZCL "stop
identifying" path and ends the blink exactly on time. That zero case was added for explicit
cancellation from HA; it turns out to terminate *every* identify. Without it each press would
over-run by a second.

## 5. Firmware version in HA — DONE, bench-verified 2026-08-17

`SWBuildID` (0x4000) and `DateCode` (0x0006) are now registered on the Basic cluster, and the
version lives in one place: `src/version.h` derives `ZB_FW_VERSION`, `ZB_FW_VERSION_DL` and the
human string from three components, so a release is one edit. The README's `--file-version` example
contradicted its own instruction and now agrees.

The two OTA constants were not in conflict after all. `ZB_FW_VERSION_DL` feeds
`ota_upgrade_downloaded_file_ver`, a different attribute from the running `FileVersion`, and
running/running+1 is Espressif's own Arduino OTA example pattern. Z2M decides updates from the
*running* version, so it is not load-bearing; it keeps its exact value and merely stops being typed
by hand. Full reasoning in `2026-08-17-identify-and-version-design.md` §1.3.

Verified on the fixture — HA's device page reads `1.0.0`, and Z2M read both strings off the air:

```
swBuildId : '1.0.0'
dateCode  : '20260817'
```

**Operational finding, and it applies to every fixture already in service: Z2M reads Basic cluster
attributes only during device interview.** A fixture paired before this firmware keeps showing empty
strings forever — a Z2M restart does not re-read them. This one showed `sw_version: null` until it
was re-interviewed, after which the values appeared immediately.

So rolling this out means: **newly paired fixtures just work; already-paired ones need a
re-interview** (Z2M frontend, or `zigbee2mqtt/bridge/request/device/interview`). Worth remembering
before concluding the firmware is broken on an existing fixture.

## 6. Gamma and the low end — DONE, bench-verified 2026-08-18

`scale8` fed both the ring and the 8-bit PWM linearly, so the bottom third of HA's brightness slider
did very little and then jumped. Replaced with the CIE 1931 lightness curve on both light sources,
and the white string's PWM widened from 8-bit to 12-bit to give the curve somewhere to land. Two
generated 256-entry tables (`scripts/gen-gamma-tables.py` -> `src/brightness.h`), one to 255 for the
ring and one to 4095 for the white string. See `2026-08-17-gamma-and-low-end-design.md`.

**The measured minimum duty is: there isn't one.** This was the open question the whole branch rested
on -- 12-bit at 1 kHz means a single count is a 0.24 uS pulse, and nothing short of hardware could
say whether the L-SD8E1's constant-current loop would respond to it. It does. Brightness 1 produces
visible light and every step above it is brighter than the last, so the provisional `max(1, ...)`
floor in `white_mix_gamma()` stands unchanged and no measured-floor lift was needed.

The curve's linear segment below L* = 8 is what makes that reachable at all; a plain power law
cannot resolve brightness 1 to a non-zero duty.

Verified across both sources: an even ramp through 1, 5, 10, 25, 50, 100, 150, 200, 255 with no jump
out of the low end, and colour temperature holding steady while dimming 255 -> 10 at 2700 K, 4000 K
and 6500 K. The 4000 K sweep is the one that matters -- it is the only test point where both mix
coefficients are non-zero, so it is the hardware confirmation of the `+127` rounding added to
`white_mix_gamma()` for levels 1-4.

**Finding: brightness 255 tops out at 254, and that is correct.** ZCL `genLevelCtrl` defines
`CurrentLevel` over `0x00`-`0xFE`, so Z2M carries `brightness_scale: 254` and the firmware never
receives 255. Index 255 of both tables is unreachable, putting peak output about 1% below
theoretical maximum. Not worth compensating for -- rescaling would desynchronise this fixture from
every other light on the network to fix something invisible.

**Finding, carried forward as its own work: ring effects collapse to black below roughly brightness
20.** At brightness 16 the curve asks for 0.7% output -- 28 counts of 4095 on the downlight, but
under 2 counts of 255 on the ring. Effects pre-scale their own pixels before the brightness
multiplier lands, so mid-gradient pixels truncate to zero and `warm_gradient` reads as "off":

```
warm_gradient at brightness 16 (gamma8(16) = 2):
  peak {255,169,87} -> {2,1,0}
  mid  {127, 84,43} -> {0,0,0}
```

A resolution limit, not a logic bug, and confined to effects -- a solid ring colour still resolves to
a non-zero count. The cheap partial is `+127` rounding in `scale_by_255()`; the real fix is temporal
dithering. Neither was done here. Full arithmetic in `2026-08-18-bench-verification.md` section 7.

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

## 9. One light entity or two — DONE, two endpoints shipped

`WHITE_SAT_THRESHOLD` in `src/light_state.h` made colour and white mutually exclusive, so the
fixture's "gradient auxiliary light" selling point — white downlight plus coloured accent ring at
the same time — was unreachable from a single light entity.

Built as a second Zigbee endpoint, so HA gets two light entities under one device: **Downlight**
(endpoint 1 — on/off, brightness, colour temperature 2700–6500 K) and **Accent Ring** (endpoint 2 —
on/off, brightness, xy colour, the six effects). Endpoint 1 stayed the downlight rather than the
ring, deliberately: existing switch bindings and the OTA/Basic-cluster strings already target it.

- **Effect-list consequence.** `static_white` and `static_color` are gone — eight effects down to
  six. White is now the Downlight entity; a solid ring colour is `effect: none` with a colour set.
  The effect cluster and dropdown live on the ring endpoint only, so this is a breaking change for
  any automation naming one of the two removed effects. Stored scenes are reseeded automatically —
  the NVS format version bumped 1 → 2, which discards the old indices rather than misreading them.
- **Binding decision.** The Inovelli's paddle (its transmitting endpoint 2) now binds to *both*
  fixture endpoints instead of just the downlight, so a switch tap moves both sources together.
  This is only safe because **the Inovelli sends discrete `On`/`Off`, not `Toggle`** — confirmed on
  the hardware. Under `Toggle`, two endpoints that had drifted into different states would diverge
  further on every tap instead of converging. "Downlight only" becomes a Home Assistant action
  rather than a switch action.
- **Re-interview requirement.** Adding the second endpoint changes the device descriptor, and Z2M
  caches endpoints from the interview — the same mechanism that left `sw_version` reading `null`
  until re-interview in item 5. A fixture paired before this firmware shows only the downlight until
  it is **re-interviewed**; newly paired fixtures just work. This is the change most likely to read
  as "the update broke my light" if missed, so it is called out at the top of the README rather than
  in a footnote.

All of the above is implemented and documented (README: "The two light sources", "Switch Control"
and "Built-in Effects"), and **bench-verified on the fixture 2026-08-18**:

- Both entities appear under one device, correctly shaped -- the downlight advertises
  `supported_color_modes: ["color_temp"]` with no colour wheel, the ring `["xy"]` with no colour
  temperature. This settles the design's first risk: `ZigbeeColorDimmableLight` configured
  `COLOR_TEMP`-only keeps the `HA_COLOR_DIMMABLE_LIGHT` device ID, and it was unproven whether Z2M
  would read `colorCapabilities` and present it correctly. It does.
- Both sources lit at once -- white downlight at 2700 K under a saturated coloured ring, and a ring
  effect running over a lit downlight. The thing `WHITE_SAT_THRESHOLD` made structurally impossible.
- The switch binding behaves as designed across both endpoints, including the load-bearing case:
  drive the two sources deliberately out of sync, tap up, and they re-converge. That is the proof
  the Inovelli sends discrete `On`/`Off` rather than `Toggle`, now confirmed under the two-endpoint
  arrangement rather than only the single-endpoint one.
- The effect survives a power cycle and Home Assistant displays it. Tested with `chase` rather than
  `warm_gradient`, deliberately -- index 0 is both the reseed default and the effect attribute's
  static-init value, so it cannot distinguish a restore from a reset. Confirmed with a forced
  attribute read that bypasses Z2M's cache, on a cold boot with USB fully unplugged.

**Wart found on the bench: the downlight's card also carries an effect dropdown -- FIXED
2026-08-19.** Z2M's HA discovery unions every enum expose named `effect` into the light entity's
`effect_list`, and that union is not endpoint-aware (`allExposes.filter(isEnumExpose).filter((e) =>
e.name === 'effect')` in `lib/extension/homeassistant.ts`, no endpoint check at all). Selecting one
turned the downlight on -- HA bundles `state: ON` into every `light.turn_on` -- and then did
nothing, because `tzEffect` resolves to endpoint 1, which carries no `0xFC00` cluster.

Turned out to be fixable from the converter after all, just not from the exposes/extend shape:
`meta.overrideHaDiscoveryPayload` is a hook Z2M calls once per light's discovery payload, after it
has already built `effect`/`effect_list` onto it, keyed on `object_id` (`light_downlight` /
`light_ring`). The converter now strips both fields when `object_id === 'light_downlight'`. Verified
against Z2M 2.13.0's `homeassistant.ts` and covered by two checks in `converter.test.mjs`. No firmware
change, no re-interview needed -- restart Z2M with the updated converter installed.

## Finding, fixed 2026-08-19: the ring's first white command after any boot could be silently dropped

`on_ring_change_rgb`/`on_ring_change_hsv` in `zigbee_light.cpp` compare an incoming colour against
the last one seen, to tell a genuine colour change from a plain dim (the ring reports state, level
and colour together on every change). The "last colour seen" sentinel was `{255,255,255}` / `hue=0,
sat=0` -- itself a valid white command, not just an unset marker -- and those sentinels reset on
every boot. So the very first colour command after any boot or rejoin that happened to be white
compared equal to the sentinel and was silently ignored: the ring stayed on whatever scene it
booted into instead of switching to white as commanded. Fixed by adding explicit
`s_ring_color_seen`/`s_ring_hsv_seen` flags so the first command is always applied, regardless of
what it is. This code path is in the untested Zigbee-adapter layer -- `light_state.h`'s host tests
cover `ring_set_color()` itself but not this dedup -- so nothing in the test suite would have caught
it. Bench-verified working after the fix.

**Rollout needs two steps, not one, and this session proved it the hard way.** The updated converter
must be installed in `data/external_converters/` and Z2M restarted *before* the re-interview means
anything. With the old converter still loaded, Z2M ran the one-entity definition and HA showed a
single light with the old nine-entry effect list -- no amount of re-interviewing would have produced
two entities.

This unblocks item 10.

## 10. Upstream the converter

Getting the definition into `zigbee-herdsman-converters` removes the
`data/external_converters/` install step entirely — pair it and it works. Requires snake_case
expose names (already the case) and a clean definition. Was blocked on item 9 being decided, then
on item 9 being implemented — the two-endpoint shape is what would be upstreamed, and now that it
has shipped this is unblocked.

## 11. Ship the switch blueprint

The README describes the hub automation for 2× tap up/down to step effects, but the repo does not
contain it. Add an HA blueprint under `ha/` so it is a two-click install. Post-PR #3 it reads
`state_attr('light.x', 'effect')` and calls `light.turn_on` with the next name.

---

## Suggested order

~~4, 5, 9~~ done — item 9 unblocks **10**, which is next. Then **3**, **7** and **11**.

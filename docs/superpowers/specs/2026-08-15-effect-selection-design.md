# Effect selection over a manufacturer cluster — design

**Date:** 2026-08-15
**Status:** approved, ready for implementation

## 1. Problem

The eight effects all render correctly on hardware (verified 2026-08-15), but **there is no way to
select one.** Three gaps, all confirmed in source:

1. **Nothing can select a scene.** `zigbee_light_next_scene()` and `prev_scene()` are exported from
   `zigbee_light.h` and implemented in `zigbee_light.cpp`, but have **no callers anywhere** — no
   Zigbee handler, no button.
2. **Nothing can save a scene.** `scene_store_save()` is called only from `scene_store_init()`,
   seeding defaults on first boot. The README's "use standard Zigbee Add Scene commands" is not
   implemented; `zigbee_light_init()` registers only `onLightChangeRgb`, `onLightChangeTemp` and
   `onIdentify`.
3. **Any colour command leaves scene mode permanently.** `light_state_set_color` and
   `light_state_set_cct` both set `mode = MODE_COLOR`, and only `light_state_set_scene` sets it
   back — reachable exclusively through the uncalled next/prev functions.

Net effect: the device renders stored scene 0 at boot, and **the first colour or brightness command
from Home Assistant makes every effect unreachable until reboot.**

## 2. Decisions

| Question | Decision |
|---|---|
| Control surface | **Both** HA and the wall switch, roughly equally |
| Scope | **Effect selection only.** Colour and brightness stay live HA controls; speed uses each effect's default. No custom scene parameters. |
| Colour while an effect runs | **Exits the effect** to static colour — the firmware's existing behaviour |

Selection must therefore be presentable in HA *and* callable from an automation, because the
Inovelli's multi-tap events go to the coordinator rather than to a bound light. A hub automation is
unavoidable regardless of mechanism.

## 3. Feasibility — spiked 2026-08-15

Two mechanisms were rejected on evidence before this one was chosen:

- **Identify trigger-effect** (riding the effect dropdown Z2M already shows): no hook exists. The
  Arduino library exposes `onIdentify(uint16_t)` only, which is the identify *command*, not
  `TriggerEffect`. Unreachable from firmware.
- **Standard Scenes cluster**: already present in `ZigbeeColorDimmableLight` via `scenes_cfg`, and
  the stack handles Add/Recall for standard attributes — but it knows nothing about `EffectParams`,
  so it can store a colour and not an effect. It also exposes no "scene recalled" callback.

The chosen mechanism was spiked and **works**:

- A subclass reached `_cluster_list` (protected on `ZigbeeEP`) and called
  `esp_zb_zcl_attr_list_create` → `esp_zb_custom_cluster_add_custom_attr` →
  `esp_zb_cluster_list_add_custom_cluster`.
- It compiled, the device **booted, joined, and ran with no crashes or resets**.
- **Z2M saw the cluster as `64512`** (`0xFC00`) after a re-interview.
- Writes are deliverable: `zbAttributeSet` is a virtual on `ZigbeeEP`, dispatched to the matching
  endpoint by `ZigbeeHandlers.cpp`, and already overridden by `ZigbeeColorDimmableLight`.

## 4. Design

### Firmware

A `LumaryLight` subclass of `ZigbeeColorDimmableLight`, in `zigbee_light.cpp` with the rest of the
adapter:

> **Corrected during implementation.** This section originally specified selection as an *attribute
> write* handled by overriding `zbAttributeSet`. That is impossible: `ZigbeeColorDimmableLight`
> declares `zbAttributeSet` **private** (`ZigbeeColorDimmableLight.h:158`), so a subclass may
> override it but cannot call it. Overriding would have left on/off, level and colour with no
> handler at all and silently broken the light. Selection is therefore a **custom command** via
> `onCustomClusterCommand`, which is public and is the library's intended extension point, and the
> attribute is **read-only** state.

- **Constructor** adds custom cluster `0xFC00` with one attribute `0x0000` `effect` (u8,
  **read-only**), kept in step so a read reports what is actually running.
- **`onCustomClusterCommand`** receives `LUMARY_CMD_SET_EFFECT` (`0x00`) with a one-byte payload —
  the effect index. The cluster, command id and payload size are all checked before use, since this
  arrives straight off the air.
- **`zigbee_light_set_effect(uint8_t n)`**: ignore if `n >= EFFECT_COUNT`; otherwise
  `light_state_set_scene` (returns to `MODE_SCENE`), `scene_store_set_active` to persist, and write
  the attribute back so reads and the HA UI reflect reality.

`zigbee_light_next_scene()` and `prev_scene()` are **deleted**. They have never had a caller and
now never will — the automation computes the next index itself. Dead exported functions imply a
working path that does not exist, which is precisely what cost time diagnosing this. Recoverable
from git if a local scene button is ever wired to the BOOT pin.

### Behaviour

| Action | Result |
|---|---|
| Write `effect` 0–7 | That effect runs; persisted across reboot |
| Colour or CCT command | Exits to static colour (`MODE_COLOR`) |
| Brightness | Continues to scale the running effect, per `light_state_resolve` |
| Write out of range | Ignored |

### Switch stepping

No firmware support. The HA automation bound to the Inovelli 2× tap reads the current `effect` and
writes `(n ± 1) mod 8`.

### Z2M

An external converter exposing `effect` as an enum using the names already in `kEffects[].name`.
This is the only piece needing maintenance across Z2M upgrades; the firmware side is ordinary
Zigbee and is unaffected by them.

> **Amended 2026-08-17.** The implementation shipped this expose as `effect_select`, not `effect`
> as specified here, to avoid colliding with the Identify trigger-effects. That reasoning was
> wrong twice over. The collision was never avoided — `m.light()` defaults to `effect: true` and
> was already exposing those six trigger-effects, none of which the firmware handles, so Home
> Assistant showed a dead dropdown either way. And the name is load-bearing: Z2M's HA discovery
> collects every enum expose named `effect` into the light's `effect_list`, which is what puts the
> control in the light card and makes `light.turn_on(effect:)`, scene capture and voice work. Under
> any other name it can only ever be a `select` entity.
>
> Restored to `effect`, with `effect: false` and `powerOnBehavior: false` passed to `m.light()` so
> it stops advertising controls the firmware does not implement. The enum also gained a `none`
> member — HA's effect list has no null — carried on the air as `LIGHT_EFFECT_NONE` (0xFF), which
> closes the gap in §6 where a colour command left the attribute naming an effect that had stopped.

## 5. Testing

- **Host:** `light_state` tests covering `set_scene` returning `mode` to `MODE_SCENE`, and
  out-of-range indices being rejected.
- **Hardware:** write each of 0–7 and confirm the expected effect renders; power-cycle and confirm
  the selection persists; send a colour command and confirm it exits to static colour.
- **Regression:** the existing 35 native tests must pass unchanged.

## 6. Out of scope

- **Custom scene parameters.** The 16-slot NVS store stays as it is — seeded with defaults, now
  selectable. Saving edited scenes, and `scene_store_save` gaining a caller, is a separate change.
- **Gamma and per-channel white balance**, observed repeatedly on 2026-08-15.
- **Reporting light state after reboot**, so HA does not show a light as on when it is physically
  off. *(Since implemented for on/off and level via `LumaryLight::publishState`; the effect
  attribute cannot be reported at all, so the converter reads it back on `deviceAnnounce`.)*
- The deferred power-cycle-reset / BLE-OTA-trigger rework (board spec §4.5).

## 7. Follow-up

The README currently oversells three things and must be corrected alongside this change: per-scene
parameters are stored but not editable, Add Scene is not implemented, and 2× tap works through a
hub automation rather than pure binding.

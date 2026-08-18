# Two light entities from one fixture — design

**Date:** 2026-08-17
**Status:** approved, ready for implementation

Item 9 of `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`, which recorded the decision
("a second Zigbee endpoint, so HA gets two light entities under one device") but not the design.
This is that design, and it revises one part of the original note — see §2.1.

Taken after item 6 (gamma) deliberately: item 9 balances the downlight against the ring by eye, and
tuning that balance on an uncorrected brightness curve would mean tuning it twice.

## 1. Problem

### 1.1 The capability the fixture was bought for is unreachable

`WHITE_SAT_THRESHOLD` in `src/light_state.h:16` routes every command to exactly one of the two light
sources: saturation at or below 32 goes to the inner CW/WW white string, anything above it goes to
the outer RGB ring. `light_state_resolve()` (`src/light_state.h:153`) is where that fork lives.

The consequence is that colour and white are mutually exclusive. The fixture is sold as a "gradient
auxiliary light" — a white downlight with a coloured accent ring around it — and that combination
cannot be commanded at all. One entity can only be in one mode.

### 1.2 What Home Assistant sees today

One `light.*` entity carrying on/off, brightness, colour temperature 154–370 mired, xy colour, and an
eight-entry effect list. One device, one light, one mode at a time.

## 2. Decisions

| Decision | Choice | Why |
|---|---|---|
| Endpoint 1 | Downlight (inner CW/WW string) | Existing switch bindings target endpoint 1 and should land on the main light; the OTA client and Basic strings already live there |
| Endpoint 2 | Accent Ring (outer 62-pixel RGB) | New endpoint, so nothing existing moves |
| Downlight capability | `COLOR_TEMP` only, 154–370 mired | It has no colour dice |
| Ring capability | `HUE_SATURATION` + `X_Y` | It has no white die; advertising CCT would expose a control that lies |
| Effects | belong to the ring; list shrinks 8 to 6 | See §2.1 |
| State model | two purpose-built structs in one container | See §3.3 |
| Switch binding | bind both endpoints | See §2.2 |

### 2.1 The effect list shrinks, and this revises the backlog note

The backlog said "the effect cluster stays on endpoint 1 — effects are a whole-fixture concept."
Reading what the effects actually do, that is no longer true:

- Seven of the eight call `white_off()` and drive only the ring.
- The eighth, `fx_static_white`, drives only the white string. Once the downlight is its own entity
  with its own on/off, level and colour temperature, that *is* the downlight — it stops being an
  effect and becomes the entity's ordinary behaviour.

`fx_static_color` goes the same way: with the ring as its own entity, "ring showing a solid colour"
is the ring being set to a colour, which the code already models as `MODE_COLOR` / `effect: none`.

So effects belong to the ring, and the list drops to six:

| Today | After |
|---|---|
| `static_white` | gone — it is the Downlight entity |
| `static_color` | gone from the list — it is `effect: none` with a colour set |
| `warm_gradient`, `color_gradient`, `breathing`, `color_cycle`, `chase`, `nightlight` | kept, renumbered from 0 |

This is a breaking change with three consequences, all handled in §3.7 and §6: NVS scene indices
shift, the converter's `EFFECTS` map is renumbered, and any Home Assistant automation naming
`static_white` or `static_color` stops working.

### 2.2 Both endpoints get bound to the switch

A binding targets one endpoint. Binding only the downlight would leave the ring lit after the light
is switched off at the wall, recoverable only if the coordinator is up — a regression against
today's behaviour, in a fixture whose accent ring is the selling point.

The alternative considered and rejected was a firmware rule mirroring downlight-off to the ring.
Every version of that rule is bad: ganging them deletes the "downlight on, ring off" state, which is
the most common one for a ceiling can; a one-way rule with no restore means every use of the wall
switch silently discards the ring setting; and a rule that restores requires the firmware to hold a
shadow copy of the ring's pre-off state that Home Assistant cannot see. All three share a fatal
flaw: the callback cannot tell a switch command from a hub command, so turning the downlight off
from the Home Assistant dashboard would darken the accent ring with nothing in the UI to explain it.

Binding both endpoints produces a similar outcome by a better mechanism. It is configuration, not
behaviour: visible in Z2M's binding table, editable, removable. The firmware stays honest — two
endpoints, genuinely independent, no hidden state — and each endpoint keeps its own level and colour
across the cycle because each simply receives a normal Zigbee command.

**Confirmed on the hardware (2026-08-17): the Inovelli sends discrete `On` and `Off`, not `Toggle`.**
This is what makes binding both safe. Under `Toggle`, two endpoints in different states would
diverge on every tap, and the fallback would be to bind the downlight only and drive the ring from
the hub. The behaviour is not documented by Inovelli — not in the manual, the help centre, the Z2M
device page, or the community threads — so it was settled empirically: with the light off, tap up
twice. Staying on means discrete `On`; going off would have meant `Toggle`.

The switch's transmitting endpoint is **2** (endpoint 1 receives, endpoint 2 is the paddle,
endpoint 3 is the config button), so both bindings are from switch endpoint 2:

```
{"from": "switch", "from_endpoint": 2, "to": "fixture", "to_endpoint": 1, "clusters": ["genOnOff", "genLevelCtrl"]}
{"from": "switch", "from_endpoint": 2, "to": "fixture", "to_endpoint": 2, "clusters": ["genOnOff", "genLevelCtrl"]}
```

Residual cost, worth stating in the README: tap up turns the ring on too, so "downlight only"
becomes a Home Assistant action rather than a switch action.

## 3. Design

### 3.1 Endpoints

Both are `ZigbeeColorDimmableLight` — the Arduino library offers no colour-temperature-only class —
differentiated by capability and by what is bolted on:

| | Endpoint | Capabilities | Extras |
|---|---|---|---|
| Downlight | 1 | `COLOR_TEMP`, range 154–370 mired | OTA client, Basic `SWBuildID`/`DateCode`, Identify |
| Accent Ring | 2 | `HUE_SATURATION` + `X_Y` | custom cluster `0xFC00` (effect) |

Identify stays on endpoint 1 and still blinks the ring blue: it answers "which can in the ceiling is
this", a whole-fixture question. It keeps darkening the downlight for the duration, because a lit
downlight washes out a blue ring — but that now happens in the render loop rather than inside the
effect (§3.5).

### 3.2 The converter

Two `m.light()` extends with `endpointNames`, an endpoint map, and `meta: {multiEndpoint: true}`:

```js
endpoint: (device) => ({downlight: 1, ring: 2}),
meta: {multiEndpoint: true},
extend: [
    m.light({endpointNames: ['downlight'], colorTemp: {range: [154, 370], startup: false},
             color: false, effect: false, powerOnBehavior: false}),
    m.light({endpointNames: ['ring'], color: {modes: ['xy']},
             effect: false, powerOnBehavior: false}),
    m.identify(),
    m.deviceAddCustomCluster('lumary', {/* unchanged */}),
],
```

`effect: false` and `powerOnBehavior: false` stay on both for the reasons already recorded in
`2026-08-15-effect-selection-design.md` and the polish backlog's item 2: the firmware implements
neither the Identify trigger-effects nor `StartUpOnOff`, and leaving them on puts dead controls in
the light card.

Home Assistant ends up with `light.<name>_downlight` and `light.<name>_ring` under one device. The
effect expose targets endpoint 2 only, so the dropdown appears on the ring entity where it belongs,
and `tzColorClearsEffect` narrows to that endpoint too.

### 3.3 State model

`src/light_state.h` becomes:

```c
struct DownlightState { bool on; uint8_t level; uint8_t cct; };
struct RingState      { bool on; uint8_t level, hue, sat, scene; LightMode mode; };
struct FixtureState   { DownlightState down; RingState ring; };
```

Two purpose-built structs rather than two copies of today's combined one, so each type states
exactly what its source has and nothing more; wrapped in one container so `zigbee_light_state()`
still returns a single pointer and `main.cpp` deals with one object.

**Deleted outright:**

- `WHITE_SAT_THRESHOLD`. It answered "is this command white or colour?" — a question that stops
  existing once each source has its own endpoint. A command arrives *at* the ring or *at* the
  downlight; there is nothing left to infer.
- `rgb_to_cct()`. It guessed warmth from an RGB colour for coordinators that express white as RGB.
  The downlight advertises colour-temperature capability only, so it receives mireds directly.

**Kept:** `rgb_to_hsv()` (the ring still converts incoming RGB), `mireds_to_cct()`, `scale_level()`,
`LIGHT_EFFECT_NONE`, `LightMode`, `CCT_MIRED_WARM` / `CCT_MIRED_COOL`.

`LightMode` survives on the ring only, now meaning exactly what it always did underneath:
`MODE_SCENE` = running an effect, `MODE_COLOR` = showing a solid colour.

`light_state_resolve()` splits in two:

```c
inline uint8_t      downlight_level(const DownlightState* s);              // one-line `on` gate
inline EffectParams ring_state_resolve(const RingState* s, const EffectParams* scene);
```

`ring_state_resolve` is today's logic minus the white branch. It does **not** encode the mode in its
return value — the render loop reads `ring.mode` directly, which is where the information already is.

In `MODE_COLOR` the returned `type` field is unused, because the render loop calls `fx_ring_solid`
without consulting it. It must still be **initialised to a valid index** rather than left
indeterminate: `EffectParams` is returned by value and a later reader of an uninitialised enum is
undefined behaviour. Set it to the active `scene`, which is always in range.

### 3.4 The effect list

`src/effect_params.h`:

```c
enum EffectType : uint8_t {
    EFFECT_WARM_GRADIENT  = 0,
    EFFECT_COLOR_GRADIENT = 1,
    EFFECT_BREATHING      = 2,
    EFFECT_COLOR_CYCLE    = 3,
    EFFECT_CHASE          = 4,
    EFFECT_NIGHTLIGHT     = 5,
    EFFECT_COUNT          = 6
};
```

`kDefaultParams` and `kEffects` both shrink to six entries in this order.

`fx_static_white` is **deleted** — the downlight renders straight through `white_mix_gamma()`.

`fx_static_color` is **renamed `fx_ring_solid` and leaves `kEffects`** rather than being deleted:
something still has to render `MODE_COLOR`. It joins `fx_identify` as a function the render loop
calls but the dropdown never lists.

`src/effects.cpp` stops including `led_driver.h`. All seven `white_off()` calls and the helper itself
come out, and the file becomes purely "fill a `CRGB` buffer" with no hardware dependency.

### 3.5 Render loop

`src/main.cpp`:

```cpp
if (identifying) {
    fx_identify(now, leds);
    led_driver_set_cw(0);
    led_driver_set_ww(0);
} else {
    const EffectParams rp = ring_state_resolve(&s->ring, &scene);
    if (s->ring.mode == MODE_COLOR) fx_ring_solid(rp, leds, s->ring.on);
    else kEffects[rp.type].fn(now - effect_start, rp, leds, s->ring.on);

    const WhiteMix w = white_mix_gamma(downlight_level(&s->down), s->down.cct);
    led_driver_set_ww(w.ww);
    led_driver_set_cw(w.cw);
}
led_driver_show(leds, RING_NUM_LEDS);
```

The downlight now goes straight through `white_mix_gamma()` from `src/brightness.h`, unchanged —
which is the payoff for having done item 6 first.

The scene reload guard in `loop()` keys on `s->ring.scene` rather than `s->scene`.

**One easily-missed edit:** the NVS corruption guard at `src/main.cpp:92` currently reads

```cpp
if (p.type >= EFFECT_COUNT) p.type = EFFECT_STATIC_WHITE;
```

and `EFFECT_STATIC_WHITE` is one of the two enumerators being deleted, so this stops compiling. Its
fallback becomes `EFFECT_WARM_GRADIENT` — the new index 0, and a ring effect, which is what the
guard should have been reaching for all along now that the ring owns the effect table.

### 3.6 `zigbee_light.cpp`

A small hierarchy keeps the raw `esp_zb` plumbing in one place:

```
LumaryEndpoint : ZigbeeColorDimmableLight    // publishState(on, level), setAttr, reportAttr
  LumaryDownlight : LumaryEndpoint           // + Basic string attrs, OTA client
  LumaryRing      : LumaryEndpoint           // + cluster 0xFC00, publishState(on, level, effect)
```

Callbacks are registered per endpoint object and carry no endpoint id, so each endpoint gets its own
static callback functions. The `s_last_r/g/b` colour-change detection (`src/zigbee_light.cpp:139`)
moves to the ring only — the downlight has no colour capability and receives colour-temperature
commands directly, so it needs no equivalent.

`publish_effect_attr()` targets `RING_ENDPOINT`. `zigbee_light_loop()`'s join-time publish covers
both endpoints.

`src/config.h` replaces `LIGHT_ENDPOINT 1` with `DOWNLIGHT_ENDPOINT 1` and `RING_ENDPOINT 2`.

### 3.7 NVS migration is one line

`NVS_FMT_VER_CURRENT` goes 1 to 2. `scene_store_init()` already discards and reseeds when the stored
version does not match, so bumping it *is* the migration: scenes stored against the old eight-entry
indices are dropped and reseeded from the new six-entry `kDefaultParams`. No new code.

## 4. Testing

`test/test_light_state` is rewritten around the two resolves. It loses its `WHITE_SAT_THRESHOLD` and
`rgb_to_cct` coverage along with those functions, and gains:

| Test area | What it asserts |
|---|---|
| Downlight gating | `on == false` yields level 0 whatever `level` holds; `on == true` passes it through |
| Downlight CCT | `mireds_to_cct` endpoints and midpoint still map as before |
| Ring mode transitions | a colour command moves `MODE_SCENE` to `MODE_COLOR`; selecting a scene moves it back |
| Ring scene validation | an out-of-range index is ignored outright, leaving scene and mode untouched |
| Effect value reporting | `MODE_COLOR` reports `LIGHT_EFFECT_NONE`; `MODE_SCENE` reports the index |
| Effect table | `EFFECT_COUNT == 6`; `kDefaultParams` and `kEffects` are both that long; every default's `type` is in range |

`z2m/test/converter.test.mjs` grows to cover the endpoint map, two `m.light()` extends, the six-entry
effect list, and that the effect expose is on endpoint 2.

`test_brightness`, `test_pixel_encode`, `test_version` and `test_identify` are untouched.

## 5. Risks

1. **`ZigbeeColorDimmableLight` configured `COLOR_TEMP`-only** keeps the `HA_COLOR_DIMMABLE_LIGHT`
   device ID. Z2M should read `colorCapabilities` and present a colour-temperature light, but this
   is the first thing to check when the fixture joins.
2. **One OTA client across two endpoints.** The library's OTA support was written against
   single-endpoint examples; needs confirming it does not assume the OTA endpoint is the only one.
3. **Two `ZigbeeColorDimmableLight` instances.** `ZigbeeCore::addEndpoint` pushes onto a list and the
   callback dispatch loops over it, so multi-endpoint is supported by construction — but this
   firmware has only ever registered one, so it is unproven here.
4. ~~Inovelli `On`/`Off` vs `Toggle`~~ — **resolved 2026-08-17: discrete `On`/`Off`.** See §2.2.

## 6. Migration for fixtures already in service

Adding an endpoint changes the device descriptor, and **Z2M caches endpoints from the interview** —
the same mechanism that made `sw_version` read `null` until re-interview in item 5.

So: newly paired fixtures just work; **every fixture already in service needs a re-interview** (Z2M
frontend, or `zigbee2mqtt/bridge/request/device/interview`) before the ring entity appears. This
belongs in the README prominently, not as a footnote.

Two further consequences of the same change:

- **Old converter, new firmware:** endpoint 1 works as a plain light and endpoint 2 is invisible.
  Degraded, not broken — which matters because the firmware and the converter update independently.
- **Automations naming the two dropped effects** (`static_white`, `static_color`) stop working, per
  §2.1. The README should say so and name the replacements: the Downlight entity, and `effect: none`
  with a colour.

## 7. Out of scope

- **Power-on behaviour** (item 2b). The downlight's colour temperature and level still do not persist
  across a power cut; that is `StartUpOnOff` and friends, and it is its own item.
- **Transitions** (item 3), **effect parameters** (item 7), **binding and reporting configuration**
  (item 8), **upstreaming the converter** (item 10), **the switch blueprint** (item 11).
- **Per-endpoint effect parameters.** Item 7 asked whether effect parameters are per-endpoint or
  global; with effects now belonging solely to the ring, the question dissolves — they are the
  ring's.

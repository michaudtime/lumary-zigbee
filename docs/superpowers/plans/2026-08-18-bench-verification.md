# Bench verification — everything outstanding

**Date:** 2026-08-18
**Status:** in progress — §§1-8 done 2026-08-18

Three separate bodies of work are merged to `main` and unverified on hardware. This consolidates
their outstanding checks into one session, in an order chosen so that each check does not mask the
next.

| Source | What it left open |
|---|---|
| Item 6 — gamma and the low end | `2026-08-17-gamma-and-low-end.md` Task 5 |
| Item 9 — two light entities | `2026-08-17-two-endpoint-split.md` bench section, plus the final review's watch list |
| Board rev A | `2026-08-01-brain-replacement-board.md` Task 6.4, and two caveats from 6.3 |

**The single question underneath all of it:** the white string's PWM went from 8-bit to 12-bit, so
minimum on-time dropped from 3.9 µs to 0.24 µs. Whether the L-SD8E1's constant-current loop responds
at all down there is what no host test can settle, and both merged features depend on the answer.

---

## Before you start

- [ ] Fixture on the bench, USB for the module, real L-SD8E1 driver for the 36 V string
- [ ] Z2M running with `z2m/lumary-brain-revA.js` installed in `data/external_converters/`
- [ ] Somewhere to write numbers down — several steps below want a measured value, not a pass/fail

Flash first:

```bash
pio run -e esp32h2 --target upload
```

Expected: `boot ok` then `LED driver init ok` on the monitor.

> **A panic at `ledc_timer_config` means 12-bit at 1 kHz was rejected.** `ESP_ERROR_CHECK` in
> `src/led_driver.cpp` makes that loud rather than silent. If it happens, drop to
> `LEDC_TIMER_10_BIT` with `PWM_DUTY_MAX` 1023 and regenerate `kGamma12` by passing `1023` to
> `emit()` in `scripts/gen-gamma-tables.py`.

> **A panic before `boot ok`** points at static-init cost: two `ZigbeeColorDimmableLight` cluster
> lists are now built before `app_main`, where there used to be one.

---

## 1. The downlight's on/off — PASS

**Order matters here more than anywhere else in this document.** The last branch fixed a bug where
the downlight's on/off and level were dropped until a colour-temperature command arrived. Touching
the CCT slider first flips the colour mode and masks the bug for the rest of the session.

- [x] Power cycle the fixture
- [x] **Without touching colour temperature**, send `light.turn_on` to `light.*_downlight` from HA
- [x] Tap up on the Inovelli

Both worked immediately. The mode-agnostic callback shims added in the item 9 review are confirmed
on hardware: `ZigbeeColorDimmableLight` leaves `_current_color_mode` at `CURRENT_X_Y`, and
registering an RGB-shaped callback alongside the CCT one is what keeps endpoint 1 — the endpoint the
wall switch binds to — responsive before any colour command arrives.

---

## 2. Both entities exist and are shaped right — PASS, with one finding

Verified 2026-08-18 against the HA entity registry and state machine, after installing the updated
converter and restarting Z2M.

- [x] Both entities appear under **one** device
      (`light.overhead_light_test_downlight`, `light.overhead_light_test_ring`, and
      `button.overhead_light_test_identify`, all on `device_id 1aca1363db02d25c5b57b49d3a95e104`)
- [x] The **downlight** shows a colour-temperature control and **no colour wheel** —
      `supported_color_modes: ["color_temp"]`, 2702–6493 K
- [x] The **ring** shows a colour wheel and **no colour-temperature slider** —
      `supported_color_modes: ["xy"]`

**Design risk 1 is settled.** `ZigbeeColorDimmableLight` configured `COLOR_TEMP`-only does present
correctly through Z2M; keeping the `HA_COLOR_DIMMABLE_LIGHT` device ID did not leak an X/Y control
onto the downlight. The old combined `light.0x744dbdfffe6b575f` was replaced outright rather than
left behind as an unavailable entity.

- [ ] Check the reported `colorMode` attribute on endpoint 1. The library's default cluster config
      leaves it at `0x01` (CurrentX/Y) on an endpoint that advertises no X/Y capability — Z2M may
      show a colour picker anyway, or log a capability warning. Record what it does.
      **Still open:** read it from the Z2M frontend's Dev console tab (endpoint 1, cluster
      `lightingColorCtrl`, attribute `colorMode`). The HA side already looks right, so this is now
      a curiosity rather than a risk.

- [x] **Does the downlight card also show an effect dropdown?** **Yes.** Both entities carry the
      identical seven-entry `effect_list`:
      `[none, warm_gradient, color_gradient, breathing, color_cycle, chase, nightlight]`.
      Z2M's Home Assistant discovery union is **not** endpoint-aware. This is a known cost rather
      than a surprise — `z2m/lumary-brain-revA.js:46` documents the union as the reason the expose
      is named `effect` at all, since that name is what puts the dropdown inside the light card
      instead of creating a separate `select` entity. What selecting an effect from the
      **downlight** card actually does is still unknown; §7 settles it.

Both entities read `state: unknown` immediately after the restart — no state message received yet.
Expect that to clear on the first command or power cycle. If it persists, it belongs in §10.

The entity IDs changed with the split. A config-body search across automations, scripts, scenes
and helpers found no references to the old `light.0x744dbdfffe6b575f`, so nothing needs repointing
for this fixture — but the other Lumary fixtures will when they are flashed.

---

## 3. The low end, on the white string — PASS

This is the 0.24 uS question, and it is the one every other result rested on.

- [x] Set the downlight to brightness **1**, then 2, 3, 4, 5

Brightness 1 produces visible dim light, and each step is brighter than the last.

**Measured N: none — there is no dead zone.** The L-SD8E1's constant-current loop does respond to a
single-count duty at 12-bit resolution (1/4095 at 1 kHz, a 0.24 uS pulse). The 8-bit to 12-bit move
is therefore sound at the bottom of its range, not merely at the top.

Consequences:

- No code change. The provisional `max(1, ...)` floor in `white_mix_gamma()`
  (`src/brightness.h`) stands as written, and no measured-minimum lift is needed.
- The CIE L* curve's linear segment below L* = 8 is doing exactly what it was chosen for: it is
  what lets brightness 1 resolve to a non-zero duty at all, and the driver honours it.
- The `+127` rounding added to `white_mix_gamma()` is exercised in this range, so a clean sweep here
  is also evidence against the truncation bug it was added to fix. Section 5 confirms it directly.

---

## 4. The brightness ramp — PASS

- [x] Step the downlight through 1, 5, 10, 25, 50, 100, 150, 200, 255

Even ramp, no jump out of the low end, no region where steps look identical.

**Brightness 255 tops out at 254 — this is correct, not a defect.** ZCL `genLevelCtrl` defines
`CurrentLevel` over `0x00`-`0xFE` with `0xFF` reserved, so Z2M's HA discovery carries
`brightness_scale: 254` and HA's 0-255 slider is scaled onto 0-254 on the wire. The firmware cannot
receive 255. Every Zigbee light on the network behaves the same way.

Two consequences, neither worth acting on:

- Index 255 of `kGamma8`/`kGamma12` is unreachable, so peak output is `gamma8(254) = 252` and
  `gamma12(254) = 4054` rather than 255 / 4095 -- about **1% below theoretical maximum**, at the
  flattest part of the curve. Rescaling so level 254 reached full duty would make this fixture
  inconsistent with every other light on the network to correct something invisible.
- `scale_level()` (`src/light_state.h:64`) exists to handle level 255 without `scale8`'s
  255 -> 254 off-by-one. That path is unreachable over Zigbee, since its only caller feeds it a
  Zigbee-sourced level. Correct and harmless, just never exercised.

- [x] Sweep the **ring** through the same values

Also even. The ring dims correctly across its full range under the CIE curve, judged against the
downlight rather than against memory -- `gamma8(64) == 11` puts mid-slider roughly 6x below the old
linear behaviour, and that reads as intended rather than as a fault.

Item 6's core claim is now confirmed on hardware for both light sources: one perceptual curve, two
very different drive paths (12-bit LEDC into a constant-current driver, and 8-bit NZR into the
pixel ring), and the ramps match each other.

---

## 5. Colour temperature holds while dimming — PASS

- [x] Set 2700 K (370 mired), sweep brightness 255 -> **10**
- [x] Repeat at 4000 K (250 mired) and at 6500 K (154 mired)

All three dim without drifting warmer or cooler.

The three points are deliberately different tests, and all three passing is what makes this
meaningful:

- **370 and 154 mired** are `CCT_MIRED_WARM` / `CCT_MIRED_COOL` (`src/light_state.h:17-18`), the
  rated temperatures of the two strings. They drive `cct` to 0 and 255, so one string carries the
  whole output and `white_mix_gamma()` reduces to a single `gamma12()` lookup.
- **250 mired** gives `cct` 141 -- a genuine split, where both coefficients are non-zero and the
  rounding matters.

That middle case sweeping to 10 without a colour shift is the hardware confirmation of the `+127`
rounding added to `white_mix_gamma()`. The bug it replaced collapsed a 50/50 request to fully warm
at level 1, and the original plan's stop-at-20 would have passed straight over it. Rounding also
makes `ww + cw == total` exactly, so no output is lost to truncation at any level.

---

## 6. Both sources at once — PASS

- [x] Downlight at 2700 K, ring at a saturated colour, **both on**
- [x] A ring effect running over a lit downlight

Both visible, both independent. **This is the result item 9 existed to produce.** Under the old
combined model `WHITE_SAT_THRESHOLD` in `src/light_state.h` made these mutually exclusive: a
saturated colour meant the white string was off, which put the fixture's "gradient auxiliary light"
selling point out of reach entirely. Two endpoints, two `FixtureState` sub-structs, two HA entities,
and the exclusion is gone.

- [x] Ring balance against a lit downlight: no wash-out reported. Gamma going in first is what this
      was resting on -- the ring's lower half now sits roughly 6x below its old linear behaviour, so
      a coloured ring against a bright white downlight was the plausible failure and did not happen.

---

## 7. The effects — PASS, with one finding and one wart

- [x] All six run and dim: `warm_gradient`, `color_gradient`, `breathing`, `color_cycle`,
      `chase`, `nightlight`
- [x] The effect dropdown is on the **ring** entity and lists six plus `none`

### FINDING: effects collapse to black below roughly brightness 20

Reported at the bench: `warm_gradient` "looks like it turns off" at brightness 16. Reproduced in
arithmetic -- this is a **resolution limit, not a logic bug**, and it is confined to the ring.

At brightness 16 the CIE curve asks for 0.7% output. That is 28 counts of 4095 on the downlight,
which is comfortable, and under 2 counts of 255 on the ring, which is not. The effect then scales
each pixel *again* before the brightness multiplier lands:

```
warm_gradient at brightness 16 (gamma8(16) = 2):
  peak  {255,169, 87} -> {2,1,0}
  3/4   {191,126, 65} -> {1,0,0}
  mid   {127, 84, 43} -> {0,0,0}
  1/4   { 63, 42, 21} -> {0,0,0}
```

Most of the ring is driven to true black and the peak is a single dim red count. Available ring
output levels by slider position: 10 -> 1, 16 -> 2, 24 -> 3, 32 -> 4, 48 -> 7, 64 -> 11.

This is a real change from pre-gamma behaviour, where linear scaling gave the peak pixel 15 counts
at slider 16 rather than 2. The curve is correct; the ring's 8-bit output cannot express it. Section
4 passed because a *solid* colour still resolves to a non-zero count -- it is specifically effects,
which pre-scale their pixels, that fall off the bottom.

Two candidate responses, deliberately not actioned mid-bench:

- **Cheap partial:** `scale_by_255()` in `src/brightness.h` truncates. Adding `+127` rounding --
  the same fix `white_mix_gamma()` already carries for the white split, for the same reason --
  recovers one output step, taking the mid pixel from 0 to 1 and moving the collapse threshold down
  a couple of slider positions. Does not solve it.
- **Real fix:** temporal dithering, alternating adjacent output values across frames so the
  time-average falls between them. This is how 8-bit pixel libraries reach effective 12-bit
  behaviour. Needs a stable frame rate and its own design cycle.

### The rest of the section

- [x] Setting a ring colour drops `effect` to `none`
- [x] **Changing the downlight's colour temperature while a ring effect runs leaves the effect
      alone.** This was the last branch's regression check: `tzColorClearsEffect` had been keyed on
      `color_temp` as well as `color`, so a downlight CCT change would have cleared the ring's
      effect. Narrowing it to `key: ['color']` is confirmed correct on hardware.
- [x] `warm_gradient` at full brightness looks good -- its rendering changed in the gamma work, not
      only its dimming, and the change is an improvement.
- [x] `fx_breathing` behaves correctly through its trough at usable brightness. (At low brightness
      it is subject to the collapse finding above, like every other effect.)

### WART: the downlight's effect dropdown turns the light on and does nothing else

Observed: with the downlight off, selecting an effect from its card **turns the downlight on**. With
it already on, cycling through effects changes nothing at all. The ring is unaffected throughout.

Both halves follow from code:

- The light coming on is Home Assistant's `light.turn_on` semantics. Selecting an effect calls
  `light.turn_on` with an `effect` attribute, and the MQTT light platform always includes
  `state: ON` in that payload. Endpoint 1 receives a legitimate On command and obeys it.
- The effect going nowhere is `tzEffect`, whose `key` is `['effect']` -- so it *does* match on the
  downlight entity, resolves the endpoint to 1, and issues `setEffect` against cluster `0xFC00`,
  which exists only on endpoint 2. The command cannot succeed.

So the §2 union finding resolves as: harmless but untidy. Nothing is corrupted and the ring is never
disturbed; the downlight card simply carries a control that does nothing except switch the light on.

Suppressing it is not available through the converter. `z2m/lumary-brain-revA.js:46` already records
why the expose is named `effect` -- that exact name is what makes it a first-class light control on
the ring rather than a separate `select` entity -- and Z2M's discovery union is the price. Both
`m.light()` calls already pass `effect: false`. The real fix is making Z2M's HA discovery
endpoint-aware, which belongs with item 10 (upstreaming the converter).

Worth capturing the Z2M log line for the failed `setEffect` next time the fixture is on the bench;
it would confirm whether the command errors or is silently absorbed.

---

## 8. Persistence and rejoin

### CRITICAL, found and fixed mid-section: the fixture would not boot without USB

Discovered 2026-08-18 while setting up the power-cycle test, and fixed in `ab5dfe9`.

Every bench session to date had the module USB-tethered to a PC. Pulling mains therefore never reset
the MCU -- USB kept it alive -- so **this board had never once cold-booted standalone.** The first
time it did, the downlight came on at full and the device was unreachable over Zigbee.

Cause: `setup()` opened with an unbounded wait.

```cpp
Serial.begin(115200);
while (!Serial) delay(10);      // never returns with no USB host
```

`platformio.ini` builds with `-DARDUINO_USB_CDC_ON_BOOT=1`, so `Serial` is the USB CDC and its
`operator bool()` stays false until a host enumerates it. With no PC attached, `setup()` never
returned and everything below that line was dead code:

- `led_driver_init()` never configured the white PWM GPIO. The pin stayed in its reset default, the
  L-SD8E1 saw no gating signal, and the driver ran at 100% -- **downlight full on**.
- `zigbee_light_init()` never started the radio -- **device unreachable**.
- `loop()` never ran -- ring dark.

Fixed by bounding the wait to one second, which still catches `boot ok` on the monitor at the bench
without stranding a mains-only boot.

**Verified on hardware the same session:** with USB fully unplugged from the PC, the fixture cold-
boots from mains alone, the downlight stays off as it should, and the device joins the network. This
is the first time the board has ever completed a standalone power-up.

**This blocked section 11 outright** -- there is no USB host in a ceiling, so the fixture could never
have worked installed. It is the single most valuable thing this bench session found, and it was
only reachable by testing the real power-up path rather than a tethered one.

Lesson worth carrying: a bench rig that differs from the deployed configuration in *any* powered
respect will hide exactly the faults that matter. Sections 1-7 all passed while this was live.

### The section proper — PASS

- [x] Select an effect, power cycle, and watch what HA shows

Tested with `chase` (index 4) rather than `warm_gradient` (index 0), deliberately: index 0 is both
the reseed default and the effect attribute's static-init value, so it cannot distinguish a restore
from a reset. Cold boot performed with USB fully unplugged, so this also exercised the deployed
power-up path.

Result, all three in agreement:

- the ring physically renders `chase` once turned on
- a **forced** read of the attribute -- the refresh control on the Effect expose, which calls
  `convertGet` -> `entity.read('lumary', ['effect'])` on endpoint 2, bypassing Z2M's cache --
  returns `chase`
- HA reports `effect: chase`, `color_mode: xy`, `brightness: 255`

The forced read matters: Z2M persists its own state store across restarts, so the Exposes tab
showing the right value proves nothing on its own. Only the explicit read confirms the device itself
holds it.

**How the attribute gets synced**, since it is not obvious and is easy to mis-read: the ZCL attribute
is built during static init, before `setup()` ever opens NVS, so at boot it does not reflect the
restored scene. `zigbee_light_init()` does not fix that, and neither does `zigbee_light_report()`.
The correction is a one-shot in `zigbee_light_loop()`, fired the first time `Zigbee.connected()`
goes true, which publishes both endpoints' real state including the effect. The comment there names
the exact failure it prevents. Anything that reorders or removes that one-shot silently reintroduces
"a read reports effect 0 whatever is really running".

Also worth noting for future readers: **Home Assistant nulls every light attribute while the light
is off** -- `effect`, `brightness` and `color_mode` all go null together. An off light showing no
effect is not evidence of anything. The ring must be on for this check to mean anything.

- [x] Confirm the stored scenes reseeded rather than misread

Passed behaviourally in section 7: all six effects rendered correctly with sensible parameters, which
they would not have if version 1's eight-effect records had survived and been reinterpreted under the
new six-effect numbering. `scene_store_init()` reseeds all `EFFECT_COUNT` slots from `kDefaultParams`
whenever the stored `fmt_ver` does not match `NVS_FMT_VER_CURRENT`.

NVS scene storage had never run on hardware before today -- the earlier bench session used
`BENCH_DEMO_MODE`, which bypasses `scene_store` entirely. It works.

---

## 9. The switch, both bindings

Bind from the Inovelli's endpoint **2** (the paddle) to both fixture endpoints, clusters `genOnOff`
and `genLevelCtrl`:

```
switch ep2 -> fixture ep1   (downlight)
switch ep2 -> fixture ep2   (accent ring)
```

- [ ] Tap down: **both** sources go off
- [ ] Tap up: **both** return, each at its own level and colour
- [ ] Hold: both dim
- [ ] **Drift them apart deliberately** — ring off, downlight on — then tap up

That last one is the important one. It is where a `Toggle` command would show itself, and the whole
binding design rests on the switch sending discrete `On`/`Off`. That was confirmed once; this
confirms it under the two-endpoint arrangement.

---

## 10. The upgrade path, on a fixture already in service

- [ ] Flash the fixture at `0x744dbdfffe6b575f` and, **before re-interviewing it**, record what Z2M
      and HA show

The README currently hedges because nobody knows: does the ring entity fail to appear at all, or
does it appear and not respond? Possibly Z2M's `configure` throws on the missing endpoint.

Observed: ______________________________________________

- [ ] Then re-interview (Z2M frontend → device → Re-interview) and confirm both entities work
- [ ] Correct `README.md`'s "Upgrading an already-paired fixture" wording to match what you saw

---

## 11. Board rev A sign-off (plan Task 6.4)

Still open from the board plan, and the gate on signing off rev A.

- [ ] Unplug the stock board, plug the new one into the existing harnesses, power the real driver
- [ ] Both light sources work in the fixture
- [ ] Zigbee binding to the Inovelli works from the installed location
- [ ] RSSI in Z2M better than −70 dBm
- [ ] A real Zigbee OTA completes from the installed location

That last one also settles **design risk 2**: one OTA client registered on endpoint 1 while two
endpoints exist. The library's OTA support was written against single-endpoint examples.

Two caveats carried from Task 6.3:

- [ ] **Re-check thermals in the sealed can.** 6.3's numbers (Q2 at 37 °C, U2 at 40 °C) were
      open-air bench at 23 °C ambient. The deltas should hold; the absolute temperatures will not.
- [ ] **Meter the leg current in series.** Never actually measured — P0.5's `I_set` is still taken on
      trust from the driver's CC rating.

---

## Recording the results

Three documents want updating, each in the style of how items 4 and 5 were closed out:

| Result | Goes in |
|---|---|
| Sections 3–7 | `2026-08-17-home-assistant-polish.md` item 6 → DONE, with the measured N from §3 |
| Sections 1, 2, 6–10 | same file, item 9 → add the bench-verified note |
| Section 11 | `2026-08-01-brain-replacement-board.md` Task 6.4 → PASS/FAIL, signs off rev A |

If §3 found a real minimum duty, that also means a code change: `white_mix_gamma()` in
`src/brightness.h` lifts non-zero results to the measured floor, plus a test in
`test/test_brightness` asserting it.

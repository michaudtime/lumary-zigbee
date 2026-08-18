# Bench verification — everything outstanding

**Date:** 2026-08-18
**Status:** in progress — §§1-3 done 2026-08-18

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

## 4. The brightness ramp — downlight PASS, ring outstanding

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

- [ ] Sweep the **ring** through the same values

Expected: also even -- and **dramatically dimmer across the whole lower half than you remember.**
`gamma8(64) == 11`, so mid-slider is roughly 6x lower than the old linear behaviour. That is correct.
Judge it against the downlight, not against memory.

---

## 5. Colour temperature holds while dimming

- [ ] Set 2700 K, sweep brightness 255 → **10**
- [ ] Repeat at 4000 K and at 6500 K

Expected: dimmer without getting warmer or cooler.

> **Sweep to 10, not 20.** The original plan stopped at 20, which sits above the region where a
> truncation bug used to bite — it would have reported a clean pass over a real defect. Rounding was
> added to `white_mix_gamma()` specifically for levels 1–4.

---

## 6. Both sources at once — the point of item 9

- [ ] Downlight at 2700 K, ring at a saturated colour, **both on**
- [ ] Then run a ring effect over a lit downlight

Expected: both visible and independent. This combination was impossible before today.

- [ ] Judge whether the ring is washed out at full downlight. The balance between them is why gamma
      was done first; if it looks wrong, that is a tuning question, not a bug.

---

## 7. The effects

- [ ] All six run and dim smoothly: `warm_gradient`, `color_gradient`, `breathing`, `color_cycle`,
      `chase`, `nightlight`
- [ ] The effect dropdown is on the **ring** entity and lists six plus `none`
- [ ] Setting a ring colour drops `effect` to `none`
- [ ] **Change the downlight's colour temperature while a ring effect runs** — the ring's effect must
      **stay** what it was. If HA flips it to `none`, the converter is still clearing on the wrong
      entity.
- [ ] `warm_gradient` **at full brightness** — its rendering changed, not just its dimming. Compare
      against memory; it should look the same or better.
- [ ] `fx_breathing`'s trough: watch that it fades to very dim rather than snapping to black

---

## 8. Persistence and rejoin

- [ ] Select an effect, power cycle, and watch what HA shows

Expected: the ring returns to its stored effect, and **HA displays it** rather than sticking at
whatever it last showed. That read-back path was broken and fixed on the last branch — this is its
regression test.

- [ ] Confirm the stored scenes reseeded rather than misread: the NVS schema went 1 → 2, so the old
      eight-effect indices should have been discarded

> **NVS scene storage has never been exercised on hardware at all** — the earlier bench run used
> `BENCH_DEMO_MODE`, which bypasses `scene_store` entirely. This is its first real test.

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

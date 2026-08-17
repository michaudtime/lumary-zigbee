# Identify blink and a single firmware version source — design

**Date:** 2026-08-17
**Status:** approved, ready for implementation

Items 4 and 5 of `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`, taken together
because they share a verification cycle: every converter change costs a Zigbee2MQTT restart, which
takes the whole Zigbee network down for a minute or two.

## 1. Problem

### 1.1 Identify does nothing

`on_identify()` in `src/zigbee_light.cpp:132` is the complete implementation:

```c
static void on_identify(uint16_t time) {
    log_i("Zigbee identify for %us", time);
}
```

The hook is registered correctly at `src/zigbee_light.cpp:157` and the duration arrives intact. It
writes a line to a serial port that nobody is watching on a fixture in a ceiling. Home Assistant has
no Identify button at all, because the converter never asks for one.

This matters exactly once per fixture and is miserable without: several identical cans in one
ceiling, and no way to ask which is which.

### 1.2 The firmware version is invisible, and lives in three places

Basic cluster `SWBuildID` (0x4000) and `DateCode` (0x0006) are never set, so HA's device page reads
"Firmware: unknown" and the update card shows a bare integer — `16777216` rather than `1.0.0`.

The deeper problem is drift. The version number is currently typed by hand in three places, and they
have already disagreed:

| Where | Value |
|---|---|
| `ZB_FW_VERSION` (`src/config.h:74`) | `0x01000000` |
| `ZB_FW_VERSION_DL` (`src/config.h:75`) | `0x01000001` |
| `--file-version` in the README example (`README.md:149`) | `0x01000001` |

`README.md:135` states that `--file-version` must equal `ZB_FW_VERSION`, and then the example three
lines later passes `ZB_FW_VERSION_DL`. Following the prose and following the example produce
different OTA images. Adding a human-readable string would make it a fourth hand-synced value.

### 1.3 What ZB_FW_VERSION_DL is, and why it stays

Investigated rather than assumed, because the plan file recorded these two constants only as
"they disagree".

`addOTAClient(file_version, downloaded_file_ver, …)` maps them to two different OTA cluster
attributes:

| Argument | Attribute | Meaning |
|---|---|---|
| `ZB_FW_VERSION` | `ota_upgrade_file_version` | the **running** version; what the coordinator compares to decide whether to offer an update |
| `ZB_FW_VERSION_DL` | `ota_upgrade_downloaded_file_ver` | the version of an image already **downloaded** |

The running/running+1 pairing is not a local mistake. It is copied from Espressif's own Arduino OTA
example, which uses `0x01010100` / `0x01010101`. The ZCL default for `DownloadedFileVersion` is
`0xFFFFFFFF` ("nothing downloaded"), so the vendor pattern is arguably wrong on paper — but Z2M
decides whether to offer an update from the *running* `FileVersion`, so the value is not
load-bearing, and vendor OTA state machines are exactly where paper correctness and field behaviour
part company.

**Decision: preserve the value exactly, but derive it.** Changing it can only be truly tested by
running a real upgrade against a fixture in a ceiling, and the upside is cosmetic.

## 2. Decisions

| Question | Decision |
|---|---|
| Identify appearance | Ring blinks **blue**, white string **off** |
| Identify architecture | Render-loop **overlay**, not a `LightState` mode |
| Version source | One version block; `ZB_FW_VERSION`, `ZB_FW_VERSION_DL` and the string all derived |
| `ZB_FW_VERSION_DL` | Stays `ZB_FW_VERSION + 1`, computed rather than typed |
| `DateCode` | Explicit define, not build-date derived, to keep builds reproducible |

Blue is unambiguous: no effect or colour setting produces a blinking blue ring, so there is no
question about which can is being looked at. It is currently unclaimed — `README.md:128` advertises a
BLE OTA mode that also flashes the ring blue, but that feature does not exist. `PIN_BLE_OTA_BUTTON`
is defined at `src/config.h:16` and referenced nowhere in `src/`; the only implementation lives in
`docs/plan.md`, a planning document. Tracked separately. **If BLE OTA is ever built, it needs a
visually distinct indication.**

## 3. Version block

`src/config.h` replaces the two hand-typed constants with:

```c
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0
#define FW_DATE_CODE      "20260817"

#define ZB_FW_VERSION     ((FW_VERSION_MAJOR << 24) | (FW_VERSION_MINOR << 16) | (FW_VERSION_PATCH << 8))
#define ZB_FW_VERSION_DL  (ZB_FW_VERSION + 1)

#define FW_STR_(x)        #x
#define FW_STR(x)         FW_STR_(x)
#define FW_VERSION_STRING FW_STR(FW_VERSION_MAJOR) "." FW_STR(FW_VERSION_MINOR) "." FW_STR(FW_VERSION_PATCH)
```

The two-step `FW_STR_` / `FW_STR` indirection is required: stringifying directly would yield the
macro *name* rather than its value.

This evaluates to `0x01000000` and `0x01000001`: bit-identical to today, so **nothing changes on
air**. A release becomes one edit to one block.

The README's `--file-version` remains the only number a human types, and its instruction is
corrected to match its example — both must equal `ZB_FW_VERSION` of the image being built.

## 4. Basic cluster attributes

The Arduino library exposes no setter for `SWBuildID` or `DateCode`, but `LumaryLight` already
subclasses `ZigbeeColorDimmableLight`, and `_cluster_list` is `protected` in `ZigbeeEP.h:229`. So the
subclass can use the same `esp_zb_basic_cluster_add_attr` call that `setManufacturerAndModel` uses
internally:

| Attribute | ID | Value |
|---|---|---|
| `SWBuildID` | 0x4000 | `FW_VERSION_STRING` |
| `DateCode` | 0x0006 | `FW_DATE_CODE` |

Both are ZCL **character strings**, meaning length-prefixed rather than null-terminated — the
Pascal-style encoding the library performs by hand at `ZigbeeEP.cpp:264`. This is the one detail here
that fails silently: get it wrong and HA displays garbage rather than erroring.

Registered before `Zigbee.begin()`, alongside the existing colour-capability setup, because the
cluster list is consumed when the stack starts.

## 5. Identify overlay

### 5.1 Why an overlay and not a mode

Identify is a temporary visual overlay, not a state the light is in. Modelling it as a
`MODE_IDENTIFY` would mean every reader of `mode` has to special-case it — concretely,
`light_state_effect_value()` at `src/light_state.h:137` derives the effect attribute from `mode`, so
HA would be told the fixture is running an effect called identify. It would also require saving and
restoring the previous mode.

The overlay avoids all of it: `LightState` is never touched, so when the deadline passes the light
resumes by itself. There is no restore path to get wrong.

### 5.2 The deadline

New `src/identify.h`, deliberately free of ESP-IDF headers so it unit-tests on the host in the same
way `light_state.h` does:

```c
inline bool identify_active(uint32_t now, uint32_t until) {
    return int32_t(now - until) < 0;      // wrap-safe across millis() rollover
}
```

`millis()` wraps about every 49.7 days and these are mains-powered fixtures that stay up for months,
so the naive `now < until` would leave a fixture blinking for weeks if a command straddled the wrap.
The signed-difference form is correct across it, and being a pure function it gets a test rather than
a comment.

`on_identify(time)` sets `s_identify_until = millis() + time * 1000` in a `volatile uint32_t`. It is
written on the Zigbee task and read on the Arduino render task; a single aligned 32-bit word needs no
mutex, but the `volatile` is load-bearing.

Per ZCL, `IdentifyTime == 0` means **stop identifying**. That case sets the deadline to now rather
than scheduling a zero-length blink — it is how a coordinator cancels, and it is easy to miss.

### 5.3 Rendering

`src/main.cpp` checks the deadline before resolving the normal effect, and draws a blue-ring frame
instead when active.

**`fx_identify` must not be added to `kEffects`.** That table is the selectable effect set: it backs
HA's `effect_list` and the NVS scene table, both indexed by position. Adding identify there would put
it in the dropdown and in the scene store — precisely the class of dead-control defect PR #3 removed.

## 6. Converter

Add `m.identify()` to the `extend` array. Verified against zigbee-herdsman-converters 26.90.0: it
exposes an enum named `identify` with a toZigbee converter keyed on `identify`, and contributes
nothing named `effect` — so it cannot be unioned into the light's `effect_list` the way the stock
Identify trigger-effects were.

## 7. Testing

Host tests for the parts with real logic:

| Test | Asserts |
|---|---|
| version macros | components produce `0x01000000`, `0x01000001` and `"1.0.0"` |
| `identify_active` | true before the deadline, false after, correct across the `millis()` wrap |
| `identify_active` | `IdentifyTime == 0` reads as inactive immediately |
| converter suite | `identify` is exposed; nothing named `effect` arrived with it |

The blink's appearance is not meaningfully unit-testable. That is what the bench is for.

## 8. Verification on hardware

One flash plus one Z2M restart covers both items:

1. Device page reads `1.0.0` rather than "Firmware: unknown"; update card shows a version, not `16777216`.
2. An Identify button exists in HA.
3. Pressing it blinks the ring blue for the requested duration, white string dark.
4. The light returns to exactly what it was doing — including when it was **off** before identify.
5. Cancelling identify from HA stops the blink immediately.

## 9. Out of scope

- **Power-on behaviour** (`StartUpOnOff`, `StartUpCurrentLevel`, `StartUpColorTemperature`). Item 2's
  remaining half; the dead controls are already switched off in the converter.
- **BLE OTA.** Tracked separately; see §2.
- **Changing OTA behaviour.** `ZB_FW_VERSION_DL` keeps its current value by deliberate decision.
- **Identify on a second endpoint.** Item 9 splits the fixture into two light entities; identify is
  a whole-fixture concept and should stay on endpoint 1 when that lands.

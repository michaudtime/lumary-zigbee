# Identify Blink and Single Version Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the fixture a working Identify blink in Home Assistant, and make the firmware version come from one place instead of three.

**Architecture:** Identify is a render-loop *overlay*, not a light state — a deadline set on the Zigbee task and read on the Arduino render task, so `LightState` is never touched and the light restores itself when the deadline passes. The version becomes three component defines in a new host-testable `src/version.h`, from which the OTA numbers and the human-readable string are all derived.

**Tech Stack:** C++17, PlatformIO, Arduino-ESP32 Zigbee library (ESP32-H2), Unity for host tests, Node for the Zigbee2MQTT converter tests.

Spec: `docs/superpowers/specs/2026-08-17-identify-and-version-design.md`

## Global Constraints

- `ZB_FW_VERSION` must still evaluate to exactly `0x01000000` and `ZB_FW_VERSION_DL` to exactly `0x01000001`. Nothing may change on air.
- Headers under test on the host (`src/version.h`, `src/identify.h`) must not include any ESP-IDF or Arduino header. `src/config.h` includes `driver/ledc.h` and therefore cannot be included from a test.
- `fx_identify` must **not** be added to `kEffects` in `src/effects.cpp`. That table is positionally indexed by both Home Assistant's `effect_list` and the NVS scene store.
- Do not change `EFFECT_COUNT` or any existing value in `enum EffectType`.
- Run `pio` from PowerShell or cmd, never Git Bash (see README Build & Flash).
- Host tests run with `pio test -e native` (gcc is installed) or `scripts\run-native-tests.bat` (MSVC).

## File Structure

| File | Responsibility |
|---|---|
| `src/version.h` | **new** — version components; derives `ZB_FW_VERSION`, `ZB_FW_VERSION_DL`, `FW_VERSION_STRING`, `FW_DATE_CODE`. No ESP-IDF headers. |
| `src/config.h` | drops the two hand-typed OTA constants; includes `version.h` |
| `src/identify.h` | **new** — pure, wrap-safe `identify_active()`. No ESP-IDF headers. |
| `src/effects.h` | declares `fx_identify` (outside `kEffects`) |
| `src/effects.cpp` | implements the blue-ring blink |
| `src/zigbee_light.h` | exports `zigbee_light_identify_until()` |
| `src/zigbee_light.cpp` | `on_identify` sets the deadline; Basic cluster `SWBuildID` / `DateCode` |
| `src/main.cpp` | render-loop overlay |
| `test/test_version/test_main.cpp` | **new** — version macro arithmetic and string |
| `test/test_identify/test_main.cpp` | **new** — deadline logic including `millis()` wrap |
| `z2m/lumary-brain-revA.js` | adds `m.identify()` |
| `z2m/test/converter.test.mjs` | asserts `identify` exposed, no `effect` collision |
| `README.md` | fixes the `--file-version` self-contradiction |

---

### Task 1: Single version source

**Files:**
- Create: `src/version.h`
- Create: `test/test_version/test_main.cpp`
- Modify: `src/config.h:71-76`
- Modify: `README.md:132-154`

**Interfaces:**
- Consumes: nothing
- Produces: `ZB_FW_VERSION` (uint32 literal expression), `ZB_FW_VERSION_DL` (uint32), `FW_VERSION_STRING` (`const char[]`, `"1.0.0"`), `FW_DATE_CODE` (`const char[]`, `"20260817"`). Task 2 uses `FW_VERSION_STRING` and `FW_DATE_CODE`.

- [ ] **Step 1: Write the failing test**

Create `test/test_version/test_main.cpp`:

```cpp
// Native (host) tests for the derived firmware version constants.
// Run: pio test -e native
#include <unity.h>
#include <string.h>
#include "version.h"

void setUp(void) {}
void tearDown(void) {}

// The OTA numbers must not move. The coordinator only offers images numbered
// above the running one, so a silent change here breaks updates in the field.

void test_zb_fw_version_is_unchanged(void) {
    TEST_ASSERT_EQUAL_HEX32(0x01000000, ZB_FW_VERSION);
}

void test_downloaded_version_is_running_plus_one(void) {
    TEST_ASSERT_EQUAL_HEX32(0x01000001, ZB_FW_VERSION_DL);
    TEST_ASSERT_EQUAL_HEX32(ZB_FW_VERSION + 1, ZB_FW_VERSION_DL);
}

// The string is what Home Assistant's device page shows. It must be the same
// three components as the OTA number, or the two drift again.

void test_version_string_matches_the_components(void) {
    TEST_ASSERT_EQUAL_STRING("1.0.0", FW_VERSION_STRING);
}

void test_date_code_is_eight_digits(void) {
    TEST_ASSERT_EQUAL_UINT32(8, strlen(FW_DATE_CODE));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zb_fw_version_is_unchanged);
    RUN_TEST(test_downloaded_version_is_running_plus_one);
    RUN_TEST(test_version_string_matches_the_components);
    RUN_TEST(test_date_code_is_eight_digits);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_version`
Expected: build FAILS with `version.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `src/version.h`:

```cpp
#pragma once

// The single source of the firmware version. Deliberately free of ESP-IDF
// headers so it can be unit-tested on the host -- config.h pulls in
// driver/ledc.h and therefore cannot be included from a test.
//
// Bump this block as a unit for a release, and pass the same ZB_FW_VERSION to
// ota_image_tool.py as --file-version. It is the only number a human types
// twice, and README.md says so.

#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0
#define FW_DATE_CODE      "20260817"   // ZCL DateCode, YYYYMMDD

// The coordinator only offers images numbered above the running one.
#define ZB_FW_VERSION     ((FW_VERSION_MAJOR << 24) | (FW_VERSION_MINOR << 16) | (FW_VERSION_PATCH << 8))

// DownloadedFileVersion. Espressif's own Arduino OTA example uses running + 1,
// and Z2M decides whether to offer an update from the RUNNING FileVersion, so
// this value is not load-bearing. Kept identical to what the fixture already
// reports -- derived here only so it cannot drift from ZB_FW_VERSION.
#define ZB_FW_VERSION_DL  (ZB_FW_VERSION + 1)

// Two-step indirection is required: stringifying directly yields the macro
// name rather than its value.
#define FW_STR_(x)        #x
#define FW_STR(x)         FW_STR_(x)
#define FW_VERSION_STRING FW_STR(FW_VERSION_MAJOR) "." FW_STR(FW_VERSION_MINOR) "." FW_STR(FW_VERSION_PATCH)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_version`
Expected: PASS, 4 tests.

- [ ] **Step 5: Point config.h at version.h**

In `src/config.h`, replace lines 71-76:

```c
// OTA. Bump ZB_FW_VERSION for every release and pass the same value to
// ota_image_tool.py as --file-version, or the coordinator will not offer the
// update (it only pushes images numbered higher than the running one).
#define ZB_FW_VERSION         0x01000000
#define ZB_FW_VERSION_DL      0x01000001
#define ZB_HW_VERSION         0x0001      // lumary-brain rev A
```

with:

```c
// ZB_FW_VERSION / ZB_FW_VERSION_DL are derived in version.h, included at the
// top of this file -- it is ESP-IDF-free so the host tests can reach it.
#define ZB_HW_VERSION         0x0001      // lumary-brain rev A
```

Then add the include at the **top** of `src/config.h`, next to the existing one:

```c
#pragma once
#include "driver/ledc.h"
#include "version.h"
```

- [ ] **Step 6: Verify the firmware still builds and the numbers are unchanged**

Run: `pio run -e esp32h2`
Expected: SUCCESS. This is the real check that `config.h` consumers still resolve `ZB_FW_VERSION`.

- [ ] **Step 7: Fix the README self-contradiction**

In `README.md`, the OTA section currently says `--file-version` must equal `ZB_FW_VERSION` (`0x01000000`) but the example passes `0x01000001`. Change the example's `--file-version` line to:

```bash
  --file-version 0x01000000 \
```

And change the prose at `README.md:134-135` to name the new location:

```
running version, so the version block in `src/version.h` must be bumped and
`ZB_FW_VERSION` passed as `--file-version` below — if they disagree, the update
silently never appears.
```

- [ ] **Step 8: Commit**

```bash
git add src/version.h src/config.h test/test_version/test_main.cpp README.md
git commit -m "refactor(fw): derive the version constants from one block"
```

---

### Task 2: Report the version to Home Assistant

**Files:**
- Modify: `src/zigbee_light.cpp:23-34` (the `LumaryLight` constructor)

**Interfaces:**
- Consumes: `FW_VERSION_STRING`, `FW_DATE_CODE` from Task 1
- Produces: nothing consumed by later tasks

There is no host test here — this is Zigbee cluster registration, verifiable only on hardware (Task 6). Do not invent one.

- [ ] **Step 1: Add the Basic cluster attributes**

`SWBuildID` (0x4000) and `DateCode` (0x0006) are ZCL **character strings**: length-prefixed, not null-terminated. Getting this wrong shows garbage in HA rather than failing. The base class does the same encoding by hand in `ZigbeeEP::setManufacturerAndModel`.

Add this private helper and call it from the `LumaryLight` constructor, after the existing custom-cluster block:

```cpp
    // ZCL character strings are length-prefixed, not null-terminated: byte 0 is
    // the length. Same encoding the base class does by hand for manufacturer
    // and model. Without these two attributes HA's device page reads
    // "Firmware: unknown" and the update card shows a bare integer.
    void addBasicStringAttr(uint16_t attr_id, const char* value) {
        char zcl[ZB_MAX_NAME_LENGTH + 2];
        const size_t len = strlen(value);
        if (len > ZB_MAX_NAME_LENGTH) {
            log_e("Basic attr 0x%04x too long (%u)", attr_id, unsigned(len));
            return;
        }
        zcl[0] = char(len);
        memcpy(zcl + 1, value, len);
        zcl[len + 1] = '\0';

        esp_zb_attribute_list_t* basic = esp_zb_cluster_list_get_cluster(
            _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        if (basic == nullptr) {
            log_e("No basic cluster for attr 0x%04x", attr_id);
            return;
        }
        const esp_err_t ret = esp_zb_basic_cluster_add_attr(basic, attr_id, (void*)zcl);
        if (ret != ESP_OK) {
            log_e("Failed to add basic attr 0x%04x: %s", attr_id, esp_err_to_name(ret));
        }
    }
```

In the constructor body, after `esp_zb_cluster_list_add_custom_cluster(...)`:

```cpp
        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, FW_VERSION_STRING);
        addBasicStringAttr(ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, FW_DATE_CODE);
```

Add `#include <string.h>` to `src/zigbee_light.cpp` if it is not already present.

- [ ] **Step 2: Verify it builds**

Run: `pio run -e esp32h2`
Expected: SUCCESS.

If either attribute-ID constant does not resolve, grep the SDK header for the real spelling before guessing:

```bash
grep -rn "SW_BUILD_ID\|DATE_CODE" ~/.platformio/packages/framework-arduinoespressif32-libs/esp32h2/include/espressif__esp-zigbee-lib/include/zcl/esp_zigbee_zcl_basic.h
```

- [ ] **Step 3: Commit**

```bash
git add src/zigbee_light.cpp
git commit -m "feat(fw): report SWBuildID and DateCode to the coordinator"
```

---

### Task 3: The identify deadline

**Files:**
- Create: `src/identify.h`
- Create: `test/test_identify/test_main.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `bool identify_active(uint32_t now, uint32_t until)` and `#define IDENTIFY_BLINK_PERIOD_MS 500`. Tasks 4 uses both.

- [ ] **Step 1: Write the failing test**

Create `test/test_identify/test_main.cpp`:

```cpp
// Native (host) tests for the identify overlay's timing.
// Run: pio test -e native
#include <unity.h>
#include "identify.h"

void setUp(void) {}
void tearDown(void) {}

void test_active_before_the_deadline(void) {
    TEST_ASSERT_TRUE(identify_active(1000, 4000));
}

void test_inactive_after_the_deadline(void) {
    TEST_ASSERT_FALSE(identify_active(5000, 4000));
}

void test_inactive_exactly_on_the_deadline(void) {
    TEST_ASSERT_FALSE(identify_active(4000, 4000));
}

// ZCL: writing IdentifyTime = 0 means "stop identifying". The caller encodes
// that as a deadline of now, so it must read as inactive immediately rather
// than scheduling a zero-length blink.
void test_cancel_reads_as_inactive(void) {
    TEST_ASSERT_FALSE(identify_active(12345, 12345));
}

// millis() wraps about every 49.7 days and these fixtures stay powered for
// months. A naive now < until would leave the ring blinking for weeks when an
// identify command straddles the wrap.
void test_active_across_the_millis_wrap(void) {
    const uint32_t now   = 0xFFFFFF00;   // 256 ms before wrap
    const uint32_t until = now + 3000;   // wraps around to 0x00000B9C
    TEST_ASSERT_TRUE(identify_active(now, until));
    TEST_ASSERT_TRUE(identify_active(0x00000500, until));   // after the wrap, still inside
    TEST_ASSERT_FALSE(identify_active(0x00001000, until));  // after the wrap, past it
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_active_before_the_deadline);
    RUN_TEST(test_inactive_after_the_deadline);
    RUN_TEST(test_inactive_exactly_on_the_deadline);
    RUN_TEST(test_cancel_reads_as_inactive);
    RUN_TEST(test_active_across_the_millis_wrap);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_identify`
Expected: build FAILS with `identify.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `src/identify.h`:

```cpp
#pragma once
#include <stdint.h>

// Timing for the Identify overlay. Deliberately free of ESP-IDF headers so it
// can be unit-tested on the host, like light_state.h.
//
// Identify is an overlay, not a light state: nothing in LightState changes
// while it runs, so when the deadline passes the fixture simply resumes.

// Full blink cycle: 250 ms lit, 250 ms dark.
#define IDENTIFY_BLINK_PERIOD_MS 500

// True while `now` is before `until`.
//
// The subtraction-and-sign form is deliberate. millis() wraps roughly every
// 49.7 days and these are mains-powered ceiling fixtures that stay up for
// months; a plain `now < until` would leave the ring blinking for weeks if an
// identify command happened to straddle the wrap. Unsigned subtraction wraps
// consistently, so the signed difference stays correct across it.
//
// A deadline equal to `now` reads as inactive, which is how ZCL's
// IdentifyTime = 0 ("stop identifying") is encoded.
inline bool identify_active(uint32_t now, uint32_t until) {
    return int32_t(now - until) < 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_identify`
Expected: PASS, 5 tests.

- [ ] **Step 5: Commit**

```bash
git add src/identify.h test/test_identify/test_main.cpp
git commit -m "feat(fw): wrap-safe identify deadline helper"
```

---

### Task 4: The blink, and wiring it into the render loop

**Files:**
- Modify: `src/effects.h`
- Modify: `src/effects.cpp`
- Modify: `src/zigbee_light.h`
- Modify: `src/zigbee_light.cpp:132-134` (`on_identify`)
- Modify: `src/main.cpp:76-94` (the render section of `loop()`)

**Interfaces:**
- Consumes: `identify_active()`, `IDENTIFY_BLINK_PERIOD_MS` (Task 3)
- Produces: `void fx_identify(uint32_t elapsed_ms, CRGB* leds)` and `uint32_t zigbee_light_identify_until()`

- [ ] **Step 1: Declare the blink**

In `src/effects.h`, after the `extern const Effect kEffects[EFFECT_COUNT];` line:

```cpp
// The Identify blink. Deliberately NOT a member of kEffects: that table is
// positionally indexed by both Home Assistant's effect_list and the NVS scene
// store, so adding identify there would put it in the effect dropdown and in
// the scene table. It is an overlay the render loop draws instead of the
// resolved effect, not something a user can select.
void fx_identify(uint32_t elapsed_ms, CRGB* leds);
```

- [ ] **Step 2: Implement the blink**

In `src/effects.cpp`, add after `fx_static_white` (it needs `ring_off` and `white_off`, which are declared above it). Note `kEffects` is **not** touched:

```cpp
// Blue was chosen because nothing else the fixture does looks like it: no
// effect or colour setting produces a blinking blue ring, so there is no
// ambiguity about which can in the ceiling is being identified.
void fx_identify(uint32_t elapsed_ms, CRGB* leds) {
    const bool lit = (elapsed_ms % IDENTIFY_BLINK_PERIOD_MS) < (IDENTIFY_BLINK_PERIOD_MS / 2);
    const CRGB c   = lit ? CRGB{0, 0, 255} : CRGB{};
    for (int i = 0; i < RING_NUM_LEDS; i++) leds[i] = c;
    white_off();
}
```

Add `#include "identify.h"` at the top of `src/effects.cpp`.

- [ ] **Step 3: Set the deadline when identify arrives**

In `src/zigbee_light.cpp`, add near the other file-scope state (beside `static LightState s_state;`):

```cpp
// Written on the Zigbee task, read on the Arduino render task. A single
// aligned 32-bit word needs no mutex, but the volatile is load-bearing.
static volatile uint32_t s_identify_until = 0;
```

Replace `on_identify` at `src/zigbee_light.cpp:132-134` with:

```cpp
// Identify is an overlay: it does not touch LightState, so when the deadline
// passes the fixture resumes whatever it was doing with no restore step.
//
// ZCL treats IdentifyTime = 0 as "stop identifying", which is how a
// coordinator cancels. Encoding it as a deadline of now makes
// identify_active() false immediately, rather than scheduling a zero-length
// blink that would leave the ring lit for one frame.
static void on_identify(uint16_t time) {
    const uint32_t now = millis();
    s_identify_until = (time == 0) ? now : now + uint32_t(time) * 1000;
    log_i("Zigbee identify for %us", time);
}
```

Add `#include "identify.h"` to `src/zigbee_light.cpp`.

Add the accessor at the end of `src/zigbee_light.cpp`:

```cpp
uint32_t zigbee_light_identify_until() {
    return s_identify_until;
}
```

And declare it in `src/zigbee_light.h`, after `zigbee_light_report()`:

```cpp
// Deadline (millis) until which the fixture should render the Identify blink.
// The render loop compares it with identify_active(); a value equal to or
// behind now means "not identifying".
uint32_t zigbee_light_identify_until();
```

- [ ] **Step 4: Draw the overlay**

In `src/main.cpp`, add `#include "identify.h"` at the top.

Replace the two render lines at the end of `loop()`:

```cpp
    kEffects[p.type].fn(now - effect_start, p, leds, on);
    led_driver_show(leds, RING_NUM_LEDS);
```

with:

```cpp
    // Identify overrides whatever is running, without disturbing it: LightState
    // is untouched, so when the deadline passes the next frame resumes normally.
    // Bench demo mode compiles out zigbee_light_* entirely, so the accessor is
    // not available to link against there.
#if BENCH_DEMO_MODE
    const bool identifying = false;
#else
    const bool identifying = identify_active(now, zigbee_light_identify_until());
#endif

    if (identifying) {
        fx_identify(now, leds);
    } else {
        kEffects[p.type].fn(now - effect_start, p, leds, on);
    }
    led_driver_show(leds, RING_NUM_LEDS);
```

Only the `identifying` flag is conditional; the render call appears once. The
`BENCH_DEMO_MODE` guard is required because that mode compiles out
`zigbee_light_*`, so `zigbee_light_identify_until()` would not link.

- [ ] **Step 5: Verify it builds and the host tests still pass**

Run: `pio run -e esp32h2`
Expected: SUCCESS.

Run: `pio test -e native`
Expected: all suites PASS (test_light_state 35, test_pixel_encode 11, test_version 4, test_identify 5).

- [ ] **Step 6: Commit**

```bash
git add src/effects.h src/effects.cpp src/zigbee_light.h src/zigbee_light.cpp src/main.cpp
git commit -m "feat(fw): render a blue ring blink on Zigbee identify"
```

---

### Task 5: Expose Identify in Home Assistant

**Files:**
- Modify: `z2m/lumary-brain-revA.js`
- Modify: `z2m/test/converter.test.mjs`
- Modify: `z2m/test/stubs/modernExtend.mjs`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: nothing consumed by later tasks

- [ ] **Step 1: Add identify to the stub**

The converter test swaps `zigbee-herdsman-converters` for local stubs. `z2m/test/stubs/modernExtend.mjs` records calls; add an `identify` export alongside `light` and `deviceAddCustomCluster`:

```js
export const identify = (args) => {
    calls.push({fn: 'identify', args});
    return {isModernExtend: true, kind: 'identify', args};
};
```

- [ ] **Step 2: Write the failing test**

In `z2m/test/converter.test.mjs`, add after the existing `light()` assertions (the block ending with the `colorTemp.startup` check):

```js
// ── identify ──────────────────────────────────────────────────────────────
// The commissioning button. Verified against zigbee-herdsman-converters
// 26.90.0: m.identify() exposes an enum named `identify`, not `effect`, so it
// cannot be unioned into the light's effect_list the way the stock Identify
// trigger-effects were.

await test('the converter asks for identify', () => {
    assert.ok(calls.find((c) => c.fn === 'identify'), 'm.identify() was never called');
});

await test('identify does not contribute a second `effect` expose', () => {
    const effects = def.exposes.filter((x) => x.name === 'effect');
    assert.equal(effects.length, 1);
});
```

- [ ] **Step 3: Run test to verify it fails**

Run: `node z2m/test/converter.test.mjs`
Expected: FAIL on "the converter asks for identify" with `m.identify() was never called`.

- [ ] **Step 4: Add identify to the converter**

In `z2m/lumary-brain-revA.js`, inside the `extend: [...]` array, after the `m.light({...})` block and before `m.deviceAddCustomCluster(...)`:

```js
        // The commissioning button: blinks the ring blue so you can tell which
        // can in the ceiling you are looking at. Exposes an enum named
        // `identify`, so unlike the stock Identify trigger-effects it cannot be
        // folded into the light's effect_list.
        m.identify(),
```

- [ ] **Step 5: Run test to verify it passes**

Run: `node z2m/test/converter.test.mjs`
Expected: PASS, 28 checks, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add z2m/lumary-brain-revA.js z2m/test/converter.test.mjs z2m/test/stubs/modernExtend.mjs
git commit -m "feat(z2m): expose the identify button"
```

---

### Task 6: Verify on hardware

**Files:** none — this is the bench pass.

**Interfaces:**
- Consumes: everything above

Nothing in Tasks 1-5 has touched hardware. Both items land in a single flash and a single Z2M restart, which is why they were planned together — the restart takes the whole Zigbee network down for a minute or two.

- [ ] **Step 1: Flash**

From PowerShell or cmd, with a UTF-8 pipe (PlatformIO's output layer crashes on cp1252 when piped, and the upload then hangs):

```bash
PYTHONIOENCODING=utf-8 pio run -e esp32h2 -t upload --upload-port COM7
```

Expected: `Hash of data verified.` then `[SUCCESS]`.

- [ ] **Step 2: Deploy the converter and restart Z2M**

Copy `z2m/lumary-brain-revA.js` to `\\192.168.1.101\config\zigbee2mqtt\external_converters\`, keeping a backup **outside** that directory (a second `.js` inside it would be loaded as a duplicate definition). Then restart the `45df7312_zigbee2mqtt` add-on.

- [ ] **Step 3: Check the version reached HA**

The device page for `light.0x744dbdfffe6b575f` should read `1.0.0` rather than "Firmware: unknown", and the update card should show a version rather than `16777216`.

If it shows garbage, the ZCL length prefix in Task 2 is wrong.

- [ ] **Step 4: Check identify**

An Identify button should exist on the device page. Pressing it must:
- blink the ring **blue**, white string dark
- run for the requested duration, then stop by itself
- leave the light in exactly the state it was in before — **including when it was off**
- stop immediately if cancelled from HA

- [ ] **Step 5: Confirm nothing regressed**

`effect_list` must still be the nine real effects, and no `power_on_behavior` or `color_temp_startup` entity may reappear.

- [ ] **Step 6: Record the result**

Update `docs/superpowers/plans/2026-08-17-home-assistant-polish.md`: mark items 4 and 5 done and bench-verified, with the observed values, matching how items 1 and 2 were recorded.

```bash
git add docs/superpowers/plans/2026-08-17-home-assistant-polish.md
git commit -m "docs: record bench verification of items 4 and 5"
```

---

## Notes for the implementer

**Do not add `fx_identify` to `kEffects`.** It is the single easiest mistake to make here and it silently recreates the dead-control defect that PR #3 removed. The table is indexed by position by both HA's `effect_list` and the NVS scene store.

**Do not "fix" `ZB_FW_VERSION_DL` to `0xFFFFFFFF`.** It looks wrong against the ZCL spec and it is not — see §1.3 of the spec. It is Espressif's own example pattern, Z2M does not use it for update decisions, and the only honest test of a change is a real OTA against a fixture in a ceiling.

**`config.h` cannot be included from a host test.** It pulls in `driver/ledc.h`. That is why the version block lives in `version.h`.

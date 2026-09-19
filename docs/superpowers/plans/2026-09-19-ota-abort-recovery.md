# Automatic OTA Abort Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After a failed or stalled Zigbee OTA, the fixture reboots itself with its light state preserved, so Zigbee2MQTT's scheduled update can retry it without anyone touching the fixture.

**Architecture:** Two new ESP-IDF-free headers carry all the logic and are host-tested: `ota_watch.h` (decides "recover now" from 1 Hz samples of the OTA client's `ImageUpgradeStatus` / `FileOffset`) and `ota_snapshot.h` (a checksummed record of `FixtureState`). `zigbee_light.cpp` is the glue: it samples the attributes, keeps the record in `__NOINIT_ATTR` RAM, calls `esp_restart()`, restores on the next boot, and re-syncs the Arduino light library's private on/off/level copies. Retry needs no firmware code: Z2M's scheduled updates persist across failures.

**Tech Stack:** PlatformIO + Arduino-ESP32 3.3.11 (esp-zigbee-lib 1.6.8), ESP32-H2, Unity host tests compiled with MSVC via `scripts\run-native-tests.bat`, Zigbee2MQTT 2.14.1 on Home Assistant.

**Spec:** `docs/superpowers/specs/2026-09-18-ota-abort-recovery-design.md`

## Global Constraints

- New logic headers must not include ESP-IDF or Arduino headers (host-testable, like `src/light_state.h`).
- Abort grace: **5000 ms** of status 0 after status 1. Stall backstop: **180000 ms** of status 1 with an unchanged `FileOffset`. Sample period: **1000 ms**.
- `ImageUpgradeStatus` values used: **0 = Normal, 1 = Download in progress, 2 = Download complete**. Any other value is ignored (neither triggers nor resets).
- Never recover after status 2 has been seen this boot.
- Snapshot lives in `__NOINIT_ATTR` (never `RTC_NOINIT_ATTR`, which can compile to nothing on this target). Restore only when `esp_reset_reason() == ESP_RST_SW` **and** the record validates; invalidate it on **every** boot.
- No retry cap in firmware. Retry is Z2M `.../ota_update/schedule`; stop with `.../ota_update/unschedule`.
- Release version: **2.1.0** (`FW_VERSION_MAJOR 2`, `FW_VERSION_MINOR 1`, `FW_VERSION_PATCH 0`).
- All time comparisons wrap-safe: unsigned `now - then`.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Host tests: `scripts\run-native-tests.bat` from the repo root (PowerShell or cmd — not Git Bash). Firmware build: `pio run -e esp32h2` (PowerShell or cmd).

---

## File Structure

| File | Responsibility |
|---|---|
| `src/ota_watch.h` (create) | Pure state machine: samples in, `OtaRecoverReason` out |
| `src/ota_snapshot.h` (create) | Pure record: save / validate / invalidate a checksummed `FixtureState` |
| `test/test_ota_watch/test_main.cpp` (create) | Host tests for the watcher |
| `test/test_ota_snapshot/test_main.cpp` (create) | Host tests for the record |
| `src/zigbee_light.cpp` (modify) | Attribute sampling, `ota_recover()`, restore on boot, library re-sync |
| `src/version.h`, `test/test_version/test_main.cpp` (modify) | 2.1.0 |
| `README.md` (modify) | Scheduled-update workflow, recovery behaviour, manual power-cycle for older firmware |
| `docs/research/ota-throughput.md`, `docs/superpowers/plans/2026-08-18-bench-verification.md` (modify) | Bench results |

Bench hardware: **Test Unit 2** (`0x744dbdfffe6d65c8`) on `COM8`, serial captured with `scripts/serial-log.py`. Z2M data dir is reachable over Samba at `\\192.168.1.101\config\zigbee2mqtt`.

---

### Task 1: Premise check — does the stack maintain the OTA client attributes? (throwaway)

Everything in this plan rests on the stack advancing `FileOffset` and setting `ImageUpgradeStatus` to 1 during a download (spec 5.1). This task proves it on hardware with a temporary log and commits **nothing**.

**Files:**
- Modify (temporarily): `src/zigbee_light.cpp`, `src/version.h`

- [ ] **Step 1: Start from a clean tree**

The working tree may still hold an uncommitted 2026-09-18 neighbour-table diagnostic in `src/zigbee_light.cpp`. Discard it:

```powershell
git checkout -- src/zigbee_light.cpp
git status --short src
```
Expected: no `src/` files listed.

- [ ] **Step 2: Add a temporary attribute log**

In `src/zigbee_light.cpp`, add at the start of `zigbee_light_loop()`:

```cpp
    // PREMISE CHECK -- temporary, not committed.
    static uint32_t s_ota_dbg_at = 0;
    if (Zigbee.connected() && millis() - s_ota_dbg_at >= 1000) {
        s_ota_dbg_at = millis();
        if (esp_zb_lock_acquire(pdMS_TO_TICKS(50))) {
            esp_zb_zcl_attr_t* st = esp_zb_zcl_get_attribute(DOWNLIGHT_ENDPOINT,
                ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
                ESP_ZB_ZCL_ATTR_OTA_UPGRADE_IMAGE_STATUS_ID);
            esp_zb_zcl_attr_t* off = esp_zb_zcl_get_attribute(DOWNLIGHT_ENDPOINT,
                ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
                ESP_ZB_ZCL_ATTR_OTA_UPGRADE_FILE_OFFSET_ID);
            const bool ok = st && off && st->data_p && off->data_p;
            const uint8_t  status = ok ? *(uint8_t*)st->data_p : 0xEE;
            const uint32_t offset = ok ? *(uint32_t*)off->data_p : 0;
            esp_zb_lock_release();
            log_i("OTA attrs: ok %d status %u offset %lu", ok, status, (unsigned long)offset);
        }
    }
```

- [ ] **Step 3: Label the build older than the staged image, flash, restore the label**

The staged OTA image in Z2M is 2.0.2, and Test Unit 2 currently runs 2.0.2. Temporarily set `src/version.h` to `FW_VERSION_PATCH 1` and `FW_DATE_CODE "20260819"`, then:

```powershell
pio run -e esp32h2 -t upload --upload-port COM8
git checkout -- src/version.h
```
Expected: `[SUCCESS]`.

- [ ] **Step 4: Capture a download**

```powershell
New-Item -ItemType Directory -Force build\ota-logs | Out-Null
$py = "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe"
Start-Process -FilePath $py -ArgumentList @('-u','scripts\serial-log.py','COM8','build\ota-logs\premise-check.log') -WindowStyle Hidden -PassThru
```

Start the update: publish to MQTT topic `zigbee2mqtt/bridge/request/device/ota_update/update` the payload `{"id": "Test Unit 2"}` (Home Assistant `mqtt.publish`). Let it run 2–3 minutes.

- [ ] **Step 5: Evaluate — the gate for the rest of the plan**

```powershell
Select-String -Path build\ota-logs\premise-check.log -Pattern 'OTA attrs' | Select-Object -First 5
Select-String -Path build\ota-logs\premise-check.log -Pattern 'OTA attrs' | Select-Object -Last 5
```

PASS requires all of: `ok 1`; `status 0` before the download starts; `status 1` during it; `offset` increasing across samples while status is 1.

**If any of these fails, STOP.** Record what the attributes actually did in the spec's §6 and return to design — the detection approach needs revisiting.

- [ ] **Step 6: Clean up (no commit)**

Stop the logger (`Stop-Process -Id <PID>`), then:
```powershell
git checkout -- src/zigbee_light.cpp
git status --short src
```
Expected: no `src/` files listed. Test Unit 2 keeps running the premise build; the OTA session can be left to fail or finish — later tasks reflash it.

---

### Task 2: `ota_watch.h` — detection state machine (TDD)

**Files:**
- Create: `src/ota_watch.h`
- Test: `test/test_ota_watch/test_main.cpp`

**Interfaces:**
- Produces:
  - `#define OTA_IMAGE_STATUS_NORMAL 0`, `OTA_IMAGE_STATUS_DOWNLOADING 1`, `OTA_IMAGE_STATUS_COMPLETE 2`
  - `#define OTA_WATCH_ABORT_GRACE_MS 5000u`, `OTA_WATCH_STALL_MS 180000u`, `OTA_WATCH_SAMPLE_MS 1000u`
  - `enum OtaRecoverReason : uint8_t { OTA_RECOVER_NONE = 0, OTA_RECOVER_ABORT = 1, OTA_RECOVER_STALL = 2 };`
  - `struct OtaWatch` (opaque to callers)
  - `inline void ota_watch_init(OtaWatch* w);`
  - `inline OtaRecoverReason ota_watch_step(OtaWatch* w, uint8_t status, uint32_t offset, uint32_t now_ms);`

- [ ] **Step 1: Write the failing tests**

Create `test/test_ota_watch/test_main.cpp`:

```cpp
// Native (host) tests for the OTA abort/stall watcher.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include "ota_watch.h"

static OtaWatch w;

void setUp(void) { ota_watch_init(&w); }
void tearDown(void) {}

// Feeds one sample per second for `secs` seconds starting at t0. The offset
// advances by `step` bytes per sample. Returns the first non-NONE reason and,
// via *fired_at, when it fired (0 if it never did).
static OtaRecoverReason feed(uint8_t status, uint32_t* offset, uint32_t step,
                             uint32_t t0, uint32_t secs, uint32_t* fired_at) {
    for (uint32_t i = 0; i < secs; i++) {
        const uint32_t now = t0 + i * 1000u;
        const OtaRecoverReason r = ota_watch_step(&w, status, *offset, now);
        *offset += step;
        if (r != OTA_RECOVER_NONE) { if (fired_at) *fired_at = now; return r; }
    }
    if (fired_at) *fired_at = 0;
    return OTA_RECOVER_NONE;
}

void test_idle_never_triggers(void) {
    uint32_t off = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 0, 3600, nullptr));
}

void test_advancing_download_never_triggers(void) {
    uint32_t off = 0;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 3600, nullptr));
}

void test_abort_triggers_after_the_grace(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);   // t = 0 .. 59 s
    // Status falls to NORMAL at t = 60 s; the grace is 5 s.
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 60000, 30, &at));
    TEST_ASSERT_EQUAL_UINT32(65000, at);
}

void test_brief_flicker_does_not_trigger(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 60000, 3, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 63000, 600, nullptr));
}

void test_stall_triggers_at_180_s(void) {
    uint32_t off = 1234, at = 0;
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 0, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(180000, at);
}

void test_stall_timer_restarts_when_the_offset_moves(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 0, 170, nullptr);   // stuck 169 s
    off += 50;                                                      // one block lands
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 170000, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(350000, at);                           // 180 s after the block
}

void test_no_trigger_without_a_download_this_boot(void) {
    // A fixture that boots into NORMAL and stays there must never recover, even
    // if the offset attribute holds a stale value.
    uint32_t off = 5000;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 0, 600, nullptr));
}

void test_complete_is_terminal(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    feed(OTA_IMAGE_STATUS_COMPLETE, &off, 0, 60000, 5, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 65000, 600, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 665000, 600, nullptr));
}

void test_unknown_status_neither_triggers_nor_resets(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(3, &off, 0, 60000, 600, nullptr));
    // The download having been seen is remembered through the unknown status.
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 660000, 30, &at));
}

void test_fires_only_once(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 10, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 10000, 30, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, ota_watch_step(&w, OTA_IMAGE_STATUS_NORMAL, off, 50000));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, ota_watch_step(&w, OTA_IMAGE_STATUS_DOWNLOADING, off, 900000));
}

// millis() wraps every ~49.7 days; these fixtures stay powered for months.
void test_abort_across_the_millis_wrap(void) {
    uint32_t off = 0, at = 0;
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;                      // 30 s before the wrap
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, t0, 28, nullptr);
    const uint32_t fall = t0 + 28000u;                             // 2 s before the wrap
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, fall, 30, &at));
    TEST_ASSERT_EQUAL_UINT32(fall + 5000u, at);                    // lands after the wrap
}

void test_stall_across_the_millis_wrap(void) {
    uint32_t off = 77, at = 0;
    const uint32_t t0 = 0xFFFFFFFFu - 60000u;
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, t0, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(t0 + 180000u, at);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_never_triggers);
    RUN_TEST(test_advancing_download_never_triggers);
    RUN_TEST(test_abort_triggers_after_the_grace);
    RUN_TEST(test_brief_flicker_does_not_trigger);
    RUN_TEST(test_stall_triggers_at_180_s);
    RUN_TEST(test_stall_timer_restarts_when_the_offset_moves);
    RUN_TEST(test_no_trigger_without_a_download_this_boot);
    RUN_TEST(test_complete_is_terminal);
    RUN_TEST(test_unknown_status_neither_triggers_nor_resets);
    RUN_TEST(test_fires_only_once);
    RUN_TEST(test_abort_across_the_millis_wrap);
    RUN_TEST(test_stall_across_the_millis_wrap);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts\run-native-tests.bat`
Expected: `--- BUILD FAILED: test_ota_watch ---` (cannot open `ota_watch.h`).

- [ ] **Step 3: Write the implementation**

Create `src/ota_watch.h`:

```cpp
#pragma once
#include <stdint.h>

// Decides when a failed or stalled Zigbee OTA download needs a recovery reboot.
// Fed one sample per second of the OTA client cluster's ImageUpgradeStatus and
// FileOffset attributes (endpoint 1). Deliberately free of ESP-IDF headers so it
// can be unit-tested on the host; zigbee_light.cpp reads the attributes and acts
// on the answer. See docs/superpowers/specs/2026-09-18-ota-abort-recovery-design.md.
//
// Why a reboot at all: the Arduino Zigbee library resets its OTA parser state
// only on a successful end, never on an abort, so after any failed download
// every retry fails INVALID_IMAGE until the fixture restarts.

// ImageUpgradeStatus values this cares about (ZCL OTA Upgrade cluster).
#define OTA_IMAGE_STATUS_NORMAL       0
#define OTA_IMAGE_STATUS_DOWNLOADING  1
#define OTA_IMAGE_STATUS_COMPLETE     2

// A status that dips to NORMAL for less than this is a flicker, not an abort.
#define OTA_WATCH_ABORT_GRACE_MS  5000u
// Backstop for a download that hangs instead of aborting. The OTA client's own
// give-up is ~134 s; normal retry stalls are 3-10 s, the longest seen ~48 s.
#define OTA_WATCH_STALL_MS        180000u
// How often the caller should sample.
#define OTA_WATCH_SAMPLE_MS       1000u

enum OtaRecoverReason : uint8_t {
    OTA_RECOVER_NONE  = 0,
    OTA_RECOVER_ABORT = 1,   // status fell DOWNLOADING -> NORMAL without completing
    OTA_RECOVER_STALL = 2,   // DOWNLOADING, but FileOffset stopped moving
};

struct OtaWatch {
    bool     seen_download;       // status DOWNLOADING seen since boot
    bool     completed;           // status COMPLETE seen: terminal, never recover
    bool     fired;               // a recovery was already requested
    bool     in_abort;            // NORMAL after DOWNLOADING, waiting out the grace
    uint32_t abort_since_ms;
    uint32_t last_offset;
    uint32_t offset_changed_ms;
};

inline void ota_watch_init(OtaWatch* w) {
    w->seen_download     = false;
    w->completed         = false;
    w->fired             = false;
    w->in_abort          = false;
    w->abort_since_ms    = 0;
    w->last_offset       = 0;
    w->offset_changed_ms = 0;
}

// Returns non-NONE at most once per boot. All time arithmetic is unsigned
// subtraction, so it is correct across the millis() wrap.
inline OtaRecoverReason ota_watch_step(OtaWatch* w, uint8_t status, uint32_t offset,
                                       uint32_t now_ms) {
    if (w->fired || w->completed) return OTA_RECOVER_NONE;

    if (status == OTA_IMAGE_STATUS_COMPLETE) {
        w->completed = true;
        return OTA_RECOVER_NONE;
    }

    if (status == OTA_IMAGE_STATUS_DOWNLOADING) {
        w->in_abort = false;
        if (!w->seen_download || offset != w->last_offset) {
            w->seen_download     = true;
            w->last_offset       = offset;
            w->offset_changed_ms = now_ms;
            return OTA_RECOVER_NONE;
        }
        if (now_ms - w->offset_changed_ms >= OTA_WATCH_STALL_MS) {
            w->fired = true;
            return OTA_RECOVER_STALL;
        }
        return OTA_RECOVER_NONE;
    }

    if (status == OTA_IMAGE_STATUS_NORMAL) {
        if (!w->seen_download) return OTA_RECOVER_NONE;
        if (!w->in_abort) {
            w->in_abort       = true;
            w->abort_since_ms = now_ms;
            return OTA_RECOVER_NONE;
        }
        if (now_ms - w->abort_since_ms >= OTA_WATCH_ABORT_GRACE_MS) {
            w->fired = true;
            return OTA_RECOVER_ABORT;
        }
        return OTA_RECOVER_NONE;
    }

    return OTA_RECOVER_NONE;   // any other status: neither triggers nor resets
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `scripts\run-native-tests.bat`
Expected: `--- test_ota_watch ---` `12 Tests 0 Failures 0 Ignored`, and `ALL SUITES PASSED`.

- [ ] **Step 5: Commit**

```powershell
git add src/ota_watch.h test/test_ota_watch/test_main.cpp
git commit -m "feat: add ota_watch, the OTA abort/stall detector" -m "Pure state machine fed 1 Hz samples of the OTA client's ImageUpgradeStatus and FileOffset. Recovers on DOWNLOADING -> NORMAL held for 5 s, or on DOWNLOADING with an unchanged offset for 180 s; never after COMPLETE, never without a download this boot, at most once, wrap-safe." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: `ota_snapshot.h` — checksummed state record (TDD)

**Files:**
- Create: `src/ota_snapshot.h`
- Test: `test/test_ota_snapshot/test_main.cpp`

**Interfaces:**
- Consumes: `FixtureState` (`src/light_state.h`), `OtaRecoverReason` (`src/ota_watch.h`, Task 2)
- Produces:
  - `#define OTA_SNAPSHOT_MAGIC 0x534F4D4Cu`, `#define OTA_SNAPSHOT_VERSION 1`
  - `struct OtaSnapshot { uint32_t magic; uint8_t version; uint8_t reason; uint16_t recoveries; uint32_t offset; FixtureState state; uint32_t checksum; };`
  - `inline uint32_t ota_snapshot_checksum(const OtaSnapshot* s);`
  - `inline void ota_snapshot_save(OtaSnapshot* s, const FixtureState* state, OtaRecoverReason reason, uint32_t offset, uint16_t prior_recoveries);`
  - `inline bool ota_snapshot_valid(const OtaSnapshot* s);`
  - `inline void ota_snapshot_invalidate(OtaSnapshot* s);`

- [ ] **Step 1: Write the failing tests**

Create `test/test_ota_snapshot/test_main.cpp`:

```cpp
// Native (host) tests for the OTA-recovery state snapshot.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include <string.h>
#include "ota_snapshot.h"

void setUp(void) {}
void tearDown(void) {}

static FixtureState distinctive(void) {
    FixtureState f;
    fixture_state_init(&f);
    f.down.on    = true;
    f.down.level = 60;
    f.down.cct   = 141;
    f.ring.on    = true;
    f.ring.level = 200;
    f.ring.hue   = 170;
    f.ring.sat   = 220;
    f.ring.scene = 4;
    f.ring.mode  = MODE_SCENE;
    return f;
}

static void assert_state_equal(const FixtureState* a, const FixtureState* b) {
    TEST_ASSERT_EQUAL(a->down.on, b->down.on);
    TEST_ASSERT_EQUAL_UINT8(a->down.level, b->down.level);
    TEST_ASSERT_EQUAL_UINT8(a->down.cct, b->down.cct);
    TEST_ASSERT_EQUAL(a->ring.on, b->ring.on);
    TEST_ASSERT_EQUAL_UINT8(a->ring.level, b->ring.level);
    TEST_ASSERT_EQUAL_UINT8(a->ring.hue, b->ring.hue);
    TEST_ASSERT_EQUAL_UINT8(a->ring.sat, b->ring.sat);
    TEST_ASSERT_EQUAL_UINT8(a->ring.scene, b->ring.scene);
    TEST_ASSERT_EQUAL(a->ring.mode, b->ring.mode);
}

void test_round_trip_preserves_everything(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 571694, 0);
    TEST_ASSERT_TRUE(ota_snapshot_valid(&s));
    assert_state_equal(&f, &s.state);
    TEST_ASSERT_EQUAL_UINT8(OTA_RECOVER_ABORT, s.reason);
    TEST_ASSERT_EQUAL_UINT32(571694, s.offset);
    TEST_ASSERT_EQUAL_UINT16(1, s.recoveries);
}

void test_recovery_count_carries_across(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_STALL, 100, 3);
    TEST_ASSERT_EQUAL_UINT16(4, s.recoveries);
}

void test_any_corrupted_byte_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    // Every byte, padding and checksum included. The layout has no trailing
    // padding after `checksum` (4+1+1+2+4+9 = 21, padded to 24, +4 = 28), so no
    // byte is outside the checksum's coverage.
    for (size_t i = 0; i < sizeof(s); i++) {
        OtaSnapshot copy = s;
        ((unsigned char*)&copy)[i] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(ota_snapshot_valid(&copy), "a flipped bit was accepted");
    }
    TEST_ASSERT_TRUE(ota_snapshot_valid(&s));   // the original is untouched
}

void test_wrong_magic_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    s.magic = 0x12345678u;
    s.checksum = ota_snapshot_checksum(&s);   // even with a matching checksum
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

void test_wrong_version_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    s.version = OTA_SNAPSHOT_VERSION + 1;
    s.checksum = ota_snapshot_checksum(&s);
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

void test_invalidated_record_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    ota_snapshot_invalidate(&s);
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

// __NOINIT RAM after a power loss is arbitrary. Two common patterns:
void test_power_up_garbage_is_rejected(void) {
    OtaSnapshot s;
    memset(&s, 0x00, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
    memset(&s, 0xA5, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
    memset(&s, 0xFF, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_everything);
    RUN_TEST(test_recovery_count_carries_across);
    RUN_TEST(test_any_corrupted_byte_is_rejected);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_wrong_version_is_rejected);
    RUN_TEST(test_invalidated_record_is_rejected);
    RUN_TEST(test_power_up_garbage_is_rejected);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `scripts\run-native-tests.bat`
Expected: `--- BUILD FAILED: test_ota_snapshot ---` (cannot open `ota_snapshot.h`).

- [ ] **Step 3: Write the implementation**

Create `src/ota_snapshot.h`:

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "light_state.h"
#include "ota_watch.h"

// The fixture's light state, carried across the reboot that recovers a failed
// OTA. zigbee_light.cpp keeps one of these in __NOINIT_ATTR RAM: it survives
// esp_restart() but holds arbitrary bytes after a power loss, which is what the
// magic + version + checksum reject. Free of ESP-IDF headers so it can be
// host-tested. See docs/superpowers/specs/2026-09-18-ota-abort-recovery-design.md.

#define OTA_SNAPSHOT_MAGIC    0x534F4D4Cu   // "LMOS" little-endian
#define OTA_SNAPSHOT_VERSION  1

struct OtaSnapshot {
    uint32_t     magic;
    uint8_t      version;
    uint8_t      reason;       // OtaRecoverReason
    uint16_t     recoveries;   // consecutive OTA recoveries, this one included
    uint32_t     offset;       // FileOffset when recovery fired
    FixtureState state;
    uint32_t     checksum;     // FNV-1a over every byte before this field
};

// FNV-1a, 32-bit, over the record up to (not including) `checksum`. Padding
// bytes are covered too; ota_snapshot_save() zeroes the whole record first so
// they are deterministic.
inline uint32_t ota_snapshot_checksum(const OtaSnapshot* s) {
    const unsigned char* p = (const unsigned char*)s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(OtaSnapshot, checksum); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

inline void ota_snapshot_save(OtaSnapshot* s, const FixtureState* state,
                              OtaRecoverReason reason, uint32_t offset,
                              uint16_t prior_recoveries) {
    memset(s, 0, sizeof(*s));
    s->magic      = OTA_SNAPSHOT_MAGIC;
    s->version    = OTA_SNAPSHOT_VERSION;
    s->reason     = uint8_t(reason);
    s->recoveries = uint16_t(prior_recoveries + 1);
    s->offset     = offset;
    s->state      = *state;
    s->checksum   = ota_snapshot_checksum(s);
}

inline bool ota_snapshot_valid(const OtaSnapshot* s) {
    return s->magic == OTA_SNAPSHOT_MAGIC
        && s->version == OTA_SNAPSHOT_VERSION
        && s->checksum == ota_snapshot_checksum(s);
}

// One-shot: called on every boot, so a record can restore at most once.
inline void ota_snapshot_invalidate(OtaSnapshot* s) {
    s->magic = 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `scripts\run-native-tests.bat`
Expected: `--- test_ota_snapshot ---` `7 Tests 0 Failures 0 Ignored`, and `ALL SUITES PASSED`.

- [ ] **Step 5: Commit**

```powershell
git add src/ota_snapshot.h test/test_ota_snapshot/test_main.cpp
git commit -m "feat: add ota_snapshot, the state record carried across an OTA recovery" -m "Checksummed (FNV-1a) FixtureState plus reason, offset and a consecutive-recovery count, destined for __NOINIT RAM. Validation rejects any flipped bit, a wrong magic or version, power-up garbage, and an invalidated record." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Detection and recovery in the firmware

**Files:**
- Modify: `src/zigbee_light.cpp` (includes; new statics after `s_identify_until`; new helpers before `zigbee_light_init()`; start of `zigbee_light_loop()`)

**Interfaces:**
- Consumes: `ota_watch_init`, `ota_watch_step`, `OTA_WATCH_SAMPLE_MS`, `OtaRecoverReason` (Task 2); `OtaSnapshot`, `ota_snapshot_save` (Task 3)
- Produces (file-static, used by Task 5): `static __NOINIT_ATTR OtaSnapshot s_ota_snapshot;`, `static uint16_t s_prior_recoveries;`, `static OtaWatch s_ota_watch;`

- [ ] **Step 1: Includes**

After `#include "Zigbee.h"` add:

```cpp
#include "esp_attr.h"
#include "esp_system.h"
#include "ota_watch.h"
#include "ota_snapshot.h"
```

- [ ] **Step 2: Statics**

After the `s_identify_until` declaration add:

```cpp
// OTA abort recovery -- see ota_watch.h and ota_snapshot.h.
//
// __NOINIT_ATTR: not zeroed at startup, so the record survives esp_restart()
// (and holds garbage after a power loss, which ota_snapshot_valid() rejects).
// Not RTC_NOINIT_ATTR, which esp_attr.h defines to nothing on targets without
// RTC memory.
static __NOINIT_ATTR OtaSnapshot s_ota_snapshot;
static uint16_t s_prior_recoveries = 0;   // from a restored record, else 0
static OtaWatch s_ota_watch;
```

- [ ] **Step 3: Attribute reader and `ota_recover()`**

Immediately before `void zigbee_light_init() {` add:

```cpp
// Reads the OTA client cluster's ImageUpgradeStatus and FileOffset from
// endpoint 1, where addOTAClient() put the client. Called on the Arduino loop
// task, so the stack lock is required. Returns false (skip this sample) if the
// lock is busy or either attribute is missing.
static bool read_ota_client(uint8_t* status, uint32_t* offset) {
    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(50))) return false;
    esp_zb_zcl_attr_t* st = esp_zb_zcl_get_attribute(DOWNLIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_IMAGE_STATUS_ID);
    esp_zb_zcl_attr_t* off = esp_zb_zcl_get_attribute(DOWNLIGHT_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_FILE_OFFSET_ID);
    const bool ok = st && off && st->data_p && off->data_p;
    if (ok) {
        *status = *(uint8_t*)st->data_p;
        *offset = *(uint32_t*)off->data_p;
    }
    esp_zb_lock_release();
    return ok;
}

// A failed download leaves the Arduino library's OTA parser mid-image (it only
// resets on success), so every retry would fail INVALID_IMAGE until a restart.
// Snapshot the light, restart, and let zigbee_light_init() put it back; the
// next image query then retries a scheduled update from scratch.
static void ota_recover(OtaRecoverReason reason, uint32_t offset) {
    ota_snapshot_save(&s_ota_snapshot, &s_state, reason, offset, s_prior_recoveries);
    log_w("OTA %s at offset %lu -- recovery #%u, restarting",
          reason == OTA_RECOVER_STALL ? "stalled" : "aborted",
          (unsigned long)offset, s_ota_snapshot.recoveries);
    delay(100);   // let the log line out
    esp_restart();
}
```

- [ ] **Step 4: Initialise the watcher**

In `zigbee_light_init()`, directly after `s_state.ring.scene = scene_store_get_active();` add:

```cpp
    ota_watch_init(&s_ota_watch);
```

- [ ] **Step 5: Sample once a second**

At the start of `zigbee_light_loop()` (before the `s_joined_once` block) add:

```cpp
    // OTA abort/stall watch -- see ota_watch.h.
    static uint32_t s_ota_sample_at = 0;
    const uint32_t now = millis();
    if (Zigbee.connected() && now - s_ota_sample_at >= OTA_WATCH_SAMPLE_MS) {
        s_ota_sample_at = now;
        uint8_t  status;
        uint32_t offset;
        if (read_ota_client(&status, &offset)) {
            const OtaRecoverReason r = ota_watch_step(&s_ota_watch, status, offset, now);
            if (r != OTA_RECOVER_NONE) ota_recover(r, offset);
        }
    }
```

- [ ] **Step 6: Build**

Run: `pio run -e esp32h2`
Expected: `[SUCCESS]`, no new warnings from `zigbee_light.cpp`.

- [ ] **Step 7: Host tests still pass**

Run: `scripts\run-native-tests.bat`
Expected: `ALL SUITES PASSED`.

- [ ] **Step 8: Commit**

```powershell
git add src/zigbee_light.cpp
git commit -m "feat: reboot to recover from a failed or stalled OTA" -m "Samples the OTA client's ImageUpgradeStatus and FileOffset once a second under the stack lock and feeds ota_watch. On an abort or stall, snapshots FixtureState into __NOINIT RAM and restarts -- the only thing that clears the Arduino library's unreset OTA parser state." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Restore the light state after a recovery restart

**Files:**
- Modify: `src/zigbee_light.cpp` (a suppression flag; a guard at the top of the five light-change callbacks; restore in `zigbee_light_init()`; re-sync in the first-join one-shot)

**Interfaces:**
- Consumes: `s_ota_snapshot`, `s_prior_recoveries`, `s_ota_watch` (Task 4); `ota_snapshot_valid`, `ota_snapshot_invalidate` (Task 3)

Why the re-sync: `ZigbeeColorDimmableLight` keeps **private** `_current_state` / `_current_level`, and `zbAttributeSet()` only calls our callbacks when a command *changes* them (`ZigbeeColorDimmableLight.cpp:139,149`). After a restore they still hold the power-up defaults (off, 255), so an Off command to a restored-on light would be swallowed. The public `setLightState()` / `setLightLevel()` update them — but then call our callbacks synchronously, which must not touch `FixtureState` (on the ring it could drop the restored effect).

- [ ] **Step 1: Suppression flag**

After the Task 4 statics add:

```cpp
static bool s_restored = false;              // this boot restored an OTA snapshot
// Set only around the library re-sync below, all on one task, so our light
// callbacks ignore the library echoing back values we just gave it.
static volatile bool s_suppress_light_callbacks = false;
```

- [ ] **Step 2: Guard the five light-change callbacks**

Make this the first line of each of `on_downlight_change_temp`, `on_downlight_change_rgb`, `on_downlight_change_hsv`, `on_ring_change_rgb` and `on_ring_change_hsv`:

```cpp
    if (s_suppress_light_callbacks) return;
```

- [ ] **Step 3: Restore in `zigbee_light_init()`**

Replace the Task 4 line `ota_watch_init(&s_ota_watch);` with:

```cpp
    // After an OTA recovery restart, put the light back as it was -- see
    // ota_recover(). Only a software restart with a valid record qualifies; a
    // power loss leaves garbage the checksum rejects. Invalidated on every boot
    // either way, so a record can restore at most once.
    if (esp_reset_reason() == ESP_RST_SW && ota_snapshot_valid(&s_ota_snapshot)) {
        s_state            = s_ota_snapshot.state;
        s_prior_recoveries = s_ota_snapshot.recoveries;
        s_restored         = true;
        log_w("Restored light state after OTA recovery #%u (%s at offset %lu)",
              s_ota_snapshot.recoveries,
              s_ota_snapshot.reason == OTA_RECOVER_STALL ? "stall" : "abort",
              (unsigned long)s_ota_snapshot.offset);
    }
    ota_snapshot_invalidate(&s_ota_snapshot);
    ota_watch_init(&s_ota_watch);
```

- [ ] **Step 4: Re-sync the library in the first-join one-shot**

In `zigbee_light_loop()`, inside `if (!s_joined_once && Zigbee.connected()) {`, directly **before** the `const uint8_t effect = ring_effect_value(&s_state.ring);` line add:

```cpp
        // A restored fixture must bring the light library's private on/off and
        // level copies into line, or an Off to a restored-on light is swallowed
        // (zbAttributeSet only calls back on a change). Normal boots skip this.
        if (s_restored) {
            s_suppress_light_callbacks = true;
            s_down.setLightLevel(s_state.down.level);
            s_down.setLightState(s_state.down.on);
            s_ring.setLightLevel(s_state.ring.level);
            s_ring.setLightState(s_state.ring.on);
            s_suppress_light_callbacks = false;
        }
```

- [ ] **Step 5: Build**

Run: `pio run -e esp32h2`
Expected: `[SUCCESS]`.

- [ ] **Step 6: Host tests still pass**

Run: `scripts\run-native-tests.bat`
Expected: `ALL SUITES PASSED`.

- [ ] **Step 7: Commit**

```powershell
git add src/zigbee_light.cpp
git commit -m "feat: restore the light after an OTA recovery restart" -m "On ESP_RST_SW with a valid __NOINIT snapshot, zigbee_light_init() puts FixtureState back and invalidates the record (one-shot). The first-join step then re-syncs ZigbeeColorDimmableLight's private on/off and level through its public setters with our callbacks suppressed -- without that, an Off to a restored-on light is swallowed because zbAttributeSet only calls back on a change." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Release 2.1.0 and document the workflow

**Files:**
- Modify: `src/version.h`, `test/test_version/test_main.cpp`, `README.md`

- [ ] **Step 1: Update the version test first**

In `test/test_version/test_main.cpp`: `0x02000200` -> `0x02010000`; `0x02000201` -> `0x02010001`; `"2.0.2"` -> `"2.1.0"`.

- [ ] **Step 2: Run to verify it fails**

Run: `scripts\run-native-tests.bat`
Expected: `test_version` fails on `test_zb_fw_version_is_unchanged`.

- [ ] **Step 3: Bump the version**

In `src/version.h`: `FW_VERSION_MINOR 1`, `FW_VERSION_PATCH 0`, `FW_DATE_CODE` = the build date as `"YYYYMMDD"`.

- [ ] **Step 4: Run to verify it passes**

Run: `scripts\run-native-tests.bat`
Expected: `ALL SUITES PASSED`.

- [ ] **Step 5: README**

In `README.md`, directly after the paragraph that begins `**There is no fallback.**`, add:

````markdown
### Scheduled updates and automatic recovery (2.1.0 and later)

**Start updates with *schedule*, not *update*:**

```
topic:   zigbee2mqtt/bridge/request/device/ota_update/schedule
payload: {"id": "Loft Overhead Light"}
```

A scheduled update starts on the fixture's next image query (on every join, and hourly), and
Zigbee2MQTT keeps it scheduled if it fails. Combined with the firmware's recovery this makes an
update self-healing:

1. If a download aborts, or stalls for 3 minutes, the fixture saves its light state, restarts
   (the light goes dark for about a second and comes back exactly as it was), and rejoins.
2. On rejoin it queries for an image, and the still-scheduled update starts again from the
   beginning.
3. When it succeeds, the fixture boots the new version and Zigbee2MQTT clears the schedule.

The restart is required: the Arduino Zigbee library never resets its OTA state after an abort, so
without it every retry fails `INVALID_IMAGE` two blocks in. To stop a fixture retrying an image that
can never succeed, send the same payload to `.../ota_update/unschedule`. Each recovery is logged on
the serial console with a running count.

**Fixtures on firmware older than 2.1.0** have no automatic recovery: after a failed update, power-
cycle the fixture before retrying. From Home Assistant, via its Inovelli switch: turn the switch
off, set its Smart Bulb Mode to `Disabled` (the switch is off, so the load loses power), wait ~15 s,
then set Smart Bulb Mode back to `Smart Bulb Mode` (full power returns and the fixture boots).
````

- [ ] **Step 6: Commit**

```powershell
git add src/version.h test/test_version/test_main.cpp README.md
git commit -m "release: 2.1.0 -- automatic recovery from a failed OTA" -m "Documents the scheduled-update workflow the recovery relies on, and the manual power-cycle (via the Inovelli's Smart Bulb Mode) for fixtures older than 2.1.0." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Bench verification and results

Verifies spec 5.3 on Test Unit 2. Needs the user for any Zigbee2MQTT stop/start the permission check refuses.

**Files:**
- Modify: `docs/research/ota-throughput.md`, `docs/superpowers/plans/2026-08-18-bench-verification.md`

- [ ] **Step 1: Build and stage the 2.1.0 OTA image**

Check first that no Lumary fixture has an update in progress (`update.loft_overhead_light`, `update.test_unit_2`: `in_progress` false) — Z2M re-reads the index for every new session.

```powershell
pio run -e esp32h2
Set-Location build\ota
python image_builder_tool.py -m 0x1001 -i 0x0001 -v 0x02010000 -s 2 -g "LumaryZigbee" --tag 0x0000 ..\..\.pio\build\esp32h2\firmware.bin
Set-Location ..\..
python scripts\gen-ota-index.py build\ota\1001-0001-02010000-ota-file.zigbee
Copy-Item build\ota\1001-0001-02010000-ota-file.zigbee, build\ota\index.json \\192.168.1.101\config\zigbee2mqtt\ota\ -Force
```
Expected: `fileVersion 0x02010000`, both files present in the Samba `ota` folder.

- [ ] **Step 2: Flash Test Unit 2 with the recovery firmware labelled older**

Temporarily set `src/version.h` to `FW_VERSION_MINOR 0`, `FW_VERSION_PATCH 9` (2.0.9), then:
```powershell
pio run -e esp32h2 -t upload --upload-port COM8
git checkout -- src/version.h
```
Start the serial logger to `build\ota-logs\recovery-bench.log` (as in Task 1 Step 4).

- [ ] **Step 3: Set a distinctive light state**

From Home Assistant: `light.test_unit_2_downlight` on at brightness 60, `color_temp_kelvin: 4000`; `light.test_unit_2_ring` on with `effect: chase`. Note what the fixture physically shows.

- [ ] **Step 4: Schedule and start the download**

Publish `{"id": "Test Unit 2"}` to `zigbee2mqtt/bridge/request/device/ota_update/schedule`, then `{"id": "Test Unit 2"}` to `zigbee2mqtt/bridge/request/device/ota_update/check` to prompt an image query. Confirm `OTA Client receives data: progress` lines appear in the log.

- [ ] **Step 5: Force a failure**

Once progress passes ~20 KB, stop the Zigbee2MQTT add-on. Expected in the serial log within ~3 minutes, in order:
- `OTA aborted at offset …` or `OTA stalled at offset …`, `recovery #1, restarting`
- boot banner, then `Restored light state after OTA recovery #1 (…)`

The fixture physically shows the Step 3 state again after a ~1 s blink.

- [ ] **Step 6: Retry happens by itself**

Start the Zigbee2MQTT add-on. Expected: within an hour (on the next image query) — or immediately after a `.../ota_update/check` — `OTA Client receives data` resumes from a low offset with no manual update request. HA shows the Step 3 state throughout.

- [ ] **Step 7: Completion clears the schedule**

Let it finish (~2–3 h). Expected: `OTA Finish`, reboot, `update.test_unit_2` shows 2.1.0 (`33619968`) installed with nothing pending, and a later `.../ota_update/check` does not start a download.

- [ ] **Step 8: Negative check**

Unplug Test Unit 2's USB for ~10 s and plug it back in. Expected: no `Restored light state` line; the fixture boots with defaults (off).

- [ ] **Step 9: Record results and commit**

Add a "2.1.0 bench verification" section to `docs/research/ota-throughput.md` with the log excerpts from Steps 5–8, and update section 11 of `docs/superpowers/plans/2026-08-18-bench-verification.md` (the OTA item) with the outcome.

```powershell
git add docs/research/ota-throughput.md docs/superpowers/plans/2026-08-18-bench-verification.md
git commit -m "docs: bench-verify 2.1.0 OTA abort recovery" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

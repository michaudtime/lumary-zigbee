# Automatic recovery from a failed Zigbee OTA — design

**Date:** 2026-09-18
**Status:** approved, ready for implementation

Follows the OTA investigation in `docs/research/ota-throughput.md` and the rev A sign-off gate in
`docs/superpowers/plans/2026-08-18-bench-verification.md` section 11.

## 1. Problem

A fixture whose OTA download fails **cannot be updated again until it is power-cycled**, and a
fixture in a ceiling has no power switch short of the breaker. There is no BLE fallback and no
remote reboot (README "OTA Updates"), so today a single failed update strands the fixture on its old
firmware.

### 1.1 Root cause: the library never resets its state on abort

`zb_ota_upgrade_status_handler()` in the Arduino Zigbee library (`ZigbeeHandlers.cpp:363`) keeps
file-static `offset`, `total_size` and `s_tagid_received`, and resets them only in the
`ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK` case (a successful end). `ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT`
falls into `default:`, which only logs -- no reset, and no `zbOTAState(false)` either. The next
session's first block is then fed to a parser that believes it is mid-image, and the device ends
the session with `INVALID_IMAGE` two blocks in. Only a reboot clears it.

Observed on hardware repeatedly: 2026-09-15 (twice after a deliberately aborted session) and
2026-09-18 (the loft fixture, days after its last aborted session, fixed only by power-cycling it
through its Inovelli switch).

### 1.2 Failures still happen on a good link

The coordinator work of 2026-09-18 took OTA from ~25 B/s to ~100 B/s and made it steady, and a full
844 KB update has now completed on the bench (2 h 20 m). But ~17,000 block exchanges per image
means an occasional failure is a normal event, not an exceptional one -- for example a lost block
*response*, after which the OTA client waits ~134 s and aborts rather than re-requesting. Recovery
has to be automatic.

## 2. Goal

After a failed or stalled OTA, the fixture recovers by itself and the update is retried, with no one
touching the fixture and no visible change to the light beyond a ~1 s blink.

**Non-goals:** resuming a download from its offset; making transfers faster; fixing the library
itself; restoring light state after a *power loss* (StartUpOnOff remains a separate future feature).
Also out of scope, though a natural follow-up: after a *successful* update the library restarts the
fixture itself, and it comes back **off** (today's behaviour). Snapshotting on status 2 would extend
this design to cover it.

## 3. Design

### 3.1 Detection: `src/ota_watch.h` (pure logic, host-tested)

No ESP-IDF includes, in the style of `light_state.h`. Fed one sample per second:

```c
typedef struct {
    uint8_t  image_status;   // OTA client ImageUpgradeStatus (0x0006)
    uint32_t file_offset;    // OTA client FileOffset (0x0001)
    uint32_t now_ms;         // millis()
} OtaSample;
```

and answers "recover now, or not" plus the reason (`OTA_RECOVER_NONE`, `_ABORT`, `_STALL`).
ImageUpgradeStatus values used: 0 = Normal, 1 = Download in progress, 2 = Download complete.

It only ever triggers **after it has seen status 1 during the current boot.** Then:

- **Abort:** status went 1 -> 0 without passing through 2, and has stayed 0 for **5 s** (the grace
  period absorbs a flicker).
- **Stall (backstop):** status is 1 and `file_offset` has not changed for **180 s**. The OTA
  client's own give-up is ~134 s, so this fires only if the client hangs instead, or if the stack
  never returns the status to 0 after an abort (not yet known -- see 5.1). Either way recovery
  happens within ~3 minutes.
- **Never** after status 2: download complete is terminal and the library reboots into the new
  image itself.

Normal retry stalls (3-10 s, the occasional ~48 s) are far below 180 s. All time comparisons are
wrap-safe (`now - then`, unsigned).

### 3.2 Glue: `src/zigbee_light.cpp`

`zigbee_light_loop()` samples once per second: reads the two attributes with
`esp_zb_zcl_get_attribute(DOWNLIGHT_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_OTA_UPGRADE,
ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE, ...)` under `esp_zb_lock_acquire()`, feeds `ota_watch`, and calls
`ota_recover(reason)` when it says so. If the lock or an attribute is unavailable, that sample is
skipped (no trigger).

### 3.3 Recovery and state snapshot: `src/ota_snapshot.h` (pure logic, host-tested) + glue

`ota_recover(reason)`:

1. Snapshots `FixtureState` -- both halves: downlight on/off, level, colour temperature; ring
   on/off, level, mode, colour, scene -- into a record placed in `__NOINIT_ATTR` RAM (not zeroed at
   startup, so it survives `esp_restart()` but not a power loss). The record carries a magic
   number, a format version, a checksum, the consecutive-recovery count, and the reason.
2. Logs the reason, the offset reached and the recovery count.
3. Calls `esp_restart()`.

`ota_snapshot.h` owns the pure parts -- `snapshot_save()`, `snapshot_valid()` (magic + version +
checksum), `snapshot_invalidate()` -- over a plain struct, so they are host-testable. The
`__NOINIT_ATTR` instance and `esp_reset_reason()` live in the glue.

`RTC_NOINIT_ATTR` is **not** used: on this target it can compile to nothing
(`esp_attr.h` defines it empty when RTC memory is unavailable). `__NOINIT_ATTR` is always backed.

### 3.4 Restore on boot

In `zigbee_light_init()`, after `fixture_state_init()` and the NVS scene load: if
`esp_reset_reason() == ESP_RST_SW` **and** `snapshot_valid()`, copy the saved state over the
defaults, then **invalidate the record immediately** so it can only be used once. The existing
first-join one-shot in `zigbee_light_loop()` then publishes the restored on/off and level, so Home
Assistant never sees the light flip.

The snapshot is used **only** after an OTA recovery restart. Any other reset -- power loss, brownout,
watchdog, panic -- boots with today's defaults: a power loss leaves random RAM (the checksum
rejects it), and other resets are not `ESP_RST_SW` or find the record already invalidated.

The consecutive-recovery count survives into the next record and is reset when the fixture boots
without a valid snapshot.

### 3.5 Retry: Zigbee2MQTT's scheduled updates (no firmware logic)

Zigbee2MQTT documents that *"if a scheduled update fails, it will remain scheduled (Device will try
again, on the next check)"*, that the schedule is persisted across Z2M restarts, and that it clears
once no update is available. Our firmware already queries for an image on every join
(`requestOTAUpdate()` in the first-join one-shot) and hourly after that. So:

1. Start updates with `zigbee2mqtt/bridge/request/device/ota_update/schedule`, `{"id": "..."}`.
2. The next image query starts the download.
3. On failure the fixture recovers (3.1-3.4), rejoins, queries, and the schedule restarts the
   download from zero.
4. On success it boots the new version and Z2M clears the schedule.

**No retry cap in firmware.** A failure costs a ~1 s blink and a restarted download; repeating is
harmless. An image that can never succeed is stopped from Z2M with `.../ota_update/unschedule`. The
logged recovery count makes a looping fixture obvious.

### 3.6 Documentation

README "OTA Updates": the schedule workflow as the recommended way to update; the abort behaviour
and its automatic recovery; and, for fixtures still on firmware older than this feature, the manual
recovery -- power-cycle the fixture (via the breaker, or via its Inovelli switch: switch off, disable
Smart Bulb Mode, wait, re-enable Smart Bulb Mode).

## 4. Release

Ships as **2.1.0** (new behaviour): `FW_VERSION_MINOR` 1, `FW_VERSION_PATCH` 0, `test_version`
updated to match. The bench board's OTA from 2.0.2 to 2.1.0 is itself a real OTA test.

## 5. Verification

### 5.1 Premise check (first, on the bench)

Log `ImageUpgradeStatus` and `FileOffset` once per second during a real download on Test Unit 2.
Confirm the stack advances `FileOffset` per block and sets status 1 during download. **If it does
not, stop and revisit the detection approach before building the rest.**

### 5.2 Host tests (`scripts\run-native-tests.bat`)

`test/test_ota_watch`:
- no trigger without a download seen this boot
- abort triggers once status has been 0 for 5 s after 1; a shorter flicker does not
- stall triggers at 180 s of unchanged offset; an advancing offset never does
- 1 -> 2 -> 0 never triggers
- correct across a `millis()` wrap

`test/test_ota_snapshot`:
- save -> valid round-trip preserves every field
- one corrupted byte, a wrong magic, or a wrong version -> invalid
- invalid after `snapshot_invalidate()`
- consecutive-recovery count carries across a save

### 5.3 Bench (Test Unit 2, serial logger attached)

1. **Forced failure:** set a distinctive state (downlight on at a dim level and a non-default colour
   temperature; ring running a non-default effect), schedule the update, stop Z2M mid-transfer.
   Expect: device abort within ~134 s -> `ota_watch` triggers -> reboot -> identical light state
   restored, HA shows no flip -> Z2M started again -> the schedule restarts the download by itself.
2. **Completion:** let a scheduled update run to the end; the schedule clears itself.
3. **Negative:** a real power cycle boots with defaults; a watchdog reset does not restore a stale
   snapshot.

Stopping/starting Z2M may be blocked by Claude Code's permission check; if so, the user does it from
Home Assistant while the serial log is watched.

## 6. Risks

- **The premise (5.1) is unverified.** Detection depends on the stack maintaining the OTA client
  attributes. Checked first; the stall path covers the "status never returns to 0" variant.
- **A false stall** would cost a needless blink and restart a healthy download. 180 s is ~3.7x the
  longest normal stall seen (48 s) and above the client's own 134 s give-up.
- **Snapshot scope creep.** Restoring state after *other* resets is deliberately excluded; it
  belongs to StartUpOnOff, which also needs NVS persistence and Z2M's `power_on_behavior`.

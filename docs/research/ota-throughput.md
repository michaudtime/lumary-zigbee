# Zigbee OTA throughput — why an update takes hours

**Date:** 2026-09-16
**Status:** research; root cause of the device-side pacing NOT yet found (needs bench serial)

Prompted by the 2.0.1 -> 2.0.2 attempts on the loft fixture
(`docs/superpowers/plans/2026-08-18-bench-verification.md` section 11). The user recalls the
**bench** OTA in August being slow as well, which is what makes this a firmware/stack question
rather than a ceiling-RF one: at the bench the fixture was USB-powered, feet from the coordinator,
and it was still slow. Nobody timed it then, so the August run is a qualitative memory, not a
number -- but it rules out "it is slow because it is in a ceiling".

## Measured

| Window (2026-09-15/16) | Throughput | Notes |
|---|---|---|
| 20:43-20:44 | 30.4 B/s | 32 exchanges sampled, LQI 90-126 |
| 17:05-17:15 | ~11 B/s | LQI 54 |
| 17:15-17:26 | ~5 B/s | |
| 17:26-17:36 | ~4.7 B/s | LQI recovered to **102** |

Three sessions have now died in the same **offset** band despite very different durations and
rates, which argues for a fixed-offset or fixed-block-count fault rather than a timeout:

| Session | Duration | Reached | Aftermath |
|---|---|---|---|
| 09-15 20:14 - ~20:50 | 36 min | 6.24% ~ 53 KB | aborted deliberately (Z2M restart) |
| 09-16 07:21 - 07:52 | 31 min | ~5.7% ~ 48 KB | fixture silent ~9 h until a user reset |
| 09-16 17:05 - 19:12 | **127 min** | ~36-40 KB (est. from rate) | **fixture stayed reachable** (LQI 60 at 20:04) |

~36-53 KB at 50 B/block is roughly 750-1,100 blocks. The third session's survival is itself
informative: whatever ends the transfer does not always take the radio down with it.

**Link quality recovering while throughput kept falling is the key observation.** It decouples the
pacing from the radio. So does the Z2M-side evidence: every block request is answered in the same
second, requests/responses/sends are 1:1 with no retransmission, and no offset is ever
re-requested (see section 11 for the full sample). The fixture simply asks for blocks slowly, and
more slowly as a session goes on.

## The arithmetic, for an 844,222-byte image

| chunk | blocks | `image_block_response_delay` | best case | throughput |
|---|---|---|---|---|
| 50 B | 16,885 | 250 ms (Z2M default) | 70.4 min | 200 B/s |
| 50 B | 16,885 | 50 ms | 14.1 min | 1,000 B/s |
| 100 B | 8,443 | 250 ms | 35.2 min | 400 B/s |
| 223 B | 3,786 | 250 ms | 15.8 min | 892 B/s |
| 223 B | 3,786 | 50 ms | 3.2 min | 4,460 B/s |

Measured 4.7 / 11 / 30 B/s = 50 h / 21 h / 7.8 h. Espressif's optimized benchmark of 8.1 KB/s
would do this image in **104 seconds**.

So there are **two independent problems**, and they need separating:

1. **Z2M's configuration caps us at 200 B/s** = 70 min, before the device is even involved. Z2M
   states this itself: `OTA update of '0x744dbdfffe6b575f' estimated at 4221.25 seconds
   (16885 chunks, 4 per second)`.
2. **The device delivers 4.7-30 B/s, i.e. 7-40x worse than that cap**, and degrades over a
   session. This is the actual bug and it is device-side.

## Espressif's own guidance, audited against our build

From the [ZCL OTA Upgrade cluster guide](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/user-guide/zcl_ota_upgrade.html)
("In testing, the optimizations above raised OTA throughput to about 8.1 KB/s"):

| Recommendation | Our state | Lever? |
|---|---|---|
| `OTA_UPGRADE_MAX_DATA_SIZE` = 223 | We request 223 (Arduino default, `ZigbeeEP.h:142`). **Z2M caps responses at 50**; its `ota.default_maximum_data_size` schema maxes at **100**, so 223 is unreachable from Z2M at all. 100 was tried and the frames did not deliver (see section 11) -- 50 is the working value. | mostly blocked |
| Raise `CONFIG_FREERTOS_HZ` above 100 | Already **1000** in the Arduino prebuilt libs | none |
| Enable `CONFIG_IEEE802154_TIMING_OPTIMIZATION` | Already **y** | none |
| "Reduce unrelated logs during the OTA upgrade" | We build `-DCORE_DEBUG_LEVEL=3`, and `zb_ota_upgrade_status_handler()` does **one `log_i` per block** (`ZigbeeHandlers.cpp:396`) -- 16,885 of them, each over USB CDC | **yes, untested** |
| Delta OTA (send only the diff) | `CONFIG_ZB_DELTA_OTA` is **not** enabled in the prebuilt libs, so `ZigbeeHandlers.cpp:381`'s `esp_delta_ota_begin()` path is compiled out. Would need a custom IDF build, not just a flag | not without an IDF build |

## Candidate causes for the device-side pacing (none confirmed)

1. **Per-block logging.** The `log_i` above, plus `ARDUINO_USB_CDC_ON_BOOT=1`. Writes to a USB CDC
   with no host attached can block; this is the same bench-vs-deployed difference that caused the
   section 8 standalone-boot failure. Weakened as a sole explanation by the user's report that the
   bench (USB host attached, writes cheap) was also slow -- but logging may still cost real time.
2. **Render-loop starvation.** `loop()` renders at ~60 fps and `led_driver_show()` busy-waits in
   `spi_device_polling_transmit()` for ~2.3 ms per frame on a single-core RISC-V part. The Arduino
   library exposes an `onOTAState` callback that this firmware does **not** use; suspending
   rendering while an OTA is in flight is the obvious experiment.
3. **Flash write cost per 50-byte chunk.** `esp_ota_write()` is called once per block with 50
   bytes, so sector erases and partial-sector buffering happen 16,885 times instead of ~206 times.
4. **Watchdog reboots mid-session.** Two sessions died near ~50 KB (6.24% and ~5.7%) after
   30-36 min, and afterwards the fixture went silent for 20 min and then ~9 h. `WDT_TIMEOUT_MS` is
   5 s and armed on the loop task. If OTA work starves the loop past 5 s the board panics, reboots,
   re-requests OTA on join, and can re-enter the same state. Not confirmed; would explain both the
   deaths and the silences.

## Next steps, in order

1. **Bench reproduction with serial attached** -- a spare rev A board flashed with 2.0.1 in the
   bench fixture, OTA run with the monitor open, and *timed*. This is the only way to see whether
   the deaths are watchdog panics, and to get a baseline number for the August run.
2. **Cheap experiments, one at a time**, measured against that baseline:
   - `CORE_DEBUG_LEVEL=0` (or gate the library's per-block log) -- tests candidate 1, and shrinks
     the image.
   - Use `onOTAState` to stop rendering during OTA -- tests candidate 2.
   - Z2M `image_block_response_delay: 250 -> 50` -- independent of the firmware, worth ~5x on
     paper, but only after the device-side limit is understood, or it will be masked.
3. **Shrink the image.** 844 KB is several times a typical Zigbee device firmware, and every
   optimization above is multiplied by whatever the image size ends up being.
4. **Make an interrupted OTA survivable** -- reset the OTA state on abort, and provide a remote
   reboot path. See section 11; this matters more than throughput, because it is what turns a slow
   update into an unrecoverable one.

## Sources

- [ESP Zigbee SDK — ZCL OTA Upgrade Cluster (ESP32-H2)](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/user-guide/zcl_ota_upgrade.html)
- [ESP Zigbee SDK — OTA API reference](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_ota.html)
- [Zigbee2MQTT — OTA updates](https://www.zigbee2mqtt.io/guide/usage/ota_updates.html)
- [Zigbee2MQTT — all settings](https://www.zigbee2mqtt.io/guide/configuration/all-settings.html)

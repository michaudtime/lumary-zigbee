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

## Bench runs, 2026-09-18 (Test Unit 2, `0x744dbdfffe6d65c8`, USB-powered, logger attached)

A second rev A board, flashed with 2.0.1, a few feet from the coordinator, with
`scripts/serial-log.py` capturing the device's own per-block log line (millisecond device clock, so
USB batching doesn't matter). Captures are in `build/ota-logs/` (git-ignored).

**The bench is exactly as slow as the ceiling: 30.5 B/s.** The user's recollection of the August
bench run was right, and distance/placement is ruled out for good.

**The stalls are quantized.** Gaps between received blocks are either ~270 ms (healthy: Z2M's
250 ms `image_block_response_delay` plus a few ms) or a multiple of **~3.27 s** -- nothing in
between:

```
first 52 blocks, baseline:  <400ms x34   400ms-3s x0   3-6s x13   >=6s x4
stall sizes in 3.27s quanta: x1 13, x2 2, x3 2        89% of wall time stalled
```

**During a stall Z2M never hears the request until the attempt that works.** For the two long
stalls inside Z2M's log window, Z2M first saw the late request 8.8-9.4 s after the previous block,
and the device logged the block 0.4-0.6 s later. So the time is lost *before* the request reaches
Z2M: an attempt fails, a fixed ~3.27 s timer expires, and it is retried -- up to three times.
~3 s matches the order of ZBOSS's APS ack-wait for a non-sleepy destination; not confirmed.

There is also a single **48.29 s** stall per session (48,285 ms and 48,289 ms in two runs, at
different offsets) -- a second fixed timer, presumably the OTA client's own give-up-and-re-request.

**The 50 KB death did not reproduce on the bench.** The baseline run passed 309 KB (36%) with no
error, no reset and no gap in the log before it was stopped deliberately. The one difference from
the ceiling is that this board has a USB host attached -- the same bench-vs-deployed difference that
caused the section 8 boot failure. That is now the lead for the field deaths, not the offset.

### Hypotheses tested and refuted

| Run | Change (one variable) | Stalled exchanges | Time stalled | Verdict |
|---|---|---|---|---|
| baseline | -- | 32% | 88% | |
| 2 | Stop rendering and yield (`onOTAStateChange` hook, `delay(20)`) while OTA active; hook confirmed firing | 37% | 92% | **refuted** -- not CPU starvation |
| 3 | `esp_ieee802154_set_cca_mode(CARRIER)` instead of energy-detect at -75 dBm; confirmed in force every 60 s | 28% | 90% | **refuted** -- not Wi-Fi energy / channel access |

Both experiments were reverted rather than stacked. Healthy-exchange time was unchanged across all
three (~270-300 ms), so the device processes a block quickly; it is the ~30% of exchanges that fail
and wait out a retry timer that cost 90% of the time.

### Root cause found: an aborted OTA can't be retried

The ceiling's `INVALID_IMAGE` on retry (section 11) is explained by the Arduino library.
`zb_ota_upgrade_status_handler()` (`ZigbeeHandlers.cpp:363`) keeps file-static `offset`,
`total_size` and `s_tagid_received`, and resets them **only** in the
`ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK` case (a successful end). `ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ABORT`
falls into `default:`, which only logs -- no reset, and no `zbOTAState(false)` either. So after an
abort, the next session's first block is fed to a parser that believes it is mid-image. Only a
reboot clears it. Consequences for our firmware:

- Anything gated on the library's OTA-active notification can stick "active" after an abort.
- A fix has to live on our side (a library patch is not under our control): e.g. detect an OTA that
  has gone quiet and reboot, since reboot is the only thing that resets the library's state.

## Next steps, in order

Updated after the 2026-09-18 bench runs. Done: bench reproduction and timing (step 1 of the
original list); render starvation and CCA both refuted.

1. **Find where the failed ~30% of exchanges die -- on the air.** Both ends look identical from
   their own logs: the device waits, Z2M hears nothing. An 802.15.4 sniffer on channel 25 settles it
   in one capture: if the lost block requests are on the air and the coordinator doesn't ack them,
   it is the coordinator/route side; if they never appear, it is the device side. A spare rev A
   board (ESP32-H2) can run as a sniffer into Wireshark, so no new hardware is needed.
2. **Check the route.** Z2M's `linkquality` is the LQI of the *last hop* into the coordinator, not
   necessarily of the device itself. If Test Unit 2 reaches the coordinator through another
   router, that hop's losses would look exactly like this. A Z2M network map answers it.
3. **Reproduce the field deaths on the bench** by removing the difference that matters: run an OTA
   with the board powered but *no USB host* (4.7 V input, or a charger-only USB supply), and
   capture what happens around 36-53 KB. The per-block `log_i` over an unconnected USB CDC is the
   leading suspect.
4. **Make an interrupted OTA survivable** -- see the root cause above. A stalled-OTA watchdog that
   reboots is the one fix fully under our control. This matters more than throughput.
5. **Then the throughput levers**, measured one at a time against the baseline: shrink the image
   (`CORE_DEBUG_LEVEL=0` also removes the per-block log), then Z2M `image_block_response_delay`.
   The latter only speeds the ~10% of time spent in healthy exchanges until the losses are fixed.

## Sources

- [ESP Zigbee SDK — ZCL OTA Upgrade Cluster (ESP32-H2)](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/user-guide/zcl_ota_upgrade.html)
- [ESP Zigbee SDK — OTA API reference](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_ota.html)
- [Zigbee2MQTT — OTA updates](https://www.zigbee2mqtt.io/guide/usage/ota_updates.html)
- [Zigbee2MQTT — all settings](https://www.zigbee2mqtt.io/guide/configuration/all-settings.html)

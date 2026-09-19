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

### Network map, 2026-09-18: the Lumary boards rate the coordinator at LQI 0

A Z2M raw network map (`type: raw, routes: true`, 62 nodes, 928 links) shows an asymmetry that no
other router on the network has:

| | Coordinator hears it | It rates the coordinator | Its route to the coordinator |
|---|---|---|---|
| Test Unit 2 (bench) | LQI 82, direct | **LQI 0** | **none** -- table holds a route to *its own* address via a plug, and a discovery stuck `DISCOVERY_UNDERWAY` |
| Loft Overhead Light | LQI 89, direct | **LQI 0** | via Living Room Light Switch (2 hops) despite the direct link |
| every other router listing the coordinator | -- | LQI 36-239 | -- |

The coordinator's own route to Test Unit 2 is direct. The **median** LQI across each Lumary
board's whole neighbour table is 0.

Hypothesis, **not yet confirmed**: the ESP32-H2 stack believes its links are far worse than they
are. LQI 0 maps to the maximum link cost (7), so the board avoids its good direct link to the
coordinator and relies on route discovery and multi-hop paths, which intermittently fail and wait
out retry timers. It fits every observation so far -- requests that never reach Z2M, fixed timers,
no sensitivity to CPU load or CCA -- and it would affect *all* traffic these boards send to the
coordinator, with OTA merely the only workload heavy enough to make it visible. Low neighbour-table
LQI from Espressif 802.15.4 parts is a reported upstream issue
([esp-zigbee-sdk #418](https://github.com/espressif/esp-zigbee-sdk/issues/418), ESP32-C6,
esp-zigbee-lib 1.4.0); no maintainer diagnosis is visible there.

Next evidence: dump the board's *own* neighbour and routing tables (`esp_zb_nwk_get_next_neighbor()`,
`esp_zb_nwk_get_next_route()` -- `lqi`, `rssi`, `outgoing_cost`, `age`, route state) to serial
periodically during an OTA, and line route changes up against the stalls.

### Confirmed by instrumentation: the ESP32-H2 zeroes LQI below ~-80 dBm, and the route flaps

A diagnostic build dumped the board's own neighbour and routing tables every 30 s
(`esp_zb_nwk_get_next_neighbor()` / `esp_zb_nwk_get_next_route()`, copied out under
`esp_zb_lock_acquire()` and logged after release). Findings:

- **The LQI the ESP32-H2 reports is a steep function of RSSI:** -55 dBm -> 127, -67 -> 66,
  -69 -> 56, -71 -> 45, -78 -> 10, -79 -> 5, **-83 dBm and weaker -> 0**. That is a straight line
  to zero at about -80 dBm -- a signal 802.15.4 can still use (sensitivity ~-100 dBm). The value
  comes up from the radio driver (`esp_ieee802154_get_recent_lqi()`); nothing in the driver or the
  Zigbee stack exposes the mapping. Still present in esp-zigbee-lib **1.6.8** / esp-zboss-lib
  **1.6.4** (Arduino core 3.3.11); upstream is at 2.0.4.
- **With the coordinator at -87 to -97 dBm the board scored it LQI 0**, so every link below the
  cliff got the same worst cost and the stack could not tell a -83 dBm link from a -97 dBm one.
  In 10 minutes the route to the coordinator changed 11 times -- via -70, -86, -93 and -97 dBm
  hops, or no route at all -- and the coordinator repeatedly aged out of the neighbour table.
  Stall windows lined up with the churn; the one stable window (route pinned on a -70 dBm hop)
  ran at 173 B/s with 1% stalls.

The coordinator is a Sonoff ZBDongle-P (CC2652P) in a comm closet a floor below the bench and the
loft, which is why the path loss was so high.

### Coordinator changes, 2026-09-18: transmit power, firmware, extension cable

| Change | Coordinator RSSI at the board | Board's LQI for it | Route | OTA (first ~5 min) |
|---|---|---|---|---|
| baseline: fw `20210708`, default TX power, dongle in the server | -90 dBm | 0 | flapping, rarely direct | 25 B/s, 21% stalled (30%+ over long runs) |
| `advanced.transmit_power: 20` (old firmware) | -84 dBm (+6 dB only) | 0 | still flapping | 21 B/s, 26% -- no improvement |
| **+ Z-Stack `20250321` + USB 2.0 extension cable** | **-67 dBm** (-73..-64) | **up to 81** | **direct** -- mostly no route entry needed | **73 B/s, 11% stalled**, steady 53-87 B/s per minute |

`transmit_power` alone gained only ~6 dB on the old firmware and was not enough. The new firmware
and moving the dongle off the server's USB ports on an extension cable went in together, so their
individual contributions are not separated -- but together they lifted the coordinator ~23 dB at
the board, clear of the LQI cliff, and the route stopped flapping.

What remains: ~11% of exchanges still wait out the 3.27 s timer, which is still ~62% of the time.
~73 B/s puts the 844 KB image at ~3.2 h -- down from 7-10 h, not yet fast.

### First long run on the new coordinator: 67.7%, then a fatal lost response

Run 6 (18:04-19:42) averaged **~100 B/s** -- accelerating from ~66 to ~136 B/s per 15-minute
window -- and passed the old 36-53 KB death zone without incident. It reached **571,694 bytes
(67.7%)** and then died in a way none of the earlier runs did. Lined up across both logs (the Z2M
host clock runs ~8 s ahead of the PC):

```
Z2M 19:40:41  imageBlockResponse fileOffset 571650  -> board logs progress 571694
Z2M 19:40:41  imageBlockResponse fileOffset 571700  -> board NEVER logs it
              (no further request from the board reaches Z2M -- no 3.27 s retries either)
board 19:42:47  zb_ota_upgrade_status_handler(): OTA status: 4        (ABORT, ~134 s later)
Z2M ~19:43-44   session closed on its 150 s image_block_request_timeout
```

The link was strong throughout: the board's 30 s dumps show the coordinator at -63..-65 dBm,
LQI 76-86, and the stack lock was always free within 100 ms, so the Zigbee task was not hung.

**Model that now fits every observation today:**

- **A lost *request*** (board -> coordinator) is never APS-acknowledged, so the stack retransmits
  it on its ~3.27 s timer. Those are the stalls: slow but self-healing.
- **A lost *response*** (coordinator -> board) is fatal. The request *was* delivered and acked,
  so nothing below ZCL retries; the OTA client waits for a block that never arrives and, after
  ~134 s, aborts the whole upgrade instead of re-requesting the offset. Combined with the
  library's abort bug (below), the board then cannot retry until it reboots.

With ~17,000 exchanges per image, even a small per-exchange response-loss probability makes a
full transfer a gamble. Priorities follow directly: (1) recover from an abort automatically --
reboot on a stalled/aborted OTA so Z2M can re-offer without anyone touching the fixture;
(2) shrink the image to cut the exchange count; (3) find out whether the ~134 s give-up is
tunable or a lost response can be made to trigger a re-request.

### Operational gotcha: the Z2M restore after a coordinator reflash

Recorded because it will recur on the next reflash. After flashing `20250321`, Z2M crash-looped
with `network commissioning timed out - most likely network with the same panId or extendedPanId
already exists nearby`. The message is generic; the cause was in the herdsman debug log:

```
zh:adapter:zstack:manager: (stage-1) adapter is not configured / not commissioned
zh:adapter:zstack:manager: (stage-2) configuration does not match backup
zh:adapter:zstack:manager: determined startup strategy: startCommissioning
```

`coordinator_backup.json` had `"channel": 25` but **`"channel_mask": [11]`** -- a stale NV value
from the old firmware -- and herdsman compares the *mask* with `configuration.yaml`'s
`channel: 25`. The mismatch made it skip `restoreBackup` (which forms a throwaway random network
first and cannot collide) and try to form a fresh network with the real PAN ID instead, which
collided with the house's own routers. Setting `channel_mask` to `[25]` (nothing else) fixed it:
the restore ran, and every device -- battery devices included -- came back without re-pairing.
Check `channel_mask` against `channel` **before** reflashing next time.

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

## 2.1.0 bench verification, 2026-09-19: automatic recovery works

Firmware 2.1.0 implements that fix (design:
[`docs/superpowers/specs/2026-09-18-ota-abort-recovery-design.md`](../superpowers/specs/2026-09-18-ota-abort-recovery-design.md)).
Verified on Test Unit 2 (`0x744dbdfffe6d65c8`, USB-powered, logger attached), flashed over USB with
the version temporarily labelled 2.0.9 so that Z2M would offer the 2.1.0 image. Capture:
`build/ota-logs/recovery-bench.log` (first build) and `recovery-bench-final.log` (after the review
fixes).

**Forced failure.** Distinctive state set first: downlight on, level 60, 4000 K; ring running
`chase`. Update started with `ota_update/schedule` at 01:40:25 (it began at once, because the board's
post-flash image query landed in the same minute). Z2M was then stopped by hand:

```
01:49:12  OTA Client receives data: progress [82744/845318]   last block before Z2M stopped
01:51:27  OTA status: 4                                        client gives up (ABORT), +135 s
01:51:33  ota_recover(): OTA aborted at offset 4294967295 -- recovery #1, restarting
01:51:33  rst:0xc (SW_CPU)
01:51:34  Restored light state after OTA recovery #1 (abort at offset 4294967295)
01:51:34  Published state: downlight on=1 level=60 / ring on=1 level=255 effect=4
```

Abort seen at the expected 135 s, the 5 s grace plus one sample, then restart and restore within a
second. When Z2M came back (01:53:54), HA showed downlight on/60/4000 K and ring `chase` throughout:
no flip. The schedule survived the restart (`"scheduledOta":{"downgrade":false}` in Z2M's
`database.db`).

The `4294967295` is a bug this run found: on abort the stack resets `FileOffset` to ZCL "none"
(0xFFFFFFFF) before the status drops, and the first build logged the current sample. It now logs
the watcher's last real in-download offset (`ota_watch_progress()`, commit 9129a0f).

**Retry without anyone touching it.** No request was sent. The library re-queries on
`OTA_UPGRADE_QUERY_INTERVAL`, which is 60 *minutes*, although its log line says "60 seconds". Its
02:52:34 query found the still-scheduled update, and the download restarted from offset 0.

**Completion clears the schedule.** 04:27:47 `OTA upgrade check status: ESP_OK`, then `OTA Finish`
(version 0x02010000), then a software restart into 2.1.0, coming back **off**. That is the library's
own restart; the record had already been invalidated at the previous boot, so nothing is restored,
as the spec's non-goals say. Z2M: `Update of 0x744dbdfffe6d65c8 successful (5713 seconds)`, schedule
cleared by 04:29:50, and a manual `ota_update/check` then returned `update_available: false`. That
is 845 KB in 1 h 35 m, ~150 B/s.

**Final build, second full update.** The final-review fixes were flashed the same way at 04:35:
progress offset, restart backoff, ring sentinel seeding, and the white PWM configured before the
USB wait (9129a0f, b884a46). This download was started with `ota_update/update`, because the
board's first query landed before the schedule was set. It ran to completion 04:36:35 -> 06:15:36
(1 h 39 m) with no abort, stall or recovery line at all, so there was no false trigger across
~6,000 one-second samples. Z2M cleared the schedule at 06:17:44. The forced failure was **not**
repeated on this build: the offset and sentinel fixes are covered by host tests and review only.

**Still open:**

- The USB-unplug negative check: a real power loss must boot with defaults.
- What the light physically does during a recovery restart. Test Unit 2 has no LED load, so this
  needs a mains fixture: the loft's first restart on 2.1.0.

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
4. ~~**Make an interrupted OTA survivable**~~ -- **done in 2.1.0**, bench-verified 2026-09-19
   (section above): an aborted or stalled OTA reboots the fixture with its light state restored, and
   a Z2M scheduled update retries by itself.
5. **Then the throughput levers**, measured one at a time against the baseline: shrink the image
   (`CORE_DEBUG_LEVEL=0` also removes the per-block log), then Z2M `image_block_response_delay`.
   The latter only speeds the ~10% of time spent in healthy exchanges until the losses are fixed.

## Sources

- [ESP Zigbee SDK — ZCL OTA Upgrade Cluster (ESP32-H2)](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32h2/user-guide/zcl_ota_upgrade.html)
- [ESP Zigbee SDK — OTA API reference](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32/api-reference/esp_zigbee_ota.html)
- [Zigbee2MQTT — OTA updates](https://www.zigbee2mqtt.io/guide/usage/ota_updates.html)
- [Zigbee2MQTT — all settings](https://www.zigbee2mqtt.io/guide/configuration/all-settings.html)

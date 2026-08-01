# Framework upgrade trial — 3.1.0 → 3.3.11

**Branch:** `try/framework-upgrade`
**Date:** 2026-08-01
**Verdict:** the upgrade **does** close both Zigbee gaps, but the build is blocked
on this machine by a Windows setting, not by anything in our code.

---

## Why we tried it

Framework 3.1.0's Arduino Zigbee library is missing two things the spec calls for:

- **Colour temperature** — no CCT cluster, so `zigbee_light.cpp` currently infers
  warm/cool from the commanded colour's red/blue balance (`rgb_to_cct`).
- **Zigbee OTA** — the spec's *primary* update path doesn't exist at all.

## What the newer library provides (verified from source)

Read from `espressif/arduino-esp32@master`:

**`libraries/Zigbee/src/ep/ZigbeeColorDimmableLight.h`**
```
ZIGBEE_COLOR_MODE_TEMPERATURE = 0x02
typedef void (*ZigbeeColorLightTempCallback)(bool state, uint8_t level, uint16_t color_temperature);
void onLightChangeRgb(...)      // onLightChange() is now deprecated
void onLightChangeHsv(...)      // native HSV -- no RGB->HSV conversion needed
void onLightChangeTemp(...)     // native colour temperature, in mireds
bool setLightColorTemperature(uint16_t color_temperature);
bool setLightColorTemperatureRange(uint16_t min_temp, uint16_t max_temp);
```

**`libraries/Zigbee/src/ZigbeeEP.h`**
```
bool addOTAClient(...);         // + OTA_UPGRADE_QUERY_INTERVAL
void requestOTAUpdate();
virtual void zbOTAState(bool otaActive);
```

So **both gaps close**: native CCT (a real slider in HA rather than our inferred
warmth) and a working Zigbee OTA client. `onLightChangeHsv` would also let us
drop the RGB→HSV step in `light_state.h`, and `onLightChangeTemp` would replace
`rgb_to_cct` with the real thing.

## What blocks the build here

`pio run` fails while **extracting** the new package, not while compiling:

```
FileNotFoundError: [Errno 2] No such file or directory:
'C:\Users\chadm\.platformio\.cache\tmp\pkg-installing-.../esp32-arduino-libs/esp32c6/
 include/espressif__esp_matter/connectedhomeip/connectedhomeip/src/app/clusters/
 camera-av-settings-user-level-management-server/
 camera-av-settings-user-level-management-server.h'
```

That path is ~262 characters. Windows' legacy `MAX_PATH` limit is 260, and this
machine has long-path support **disabled**:

```
HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 0
```

The newer package bundles Matter/connectedhomeip headers for every chip, and some
of those paths exceed the limit. Nothing to do with the ESP32-H2 or our sources.

## Two ways forward

**A. Enable Windows long paths** (recommended; one-time, helps every future project)

Run in an **Administrator** PowerShell, then reboot:

```powershell
New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
```

This is a system setting, so it needs to be done by hand — deliberately not
automated here.

**B. Shorten the PlatformIO package root** (no system change)

Add to `platformio.ini`:

```ini
[platformio]
core_dir = C:/pio
```

Trims ~20 characters off every package path, which is enough to clear the limit.
Cost: PlatformIO re-downloads everything into the new root (~1.4 GB currently
under `~/.platformio/packages`).

## Recommendation

Take option **A**, then rerun `pio run -e esp32h2` on this branch. If it builds,
the follow-up work is worth doing in its own right:

1. Swap `onLightChange` → `onLightChangeRgb` (the old name is now deprecated).
2. Add `onLightChangeTemp` and drop `rgb_to_cct` inference in favour of real CCT;
   `light_state.h`'s `MODE_COLOR` white branch becomes exact rather than heuristic.
3. Add `addOTAClient()` + `requestOTAUpdate()` to close the OTA gap, then build an
   `.ota` image per the README and test through Z2M.

Until then, `main` stays on 3.1.0, which builds and works — just with inferred
white balance and no Zigbee OTA.

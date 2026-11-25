# Rev B bench results -- first board (2026-11-25)

Board: IEEE `0x1051dbfffe6435ce`, firmware 2.1.1 (flashed over USB, blank flash from JLC),
ESP32-H2-MINI-1-H4S. Pin map in `src/config.h` matches the rev B board file; no firmware change.
Z2M converter `lumary-brain-revA.js` matches the device unchanged.

- [x] Boots on the H4S (chip v1.2), flashes and joins Z2M, interview passes
- [x] Stock harness mates with J2 -- **but assembled 180 deg flipped; harness re-pinned** (see
      `docs/rev-c-fixes.md` #1)
- [x] Ring colour order (R/G/B), solid colours
- [x] Downlight low end (brightness 1-10) and ramp
- [x] Colour temperature sweep and dimming
- [x] Both sources at once; ring effect over a lit downlight
- [x] All six effects
- [x] Standalone cold boot from the driver with no USB; rejoins
- [~] Power-up: downlight flashes on ~1 s before the firmware gates it (`docs/rev-c-fixes.md` #2)
- [x] Persistence across a power cycle
- [x] Heat check, full-on downlight
- [ ] Zigbee OTA on ESP32-H2 v1.2 silicon
- [ ] Inovelli binding from the installed location

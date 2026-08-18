#pragma once
#include <stdint.h>

// Timing for the Identify overlay. Deliberately free of ESP-IDF headers so it
// can be unit-tested on the host, like light_state.h.
//
// Identify is an overlay, not a light state: nothing in FixtureState changes
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

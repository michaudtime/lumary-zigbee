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

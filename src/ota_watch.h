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

// ZCL "none": the stack resets the OTA client's FileOffset to this when a
// session ends (before/as ImageUpgradeStatus drops), so a sample taken right
// at abort time reports it instead of the last real offset downloaded.
#define OTA_FILE_OFFSET_NONE 0xFFFFFFFFu

// Backoff for repeated recoveries -- see ota_recover_delay_ms() below.
#define OTA_RECOVER_BACKOFF_FROM    3u          // first recovery number that waits
#define OTA_RECOVER_BACKOFF_BASE_MS 300000u     // 5 min at recovery #3
#define OTA_RECOVER_BACKOFF_MAX_MS  3600000u    // 60 min ceiling

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
    uint32_t progress_offset;     // last real FileOffset seen while downloading
};

inline void ota_watch_init(OtaWatch* w) {
    w->seen_download     = false;
    w->completed         = false;
    w->fired             = false;
    w->in_abort          = false;
    w->abort_since_ms    = 0;
    w->last_offset       = 0;
    w->offset_changed_ms = 0;
    w->progress_offset   = OTA_FILE_OFFSET_NONE;
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
        if (offset != OTA_FILE_OFFSET_NONE) w->progress_offset = offset;
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

// The last real FileOffset seen while downloading, OTA_FILE_OFFSET_NONE if
// none was seen (e.g. an abort reported before any DOWNLOADING sample landed).
inline uint32_t ota_watch_progress(const OtaWatch* w) { return w->progress_offset; }

// How long to wait before restarting for recovery number `recovery` (the count
// this recovery will record: prior consecutive recoveries + 1).
// #1, #2 -> 0 (immediately); #3 -> 5 min; then doubling; #7 and later -> 60 min.
inline uint32_t ota_recover_delay_ms(uint16_t recovery) {
    if (recovery < OTA_RECOVER_BACKOFF_FROM) return 0;
    uint32_t shift = uint32_t(recovery - OTA_RECOVER_BACKOFF_FROM);
    // Cap before shifting: shifting a uint32_t by >= 32 is undefined, and the
    // result already saturates to the ceiling well before shift reaches 32.
    if (shift > 6) shift = 6;
    const uint32_t delay = OTA_RECOVER_BACKOFF_BASE_MS << shift;
    return delay > OTA_RECOVER_BACKOFF_MAX_MS ? OTA_RECOVER_BACKOFF_MAX_MS : delay;
}

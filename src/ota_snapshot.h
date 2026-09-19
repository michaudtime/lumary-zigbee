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

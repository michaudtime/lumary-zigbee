// Native (host) tests for the OTA abort/stall watcher.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include "ota_watch.h"

static OtaWatch w;

void setUp(void) { ota_watch_init(&w); }
void tearDown(void) {}

// Feeds one sample per second for `secs` seconds starting at t0. The offset
// advances by `step` bytes per sample. Returns the first non-NONE reason and,
// via *fired_at, when it fired (0 if it never did).
static OtaRecoverReason feed(uint8_t status, uint32_t* offset, uint32_t step,
                             uint32_t t0, uint32_t secs, uint32_t* fired_at) {
    for (uint32_t i = 0; i < secs; i++) {
        const uint32_t now = t0 + i * 1000u;
        const OtaRecoverReason r = ota_watch_step(&w, status, *offset, now);
        *offset += step;
        if (r != OTA_RECOVER_NONE) { if (fired_at) *fired_at = now; return r; }
    }
    if (fired_at) *fired_at = 0;
    return OTA_RECOVER_NONE;
}

void test_idle_never_triggers(void) {
    uint32_t off = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 0, 3600, nullptr));
}

void test_advancing_download_never_triggers(void) {
    uint32_t off = 0;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 3600, nullptr));
}

void test_abort_triggers_after_the_grace(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);   // t = 0 .. 59 s
    // Status falls to NORMAL at t = 60 s; the grace is 5 s.
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 60000, 30, &at));
    TEST_ASSERT_EQUAL_UINT32(65000, at);
}

void test_brief_flicker_does_not_trigger(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 60000, 3, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 63000, 600, nullptr));
}

void test_stall_triggers_at_180_s(void) {
    uint32_t off = 1234, at = 0;
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 0, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(180000, at);
}

void test_stall_timer_restarts_when_the_offset_moves(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 0, 170, nullptr);   // stuck 169 s
    off += 50;                                                      // one block lands
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 170000, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(350000, at);                           // 180 s after the block
}

void test_no_trigger_without_a_download_this_boot(void) {
    // A fixture that boots into NORMAL and stays there must never recover, even
    // if the offset attribute holds a stale value.
    uint32_t off = 5000;
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 0, 600, nullptr));
}

void test_complete_is_terminal(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    feed(OTA_IMAGE_STATUS_COMPLETE, &off, 0, 60000, 5, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 65000, 600, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, 665000, 600, nullptr));
}

void test_unknown_status_neither_triggers_nor_resets(void) {
    uint32_t off = 0, at = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 60, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, feed(3, &off, 0, 60000, 600, nullptr));
    // The download having been seen is remembered through the unknown status.
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 660000, 30, &at));
}

void test_fires_only_once(void) {
    uint32_t off = 0;
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, 0, 10, nullptr);
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, 10000, 30, nullptr));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, ota_watch_step(&w, OTA_IMAGE_STATUS_NORMAL, off, 50000));
    TEST_ASSERT_EQUAL(OTA_RECOVER_NONE, ota_watch_step(&w, OTA_IMAGE_STATUS_DOWNLOADING, off, 900000));
}

// millis() wraps every ~49.7 days; these fixtures stay powered for months.
void test_abort_across_the_millis_wrap(void) {
    uint32_t off = 0, at = 0;
    const uint32_t t0 = 0xFFFFFFFFu - 30000u;                      // 30 s before the wrap
    feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 50, t0, 28, nullptr);
    const uint32_t fall = t0 + 28000u;                             // 2 s before the wrap
    TEST_ASSERT_EQUAL(OTA_RECOVER_ABORT, feed(OTA_IMAGE_STATUS_NORMAL, &off, 0, fall, 30, &at));
    TEST_ASSERT_EQUAL_UINT32(fall + 5000u, at);                    // lands after the wrap
}

void test_stall_across_the_millis_wrap(void) {
    uint32_t off = 77, at = 0;
    const uint32_t t0 = 0xFFFFFFFFu - 60000u;
    TEST_ASSERT_EQUAL(OTA_RECOVER_STALL, feed(OTA_IMAGE_STATUS_DOWNLOADING, &off, 0, t0, 400, &at));
    TEST_ASSERT_EQUAL_UINT32(t0 + 180000u, at);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_never_triggers);
    RUN_TEST(test_advancing_download_never_triggers);
    RUN_TEST(test_abort_triggers_after_the_grace);
    RUN_TEST(test_brief_flicker_does_not_trigger);
    RUN_TEST(test_stall_triggers_at_180_s);
    RUN_TEST(test_stall_timer_restarts_when_the_offset_moves);
    RUN_TEST(test_no_trigger_without_a_download_this_boot);
    RUN_TEST(test_complete_is_terminal);
    RUN_TEST(test_unknown_status_neither_triggers_nor_resets);
    RUN_TEST(test_fires_only_once);
    RUN_TEST(test_abort_across_the_millis_wrap);
    RUN_TEST(test_stall_across_the_millis_wrap);
    return UNITY_END();
}

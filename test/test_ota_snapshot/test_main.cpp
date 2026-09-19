// Native (host) tests for the OTA-recovery state snapshot.
// Run: scripts\run-native-tests.bat
#include <unity.h>
#include <string.h>
#include "ota_snapshot.h"

void setUp(void) {}
void tearDown(void) {}

static FixtureState distinctive(void) {
    FixtureState f;
    fixture_state_init(&f);
    f.down.on    = true;
    f.down.level = 60;
    f.down.cct   = 141;
    f.ring.on    = true;
    f.ring.level = 200;
    f.ring.hue   = 170;
    f.ring.sat   = 220;
    f.ring.scene = 4;
    f.ring.mode  = MODE_SCENE;
    return f;
}

static void assert_state_equal(const FixtureState* a, const FixtureState* b) {
    TEST_ASSERT_EQUAL(a->down.on, b->down.on);
    TEST_ASSERT_EQUAL_UINT8(a->down.level, b->down.level);
    TEST_ASSERT_EQUAL_UINT8(a->down.cct, b->down.cct);
    TEST_ASSERT_EQUAL(a->ring.on, b->ring.on);
    TEST_ASSERT_EQUAL_UINT8(a->ring.level, b->ring.level);
    TEST_ASSERT_EQUAL_UINT8(a->ring.hue, b->ring.hue);
    TEST_ASSERT_EQUAL_UINT8(a->ring.sat, b->ring.sat);
    TEST_ASSERT_EQUAL_UINT8(a->ring.scene, b->ring.scene);
    TEST_ASSERT_EQUAL(a->ring.mode, b->ring.mode);
}

void test_round_trip_preserves_everything(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 571694, 0);
    TEST_ASSERT_TRUE(ota_snapshot_valid(&s));
    assert_state_equal(&f, &s.state);
    TEST_ASSERT_EQUAL_UINT8(OTA_RECOVER_ABORT, s.reason);
    TEST_ASSERT_EQUAL_UINT32(571694, s.offset);
    TEST_ASSERT_EQUAL_UINT16(1, s.recoveries);
}

void test_recovery_count_carries_across(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_STALL, 100, 3);
    TEST_ASSERT_EQUAL_UINT16(4, s.recoveries);
}

void test_any_corrupted_byte_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    // Every byte, padding and checksum included. The layout has no trailing
    // padding after `checksum` (4+1+1+2+4+9 = 21, padded to 24, +4 = 28), so no
    // byte is outside the checksum's coverage.
    for (size_t i = 0; i < sizeof(s); i++) {
        OtaSnapshot copy = s;
        ((unsigned char*)&copy)[i] ^= 0x01;
        TEST_ASSERT_FALSE_MESSAGE(ota_snapshot_valid(&copy), "a flipped bit was accepted");
    }
    TEST_ASSERT_TRUE(ota_snapshot_valid(&s));   // the original is untouched
}

void test_wrong_magic_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    s.magic = 0x12345678u;
    s.checksum = ota_snapshot_checksum(&s);   // even with a matching checksum
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

void test_wrong_version_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    s.version = OTA_SNAPSHOT_VERSION + 1;
    s.checksum = ota_snapshot_checksum(&s);
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

void test_invalidated_record_is_rejected(void) {
    const FixtureState f = distinctive();
    OtaSnapshot s;
    ota_snapshot_save(&s, &f, OTA_RECOVER_ABORT, 1, 0);
    ota_snapshot_invalidate(&s);
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

// __NOINIT RAM after a power loss is arbitrary. Two common patterns:
void test_power_up_garbage_is_rejected(void) {
    OtaSnapshot s;
    memset(&s, 0x00, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
    memset(&s, 0xA5, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
    memset(&s, 0xFF, sizeof(s));
    TEST_ASSERT_FALSE(ota_snapshot_valid(&s));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_preserves_everything);
    RUN_TEST(test_recovery_count_carries_across);
    RUN_TEST(test_any_corrupted_byte_is_rejected);
    RUN_TEST(test_wrong_magic_is_rejected);
    RUN_TEST(test_wrong_version_is_rejected);
    RUN_TEST(test_invalidated_record_is_rejected);
    RUN_TEST(test_power_up_garbage_is_rejected);
    return UNITY_END();
}

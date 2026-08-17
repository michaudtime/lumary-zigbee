// Native (host) tests for the derived firmware version constants.
// Run: pio test -e native
#include <unity.h>
#include <string.h>
#include "version.h"

void setUp(void) {}
void tearDown(void) {}

// The OTA numbers must not move. The coordinator only offers images numbered
// above the running one, so a silent change here breaks updates in the field.

void test_zb_fw_version_is_unchanged(void) {
    TEST_ASSERT_EQUAL_HEX32(0x01000000, ZB_FW_VERSION);
}

void test_downloaded_version_is_running_plus_one(void) {
    TEST_ASSERT_EQUAL_HEX32(0x01000001, ZB_FW_VERSION_DL);
    TEST_ASSERT_EQUAL_HEX32(ZB_FW_VERSION + 1, ZB_FW_VERSION_DL);
}

// The string is what Home Assistant's device page shows. It must be the same
// three components as the OTA number, or the two drift again.

void test_version_string_matches_the_components(void) {
    TEST_ASSERT_EQUAL_STRING("1.0.0", FW_VERSION_STRING);
}

void test_date_code_is_eight_digits(void) {
    TEST_ASSERT_EQUAL_UINT32(8, strlen(FW_DATE_CODE));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zb_fw_version_is_unchanged);
    RUN_TEST(test_downloaded_version_is_running_plus_one);
    RUN_TEST(test_version_string_matches_the_components);
    RUN_TEST(test_date_code_is_eight_digits);
    return UNITY_END();
}

// Native (host) tests for the identify overlay's timing.
// Run: pio test -e native
#include <unity.h>
#include "identify.h"

void setUp(void) {}
void tearDown(void) {}

void test_active_before_the_deadline(void) {
    TEST_ASSERT_TRUE(identify_active(1000, 4000));
}

void test_inactive_after_the_deadline(void) {
    TEST_ASSERT_FALSE(identify_active(5000, 4000));
}

void test_inactive_exactly_on_the_deadline(void) {
    TEST_ASSERT_FALSE(identify_active(4000, 4000));
}

// ZCL: writing IdentifyTime = 0 means "stop identifying". The caller encodes
// that as a deadline of now, so it must read as inactive immediately rather
// than scheduling a zero-length blink.
void test_cancel_reads_as_inactive(void) {
    TEST_ASSERT_FALSE(identify_active(12345, 12345));
}

// millis() wraps about every 49.7 days and these fixtures stay powered for
// months. A naive now < until would leave the ring blinking for weeks when an
// identify command straddles the wrap.
void test_active_across_the_millis_wrap(void) {
    const uint32_t now   = 0xFFFFFF00;   // 256 ms before wrap
    const uint32_t until = now + 3000;   // wraps around to 0x00000B9C
    TEST_ASSERT_TRUE(identify_active(now, until));
    TEST_ASSERT_TRUE(identify_active(0x00000500, until));   // after the wrap, still inside
    TEST_ASSERT_FALSE(identify_active(0x00001000, until));  // after the wrap, past it
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_active_before_the_deadline);
    RUN_TEST(test_inactive_after_the_deadline);
    RUN_TEST(test_inactive_exactly_on_the_deadline);
    RUN_TEST(test_cancel_reads_as_inactive);
    RUN_TEST(test_active_across_the_millis_wrap);
    return UNITY_END();
}

// Native (host) tests for the NZR bit-encoder and colour helpers.
// Run: pio test -e native
#include <unity.h>
#include <string.h>
#include "pixel_encode.h"

void setUp(void) {}
void tearDown(void) {}

// One NZR "0" encodes to SPI bits 100, a "1" to 110. Eight "0" bits therefore
// repeat 100 -> 0x92 0x49 0x24, and eight "1" bits repeat 110 -> 0xDB 0x6D 0xB6.
static const uint8_t ZERO_BYTE[3] = {0x92, 0x49, 0x24};
static const uint8_t ONES_BYTE[3] = {0xDB, 0x6D, 0xB6};

void test_encoded_size_is_three_spi_bytes_per_colour_byte(void) {
    TEST_ASSERT_EQUAL_UINT32(9,   pixel_encode_size(1));    // 1 px * 3 bytes * 3
    TEST_ASSERT_EQUAL_UINT32(558, pixel_encode_size(62));   // the real ring
}

void test_black_pixel_encodes_as_all_zero_bits(void) {
    uint8_t buf[9];
    const CRGB px[1] = {{0, 0, 0}};
    pixel_encode(px, 1, buf, sizeof(buf));
    for (int byte_i = 0; byte_i < 3; byte_i++)
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + byte_i * 3, 3);
}

void test_green_is_sent_first(void) {
    uint8_t buf[9];
    const CRGB px[1] = {{0, 0xFF, 0}};       // green only
    pixel_encode(px, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONES_BYTE, buf + 0, 3);   // slot 0 == green
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 3, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 6, 3);
}

void test_red_is_sent_second(void) {
    uint8_t buf[9];
    const CRGB px[1] = {{0xFF, 0, 0}};       // red only
    pixel_encode(px, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 0, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONES_BYTE, buf + 3, 3);   // slot 1 == red
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 6, 3);
}

void test_blue_is_sent_third(void) {
    uint8_t buf[9];
    const CRGB px[1] = {{0, 0, 0xFF}};       // blue only
    pixel_encode(px, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 0, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + 3, 3);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONES_BYTE, buf + 6, 3);   // slot 2 == blue
}

void test_pixels_are_encoded_back_to_back(void) {
    uint8_t buf[18];
    const CRGB px[2] = {{0, 0xFF, 0}, {0, 0, 0}};
    pixel_encode(px, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONES_BYTE, buf + 0, 3);   // px0 green
    for (int byte_i = 1; byte_i < 6; byte_i++)              // rest is zeros
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BYTE, buf + byte_i * 3, 3);
}

void test_trailing_buffer_is_left_zero_as_reset_pulse(void) {
    uint8_t buf[9 + 20];
    memset(buf, 0xAA, sizeof(buf));                          // poison
    const CRGB px[1] = {{0xFF, 0xFF, 0xFF}};
    pixel_encode(px, 1, buf, sizeof(buf));
    for (size_t i = 9; i < sizeof(buf); i++)
        TEST_ASSERT_EQUAL_UINT8(0x00, buf[i]);               // latch/reset
}

void test_encode_never_writes_past_a_short_buffer(void) {
    uint8_t buf[12];
    memset(buf, 0x00, sizeof(buf));
    const CRGB px[4] = {{0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
                        {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}};
    pixel_encode(px, 4, buf, sizeof(buf));                   // needs 36, has 12
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ONES_BYTE, buf + 0, 3);    // wrote what it could
}

void test_zero_saturation_gives_neutral_white(void) {
    const CRGB c = hsv_to_rgb(0, 0, 200);
    TEST_ASSERT_EQUAL_UINT8(200, c.r);
    TEST_ASSERT_EQUAL_UINT8(200, c.g);
    TEST_ASSERT_EQUAL_UINT8(200, c.b);
}

void test_full_saturation_red_hue_is_red(void) {
    const CRGB c = hsv_to_rgb(0, 255, 255);
    TEST_ASSERT_EQUAL_UINT8(255, c.r);
    TEST_ASSERT_EQUAL_UINT8(0,   c.g);
    TEST_ASSERT_EQUAL_UINT8(0,   c.b);
}

void test_brightness_scaling_is_proportional(void) {
    const CRGB c = scale_brightness({200, 100, 50}, 128);
    TEST_ASSERT_EQUAL_UINT8(100, c.r);
    TEST_ASSERT_EQUAL_UINT8(50,  c.g);
    TEST_ASSERT_EQUAL_UINT8(25,  c.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_encoded_size_is_three_spi_bytes_per_colour_byte);
    RUN_TEST(test_black_pixel_encodes_as_all_zero_bits);
    RUN_TEST(test_green_is_sent_first);
    RUN_TEST(test_red_is_sent_second);
    RUN_TEST(test_blue_is_sent_third);
    RUN_TEST(test_pixels_are_encoded_back_to_back);
    RUN_TEST(test_trailing_buffer_is_left_zero_as_reset_pulse);
    RUN_TEST(test_encode_never_writes_past_a_short_buffer);
    RUN_TEST(test_zero_saturation_gives_neutral_white);
    RUN_TEST(test_full_saturation_red_hue_is_red);
    RUN_TEST(test_brightness_scaling_is_proportional);
    return UNITY_END();
}

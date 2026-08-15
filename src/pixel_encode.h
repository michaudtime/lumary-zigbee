#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "color.h"

// Bit-bangs the single-wire NZR protocol over SPI MOSI: each data bit becomes
// four SPI bits (1 -> 1100, 0 -> 1000), so a 3.2 MHz SPI clock yields the
// 800 kHz bit rate the pixels expect. Kept free of ESP-IDF headers so the
// encoding can be unit-tested on the host -- see test/test_pixel_encode.

// Wire order. WS2812/SK6812-family parts latch green first; if bring-up shows
// red and green swapped, build with -DPIXEL_WIRE_ORDER_GRB=0.
#ifndef PIXEL_WIRE_ORDER_GRB
#define PIXEL_WIRE_ORDER_GRB 1
#endif

// 4 SPI bits per NZR bit at 3.2 MHz: "0" -> 1000 (T0H 312 ns), "1" -> 1100
// (T1H 625 ns), 1.25 us period. The earlier 3-bits-at-2.4 MHz scheme gave
// T0H 417 ns, which is nominal for WS2812B but sits on the SK6812 family's
// 450 ns ceiling -- bring-up 2026-08-15 saw random single-pixel corruption
// consistent with '0' bits occasionally latching as '1'. A logic capture at
// both ends of the data lead proved the wire and the framing were perfect
// (1488 bits every frame, zero bit differences), leaving pulse width as the
// only candidate. 312/625 ns is centred for SK6812 and still inside WS2812B.
static const int kSpiBitsPerNzrBit = 4;
static const int kColourBytesPerPixel = 3;

// SPI bytes needed for `count` pixels, excluding the trailing reset padding.
inline size_t pixel_encode_size(uint16_t count) {
    return (size_t(count) * kColourBytesPerPixel * 8 * kSpiBitsPerNzrBit + 7) / 8;
}

inline void pixel_set_bit(uint8_t* buf, size_t pos) {
    buf[pos / 8] |= uint8_t(1 << (7 - (pos % 8)));
}

// Encodes `count` pixels into `buf`. Any space beyond the encoded pixels is left
// zeroed, which the strip reads as the latch/reset pulse. Writes are bounded by
// `buf_size`, so a short buffer truncates instead of overrunning.
inline void pixel_encode(const CRGB* leds, uint16_t count, uint8_t* buf, size_t buf_size) {
    memset(buf, 0, buf_size);
    const size_t capacity_bits = buf_size * 8;
    size_t out = 0;
    for (uint16_t i = 0; i < count; i++) {
#if PIXEL_WIRE_ORDER_GRB
        const uint8_t bytes[kColourBytesPerPixel] = {leds[i].g, leds[i].r, leds[i].b};
#else
        const uint8_t bytes[kColourBytesPerPixel] = {leds[i].r, leds[i].g, leds[i].b};
#endif
        for (int byte_i = 0; byte_i < kColourBytesPerPixel; byte_i++) {
            for (int b = 7; b >= 0; b--) {
                if (out + kSpiBitsPerNzrBit > capacity_bits) return;
                pixel_set_bit(buf, out);                       // leading 1
                if ((bytes[byte_i] >> b) & 1)
                    pixel_set_bit(buf, out + 1);               // 1 -> 110, 0 -> 100
                out += kSpiBitsPerNzrBit;                      // trailing 0 already clear
            }
        }
    }
}

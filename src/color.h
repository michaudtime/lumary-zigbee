#pragma once
#include <stdint.h>

struct CRGBW {
    uint8_t r, g, b, w;
};

inline uint8_t scale8(uint8_t val, uint8_t scale) {
    return (uint16_t(val) * scale) >> 8;
}

inline CRGBW hsv_to_rgbw(uint8_t hue, uint8_t sat, uint8_t val) {
    if (sat == 0) {
        return {0, 0, 0, val};
    }
    uint8_t region    = hue / 43;
    uint8_t remainder = (hue % 43) * 6;
    uint8_t p = scale8(val, 255 - sat);
    uint8_t q = scale8(val, 255 - scale8(sat, remainder));
    uint8_t t = scale8(val, 255 - scale8(sat, 255 - remainder));
    switch (region) {
        case 0:  return {val, t,   p,   0};
        case 1:  return {q,   val, p,   0};
        case 2:  return {p,   val, t,   0};
        case 3:  return {p,   q,   val, 0};
        case 4:  return {t,   p,   val, 0};
        default: return {val, p,   q,   0};
    }
}

inline CRGBW scale_brightness(CRGBW c, uint8_t brightness) {
    return {
        scale8(c.r, brightness),
        scale8(c.g, brightness),
        scale8(c.b, brightness),
        scale8(c.w, brightness)
    };
}

inline CRGBW blend(CRGBW a, CRGBW b, uint8_t amount) {
    return {
        uint8_t(a.r + (int(b.r - a.r) * amount >> 8)),
        uint8_t(a.g + (int(b.g - a.g) * amount >> 8)),
        uint8_t(a.b + (int(b.b - a.b) * amount >> 8)),
        uint8_t(a.w + (int(b.w - a.w) * amount >> 8)),
    };
}

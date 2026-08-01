#pragma once
#include <stdint.h>

// The outer ring is a 3535 RGBIC strip (UT-08-ZC03-01-5V3535RGBIC): three colour
// dice per pixel, no white die. All white output comes from the separate inner
// CW/WW string on its own PWM channels -- never from these pixels.
struct CRGB {
    uint8_t r, g, b;
};

inline uint8_t scale8(uint8_t val, uint8_t scale) {
    return (uint16_t(val) * scale) >> 8;
}

inline CRGB hsv_to_rgb(uint8_t hue, uint8_t sat, uint8_t val) {
    if (sat == 0) {
        return {val, val, val};          // neutral white from all three dice
    }
    uint8_t region    = hue / 43;
    uint8_t remainder = (hue % 43) * 6;
    uint8_t p = scale8(val, 255 - sat);
    uint8_t q = scale8(val, 255 - scale8(sat, remainder));
    uint8_t t = scale8(val, 255 - scale8(sat, 255 - remainder));
    switch (region) {
        case 0:  return {val, t,   p  };
        case 1:  return {q,   val, p  };
        case 2:  return {p,   val, t  };
        case 3:  return {p,   q,   val};
        case 4:  return {t,   p,   val};
        default: return {val, p,   q  };
    }
}

// Approx. 2700K warm white mixed from RGB dice, for effects that want a soft
// glow out of the colour ring (the inner string handles real white).
inline CRGB warm_white(uint8_t level) {
    return {scale8(255, level), scale8(169, level), scale8(87, level)};
}

inline CRGB scale_brightness(CRGB c, uint8_t brightness) {
    return {
        scale8(c.r, brightness),
        scale8(c.g, brightness),
        scale8(c.b, brightness)
    };
}

inline CRGB blend(CRGB a, CRGB b, uint8_t amount) {
    return {
        uint8_t(a.r + (int(b.r - a.r) * amount >> 8)),
        uint8_t(a.g + (int(b.g - a.g) * amount >> 8)),
        uint8_t(a.b + (int(b.b - a.b) * amount >> 8)),
    };
}

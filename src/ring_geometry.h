#pragma once

// The ring's pixel count, split out of config.h so the effect renderer can be
// compiled and tested on the host.
//
// config.h includes driver/ledc.h, so anything that reaches for RING_NUM_LEDS
// through it drags in ESP-IDF and stops being host-testable. That is exactly
// what kept effects.cpp off the native test runner -- the file says so itself.
// recipe_render.h needs the pixel count and nothing else from config.h, so the
// count lives here and config.h includes this rather than the other way round.
//
// Deliberately not a general "hardware constants" header: only values that are
// pure geometry and free of peripheral headers belong here. RING_SPI_BUF_SIZE
// is derived from this count but is an SPI concern, so it stays in config.h.

// UT-08-ZC03-01-5V3535RGBIC: 62 pixels, 3 colour bytes each (no white die).
#define RING_NUM_LEDS 62

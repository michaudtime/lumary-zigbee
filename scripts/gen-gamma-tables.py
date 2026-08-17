#!/usr/bin/env python3
"""Regenerates the CIE lightness tables in src/brightness.h.

The tables are checked in rather than computed at runtime (the ESP32-H2 is
RISC-V with no FPU, so pow() is soft-float) and rather than constexpr-generated
(std::pow is not constexpr in C++17, which is what the host test runner uses).

test/test_brightness recomputes this same formula in double and asserts every
entry matches, so the checked-in tables cannot silently drift from this script.

Usage:  python scripts/gen-gamma-tables.py
Then paste each block between the corresponding markers in src/brightness.h.
"""

import math


def cie(brightness, out_max):
    """CIE 1931 lightness. Returns 0 only for an input of 0."""
    if brightness == 0:
        return 0
    lightness = brightness / 255.0 * 100.0
    if lightness <= 8.0:
        luminance = lightness / 903.3
    else:
        luminance = ((lightness + 16.0) / 116.0) ** 3
    # math.floor(... + 0.5) rather than Python's round(): round() is
    # banker's rounding (half-to-even), while the C reference in
    # test/test_brightness uses lround() (half-away-from-zero). No exact
    # ties exist today, so the tables agree either way, but a future
    # regeneration (e.g. at a different out_max) could land on one and the
    # two rounding modes would silently disagree.
    return max(1, math.floor(out_max * luminance + 0.5))


def emit(out_max, per_line, width):
    values = [cie(i, out_max) for i in range(256)]
    lines = []
    for start in range(0, 256, per_line):
        row = ", ".join(f"{v:{width}d}" for v in values[start:start + per_line])
        lines.append(f"    {row},")
    return "\n".join(lines)


print("// --- gamma8 table begin ---")
print(emit(255, 16, 3))
print("// --- gamma8 table end ---")
print()
print("// --- gamma12 table begin ---")
print(emit(4095, 12, 4))
print("// --- gamma12 table end ---")

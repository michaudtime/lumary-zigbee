#!/usr/bin/env python3
"""Regenerates tools/designer/gamma.mjs from the table in src/brightness.h.

The designer's simulator must apply the same gamma curve the firmware does,
entry for entry. Extracting the table rather than recomputing the CIE formula
in JavaScript keeps them identical by construction -- a recomputed curve would
differ by a count in a few places, and those places are at the bottom of the
range where the ring has the least resolution to spare.

Usage:  python scripts/gen-designer-gamma.py
"""

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "brightness.h"
OUT = ROOT / "tools" / "designer" / "gamma.mjs"

BANNER = """// GENERATED from src/brightness.h -- do not edit by hand.
// Regenerate with:  python scripts/gen-designer-gamma.py
//
// The CIE 1931 lightness curve the firmware applies, as a lookup table. The
// simulator has to use the same 256 entries rather than recompute the curve in
// floating point: the whole point of the golden vectors is that the browser and
// the fixture agree exactly, and a recomputed curve would disagree in the last
// count in a handful of places -- which is precisely where the low end lives.
"""


def main():
    block = re.search(
        r"--- gamma8 table begin ---.*?\{(.*?)\};", HEADER.read_text(), re.S
    )
    if block is None:
        raise SystemExit("could not find the gamma8 table markers in src/brightness.h")

    values = [int(v) for v in re.findall(r"\d+", block.group(1))]
    if len(values) != 256:
        raise SystemExit(f"expected 256 gamma entries, found {len(values)}")

    rows = [
        ", ".join(f"{v:3d}" for v in values[i : i + 16]) for i in range(0, 256, 16)
    ]
    body = ",\n    ".join(rows)
    OUT.write_text(f"{BANNER}export const GAMMA8 = Uint8Array.from([\n    {body},\n]);\n")
    print(f"wrote {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()

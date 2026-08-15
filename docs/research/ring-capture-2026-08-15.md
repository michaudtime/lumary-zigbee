# Outer-ring data capture — 2026-08-15

Raw export: [`ring-capture-2026-08-15.csv`](ring-capture-2026-08-15.csv) (17.6 MB,
transition-format: one row per edge, `Time [s], Channel 0, Channel 1`).

> **This is a capture of the FAULT, not of the fix.** It was taken while the firmware
> still used the 3-SPI-bits-at-2.4 MHz encoding, which is the configuration that produced
> ~11% corrupted frames. Do not use it as a reference for correct timing — the shipped
> encoding is 4 bits at 3.2 MHz. Its value is that it *exonerates* everything except
> pulse width.

## Conditions

| | |
|---|---|
| Board | lumary-brain rev A, MAC `74:4d:bd:6b:57:5f` |
| Analyser | Saleae Logic (original 8-ch), **24 MS/s**, 2 channels, no trigger |
| **Ch 0** | `J2` pin 6 (`DIM`) at the board — source end |
| **Ch 1** | ring `DIN` — far end of the hand-soldered flying lead |
| Ground | ring ground (deliberately *not* the board or PSU, to see what pixel 1 sees) |
| Rail | 4.7 V bench supply, module powered from USB |
| Commanded | red, brightness 25 → rendered `(23,0,0)` after the `MAX_BRIGHTNESS` clamp |
| Span | 5.056 s, 1,086,316 edges |
| Firmware | RGB wire order already fixed; **3 SPI bits @ 2.4 MHz**, `T0H` 417 ns |

Note the CSV logs a row whenever *either* channel changes, and the two channels sample
one period apart, so ~146k rows show a 41–42 ns "disagreement" that is pure sampling
skew. Rebuild each channel's own edge list before measuring anything.

## Findings

| Measurement | Result |
|---|---|
| Frames | 316 |
| Bits per frame | **1488 in every frame** — exactly 62 px × 24 bits, no drops or extras |
| Ch0 vs Ch1 bit differences | **0** across 470,208 bits |
| Decoded payload | `(23, 0, 0)` on every pixel — red, RGB order |
| Frame period | 16.00 ms median (min 10.94, max 19.21) |
| `T0H` | 417–458 ns (quantised at 41.7 ns) |
| `T1H` | 833–875 ns |

## What it proved

The wire, the encoder, the framing and the colour order are all correct — the ring
receives byte-identical data to what the board sends. That left **pulse width** as the
only remaining explanation for the observed corruption: `T0H` at 417 ns sits on the
SK6812 family's 450 ns ceiling, so `0` bits occasionally latch as `1`. Moving to
4 bits at 3.2 MHz (`T0H` 312 ns) fixed it.

Closes **P0.4**. The exact pixel part is still unmarked, but this timing behaviour rules
out plain WS2812B.

## Reproducing the analysis

`scripts/` does not carry the analysis script; it was ad-hoc. The essentials:

1. Rebuild per-channel transition lists (drop rows where *that* channel did not change).
2. High-pulse width > 625 ns ⇒ bit `1`, else bit `0`.
3. Split frames on low gaps > 40 µs.
4. Group decoded bits in 24s; each triple is one pixel, R/G/B in order.

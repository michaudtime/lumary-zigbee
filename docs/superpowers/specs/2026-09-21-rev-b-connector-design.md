# Rev B: a J2 that mates with the stock harness — design

**Date:** 2026-09-21
**Status:** approved, ready for implementation

Closes the J2 footprint mismatch first recorded in `docs/research/teardown-reference.md` and P0.1 of
`docs/superpowers/research/phase0-measurements.md`, and picks up the trace-width item deferred to
rev B in `hardware/calcs.md`.

## 1. Problem

### 1.1 J2 does not mate with CN1

Rev A was fabbed with J2 as a **Molex PicoBlade 1.25 mm 7-pin**
(`Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical`), chosen from an early reading of the stock
board. On 2026-09-04 the real connector was physically verified against purchased parts: CN1 is
**JST ZH, 1.5 mm pitch, 7-position**. The pitch is wrong, so the stock harness cannot plug into a
rev A board at all.

The workaround — a ZH1.5-7P-to-PicoBlade-7P pigtail — is built and working on Test Unit 2. It is
not a defect, but every board carries an extra adapter, extra crimps and two extra contact pairs in
the ring's current path.

### 1.2 Every power track is at minimum width

Measured directly from `hardware/kicad/lumary-brain.kicad_pcb`, **every routed segment on the board
is 0.20 mm** — including `+36V`, `+4V7`, `+4V7_IN`, `VBUS`, `CW_RET` and `WW_RET`. `GND` has no
segments at all; it is a pour.

`build_board.py` writes a `Power` net class of 0.5 mm into `lumary-brain.kicad_pro` and assigns
those nets to it, but the hand routing never honoured it. This is the "rev B should fix the trace
width" item in `hardware/calcs.md`, and it is broader than that note implies — it is not one ring
trace, it is the whole power distribution.

## 2. Goal

A board the stock harness plugs straight into, with power tracks at the width the design already
says they should be. Explicitly **not** a re-layout: everything that works stays where it is.

## 3. Design

### 3.1 J2 → JST ZH side entry

| | rev A | rev B |
|---|---|---|
| Footprint | `Connector_Molex:Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical` | `Connector_JST:JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal` |
| Pitch / pin span | 1.25 mm / 7.5 mm | 1.50 mm / 9.0 mm |
| Courtyard | 4.3 × 11.6 mm | 15.0 × 8.0 mm |
| Entry | vertical (top) | **side, harness over the board edge** |

Side entry, not vertical, for two reasons: it is what the stock board does — the solder tabs face
inboard and the harness exits over the board edge, visible in
`docs/research/stock-board-cn1-labels.jpg` — and J1 is already a horizontal JST-PH, so the board
gains consistency rather than losing it.

All three ZH 7-pin variants ship in stock KiCad, so no custom footprint is needed.

Net assignment is unchanged from `hardware/schematic-nets.md` §2.5:

| J2 pin | Label | Net | Harness wire |
|---|---|---|---|
| 1 | `V+` | `+36V` | black |
| 2 | `CW-` | `CW_RET` | white |
| 3 | `WW-` | `WW_RET` | yellow |
| 4 | `5V+` | `+4V7` | red |
| 5 | `GND` | `GND` | green |
| 6 | `DIM` | `RING_DATA` | blue |
| 7 | — | NC | (unpopulated) |

The wire-colour column is from P0.3 and was confirmed against the physical part on 2026-09-21: the
silkscreen order `V+ · CW- · WW- · 5V+ · GND · DIM` and the exiting wire colours agree.

### 3.2 Placement: centred on the board axis, at the rounded tip

The new courtyard is 8 mm deep against the old 4.3 mm. Left at J2's rev A position (x = 59.5, right
edge 62.06) it would reach x ≈ 64.0 on a board that ends at 63.3 — it overhangs. It must move
inboard, and the board's right end is a single 18 mm-radius arc, so every millimetre further right
costs vertical room.

**Superseded 2026-09-22:** the outline itself has since shrunk to 62.3 × 30.3 mm (0.5 mm inset per
side, see §4), because rev A's 63.3 × 31.3 mm outline fits the housing too tightly. Every "ends at
63.3" figure above is from the pre-shrink outline; it does not change where J2 sits (still
board-local (56.15, 15.15), rotation 90 — see `hardware/kicad/check_board.py`), only how much margin
is left around it.

**J2 goes on the board's Y midline (y ≈ 15.65), courtyard X ≈ 53.7–61.7**, rotated so the opening
faces +X. Centring on the midline is what buys the rightmost position, because that is where the arc
is widest: requiring the courtyard's corners at y = 15.65 ± 7.5 to fall inside the arc gives a
maximum right edge of x = 61.66, and less than that at any other Y. It also puts the harness exiting
straight off the rounded tip, which is what the stock board does.

The exact origin is settled at layout against DRC clearance; aim for ≥ 0.5 mm from the outline,
which pulls the right edge back to about x = 61.1.

**C1 moves ~2 mm left** (from `POS` 53.4 → ~51.4) to clear the new courtyard, staying adjacent to
J2 pin 1 so it still serves as the 36 V inrush bulk. Nothing else moves: `J3` ends at x = 47.25,
and `C4`/`Q3` sit at a different Y. `U3` clears in Y by ~0.6 mm and must be checked at layout.

### 3.3 Execution: edit the board, then sync the script

`hardware/kicad/build_board.py` seeds the board from the netlist — it places footprints, assigns
nets and draws the outline, then `board.Save()` writes `lumary-brain.kicad_pcb` **unconditionally**
and also overwrites `lumary-brain.kicad_pro`. Re-running it destroys all hand routing.

So rev B is a **surgical edit in KiCad**, not a regenerate:

1. In KiCad: delete J2, place the ZH footprint at the new position, re-assign its six nets, move C1,
   re-route what those two disturb.
2. Widen the power nets (§3.4).
3. Update `build_board.py`'s `COMPONENTS["J2"]`, `POS["J2"]` and `POS["C1"]` to match, so the script
   still describes the board it would seed.

The script's own docstring — "routing is done by hand in KiCad afterwards" — establishes that the
`.kicad_pcb` has always been the artifact of record, never a reproducible build output. Regenerating
would pay the full routing cost for a reproducibility the project does not have.

### 3.4 Power tracks to the width the design already specifies

Re-width the `Power` class nets (`+36V`, `+4V7`, `+4V7_IN`, `+3V3`, `VBUS`, `LDO_IN`, `CW_RET`,
`WW_RET`) from 0.20 mm to the class's **0.5 mm**, as space allows. `GND` needs nothing — it is a
pour.

**This does not raise the brightness cap, and the docs must not claim it does.** The limits, in
order:

| Limit | Rev A | Rev B |
|---|---|---|
| `+4V7` track (1 oz, 10 °C rise) | ~0.74 A ← binding | ~1.5 A |
| J2 contact rating | 1.0 A | 1.0 A ← **becomes binding** |
| Driver `+4V7` capacity | **never measured** | never measured |
| Ring cold-start, full white | 1.21 A | 1.21 A |

Widening moves the track out of the way and hands the constraint to the connector and to a supply
rail nobody has metered. It is worth doing anyway — 0.2 mm carrying 380 mA on `+36V` is thin, and
the cost while already re-routing is near zero — but the cap stays where it is until the `+4V7`
rail is measured. `hardware/calcs.md` line 30 should be corrected to say so.

### 3.5 Documentation

- `hardware/bom.csv` and `hardware/kicad/bom-jlc.csv`: J2 becomes a ZH 1.5 mm 7-position part
  (`S7B-ZR-SM4A-TF`). It has **no LCSC number and `VERIFY` status today** — it was never
  JLC-assembled. **Decision: keep J2 hand-fit**, matching rev A and the other blank-LCSC parts
  (`C1`, `C2`, `Q1`, `Q2`, `SW1`, `SW2`); the user already has ZH parts in hand. Revisit only if
  rev B moves to full JLC assembly, which is out of scope here.
- `hardware/kicad/footprints-and-pins.md`: J2 footprint name.
- `hardware/schematic-nets.md` §2.5: drop "J2 = PicoBlade 7-pos"; record that the physical 1..7
  order is now resolved.
- `docs/research/teardown-reference.md` and P0.1: mark the mismatch closed by rev B.
- `hardware/calcs.md`: record the 0.20 mm measurement and correct line 30.

## 4. Verification

Before releasing gerbers:

1. **DRC clean**, with attention to the J2/C1 and J2/U3 clearances called out in §3.2.
2. **Netlist assertion**: J2 pads 1–7 carry `+36V`, `CW_RET`, `WW_RET`, `+4V7`, `GND`, `RING_DATA`,
   NC. This is the failure that destroys boards — 36 V landing on the 4.7 V rail or the data line —
   so it is checked explicitly, not assumed from a successful placement.
3. **Track-width regression**: re-run the per-net width measurement over the `.kicad_pcb` and
   confirm the Power nets are no longer 0.20 mm.
4. **1:1 print** checked against the physical ZH connector already purchased.
5. **Outline shrunk 0.5 mm/side (superseded 2026-09-22):** the plan to leave the outline unchanged
   at 63.3 × 31.3 mm did not survive contact with the housing — rev A's board fits the can too
   tightly. The board is now **62.3 × 30.3 mm**, end arcs radius 17.5 mm about unchanged centres.
   The stock can/housing envelope itself is still 63.3 × 31.3 × 6.7 mm (`phase0-measurements.md`
   §P0.6, `2026-08-03-tht-board-design.md` line 32) — it is our board that shrank, not the can.

After fab, the first board is brought up on the bench against a real harness with no pigtail, and
the existing bench checklist re-run.

## 5. Risks

- **Silent script/board drift.** §3.3 leaves `build_board.py` describing a board it did not
  generate. Mitigated by updating it in the same commit, but it stays a standing hazard: anyone who
  re-runs the script loses the routing. Worth a warning banner in the script itself.
- **The 0.5 mm re-width may not fit everywhere** on a dense 63 × 31 mm two-layer board. Where it
  does not, leave the segment at 0.2 mm and record which — a partial widening is fine, an
  undocumented one is not.
- **`U3` clears J2 by only ~0.6 mm** in Y. If DRC objects, `U3` moves rather than J2 — J2's position
  is pinned by the arc.
- **The pin-1 physical order was never scanned** (P0.6: "no scan will be provided"). Rev B resolves
  it by construction. As built (board-local, post-shrink frame): all seven J2 pads sit at x =
  54.500, pitch 1.500 mm, span pad 1 → pad 7 = 9.000 mm — pad 1 (`+36V`) is the bottom-most at
  y = 19.650, pad 4 (`+4V7`) is the centre pad at y = 15.150 (orientation-independent either way),
  and pad 7 (NC) is the top-most at y = 10.650. The two MP mounting tabs sit at x = 58.200, y =
  21.600 and 8.700. Orientation is confirmed, not just inferred: a photo of the working rev A board
  shows the black `V+` wire at the bottom end, which is pad 1 — established in rev A's own
  pre-shrink frame (pads at x 59.50, pitch 1.25 mm, pad 1 at y 19.00) and carried forward unchanged
  into rev B's geometry. The harness's blank position lands on pad 7, at the top.

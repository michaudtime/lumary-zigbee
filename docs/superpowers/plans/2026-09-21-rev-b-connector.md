# Rev B Connector + Power-Track Widening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Respin the brain board so the stock CN1 harness plugs straight into J2, and route the power nets at the 0.5 mm the design already specifies.

**Architecture:** A surgical edit of `hardware/kicad/lumary-brain.kicad_pcb` in the KiCad GUI — swap J2's footprint, move it and the parts it displaces, widen the power tracks, re-route only what those disturb. `build_board.py` is then edited to *describe* the new board; it is never re-run. A new plain-CPython checker (`hardware/kicad/check_board.py`) reads the `.kicad_pcb` directly and asserts the things that matter, so each task has a red/green gate that does not depend on a human reading the layout.

**Tech Stack:** KiCad 10.0 (`C:\Program Files\KiCad\10.0\bin\` — `pcbnew.exe`, `kicad-cli.exe`), CPython 3.13 (`python` on PATH, no pcbnew import), git.

**Spec:** `docs/superpowers/specs/2026-09-21-rev-b-connector-design.md`

## Global Constraints

- **J2 footprint:** `Connector_JST:JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal` (side entry, SMT, ships in stock KiCad 10).
- **J2 pad → net (from spec §3.1, unchanged from `hardware/schematic-nets.md` §2.5):** 1=`+36V` (V+), 2=`CW_RET` (CW−), 3=`WW_RET` (WW−), 4=`+4V7` (5V+), 5=`GND`, 6=`RING_DATA` (DIM), 7=NC. The two `MP` mounting tabs carry no net.
- **Board outline (CHANGED 2026-09-22):** **62.3 × 30.3 mm**, end arcs **R = 17.5** about unchanged centres (file x 135.607447 and 162.907447, y 96.75), straight edges at file y 81.6 and 111.9. This is a deliberate 0.5 mm-per-side inset, taken because rev A fits the housing too tightly; it was NOT part of the original spec, which called the outline unchanged. Nothing else moved. **Board-local coordinates therefore shifted by −0.5 in both axes** — the checker derives its origin from the outline bbox, so every part reads 0.5 mm lower in x and y than it did before, without having moved.
- **Power nets to 0.5 mm:** `+36V`, `+4V7`, `+4V7_IN`, `+3V3`, `VBUS`, `LDO_IN`, `CW_RET`, `WW_RET`. `GND` is a pour with no segments — nothing to do.
- **Never run `build_board.py` against the real board.** Its `board.Save(OUT)` is unconditional and it also overwrites `lumary-brain.kicad_pro`; re-running it destroys every hand-routed track. The `.kicad_pcb` is the artifact of record.
- **J2 stays hand-fit** — no LCSC number, `VERIFY` status, matching `C1`, `C2`, `Q1`, `Q2`, `SW1`, `SW2`. The user already has ZH parts in hand.
- **The widening does not raise the brightness cap** and no document may say it does. Track limit moves ~0.74 A → ~1.5 A; J2's 1.0 A/contact then binds, and the driver's `+4V7` capacity has never been measured.
- **Coordinate convention:** this plan quotes **board-local** mm (origin = top-left of the outline bounding box). The `.kicad_pcb` stores file coordinates: `file = local + (117.607447, 81.1)`. `check_board.py` derives the origin from Edge.Cuts itself, so it always reports board-local.

---

## Measured starting state

Taken from `hardware/kicad/lumary-brain.kicad_pcb` on 2026-09-21, before any edit:

| Fact | Value |
|---|---|
| J2 today | `Connector_Molex:Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical`, board-local (59.50, 19.00) rot 90, thru-hole |
| C1 today | `Capacitor_SMD:C_0805_2012Metric`, board-local (53.40, 20.85) rot 90 |
| Q3 today | `Package_TO_SOT_SMD:SOT-23`, board-local (51.50, 9.3375) rot 90 |
| Routed segment widths | 0.20 mm ×187, 0.12 ×146, 0.05 ×143, 0.10 ×93, 0.15 ×4 |
| Power-net segments, all at 0.20 mm | `+36V` 5, `+4V7` 13, `+4V7_IN` 10, `+3V3` 18, `VBUS` 22, `LDO_IN` 9, `CW_RET` 3, `WW_RET` 3 |
| ZH footprint courtyard (library) | 15.000 × 8.000 mm; x −7.5..+7.5, y −3.5..+4.5 in the footprint's own frame |
| ZH signal pads | 1..7 at local x −4.5..+4.5 step 1.5, y −1.65, size 0.7 × 2.7; `MP` tabs at (±6.45, 2.05) |

**Placement arithmetic (this plan's contribution over the spec).** The footprint's opening faces its own local +Y, and its pads sit at local y = −1.65.

KiCad's placement transform, **verified against this board's own copper** rather than assumed — see "Pad geometry, resolved from the routing" below — is `file = (fx + px·cosθ + py·sinθ, fy − px·sinθ + py·cosθ)`. At **θ = 90** that sends local +Y to file +X, which is what turns the opening toward the board's +X (the rounded tip), and sends local +X to file −Y. The courtyard then lands at `x = origin_x − 3.5 .. origin_x + 4.5` and `y = origin_y ± 7.5`.

Board right edge at height y is `x = 45.3 + sqrt(18² − (y − 15.65)²)`, i.e. **61.663 mm at y = 8.15 and y = 23.15** — the courtyard's corners. So `origin_x = 56.65` puts those corners 0.51 mm inside the outline, and the connector *body* (fab outline, ±6.75 × −2..+4) clears by ~1.3 mm.

**J2 rev B: board-local (56.65, 15.65) rot 90 → file (174.257447, 96.75).** Courtyard x 53.15..61.15, y 8.15..23.15. Pads sit 1.65 mm inboard of the origin, at board-local x = 55.00.

> Those board-local figures are **pre-shrink**. The file coordinates below are the ones to type and have not changed; after the 2026-09-22 outline inset, the same placement reads as board-local (56.15, 15.15) because the origin moved. Post-shrink clearances: J2 copper 1.534 mm to the edge, courtyard +0.071 mm.

**The spec's "nothing else moves" is wrong, and this plan corrects it.** Measured against that courtyard:

| Part | Courtyard (board-local) | vs new J2 |
|---|---|---|
| C1 | 52.42..54.38 × 19.15..22.55 | **overlaps by 1.23 mm** → moves, as the spec says |
| Q3 | 49.80..53.20 × 7.41..11.27 | **overlaps by 0.05 mm** → the spec missed this |
| C4 | 50.02..52.98 × 12.11..13.57 | 0.17 mm — legal, tight |
| U3 | 53.15..56.55 × 3.41..7.51 | 0.64 mm — matches the spec's "~0.6 mm" note |

Q3 cannot move far left: **D1's courtyard ends at x = 49.30** and the two overlap in y (7.41..7.95). Q3 left by 0.35 mm (to board-local 51.15) clears J2 by 0.30 mm and D1 by 0.15 mm. That is Task 2's default; Option B there is the fallback.

### Pad geometry, resolved from the routing (2026-09-22)

The rotation convention was not assumed — it was resolved by computing rev A's J2 pad positions under both candidate transforms and checking which one has the routed tracks landing on the pads. Under `file = (fx + px·cosθ + py·sinθ, fy − px·sinθ + py·cosθ)`, **five of five routable pads have a track endpoint 0.000 mm away**; under the opposite sign, one does (pad 1, which is invariant). The convention is settled.

Rev A's J2 pads, board-local, all at x = 59.50:

| pad | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| net | `+36V` | `CW_RET` | `WW_RET` | `+4V7` | `GND` | `RING_DATA` | NC |
| y | 19.00 | 17.75 | 16.50 | 15.25 | 14.00 | 12.75 | 11.50 |

**This contradicts the spec and `phase0-measurements.md` P0.3/P0.6, and they are wrong.** The spec's §5 says "pad 7 is the pad nearest the board edge (4.8 mm) and pad 1 is 7.5 mm inboard", offering that as a self-checking assembly rule. Neither number appears in the geometry: pad 7 is 11.50 mm from the top edge and 3.32 mm from the curved right edge; pad 1 is 12.30 mm from the bottom edge and 3.49 mm from the curved edge. The ordering is also inverted — **pad 1 is the bottom-most pad, not pad 7**. Task 6 corrects those documents.

The practical consequence is the opposite of a problem: pad 1 sits below the midline on rev A *and* on rev B, so the orientation carries over unchanged.

---

## File Structure

| File | Responsibility |
|---|---|
| `hardware/kicad/check_board.py` | **new.** Parses the `.kicad_pcb` s-expression in plain CPython and asserts footprint, placement, pad nets, track widths, and outline; also that `build_board.py` and the docs still describe the same board. Three check groups (`board`, `script`, `docs`) so a task can gate on its own group. |
| `hardware/kicad/lumary-brain.kicad_pcb` | The board. Edited in the KiCad GUI only. |
| `hardware/kicad/build_board.py` | Seeder script. Edited to match the board (`COMPONENTS["J2"]`, `POS["J2"]`, `POS["C1"]`, `POS["Q3"]`) plus a banner warning that running it is destructive. |
| `hardware/kicad/drc.json` | DRC report, regenerated by `kicad-cli`. |
| `hardware/kicad/cpl.csv`, `pos-raw.csv`, `lumary-brain-gerbers.zip` | Fab outputs. J2 and C1 move, so all three are stale until regenerated. |
| `hardware/bom.csv`, `hardware/kicad/bom-jlc.csv` | J2 becomes the ZH part, still hand-fit. |
| `hardware/kicad/footprints-and-pins.md`, `hardware/schematic-nets.md`, `hardware/calcs.md` | Board-describing docs. |
| `docs/research/teardown-reference.md`, `docs/superpowers/research/phase0-measurements.md` | Close the CN1 mismatch finding. |

---

### Task 1: The board checker, green against rev A

Build the gate first and prove it reads the *current* board correctly. A checker written after the edit only proves it agrees with itself.

**Files:**
- Create: `hardware/kicad/check_board.py`
- Reads: `hardware/kicad/lumary-brain.kicad_pcb`, `hardware/kicad/build_board.py`, `hardware/kicad/footprints-and-pins.md`, `hardware/bom.csv`

**Interfaces:**
- Consumes: nothing.
- Produces: `python hardware/kicad/check_board.py [--only board|script|docs]`, exit 0 when every check passes, 1 otherwise. Every later task flips values in its `EXPECT` dict and re-runs it. `EXPECT` keys: `j2_footprint`, `j2_pos`, `j2_pads`, `c1_pos`, `q3_pos`, `power_nets`, `min_power_width`, `width_exceptions`, `board_size`, `j2_bom_keyword`.

- [ ] **Step 1: Write the checker, with `EXPECT` describing rev A as it is today**

```python
#!/usr/bin/env python
"""Static checks over lumary-brain.kicad_pcb and the docs that describe it.

Plain CPython -- no pcbnew, no KiCad needed:

    python hardware/kicad/check_board.py            # every group
    python hardware/kicad/check_board.py --only board

Groups: board (the .kicad_pcb itself), script (build_board.py agrees with it),
docs (BOM + footprint table agree with it). Exit status is 1 if anything failed.
"""
import argparse
import ast
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
PCB = os.path.join(HERE, "lumary-brain.kicad_pcb")
SCRIPT = os.path.join(HERE, "build_board.py")

EXPECT = {
    "j2_footprint": "Connector_Molex:Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical",
    "j2_pos": (59.5, 19.0, 90.0),
    "j2_pads": {"1": "+36V", "2": "CW_RET", "3": "WW_RET", "4": "+4V7",
                "5": "GND", "6": "RING_DATA", "7": None},
    "c1_pos": (53.4, 20.85, 90.0),
    "q3_pos": (51.5, 9.3375, 90.0),
    "power_nets": ["+36V", "+4V7", "+4V7_IN", "+3V3", "VBUS", "LDO_IN", "CW_RET", "WW_RET"],
    "min_power_width": 0.2,
    # net -> (allowed_uuids, why). Segments with UUIDs in the list are allowed to be thin.
    "width_exceptions": {},
    "board_size": (63.3, 31.3),
    "j2_bom_keyword": "PicoBlade",
}
TOL = 0.02
TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|[()]|[^\s()]+')


def parse(text):
    root = cur = []
    stack = []
    for t in TOKEN.findall(text):
        if t == "(":
            n = []
            cur.append(n)
            stack.append(cur)
            cur = n
        elif t == ")":
            cur = stack.pop()
        elif t.startswith('"'):
            cur.append(t[1:-1].replace('\\"', '"'))
        else:
            cur.append(t)
    return root[0]


def kids(node, tag):
    return [c for c in node if isinstance(c, list) and c and c[0] == tag]


def first(node, tag):
    k = kids(node, tag)
    return k[0] if k else None


def fp_ref(fp):
    for p in kids(fp, "property"):
        if len(p) > 2 and p[1] == "Reference":
            return p[2]
    return None


class Report:
    def __init__(self):
        self.fails = []

    def check(self, ok, msg):
        print(("PASS  " if ok else "FAIL  ") + msg)
        if not ok:
            self.fails.append(msg)

    def note(self, msg):
        print("note  " + msg)


def outline_points(board):
    pts = []
    for sh in kids(board, "gr_line") + kids(board, "gr_arc"):
        lay = first(sh, "layer")
        if not lay or lay[1] != "Edge.Cuts":
            continue
        for tag in ("start", "mid", "end"):
            p = first(sh, tag)
            if p:
                pts.append((float(p[1]), float(p[2])))
    return pts


def check_board(rep):
    board = parse(open(PCB, encoding="utf-8").read())
    fps = {fp_ref(f): f for f in kids(board, "footprint")}

    pts = outline_points(board)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    ox, oy = min(xs), min(ys)
    w, h = max(xs) - ox, max(ys) - oy
    ew, eh = EXPECT["board_size"]
    rep.check(abs(w - ew) < TOL and abs(h - eh) < TOL,
              "outline %.3f x %.3f mm (expect %s x %s)" % (w, h, ew, eh))

    def local(fp):
        at = first(fp, "at")
        rot = float(at[3]) if len(at) > 3 else 0.0
        return (round(float(at[1]) - ox, 4), round(float(at[2]) - oy, 4), rot % 360)

    def at_check(ref, key):
        fp = fps.get(ref)
        if fp is None:
            rep.check(False, "%s present" % ref)
            return
        got, exp = local(fp), EXPECT[key]
        exp = (exp[0], exp[1], exp[2] % 360)
        rep.check(all(abs(a - b) < TOL for a, b in zip(got, exp)),
                  "%s at board-local %s (expect %s)" % (ref, got, exp))

    j2 = fps.get("J2")
    rep.check(j2 is not None, "J2 present")
    if j2:
        rep.check(j2[1] == EXPECT["j2_footprint"], "J2 footprint %s" % j2[1])
        pads = {}
        for pad in kids(j2, "pad"):
            if pad[1] == "MP":          # mounting tabs carry no net
                continue
            n = first(pad, "net")
            pads[pad[1]] = n[-1] if n else None
        rep.check(pads == EXPECT["j2_pads"], "J2 pad nets %s" % pads)
    at_check("J2", "j2_pos")
    at_check("C1", "c1_pos")
    at_check("Q3", "q3_pos")

    widths = {}
    uuids = {}
    for seg in kids(board, "segment"):
        n = first(seg, "net")
        if not n:
            continue
        net_name = n[-1]  # Handle both (net "NAME") and (net 3 "NAME")
        u = first(seg, "uuid")
        seg_uuid = u[1] if u else None
        w = float(first(seg, "width")[1])
        widths.setdefault(net_name, []).append(w)
        if seg_uuid:
            uuids.setdefault(net_name, []).append((w, seg_uuid))
    minw = EXPECT["min_power_width"]
    for net in EXPECT["power_nets"]:
        ws = widths.get(net, [])
        if not ws:
            rep.check(False, "%s: no routed segments" % net)
            continue
        thin_segs = [(w, u) for w, u in uuids.get(net, []) if w < minw - 1e-9]
        thin = [w for w in ws if w < minw - 1e-9]
        allowed_uuids, why = EXPECT["width_exceptions"].get(net, ([], ""))
        offending = [u for w, u in thin_segs if u not in allowed_uuids]
        ok = len(offending) == 0
        detail = "" if not offending else "  [offending: %s%s]" % (
            ", ".join(offending), ": " + why if why else "")
        rep.check(ok, "%s: %d segs, min %.2f mm (need >= %s)%s"
                  % (net, len(ws), min(ws), minw, detail))


def literal(src, name):
    i = src.index(name + " = {")
    j = src.index("{", i)
    depth, k = 0, j
    while True:
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                break
        k += 1
    return ast.literal_eval(src[j:k + 1])


def check_script(rep):
    src = open(SCRIPT, encoding="utf-8").read()
    comps, pos = literal(src, "COMPONENTS"), literal(src, "POS")
    got = "%s:%s" % (comps["J2"][0], comps["J2"][1])
    rep.check(got == EXPECT["j2_footprint"], "build_board.py COMPONENTS['J2'] -> %s" % got)
    for ref, key in (("J2", "j2_pos"), ("C1", "c1_pos"), ("Q3", "q3_pos")):
        g, exp = tuple(pos[ref]), EXPECT[key]
        rep.check(all(abs(a - b) < TOL for a, b in zip(g, exp)),
                  "build_board.py POS['%s'] = %s (expect %s)" % (ref, g, exp))


def check_docs(rep):
    fpdoc = open(os.path.join(HERE, "footprints-and-pins.md"), encoding="utf-8").read()
    rep.check(EXPECT["j2_footprint"] in fpdoc, "footprints-and-pins.md names the J2 footprint")
    rows = [l for l in open(os.path.join(ROOT, "hardware", "bom.csv"), encoding="utf-8")
            if l.startswith("J2,")]
    rep.check(bool(rows) and EXPECT["j2_bom_keyword"] in rows[0],
              "bom.csv J2 row mentions %s" % EXPECT["j2_bom_keyword"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", choices=["board", "script", "docs"], action="append")
    args = ap.parse_args()
    groups = args.only or ["board", "script", "docs"]
    rep = Report()
    for g in groups:
        print("--- %s ---" % g)
        {"board": check_board, "script": check_script, "docs": check_docs}[g](rep)
    print("\n%d failure(s)" % len(rep.fails))
    return 1 if rep.fails else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it — every check must pass on the untouched board**

Run: `python hardware/kicad/check_board.py`

Expected: **21** `PASS` lines across the three groups (15 board + 4 script + 2 docs), `0 failure(s)`, exit 0. In particular `Q3 at board-local (51.5, 9.3375, 90.0)` and eight power nets reporting `min 0.20 mm`. No `note` lines — every power net has routed segments today.

- [ ] **Step 3: Prove the checker is not vacuous**

Temporarily change one expectation and confirm it goes red:

```bash
python - <<'PY'
import re, pathlib
p = pathlib.Path("hardware/kicad/check_board.py")
s = p.read_text(encoding="utf-8")
p.write_text(s.replace('"j2_pos": (59.5, 19.0, 90.0)', '"j2_pos": (59.5, 18.0, 90.0)'), encoding="utf-8")
PY
python hardware/kicad/check_board.py --only board
```

Expected: `FAIL  J2 at board-local (59.5, 19.0, 90.0) (expect (59.5, 18.0, 90.0))` and `1 failure(s)`, exit 1.

- [ ] **Step 4: Restore the correct expectation and confirm green again**

```bash
python - <<'PY'
import pathlib
p = pathlib.Path("hardware/kicad/check_board.py")
s = p.read_text(encoding="utf-8")
p.write_text(s.replace('"j2_pos": (59.5, 18.0, 90.0)', '"j2_pos": (59.5, 19.0, 90.0)'), encoding="utf-8")
PY
python hardware/kicad/check_board.py
```

Expected: `0 failure(s)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add hardware/kicad/check_board.py
git commit -m "test: add a static checker for the brain board

Reads lumary-brain.kicad_pcb directly -- plain CPython, no pcbnew -- and
asserts footprint, placement, pad nets, power-track widths and the outline,
plus that build_board.py and the BOM still describe the same board. Green
against rev A as it stands, so the rev B edits have a gate to move.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Swap J2 to the ZH footprint and place it

**Files:**
- Modify: `hardware/kicad/lumary-brain.kicad_pcb` (KiCad GUI)
- Modify: `hardware/kicad/check_board.py` (`EXPECT`: `j2_footprint`, `j2_pos`, `c1_pos`, `q3_pos`)

**Interfaces:**
- Consumes: `check_board.py --only board` from Task 1.
- Produces: J2 at board-local (56.65, 15.65) rot 90 carrying the six nets; C1 at (51.40, 20.85) rot 90; Q3 at (51.15, 9.3375) rot 90. Tasks 3–7 assume these.

- [ ] **Step 1: Settle pin 1's side against the physical harness — before any CAD**

At rot 90, pad 1 sits at board-local y = 20.15 (**below the midline, toward the bottom edge**) and pad 7 at y = 11.15.

**RESOLVED 2026-09-22 — no dry-fit needed.** The user photographed the working rev A board with its pigtail fitted: the black `V+` wire is at the bottom end of the connector. Rev A's pad 1 is at board-local y = 19.00, also below the midline (see "Pad geometry, resolved from the routing"), so black `V+` = pad 1 on a board known to work. Rev B keeps pad 1 at the bottom, so **the Global Constraints mapping stands unchanged: pad 1 = `+36V`**, and the harness's empty position lands on pad 7, the top-most pad.

Dry-fit the purchased ZH 7-position connector against the stock harness and record which end the **black `V+` wire** occupies relative to the connector's keyed side. Write the answer into this step.

- If `V+` lands on the pad nearest the **bottom** edge → keep the Global-Constraints mapping (pad 1 = `+36V`).
- If it lands on the **top** → reverse the assignment: pad 7=`+36V`, 6=`CW_RET`, 5=`WW_RET`, 4=`+4V7`, 3=`GND`, 2=`RING_DATA`, 1=NC, and update `EXPECT["j2_pads"]` to match. Nothing else in the plan changes.

This is the failure that destroys boards (36 V onto the 4.7 V rail or the data line), so it is decided by looking at the part, not by inference.

- [ ] **Step 2: Flip `EXPECT` to rev B and watch it go red**

Edit `hardware/kicad/check_board.py`:

```python
    "j2_footprint": "Connector_JST:JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal",
    "j2_pos": (56.15, 15.15, 90.0),
    "c1_pos": (50.8926, 20.4, 90.0),
    "q3_pos": (50.65, 8.8375, 90.0),
    "board_size": (62.3, 30.3),
```

- [ ] **Step 3: Run the board group to confirm it fails, and that it fails for the right reasons**

Run: `python hardware/kicad/check_board.py --only board`

Expected: exit 1 with exactly four failures —
```
FAIL  J2 footprint Connector_Molex:Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical
FAIL  J2 at board-local (59.5, 19.0, 90.0) (expect (56.65, 15.65, 90.0))
FAIL  C1 at board-local (53.4, 20.85, 90.0) (expect (51.4, 20.85, 90.0))
FAIL  Q3 at board-local (51.5, 9.3375, 90.0) (expect (51.15, 9.3375, 90.0))
```
The outline, pad-net and width checks must still pass. If the outline check fails here, stop — something already moved that should not have.

- [ ] **Step 4: Make the edit in KiCad**

Open `pcbnew.exe` on `hardware/kicad/lumary-brain.kicad_pcb`. In file coordinates (KiCad's own display), the targets are:

| Part | File X | File Y | Rotation |
|---|---|---|---|
| J2 | 174.257447 | 96.75 | 90 |
| C1 | 169.007447 | 101.95 | 90 |
| Q3 | 168.757447 | 90.4375 | 90 |

1. Note J2's existing net assignment on screen, then delete J2 and the tracks that landed on it.
2. **Place → Add Footprint**, library `Connector_JST`, footprint `JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal`. Set Reference `J2`, Value `CN1`.
3. Open its properties and type the position and rotation from the table. Confirm on screen that the opening faces the rounded right tip.
4. Assign the six nets pad by pad, per Step 1's decision.
5. Move C1 and Q3 to their coordinates.
6. Re-route only what steps 1–5 disturbed: `+36V` (J2↔C1↔J1), `CW_RET`/`WW_RET` (J2↔Q1/Q2), `+4V7` (J2↔the rail), `GND` (pour — re-fill zones), `RING_DATA` (J2↔R5), and Q3's `+36V`/`+4V7_IN` legs. Route these at **0.5 mm** now; Task 3 handles the rest.
7. Save.

**If DRC objects to the Q3 or C4 clearance** (measured 0.30 mm and 0.17 mm to J2's courtyard at these coordinates): Option B is to move **J2** right by 0.25 mm to board-local (56.90, 15.65) — file X 174.507447 — which gives Q3 0.20 mm without touching it, at the cost of the outline-to-courtyard margin dropping 0.51 → 0.26 mm (the connector *body* still clears by ~1.1 mm). Do not move Q3 further left than board-local 51.15: **D1's courtyard ends at x = 49.30** and the two overlap in y. Whichever option is taken, put the as-placed coordinates into `EXPECT`.

- [ ] **Step 5: Run the board group to verify it passes**

Run: `python hardware/kicad/check_board.py --only board`

Expected: `0 failure(s)`, exit 0 — including `J2 pad nets {...}` matching `EXPECT["j2_pads"]`.

A full run (`python hardware/kicad/check_board.py`) is still red on the `script` and `docs` groups at this point; Tasks 5 and 6 close those.

- [ ] **Step 6: Commit**

```bash
git add hardware/kicad/lumary-brain.kicad_pcb hardware/kicad/check_board.py
git commit -m "fix: J2 becomes a JST ZH 1.5mm side-entry connector

The stock CN1 harness is JST ZH 1.5mm 7-position; rev A's PicoBlade 1.25mm J2
never mated with it. Swaps the footprint to S7B-ZR-SM4A-TF and places it at
board-local (56.65, 15.65) rot 90 -- on the Y midline at the rounded tip,
where the 18mm arc is widest, opening facing the board edge like the stock
board does.

C1 moves 2mm left as the spec called for, and Q3 0.35mm left, which the spec
missed: its courtyard overlapped the new one by 0.05mm. Q3 cannot go further
than this -- D1 is 0.15mm beyond.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Widen the power tracks to 0.5 mm

**Files:**
- Modify: `hardware/kicad/lumary-brain.kicad_pcb` (KiCad GUI)
- Modify: `hardware/kicad/check_board.py` (`EXPECT["min_power_width"]`, `EXPECT["width_exceptions"]`)

**Interfaces:**
- Consumes: the board from Task 2.
- Produces: every segment on the eight power nets at ≥ 0.5 mm, or listed in `width_exceptions` with a reason.

- [ ] **Step 1: Raise the expectation and watch it go red**

In `hardware/kicad/check_board.py`:

```python
    "min_power_width": 0.5,
```

- [ ] **Step 2: Run the board group to confirm it fails**

Run: `python hardware/kicad/check_board.py --only board`

Expected: exit 1, one failure per power net that still has 0.20 mm segments, e.g.
```
FAIL  +3V3: 18 segs, min 0.20 mm (need >= 0.5)  [offending: 0ffc0335-8a37-4088-8f38-5a702aab2637, 12b06e68-45bb-4eb2-a7fb-2c07cc7cca7f, ...]
```
(UUID list omitted here for brevity. `+36V`, `CW_RET`, `WW_RET`, `+4V7` and `RING_DATA` may already be partly green from Task 2's re-routing.)

- [ ] **Step 3: Widen them in KiCad**

For each of `+36V`, `+4V7`, `+4V7_IN`, `+3V3`, `VBUS`, `LDO_IN`, `CW_RET`, `WW_RET`: select the net (right-click a track → **Select → Select All Tracks in Net**), then **Edit → Edit Track & Via Properties**, set track width to 0.5 mm, apply. Re-route locally where the wider track now collides rather than leaving a DRC error.

Where 0.5 mm genuinely does not fit on a 63 × 31 mm two-layer board, leave that segment at 0.2 mm and record it — a partial widening is fine, an undocumented one is not.

- [ ] **Step 4: Record any segment that could not be widened**

For each exception, add an entry to `EXPECT["width_exceptions"]` with a list of the segment UUIDs that are allowed to stay thin and the reason:

```python
    "width_exceptions": {
        "+3V3": (["0ffc0335-8a37-4088-8f38-5a702aab2637", "12b06e68-45bb-4eb2-a7fb-2c07cc7cca7f"], "0.5mm will not pass between U1 pads 14/15"),
    },
```

Leave it `{}` if everything widened. To identify segment UUIDs, run `python hardware/kicad/check_board.py --only board` after widening — it names each offending UUID. Copy those UUIDs into the list, not a count; if a future edit fixes one UUID but introduces a different thin segment, both the fix and the regression are visible in the checker's output.

- [ ] **Step 5: Run the board group to verify it passes**

Run: `python hardware/kicad/check_board.py --only board`

Expected: `0 failure(s)`, exit 0. Any net with an exception prints `PASS` with its `[offending: <uuid>, <uuid>, ...: reason]` detail, so the compromise stays visible in the output.

- [ ] **Step 6: Commit**

```bash
git add hardware/kicad/lumary-brain.kicad_pcb hardware/kicad/check_board.py
git commit -m "fix: route the power nets at the 0.5mm the design specifies

Every routed segment on rev A was 0.20mm -- the Power net class in the
project file was never honoured by the hand routing. Widens +36V, +4V7,
+4V7_IN, +3V3, VBUS, LDO_IN, CW_RET and WW_RET to 0.5mm. GND is a pour and
needs nothing.

This moves the track limit ~0.74A -> ~1.5A. It does NOT raise the brightness
cap: J2 is rated 1.0A per contact and the driver's +4V7 capacity has never
been measured.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: DRC clean

**Files:**
- Modify: `hardware/kicad/drc.json` (regenerated)

**Interfaces:**
- Consumes: the board from Task 3.
- Produces: a DRC report with zero unwaived violations, committed alongside the board it describes.

- [ ] **Step 1: Run DRC from the command line**

```bash
"/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --format json --output hardware/kicad/drc.json --severity-error --refill-zones --exit-code-violations hardware/kicad/lumary-brain.kicad_pcb
```

`--refill-zones` matters here: `GND` is a pour, and Task 2 moved parts inside it. There is **no `--schematic-parity`** in this project — it has no `.kicad_sch` at all (the netlist lives in `hardware/schematic-nets.md` and was seeded by `build_board.py`), so parity has nothing to compare against. `check_board.py`'s pad-net assertion is what stands in for it.

- [ ] **Step 2: Read the violation count**

```bash
python -c "import json;d=json.load(open('hardware/kicad/drc.json'));print(sum(len(d.get(k,[])) for k in ('violations','unconnected_items')))"
```

Expected at first run: a non-zero count is likely — courtyard clearance around J2/Q3/C4 and any track the widening pushed into a neighbour. (`--exit-code-violations` also makes the command in Step 1 exit non-zero, so it can gate a script directly.)

- [ ] **Step 3: Fix each violation in KiCad and re-run until the count is zero**

Fix by moving the *smaller* part, never J2 — its position is pinned by the arc (spec §5). Re-run Steps 1–2 after each pass.

Expected on the final run: `0`.

- [ ] **Step 4: Confirm the board checker is still green**

Run: `python hardware/kicad/check_board.py --only board`

Expected: `0 failure(s)`. If a DRC fix moved C1, Q3 or J2, update `EXPECT` to the as-placed coordinates and say so in the commit message.

- [ ] **Step 5: Commit**

**`drc.json` is not committed** — `hardware/kicad/.gitignore:1` ignores it, and it has never been tracked. That is the repo's existing decision and this plan keeps it: the gate is the violation count you just read, not a committed report. Quote that count in the commit message instead.

```bash
git add hardware/kicad/lumary-brain.kicad_pcb hardware/kicad/check_board.py
git commit -m "test: DRC clean on the rev B layout

0 violations, 0 unconnected items. drc.json stays local -- it is gitignored.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Make `build_board.py` describe the board it would seed

**Files:**
- Modify: `hardware/kicad/build_board.py` (line 43 `COMPONENTS["J2"]`, line 82 `POS["J2"]`, line 90 `POS["C1"]`, `POS["Q3"]`, plus a banner at the top)

**Interfaces:**
- Consumes: the as-placed coordinates from Tasks 2–4.
- Produces: `check_board.py --only script` green.

- [ ] **Step 1: Run the script group to confirm it is red**

Run: `python hardware/kicad/check_board.py --only script`

Expected: exit 1 with four failures — the script still names the PicoBlade footprint and rev A's positions for J2, C1 and Q3.

- [ ] **Step 2: Update the component and position tables**

In `hardware/kicad/build_board.py`:

```python
 "J2": ("Connector_JST","JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal","CN1"),
```

```python
 "J2":(56.65, 15.65, 90.0),
```

```python
 "C1":(51.4, 20.85, 90.0),
```

and set `POS["Q3"]` to `(50.65, 8.8375, 90.0)`. Use the as-placed values if Task 4 moved anything.

**Also the outline constants**, which the 2026-09-22 shrink invalidated — the script would otherwise seed a board of the old size:

```python
BOARD_W, BOARD_H = 62.3, 30.3   # mm
```

```python
END_R  = 17.5
```

`SAG` is derived from those two and needs no edit.

- [ ] **Step 3: Add the banner the spec's risk section asks for**

Immediately after the module docstring's closing `"""`:

```python
# WARNING -- DO NOT RUN THIS AGAINST THE REAL BOARD.
# board.Save() below is unconditional and this script also overwrites
# lumary-brain.kicad_pro. lumary-brain.kicad_pcb is hand-routed and is the
# artifact of record; re-running this destroys every track. The tables below
# are kept in sync with the board by hand -- hardware/kicad/check_board.py
# fails if they drift. To seed a *new* board, copy this script and change OUT.
```

- [ ] **Step 4: Run the script group to verify it passes**

Run: `python hardware/kicad/check_board.py --only script`

Expected: `0 failure(s)`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add hardware/kicad/build_board.py
git commit -m "docs: build_board.py describes the rev B board

Syncs COMPONENTS['J2'] and the J2/C1/Q3 positions to what was actually laid
out, and states at the top that running the script destroys the hand routing.
check_board.py --only script fails if the two drift apart again.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Documentation and BOM

**Files:**
- Modify: `hardware/bom.csv:13`, `hardware/kicad/bom-jlc.csv:10`, `hardware/kicad/footprints-and-pins.md:19`, `hardware/schematic-nets.md` §2.5 (line ~159), `hardware/calcs.md` (lines 30–32 and the "As-built trace widths (rev A)" section at ~line 210), `docs/research/teardown-reference.md` (lines ~20–22), `docs/superpowers/research/phase0-measurements.md` (P0.1, lines ~17–23)
- Modify: `hardware/kicad/check_board.py` (`EXPECT["j2_bom_keyword"]`)

**Interfaces:**
- Consumes: the board from Task 4.
- Produces: `check_board.py` fully green across all three groups.

- [ ] **Step 1: Flip the docs expectation and watch it go red**

In `hardware/kicad/check_board.py`:

```python
    "j2_bom_keyword": "ZH 1.5mm",
```

Run: `python hardware/kicad/check_board.py --only docs`

Expected: exit 1 —
```
FAIL  footprints-and-pins.md names the J2 footprint
FAIL  bom.csv J2 row mentions ZH 1.5mm
```

- [ ] **Step 2: Update the BOM rows**

`hardware/bom.csv` line 13 becomes:

```csv
J2,1,ZH 1.5mm 7-pos side entry (CN1 out),S7B-ZR-SM4A-TF 1.5mm 7P SMD,,VERIFY,To LED module; 6 populated per P0.3 pinout; hand-fit like C1/C2/Q1/Q2/SW1/SW2 -- no LCSC; never JLC-assembled
```

The Notes field must not contain a comma — this file is plain unquoted CSV (`Designator,Qty,Comment/Value,Footprint,LCSC,Status,Notes`), and a comma there silently shifts every later column. Semicolons only.

`hardware/kicad/bom-jlc.csv` line 10 becomes:

```csv
JST ZH S7B-ZR-SM4A-TF 7-pin side entry,J2,ZH-7-SIDE,
```

- [ ] **Step 3: Update the footprint table**

`hardware/kicad/footprints-and-pins.md` line 19 becomes:

```markdown
| J2 | JST ZH 1.5 7-pin side entry | `Connector_JST:JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal` |
```

- [ ] **Step 4: Run the docs group to verify it passes**

Run: `python hardware/kicad/check_board.py --only docs`

Expected: `0 failure(s)`, exit 0.

- [ ] **Step 5: Update the prose the checker cannot see**

- `hardware/schematic-nets.md` §2.5: change the heading line **`**J2 = PicoBlade 7-pos (6 populated, pin 7 = NC):**`** to name the ZH part, and add below the table: the physical 1..7 order is resolved by the footprint as of rev B — pad 1 is the pad nearest the board's bottom edge, and the harness's empty position lands on pad 7 (or the reverse, per Task 2 Step 1's finding).
- `docs/superpowers/specs/2026-09-21-rev-b-connector-design.md` §4 item 5 ("Outline unchanged: 63.3 × 31.3") and the §3.2 sentence reasoning from "a board that ends at 63.3": both are superseded by the 2026-09-22 shrink to 62.3 × 30.3. Record the reason — rev A fits the housing too tightly — rather than silently editing the numbers.
- `docs/superpowers/specs/2026-09-21-rev-b-connector-design.md` §5: the pin-1 rule ("pad 7 nearest the board edge (4.8 mm)") matches no geometry and has the order backwards; pad 1 is the bottom-most pad. Same correction in `phase0-measurements.md` P0.3/P0.6.
- **Leave alone:** `phase0-measurements.md` line 89 and `2026-08-03-tht-board-design.md` line 32 both give 63.3 × 31.3 as the **stock envelope** measurement. That is still true — it is our board that shrank, not the can.
- `docs/research/teardown-reference.md` lines ~20–22: keep the ZH finding, and replace "rev A's board/BOM need a J2 footprint swap" with a note that rev B carries that swap.
- `docs/superpowers/research/phase0-measurements.md` P0.1: change the heading's "physical 1..7 order still TODO" — rev B resolves it by construction.
- `hardware/calcs.md` lines 30–32: "rev B should fix the trace width rather than spend the margin" becomes: rev B *did* widen the power nets to 0.5 mm, moving the track limit to ~1.5 A, and the binding constraint is now J2's 1.0 A/contact and the unmeasured `+4V7` rail — **not** a licence to raise the cap.
- `hardware/calcs.md` "As-built trace widths (rev A)" section: retitle to make clear it describes rev A, record that *every* routed segment measured 0.20 mm (not just the nets the class named), and add the rev B row.

- [ ] **Step 6: Run everything and verify all three groups pass**

Run: `python hardware/kicad/check_board.py`

Expected: `0 failure(s)`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add hardware/bom.csv hardware/kicad/bom-jlc.csv hardware/kicad/footprints-and-pins.md hardware/schematic-nets.md hardware/calcs.md docs/research/teardown-reference.md docs/superpowers/research/phase0-measurements.md hardware/kicad/check_board.py
git commit -m "docs: the CN1 mismatch is closed by rev B

J2 is a JST ZH 1.5mm side-entry part across the BOMs, the footprint table and
the net map, and P0.1's open 'physical 1..7 order' is resolved by the
footprint. calcs.md now says the 0.5mm widening moved the track limit to
~1.5A and handed the constraint to J2's 1.0A contacts and an unmeasured +4V7
rail -- it is not a brightness-cap increase.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Fab outputs and the physical gate

**Files:**
- Modify: `hardware/kicad/cpl.csv`, `hardware/kicad/pos-raw.csv`, `hardware/kicad/lumary-brain-gerbers.zip`, and the **tracked `hardware/kicad/gerbers/` directory** (14 files) — J2 and C1 moved, so every one of them is stale. The loose directory is tracked alongside the zip; regenerating only the zip would leave rev A gerbers in the repo next to a rev B archive.

**Interfaces:**
- Consumes: the board from Task 6.
- Produces: a fab package that matches the committed `.kicad_pcb`.

- [ ] **Step 1: Regenerate the raw position file**

`pos-raw.csv` is the direct `kicad-cli` export (`Ref,Val,Package,PosX,PosY,Rot,Side`):

```bash
"/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb export pos --format csv --units mm --side both --output hardware/kicad/pos-raw.csv hardware/kicad/lumary-brain.kicad_pcb
```

- [ ] **Step 2: Derive the JLC CPL from it, and confirm J2 and C1 moved**

`cpl.csv` is not a `kicad-cli` output — it is `pos-raw.csv` rewritten into JLC's `Designator,Mid X,Mid Y,Layer,Rotation` shape, with `mm` suffixes and a capitalised side:

```bash
python - <<'PY'
import csv, pathlib
rows = list(csv.DictReader(open("hardware/kicad/pos-raw.csv", encoding="utf-8")))
out = pathlib.Path("hardware/kicad/cpl.csv").open("w", newline="", encoding="utf-8")
w = csv.writer(out)
w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
for r in rows:
    w.writerow([r["Ref"], "%.4fmm" % float(r["PosX"]), "%.4fmm" % float(r["PosY"]),
                r["Side"].capitalize(), "%.6f" % float(r["Rot"])])
out.close()
PY
grep -E "^(J2|C1)," hardware/kicad/cpl.csv
```

Expected: `J2,174.2574mm,-96.7500mm,Top,…` and `C1,169.0074mm,-101.9500mm,Top,90.000000`, matching Task 2's table. J2's rotation reads `90.000000`. If the X values still read `177.1074` / `171.0074`, the export ran against a stale file.

- [ ] **Step 3: Regenerate the gerbers and drill files**

Regenerate **into the tracked directory**, not a temp one, then pack that same directory into the zip:

```bash
rm -f hardware/kicad/gerbers/* && "/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb export gerbers --output hardware/kicad/gerbers hardware/kicad/lumary-brain.kicad_pcb && "/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb export drill --output hardware/kicad/gerbers hardware/kicad/lumary-brain.kicad_pcb && python - <<'PY'
import pathlib, zipfile
src = pathlib.Path("hardware/kicad/gerbers")
with zipfile.ZipFile("hardware/kicad/lumary-brain-gerbers.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(src.iterdir()):
        if f.is_file():
            z.write(f, f.name)
            print("packed", f.name)
PY
git status --short hardware/kicad/gerbers/
```

(Python's `zipfile` rather than `zip`, which is not on PATH in this Git Bash.)

The rev A directory held 14 files including `lumary-brain-In1_Cu.g1` and `-In2_Cu.g2`. This is a **two-layer** board, so a clean export should not produce inner-layer files at all — if they do not come back, that is correct and their deletion gets committed. Stage the directory with `git add -A hardware/kicad/gerbers/` in Step 6 so removals are staged along with changes; a plain `git add` of the path would leave the stale files tracked.

- [ ] **Step 4: 1:1 print, checked against the physical part**

Print the F.Cu + F.Paste + F.SilkS layers at exactly 1:1 (**File → Plot → PDF**, scale 1.0, no "fit to page") and lay the purchased S7B-ZR-SM4A-TF on the J2 pads. All seven signal pads and both `MP` tabs must sit under their metal. Then lay the stock harness plug against the print and confirm the empty position falls on the pad Task 2 Step 1 predicted.

This is the gate that rev A failed. Do not order boards until it passes.

- [ ] **Step 5: Final full verification**

```bash
python hardware/kicad/check_board.py && "/c/Program Files/KiCad/10.0/bin/kicad-cli.exe" pcb drc --format json --output hardware/kicad/drc.json --severity-error --refill-zones hardware/kicad/lumary-brain.kicad_pcb && python -c "import json;d=json.load(open('hardware/kicad/drc.json'));print('violations:',sum(len(d.get(k,[])) for k in ('violations','unconnected_items')))"
```

Expected: `0 failure(s)` from the checker and `violations: 0`.

- [ ] **Step 6: Commit**

```bash
git add -A hardware/kicad/gerbers/ && git add hardware/kicad/cpl.csv hardware/kicad/pos-raw.csv hardware/kicad/lumary-brain-gerbers.zip
git commit -m "release: rev B fab package

Gerbers, drill and CPL regenerated against the rev B board -- J2 and C1 both
moved, so the rev A set was stale. 1:1 print checked against the purchased
S7B-ZR-SM4A-TF and against the stock harness plug.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Verification against the spec

Spec §4 asks for five things before gerbers are released:

| Spec §4 item | Where it happens |
|---|---|
| 1. DRC clean, attention to J2/C1 and J2/U3 | Task 4 (and Task 2 Step 4's measured clearances: C1 0.77 mm, Q3 0.30 mm, C4 0.17 mm, U3 0.64 mm) |
| 2. Netlist assertion on J2 pads 1–7 | Task 1's `J2 pad nets` check, re-run at Tasks 2, 4, 6, 7 — explicit, not inferred from placement |
| 3. Track-width regression over the `.kicad_pcb` | Task 1's per-net width check, tightened to 0.5 mm in Task 3 |
| 4. 1:1 print against the physical connector | Task 7 Step 4 |
| 5. Outline ~~unchanged~~ — now 62.3 × 30.3 with R 17.5 ends | Task 1's outline check, which runs on every invocation. The spec's "unchanged" wording is superseded; see the Global Constraints note and Task 6. |

## After fab

Out of scope for this plan, from spec §4: bring the first rev B board up on the bench against a real harness with **no pigtail**, and re-run the bench checklist in `docs/superpowers/plans/2026-08-18-bench-verification.md`.

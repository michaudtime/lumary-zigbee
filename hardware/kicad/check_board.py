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
    "j2_footprint": "Connector_JST:JST_ZH_S7B-ZR-SM4A-TF_1x07-1MP_P1.50mm_Horizontal",
    "j2_pos": (56.15, 15.15, 90.0),
    "j2_pads": {"1": "+36V", "2": "CW_RET", "3": "WW_RET", "4": "+4V7",
                "5": "GND", "6": "RING_DATA", "7": None},
    "c1_pos": (50.8926, 20.45, 90.0),
    "q3_pos": (50.65, 8.8375, 90.0),
    "power_nets": ["+36V", "+4V7", "+4V7_IN", "+3V3", "VBUS", "LDO_IN", "CW_RET", "WW_RET"],
    "min_power_width": 0.2,
    # net -> (allowed_uuids, why). Segments with UUIDs in the list are allowed to be thin.
    "width_exceptions": {},
    "board_size": (62.3, 30.3),
    "j2_bom_keyword": "ZH 1.5mm",
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

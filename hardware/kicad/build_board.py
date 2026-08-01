#!/usr/bin/env python
"""
Build the Lumary brain-replacement board (lumary-brain.kicad_pcb) from the
netlist in ../schematic-nets.md. Run with KiCad 10's bundled Python:

  "/c/Program Files/KiCad/10.0/bin/python.exe" build_board.py

Produces: lumary-brain.kicad_pcb  (all footprints placed + every net assigned +
board outline). Routing is done by hand in KiCad afterwards.
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "lumary-brain.kicad_pcb")
FPBASE = r"C:/Program Files/KiCad/10.0/share/kicad/footprints"

BOARD_W, BOARD_H = 63.3, 31.3   # mm
# Ends are single 18mm-radius arcs (case corner rounding, user-measured): each end
# bulges sagitta = R - sqrt(R^2-(H/2)^2) ~= 9.1mm deep -> ~45mm straight section
# (matches user's "about 44mm square portion").
import math as _math
END_R  = 18.0
SAG    = END_R - _math.sqrt(END_R**2 - (BOARD_H/2)**2)   # ~9.11 mm

def mm(v): return pcbnew.FromMM(v)
def xy(x, y): return pcbnew.VECTOR2I(mm(x), mm(y))

# ---- components: ref -> (lib, footprint, value) --------------------------
GNDMOD = ["1","2","11","14"] + [str(n) for n in range(36,54)]   # module GND pads
COMPONENTS = {
 "U1": ("RF_Module","ESP32-C6-MINI-1","ESP32-H2-MINI-1-N4"),
 "U2": ("Package_TO_SOT_SMD","SOT-23-5","ME6211C33M5G"),
 "U3": ("Package_TO_SOT_SMD","SOT-23-5","74AHCT1G125"),
 "U4": ("Package_TO_SOT_SMD","SOT-23-6","USBLC6-2SC6"),
 "Q1": ("Package_TO_SOT_SMD","SOT-23","NMOS_60V"),
 "Q2": ("Package_TO_SOT_SMD","SOT-23","NMOS_60V"),
 "Q3": ("Package_TO_SOT_SMD","SOT-23","AO3401A"),
 "D1": ("Diode_SMD","D_SOD-123","B5819W"),
 "D2": ("Diode_SMD","D_SOD-123","B5819W"),
 "D3": ("Diode_SMD","D_SMA","SMAJ5.0A"),
 "J1": ("Connector_JST","JST_PH_S3B-PH-K_1x03_P2.00mm_Horizontal","PWR_IN"),
 "J2": ("Connector_Molex","Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical","CN1"),
 "J3": ("Connector_USB","USB_C_Receptacle_HRO_TYPE-C-31-M-12","USB-C"),
 "SW1":("Button_Switch_SMD","Panasonic_EVQPUJ_EVQPUA","BOOT"),
 "SW2":("Button_Switch_SMD","Panasonic_EVQPUJ_EVQPUA","EN"),
 "C1": ("Capacitor_SMD","C_0805_2012Metric","10uF/50V"),
 "C2": ("Capacitor_SMD","C_0603_1608Metric","10uF"),
 "C3": ("Capacitor_SMD","C_0603_1608Metric","1uF"),
 "C4": ("Capacitor_SMD","C_0603_1608Metric","10uF"),
 "C5": ("Capacitor_SMD","C_0603_1608Metric","10uF"),
 "C6": ("Capacitor_SMD","C_0402_1005Metric","100nF"),
 "C7": ("Capacitor_SMD","C_0402_1005Metric","100nF"),
 "C8": ("Capacitor_SMD","C_0402_1005Metric","1uF"),
 "R1": ("Resistor_SMD","R_0402_1005Metric","100R"),
 "R2": ("Resistor_SMD","R_0402_1005Metric","100R"),
 "R3": ("Resistor_SMD","R_0402_1005Metric","100k"),
 "R4": ("Resistor_SMD","R_0402_1005Metric","100k"),
 "R5": ("Resistor_SMD","R_0402_1005Metric","200R"),
 "R6": ("Resistor_SMD","R_0402_1005Metric","10k"),
 "R7": ("Resistor_SMD","R_0402_1005Metric","5.1k"),
 "R8": ("Resistor_SMD","R_0402_1005Metric","5.1k"),
 "R9": ("Resistor_SMD","R_0402_1005Metric","100k"),
}

# ---- placement --------------------------------------------------------------
# Module anchored at top edge: antenna keepout (local y -26..-5.6) hangs off-board
# into air above y=0 -- the recommended edge mounting. Body spans y ~0.2..16.8.
# Fixed-part courtyards (measured): U1 x9.9..53.3/y0..14.4; J1 x3.6..12.3/y19.5..28.5;
# J2 x52.8..57.1/y18.2..29.8; J3 x26.3..36.9/y22.7..31.3.
#
# Zones (grouped by circuit function for short routes):
#   LEFT COLUMN  (x~5, y 3..18.5)   power chain (LDO, rev-pol, OR diodes, TVS-adjacent Rs/Cs)
#   BOTTOM-LEFT  (x 14..24, y>17.5) buttons + USB ESD + CC (near J3), module bulk caps
#   BOTTOM-RIGHT (x 39..51, y>17.5) white MOSFET switches + ring buffer (near J2)
POS = {
 # === USER'S LAYOUT v2 (adopted from GUI, 2026-08-01) ===
 # module top-left (antenna dead zone x<14.2), power cluster top-center,
 # USB right-of-center bottom w/ ESD+CC hugging it, buffer top-right by J2.
 "U1":(19.8, 8.3, 90.0),
 "J1":(23.85, 23.6, -90.0),
 "J2":(59.5, 19.0, 90.0),
 "J3":(41.93, 25.95, 0.0),
 "Q1":(37.35, 12.66, 90.0),
 "Q2":(42.2, 8.54, 90.0),
 "Q3":(51.5, 9.34, 90.0),
 "D1":(46.95, 6.8, 0.0),
 "D2":(41.55, 5.2, 0.0),
 "D3":(21.6, 19.05, 0.0),
 "C1":(53.4, 20.85, 90.0),
 "SW1":(31.12, 20.3, 0.0),
 "SW2":(31.12, 26.3, 0.0),
 "U2":(36.75, 4.44, 90.0),
 "U3":(54.85, 5.46, 90.0),
 "U4":(41.86, 17.75, 0.0),
 "R1":(33.91, 13.6, 0.0),
 "R2":(39.2, 9.6, 0.0),
 "R3":(46.0, 12.34, 0.0),
 "R4":(48.5, 12.34, 0.0),
 "R5":(43.5, 12.34, 0.0),
 "R6":(20.4, 16.2, 90.0),
 "R7":(38.2, 19.2, 90.0),
 "R8":(45.4, 19.11, 90.0),
 "R9":(51.0, 6.0, 0.0),
 "C2":(33.02, 4.58, -90.0),
 "C3":(34.2, 7.8, 180.0),
 "C4":(51.5, 12.84, 0.0),
 "C5":(17.0, 16.82, 90.0),
 "C6":(15.4, 17.0, 90.0),
 "C7":(51.4, 3.72, -90.0),
 "C8":(22.2, 16.2, 90.0),
}

# ---- netlist: (ref, pad) -> net ------------------------------------------
def N(net, *pairs):
    for ref, pads in pairs:
        for p in (pads if isinstance(pads, list) else [pads]):
            PADNET[(ref, p)] = net
PADNET = {}
N("GND", ("U1",GNDMOD),("U2","2"),("U3","1"),("U3","3"),("U4","2"),
         ("Q1","2"),("Q2","2"),("D3","2"),
         ("C1","2"),("C2","2"),("C3","2"),("C4","2"),("C5","2"),("C6","2"),("C7","2"),("C8","2"),
         ("R3","2"),("R4","2"),("R7","2"),("R8","2"),("R9","2"),
         ("J1","2"),("J2","5"),("SW1","2"),("SW2","2"),
         ("J3",["A1","A12","B1","B12","SH"]))
N("+36V", ("J1","3"),("C1","1"),("J2","1"))
N("+4V7_IN", ("J1","1"),("Q3","3"),("D3","1"))
N("+4V7", ("Q3","2"),("D1","2"),("U3","5"),("C4","1"),("C7","1"),("J2","4"))
N("Q3_GATE", ("Q3","1"),("R9","1"))
N("LDO_IN", ("D1","1"),("D2","1"),("U2","1"),("U2","3"),("C3","1"))
N("+3V3", ("U2","5"),("U1","3"),("C2","1"),("C5","1"),("C6","1"),("R6","2"))
N("VBUS", ("J3",["A4","A9","B4","B9"]),("D2","2"),("U4","5"))
N("USB_DP", ("U1","27"),("U4","1"),("U4","6"),("J3",["A6","B6"]))
N("USB_DM", ("U1","26"),("U4","3"),("U4","4"),("J3",["A7","B7"]))
N("CC1", ("J3","A5"),("R7","1"))
N("CC2", ("J3","B5"),("R8","1"))
N("EN", ("U1","8"),("R6","1"),("C8","1"),("SW2","1"))
N("IO9", ("U1","23"),("SW1","1"))
N("CW_PWM", ("U1","18"),("R1","1"))
N("WW_PWM", ("U1","19"),("R2","1"))
N("CW_GATE", ("R1","2"),("Q1","1"),("R3","1"))
N("WW_GATE", ("R2","2"),("Q2","1"),("R4","1"))
N("CW_RET", ("Q1","3"),("J2","2"))
N("WW_RET", ("Q2","3"),("J2","3"))
N("RING_DATA_3V3", ("U1","21"),("U3","2"))
N("RING_BUF_OUT", ("U3","4"),("R5","1"))
N("RING_DATA", ("R5","2"),("J2","6"))

# ==========================================================================
def main():
    board = pcbnew.NewBoard(OUT)
    board.SetCopperLayerCount(4)

    # create nets
    nets = {}
    for net in sorted(set(PADNET.values())):
        ni = pcbnew.NETINFO_ITEM(board, net)
        board.Add(ni); nets[net] = ni

    fps = {}
    def load(ref):
        lib,fp,val = COMPONENTS[ref]
        footprint = pcbnew.FootprintLoad(f"{FPBASE}/{lib}.pretty", fp)
        if footprint is None:
            print(f"!! FAILED to load {lib}:{fp} for {ref}"); sys.exit(1)
        footprint.SetReference(ref); footprint.SetValue(val)
        footprint.SetFPIDAsString(f"{lib}:{fp}")
        board.Add(footprint)
        fps[ref] = footprint
        return footprint

    # place every part at its zoned position, then validate with pairwise
    # courtyard-intersection checks (bigger parts claim their real area).
    for ref,(x,y,rot) in POS.items():
        f = load(ref)
        f.SetPosition(xy(x,y))
        if rot: f.SetOrientationDegrees(rot)

    def courtyard_of(f):
        cy = pcbnew.SHAPE_POLY_SET(f.GetCourtyard(pcbnew.F_CrtYd))
        for z in f.Zones():                      # include keepout zones (antenna)
            cy.Append(z.Outline())
        cy.Simplify()
        return cy

    refs=list(POS)
    polys={r:courtyard_of(fps[r]) for r in refs}
    clashes=[]
    for i,a in enumerate(refs):
        for b in refs[i+1:]:
            t=pcbnew.SHAPE_POLY_SET(polys[a])
            t.BooleanIntersection(polys[b])
            if t.OutlineCount()>0: clashes.append((a,b))
    if clashes:
        print("!! COURTYARD CLASHES:", clashes)

    # assign nets to pads
    matched = set()
    for ref, footprint in fps.items():
        for pad in footprint.Pads():
            key = (ref, pad.GetNumber())
            if key in PADNET:
                pad.SetNet(nets[PADNET[key]])
                matched.add(key)
    missing = set(PADNET) - matched
    if missing:
        print("!! PAD KEYS WITH NO MATCHING PAD (check footprint pad names):")
        for k in sorted(missing): print("   ", k)

    # silkscreen hygiene: put reference designators on F.Fab (assembly layer)
    # instead of silk -- standard for dense boards; JLC assembles from the CPL.
    for f in fps.values():
        ref = f.Reference()
        ref.SetLayer(pcbnew.F_Fab)
        ref.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        ref.SetTextThickness(mm(0.12))
        ref.SetTextPos(f.GetPosition())
        f.Value().SetVisible(False)

    # board outline: straight top/bottom + single 18mm-radius arc at each end
    W,H,R,S = BOARD_W, BOARD_H, END_R, SAG
    def line(x1,y1,x2,y2):
        s=pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(xy(x1,y1)); s.SetEnd(xy(x2,y2))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.15)); board.Add(s)
    def arc(sx,sy,mx,my,ex,ey):
        s=pcbnew.PCB_SHAPE(board); s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetArcGeometry(xy(sx,sy), xy(mx,my), xy(ex,ey))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.15)); board.Add(s)
    line(S,0, W-S,0)                       # top
    arc(W-S,0, W,H/2, W-S,H)               # right end bulge
    line(W-S,H, S,H)                       # bottom
    arc(S,H, 0,H/2, S,0)                   # left end bulge

    # x of the left/right board edge at height y (arc equation)
    def xl(y): return END_R - _math.sqrt(END_R**2 - (y-H/2)**2)
    def xr(y): return W - xl(y)

    # audit: warn about any copper pad outside (or within 0.5mm of) the outline
    for f in fps.values():
        for pad in f.Pads():
            if pad.GetAttribute()==pcbnew.PAD_ATTRIB_NPTH: continue
            p=pad.GetPosition(); px,py=pcbnew.ToMM(p.x),pcbnew.ToMM(p.y)
            if not (0<=py<=H) or px < xl(py)+0.5 or px > xr(py)-0.5:
                print(f"  !! pad near/off edge: {f.GetReference()}.{pad.GetNumber()} at ({px:.1f},{py:.1f})")

    # no-fill keepouts around NPTH mounting holes (J3 USB-C pegs) so zone fill
    # honours hole clearance
    for f in fps.values():
        for pad in f.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                r = max(pad.GetDrillSize().x, pad.GetDrillSize().y)/2 + mm(0.35)
                ka = pcbnew.ZONE(board)
                ka.SetIsRuleArea(True)
                ka.SetDoNotAllowZoneFills(True)
                ka.SetDoNotAllowTracks(False); ka.SetDoNotAllowVias(False)
                ka.SetDoNotAllowPads(False);   ka.SetDoNotAllowFootprints(False)
                ls = pcbnew.LSET(); [ls.AddLayer(l) for l in
                     (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)]
                ka.SetLayerSet(ls)
                o = ka.Outline().NewOutline()
                c = pad.GetPosition()
                for dx,dy in ((-r,-r),(r,-r),(r,r),(-r,r)):
                    ka.Outline().Append(int(c.x+dx), int(c.y+dy), o)
                board.Add(ka)

    # GND zones: pours on F/B + solid planes on both inner layers, following the
    # rounded outline inset 0.6mm (arcs polygonised). The module's antenna
    # rule-area keeps copper out of the antenna region.
    INSET = 0.6
    def zone_pts():
        # radial inset: end arcs shrink to radius END_R-INSET about the same centers
        Ri = END_R - INSET
        def xli(y): return END_R - _math.sqrt(Ri**2 - (y-H/2)**2)
        pts=[]; n=24
        ys=[INSET + i*(H-2*INSET)/n for i in range(n+1)]
        for y in ys:            pts.append((xli(y), y))              # left arc, top→bottom
        for y in reversed(ys):  pts.append((W - xli(y), y))          # right arc, bottom→top
        return pts
    ZPTS = zone_pts()
    for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(nets["GND"])
        o = z.Outline().NewOutline()
        for cx,cy in ZPTS:
            z.Outline().Append(mm(cx), mm(cy), o)
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        # solid connections: board is machine-reflowed (JLC PCBA), thermal reliefs
        # only matter for hand soldering and they starve on this dense a board
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetZoneName(f"GND_{pcbnew.LayerName(layer)}")
        board.Add(z)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())

    board.Save(OUT)

    # project file with net classes (KiCad reads it next to the .kicad_pcb)
    import json
    pro = os.path.join(HERE, "lumary-brain.kicad_pro")
    def nc(name, tw, via, vdrill, clr):
        return {"name":name,"clearance":clr,"track_width":tw,
                "via_diameter":via,"via_drill":vdrill,
                "bus_width":12,"diff_pair_gap":0.25,"diff_pair_via_gap":0.25,
                "diff_pair_width":0.2,"line_style":0,"microvia_diameter":0.3,
                "microvia_drill":0.1,"wire_width":6,
                "pcb_color":"rgba(0, 0, 0, 0.000)","schematic_color":"rgba(0, 0, 0, 0.000)"}
    POWER_NETS = ["+36V","+4V7","+4V7_IN","+3V3","VBUS","LDO_IN","CW_RET","WW_RET"]
    data = {
      "board": {"design_settings": {"defaults": {}}},
      "meta": {"filename": "lumary-brain.kicad_pro", "version": 3},
      "net_settings": {
        "classes": [nc("Default",0.25,0.6,0.3,0.2), nc("Power",0.5,0.8,0.4,0.2)],
        "meta": {"version": 4},
        "net_colors": None,
        "netclass_assignments": None,
        "netclass_patterns": [{"netclass":"Power","pattern":p} for p in POWER_NETS],
      },
    }
    with open(pro,"w",encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)
    print(f"    wrote {pro}")
    # summary
    ratsnest_pads = sum(1 for f in fps.values() for p in f.Pads() if p.GetNetCode()>0)
    print(f"OK  components={len(fps)}  nets={len(nets)}  connected_pads={ratsnest_pads}  missing={len(missing)}")
    print(f"    wrote {OUT}")

if __name__ == "__main__":
    main()

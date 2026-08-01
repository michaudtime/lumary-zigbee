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
 # fixed anchors (courtyards: U1 x10..53.2/y0..14.4; J1 x6.1..14.8/y17.5..26.5;
 #                J2 x52.4..56.7/y14.5..26.0; J3 x26.2..37.0/y22.2..31.7)
 "U1":(31.6, 8.5, 0), "J1":(8,24,90), "J2":(55,24,90), "J3":(31.6,27.5,0),
 # left column (x<8, above/beside J1): power chain, top to bottom
 "U2":(5, 2.6, 90), "Q3":(5, 7.2, 90), "D1":(5, 10.7, 0), "D2":(5, 13.4, 0),
 "C3":(5, 16.1, 0), "C4":(4.2, 18.3, 0), "R9":(4.2, 20.3, 0), "D3":(4.7, 29.3, 0),
 # under-module band (x26..37, y15..21, above J3): module bulk + EN support
 "C5":(28, 15.9, 0), "C6":(30.5, 15.9, 0), "R6":(33, 15.9, 0), "C8":(35.5, 15.9, 0),
 # bottom-left (between J1 and J3): the two bench buttons
 "SW1":(20, 18.8, 0), "SW2":(20, 25.3, 0),
 # right of J3, below right band: USB ESD + CC pulldowns (hug the USB-C)
 "U4":(39.5, 28.5, 90), "R7":(43, 28.5, 0), "R8":(45.5, 28.5, 0),
 # bottom-right band: white switches + ring buffer (near J2)
 "Q1":(40, 17.6, 90), "Q2":(44, 17.6, 90), "C1":(48.5, 17.6, 0),
 "R1":(40, 20.5, 0), "R2":(44, 20.5, 0), "R3":(47.5, 20.5, 0), "R4":(50.5, 20.5, 0),
 "U3":(40, 23.5, 90), "R5":(44, 23.5, 0), "C7":(47, 23.5, 0), "C2":(50, 23.5, 0),
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

    # board outline (rectangle) on Edge.Cuts
    corners = [(0,0),(BOARD_W,0),(BOARD_W,BOARD_H),(0,BOARD_H),(0,0)]
    for (x1,y1),(x2,y2) in zip(corners, corners[1:]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(xy(x1,y1)); seg.SetEnd(xy(x2,y2))
        seg.SetLayer(pcbnew.Edge_Cuts); seg.SetWidth(mm(0.15))
        board.Add(seg)

    board.Save(OUT)
    # summary
    ratsnest_pads = sum(1 for f in fps.values() for p in f.Pads() if p.GetNetCode()>0)
    print(f"OK  components={len(fps)}  nets={len(nets)}  connected_pads={ratsnest_pads}  missing={len(missing)}")
    print(f"    wrote {OUT}")

if __name__ == "__main__":
    main()

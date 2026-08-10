# `lumary-brain-tht` — Through-Hole Board Design Spec

**Date:** 2026-08-03
**Status:** Design approved — ready for implementation plan
**Goal:** A second controller board with identical functionality to `lumary-brain` rev A,
built entirely from through-hole parts so it can be assembled by hand with a basic
soldering iron and no SMD skills.

---

## 1. Objective

Rev A is a fully-assembled SMD board (JLCPCB PCBA, ~$20/board with a $25 setup fee, and
an LGA module nobody can hand-solder). This board trades size for buildability: anyone
with an iron can assemble it in about half an hour for roughly **$11 in parts**.

**Same functionality as rev A:** Zigbee control, 62-pixel addressable RGB ring, tunable
white via the fixture's constant-current driver, scenes, and OTA.

### Locked decisions

| Decision | Choice | Rationale |
|---|---|---|
| Brain | **ESP32-H2 Super Mini** on headers | The only practical way to get an H2 into a THT design; also brings a regulator, USB-C and buttons |
| Power source | **Same as rev A** — Lumary's 36.63 V CC + 4.7 V rails | Targets the same fixtures; the board steers current, never sources it |
| Enclosure fit | **Not constrained** — need not fit the stock can | Frees the layout entirely; board can be mounted in the ceiling void or a printed enclosure |
| Connectors | **Same PicoBlade + JST-PH as rev A** | The light's existing harness plugs straight in — still a drop-in electrically, no cutting |
| Layers | **2** | No fine pitch, modest currents; halves PCB cost |

### Non-goals (YAGNI)

- Fitting the 63.3 × 31.3 × 6.7 mm stock envelope.
- Supporting generic 5/12/24 V constant-voltage supplies. This board targets the Lumary
  driver specifically; a CV variant would need its own regulator and a different white
  drive stage, and is a separate project if ever wanted.
- Any SMD part, including "easy" ones. The whole point is that a beginner can build it.

---

## 2. The key win: one firmware for both boards

The Super Mini exposes **GPIO 4, 5 and 11**, the exact pins rev A uses. Verified against
the ESP32-H2 available-GPIO set (GPIO 0–5, 8–14, 22–27) and the Super Mini's breakout.

| Function | Pin | Same as rev A? |
|---|---|---|
| Ring data (SPI2 MOSI) | GPIO 11 | ✅ |
| CW PWM → Q1 gate | GPIO 4 | ✅ |
| WW PWM → Q2 gate | GPIO 5 | ✅ |

So **no firmware fork, no separate build, one `.ota` image serves both boards.** Nothing
in `src/` changes for this board. GPIO 2/3 remain avoided here for the same reason as
rev A — they are strapping pins, and the gate pulldowns would hold them low at reset.

---

## 3. Architecture

```
  Driver harness (J1)          THT carrier board                 LED module (J2 / CN1)
 ┌──────────────┐   ┌─────────────────────────────────────┐   ┌────────────────────┐
 │ GND         ─┼──►│ ────────────────────────────────────┼──►│ GND                │
 │ +4V7        ─┼──►│ ─┬─[D1 1N5819]──► Super Mini "5V"    │   │                    │
 │              │   │  │                 (regulates 3V3)   │   │                    │
 │              │   │  ├─[D2 TVS]──► GND                   │   │                    │
 │              │   │  ├──────────────────────────────────┼──►│ 5V+  (ring power)  │
 │              │   │  └──► U1 74AHCT125 Vcc               │   │                    │
 │              │   │            ▲          │              │   │                    │
 │              │   │  GPIO11 ───┘          └─[R5 200R]───┼──►│ DIM  (ring data)   │
 │              │   │  GPIO4 ─[R1]─► Q1 gate, Q1 drain ───┼──►│ CW-  (6500K)       │
 │              │   │  GPIO5 ─[R2]─► Q2 gate, Q2 drain ───┼──►│ WW-  (2700K)       │
 │ +36V        ─┼──►│ ────────────────────────────────────┼──►│ V+   (white anode) │
 └──────────────┘   └─────────────────────────────────────┘   └────────────────────┘
```

### 3.1 Power

- **`+4V7` → D1 (1N5819 Schottky) → Super Mini `5V` pin.** The Super Mini's onboard LDO
  makes 3.3 V from it; ~4.35 V after the diode drop is ample headroom.
- **D1 does double duty:** besides reverse-polarity protection, it stops USB VBUS
  back-feeding the fixture's 4.7 V rail if someone plugs in USB while the light is
  powered. Without it that would be a genuine hazard on the bench.
- **D2 (P6KE6.8A TVS)** clamps transients on the incoming 4.7 V.
- **`+36V` passes straight through** to `CN1.V+`. The board never regulates it — the
  external `L-SD8E1` driver holds 380 mA constant-current.

### 3.2 Ring data (level shift)

`GPIO11` → **U1 74AHCT125** (DIP-14) → `R5` 200 Ω series → `CN1.DIM`.

U1's Vcc is taken from **`+4V7` *before* D1**, the same node that feeds `CN1.5V+`, so the
buffer's output-high matches the strip's own supply exactly. AHCT is required, not AHC:
its TTL input thresholds accept 3.3 V as a logic high. Only one of the four gates is
used; tie the unused inputs low and leave their outputs open.

### 3.3 White output

Two **logic-level N-MOSFET low-side switches**, gates from `GPIO4`/`GPIO5` through 100 Ω
series resistors, with 100 kΩ pulldowns so both stay off through reset and boot.

**Part choice matters here.** The obvious TO-92 candidates are wrong: 2N7000 and BS170
specify on-resistance at a 10 V gate and only partially enhance at the ESP32's 3.3 V. A
2N7000 at our 380 mA would drop ~1.9 V and dissipate ~0.7 W. Use a true logic-level part:

- **FQP30N06L** (TO-220, 60 V, 32 A, 0.035 Ω @ Vgs=5 V) — recommended; cheap, ubiquitous,
  hugely overspecced, impossible to get wrong.
- Alternates: STP16NF06L (60 V), IRLZ44N (55 V — still 54% margin over the 35.8 V
  compliance rail, acceptable but prefer 60 V).

Dissipation at 380 mA is ~5 mW. No heatsink, no thermal design needed.

---

## 4. Bill of materials (13 line items, all through-hole)

| Ref | Part | Package | Notes |
|---|---|---|---|
| — | ESP32-H2 Super Mini | 0.1″ headers | socketed on female headers, so it can be swapped/programmed off-board |
| U1 | 74AHCT125 | DIP-14 | ring data buffer; **AHCT**, not AHC |
| Q1, Q2 | FQP30N06L | TO-220 | white low-side switches |
| D1 | 1N5819 | DO-41 | reverse-polarity + USB back-feed block |
| D2 | P6KE6.8A | DO-15 | TVS on 4.7 V input |
| C1 | 100 µF electrolytic | radial | bulk on 4.7 V |
| C2 | 100 nF ceramic | 0.1″ | U1 decoupling, close to pin 14 |
| C3 | 10 µF electrolytic | radial | bulk on 36 V (≥50 V rated) |
| R1, R2 | 100 Ω | ¼ W axial | gate series |
| R3, R4 | 100 kΩ | ¼ W axial | gate pulldowns |
| R5 | 200 Ω | ¼ W axial | ring data damping |
| J1 | JST-PH 3-pin | THT vertical | power in (GND / +36 V / +4V7) |
| J2 | PicoBlade 1.25 mm 7-pin | THT vertical | to LED module; same part as rev A (LCSC C10824) |

Estimated cost: **≈$2 PCB + ≈$4 parts + ≈$5 Super Mini ≈ $11/board**, no assembly fee.

---

## 5. Mechanical

- **~70 × 50 mm**, 2-layer, generous spacing. Not size-critical, so favour easy soldering:
  wide pads, clear silkscreen, parts not crowded.
- **4 × M3 mounting holes** at the corners.
- Super Mini on female headers along its two 0.1″ rows; keep copper clear under its
  **antenna end** on both layers.
- TO-220s may stand or lie flat — no height limit.
- Silkscreen must label J1 and J2 pin-for-pin (`GND / +36V / +4V7`, and
  `V+ / CW- / WW- / 5V+ / GND / DIM`) since builders will be checking against the
  fixture's wire colours: black=V+, white=CW-, yellow=WW-, red=5V+, green=GND, blue=DIM.

---

## 6. Validation

1. **Bare board + Super Mini on USB only:** boots, enumerates, Zigbee joins.
2. **Add 4.7 V** from a current-limited bench supply: ring lights and animates.
3. **Add 36 V** (current-limited to 0.4 A) with the white module: CW/WW steering, CCT
   sweep, dimming, and thermals under sustained load.
4. **In-fixture:** plug the light's existing harness into J1/J2, power the real driver,
   confirm both rings and Zigbee binding.

Reuse the rev A firmware unchanged; the ring's `MAX_BRIGHTNESS` ceiling is a rev A trace
limitation and can be raised on this board once its own trace widths are confirmed
(see `hardware/calcs.md`).

---

## 7. Risks

- **R1 — Super Mini pinout varies by vendor.** "ESP32-H2 Super Mini" is a generic name;
  clones may differ in pin order or header spacing. Confirm the physical pinout of the
  exact board on hand before finalizing the footprint.
- **R2 — 4.7 V rail current budget.** Unchanged from rev A: the ring can draw more than
  the fixture's driver can spare at full brightness. Keep the firmware brightness cap
  until the rail's real limit is measured.
- **R3 — Socket height.** Not a fit risk here (no enclosure constraint) but female
  headers add ~8 mm to the stack; fine unless someone later wants it in the can.
- **R4 — 1.25 mm crimping.** Keeping PicoBlade means anyone *making* a harness from
  scratch faces fiddly crimps. Not an issue for a drop-in swap, where the fixture's
  harness is reused as-is.

# KiCad build reference — footprints & confirmed pin maps

Data the `build_board.py` script encodes. All footprints are KiCad 10 stock libs (portable).
Toolchain: KiCad 10.0.5, `kicad-cli` + bundled `pcbnew` Python API confirmed working.

## Footprint assignments

| Ref | Part | KiCad footprint (lib:name) |
|---|---|---|
| U1 | ESP32-H2-MINI-1-N4 | `RF_Module:ESP32-C6-MINI-1` (identical MINI-1 land pattern; pads 1–53 + EPAD) |
| U2 | ME6211C33M5G LDO | `Package_TO_SOT_SMD:SOT-23-5` |
| U3 | 74AHCT1G125 buffer | `Package_TO_SOT_SMD:SOT-23-5` |
| U4 | USBLC6-2SC6 ESD | `Package_TO_SOT_SMD:SOT-23-6` |
| Q1,Q2 | N-MOSFET ≥60 V | `Package_TO_SOT_SMD:SOT-23` |
| Q3 | AO3401A P-MOSFET | `Package_TO_SOT_SMD:SOT-23` |
| D1,D2 | B5819W Schottky | `Diode_SMD:D_SOD-123` |
| D3 | SMAJ5.0A TVS | `Diode_SMD:D_SMA` |
| J1 | JST-PH 2.0 3-pin | `Connector_JST:JST_PH_S3B-PH-K_1x03_P2.00mm_Horizontal` |
| J2 | PicoBlade 1.25 7-pin | `Connector_Molex:Molex_PicoBlade_53047-0710_1x07_P1.25mm_Vertical` |
| J3 | USB-C TYPE-C-31-M-12 | `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` |
| SW1,SW2 | tact | `Button_Switch_SMD:SW_SPST_SKQG_WithoutStem` (verify at build) |
| C1 | 10 µF/50 V | `Capacitor_SMD:C_0805_2012Metric` |
| C2,C4,C5 | 10 µF | `Capacitor_SMD:C_0603_1608Metric` |
| C3 | 1 µF | `Capacitor_SMD:C_0603_1608Metric` |
| C6,C7 | 100 nF | `Capacitor_SMD:C_0402_1005Metric` |
| C8 | 1 µF | `Capacitor_SMD:C_0402_1005Metric` |
| R1–R9 | resistors | `Resistor_SMD:R_0402_1005Metric` |

## Confirmed pin maps

**U1 ESP32-H2-MINI-1 (pad → function):**
- 3V3 = 3 · EN = 8 · IO9(BOOT) = 23 · IO11(ring data) = 21 · IO4(CW PWM) = 18 · IO5(WW PWM) = 19
- USB_D− = 26 · USB_D+ = 27
- GND = 1, 2, 11, 14, 36–53 (49 = EPAD)

**U2 ME6211 (SOT-23-5):** 1=VIN, 2=GND, 3=CE, 4=NC, 5=VOUT
**U3 74AHCT1G125 (SOT-23-5):** 1=/OE, 2=A(in), 3=GND, 4=Y(out), 5=VCC
**U4 USBLC6-2SC6 (SOT-23-6):** 1&6=I/O1 (→D+), 3&4=I/O2 (→D−), 2=GND, 5=VBUS
**Q1/Q2 N-FET, Q3 AO3401A P-FET (SOT-23):** 1=Gate, 2=Source, 3=Drain
**D1/D2 SOD-123, D3 SMA:** pad 1 = cathode (K), pad 2 = anode (A)

## Still to extract at build time
- USB-C `HRO_TYPE-C-31-M-12` footprint pad names → map VBUS / GND / CC1 / CC2 / D+ / D− / SHIELD
  (grep the `.kicad_mod` pad labels when writing the net assignments).

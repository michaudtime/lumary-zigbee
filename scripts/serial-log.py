#!/usr/bin/env python3
"""Log a board's serial output to a file, timestamped, surviving reboots.

Written for timing a Zigbee OTA at the bench (docs/research/ota-throughput.md):
the firmware logs one line per received OTA block, so a host-timestamped
capture gives the device's own view of the block cadence, and a panic or
watchdog reset mid-transfer lands in the file instead of scrolling past.

    python scripts/serial-log.py COM8 build/ota-logs/run.log

Opens the port with DTR and RTS held low BEFORE open. The ESP32-H2's
USB-Serial-JTAG treats a DTR/RTS toggle as a reset request, and a plain open
from .NET's SerialPort resets the board (seen 2026-09-18: rst:0x15
USB_UART_HPSYS) -- which would restart any OTA in flight.

If the port drops -- the board rebooted, or USB was pulled -- it logs that and
keeps retrying, so a reboot shows up as a gap plus a fresh boot banner.
"""

import datetime
import sys
import time

import serial


def stamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    port, path = sys.argv[1], sys.argv[2]

    def note(msg):
        with open(path, "a", encoding="utf-8") as f:
            f.write(f"{stamp()} [logger] {msg}\n")

    while True:
        try:
            s = serial.Serial()
            s.port = port
            s.baudrate = 115200
            s.timeout = 1
            s.dtr = False   # set before open, so open() never toggles them
            s.rts = False
            s.open()
            note(f"opened {port}")
            buf = b""
            with open(path, "a", encoding="utf-8", errors="replace") as f:
                while True:
                    data = s.read(4096)
                    if not data:
                        continue
                    buf += data
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        f.write(f"{stamp()} {line.decode('utf-8', 'replace').rstrip()}\n")
                    f.flush()
        except serial.SerialException as e:
            note(f"port error: {e}")
            time.sleep(1)
        except KeyboardInterrupt:
            note("stopped")
            return


if __name__ == "__main__":
    main()

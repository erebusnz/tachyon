#!/usr/bin/env python
"""Serial helper for the Tachyon bench bring-up.

Opens a COM port, optionally sends one or more lines (with a read pause after
each), then drains input for a final window and prints everything received.

  python serial_cli.py --port COM13 --send id
  python serial_cli.py --port COM13 --send "dac a mv 2500" --send "adc a"
  python serial_cli.py --port COM14 --eol cr --send n --send binmode   # Bus Pirate
  python serial_cli.py --port COM13 --read-ms 2000                      # just read

Defaults target the Tachyon USB-CDC console (CRLF). For the Bus Pirate terminal
use --eol cr.
"""
import argparse
import sys
import time

import serial

EOL = {"crlf": "\r\n", "cr": "\r", "lf": "\n", "none": ""}


def drain(sp, ms):
    out = bytearray()
    deadline = time.monotonic() + ms / 1000.0
    while time.monotonic() < deadline:
        n = sp.in_waiting
        if n:
            out += sp.read(n)
        else:
            time.sleep(0.02)
    return out.decode("ascii", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--send", action="append", default=[], help="line to send (repeatable)")
    ap.add_argument("--eol", choices=EOL.keys(), default="crlf")
    ap.add_argument("--read-ms", type=int, default=1200, help="final drain window")
    ap.add_argument("--step-ms", type=int, default=600, help="read window after each non-final send")
    ap.add_argument("--init-ms", type=int, default=300, help="read window right after open")
    ap.add_argument("--no-dtr", action="store_true", help="leave DTR/RTS deasserted")
    args = ap.parse_args()

    eol = EOL[args.eol]
    sp = serial.Serial()
    sp.port = args.port
    sp.baudrate = args.baud
    sp.timeout = 0.1
    sp.write_timeout = 1.0
    if args.no_dtr:
        sp.dtr = False
        sp.rts = False
    else:
        sp.dtr = True
        sp.rts = True
    sp.open()
    try:
        pre = drain(sp, args.init_ms)
        if pre:
            sys.stdout.write(pre)
        for i, line in enumerate(args.send):
            sp.write((line + eol).encode("ascii"))
            sp.flush()
            last = i == len(args.send) - 1
            sys.stdout.write(drain(sp, args.read_ms if last else args.step_ms))
        if not args.send:
            sys.stdout.write(drain(sp, args.read_ms))
    finally:
        sp.close()
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()

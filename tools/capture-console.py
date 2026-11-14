# @file capture-console.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-02
#
# @copyright Copyright (c) 2026
#
"""Capture TAB5 UART logs without resetting the board or changing firmware."""
import argparse
from pathlib import Path
import sys
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True)
parser.add_argument('--baud', type=int, default=115200)
parser.add_argument('--seconds', type=float, default=30)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
# TAB5's UART bridge uses DTR/RTS for download/reset. PySerial defaults both
# asserted; closing such a monitor can pulse EN through the auto-reset circuit.
# Set inactive levels BEFORE opening and leave them inactive when closing.
uart = serial.Serial(port=None, baudrate=args.baud, timeout=0.2)
uart.port = args.port
uart.dtr = False
uart.rts = False
uart.open()
try:
    log = args.output.open('wb')
    deadline = time.monotonic() + args.seconds
    try:
        while time.monotonic() < deadline:
            data = uart.read(max(1, uart.in_waiting))
            if data:
                log.write(data)
                log.flush()
                sys.stdout.write(data.decode('utf-8', errors='replace'))
                sys.stdout.flush()
    finally:
        log.close()
finally:
    uart.rts = False
    uart.dtr = False
    uart.close()

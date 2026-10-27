"""Capture radio diagnostics without changing DTR/RTS or sending commands."""
import argparse
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument('--port', default='COM17')
p.add_argument('--seconds', type=float, default=25)
p.add_argument('--output', required=True)
args = p.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=2)
port.dtr = False
port.rts = False
port.port = args.port
deadline = time.monotonic() + args.seconds
with port, Path(args.output).open('wb') as log:
    while time.monotonic() < deadline:
        block = port.read(4096)
        if block:
            log.write(block)
            log.flush()
print(f'Serial capture saved: {args.output}')

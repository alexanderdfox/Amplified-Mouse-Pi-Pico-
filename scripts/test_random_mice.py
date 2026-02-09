#!/usr/bin/env python3
"""
Send random (dx, dy) for each of 6 mouse slots over UART to test the Pico
without real mice. Useful to verify combined vs 6-separate-mice behaviour.

Usage:
  python3 scripts/test_random_mice.py /dev/ttyUSB0
  python3 scripts/test_random_mice.py --port /dev/cu.usbmodem101   # macOS
  python3 scripts/test_random_mice.py /dev/ttyUSB0 --duration 10 --rate 50
  python3 scripts/test_random_mice.py /dev/ttyUSB0 --single 0      # only mouse 0 (test separate mode)

Requires: pyserial. Use UART port (Linux) or Pico USB serial (macOS: /dev/cu.usbmodem101).
"""
import argparse
import random
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    sys.exit(1)

SYNC = 0xAA
PACKET_LEN = 15
NUM_MICE_MAX = 6


def clamp_s8(x):
    return max(-128, min(127, int(x)))


def make_packet(deltas):
    """deltas: list of 6 (dx, dy) pairs, each signed 8-bit. buttons=0, wheel=0."""
    buf = bytearray(PACKET_LEN)
    buf[0] = SYNC
    for i in range(NUM_MICE_MAX):
        dx, dy = deltas[i][0], deltas[i][1]
        buf[1 + i * 2] = clamp_s8(dx) & 0xFF
        buf[2 + i * 2] = clamp_s8(dy) & 0xFF
    buf[13] = 0  # buttons
    buf[14] = 0  # wheel
    return buf


def main():
    ap = argparse.ArgumentParser(
        description="Send random mouse movements over UART to test Pico (no real mice)"
    )
    ap.add_argument("port", nargs="?", default=None, help="Serial port (positional)")
    ap.add_argument("--port", "-p", dest="port_opt", metavar="DEV", help="Serial port (e.g. /dev/ttyUSB0 or /dev/cu.usbmodem101)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate")
    ap.add_argument("--duration", type=float, default=0, help="Run for N seconds (0 = until Ctrl+C)")
    ap.add_argument("--rate", type=float, default=50, help="Packets per second")
    ap.add_argument(
        "--magnitude", type=int, default=4,
        help="Max |dx|,|dy| per mouse per packet (default 4)"
    )
    ap.add_argument(
        "--single", type=int, metavar="N",
        help="Only move mouse N (0-5); others zero. Good for testing 6-separate mode."
    )
    args = ap.parse_args()

    if args.single is not None and (args.single < 0 or args.single >= NUM_MICE_MAX):
        print(f"--single must be 0..{NUM_MICE_MAX - 1}", file=sys.stderr)
        sys.exit(1)

    port = args.port_opt or args.port
    if not port:
        ap.error("port required (positional or --port)")
    ser = serial.Serial(port, args.baud, timeout=0)
    interval = 1.0 / args.rate
    mag = max(1, min(127, args.magnitude))
    start = time.monotonic()
    packets = 0

    print(
        f"Sending random movements to {port} at {args.rate} Hz "
        f"(±{mag} per axis per mouse). Ctrl+C to stop."
    )
    if args.single is not None:
        print(f"Only mouse {args.single} will move (others zero).")
    if args.duration > 0:
        print(f"Will run for {args.duration} s.")

    try:
        while True:
            deltas = [[0, 0] for _ in range(NUM_MICE_MAX)]
            if args.single is not None:
                i = args.single
                deltas[i][0] = random.randint(-mag, mag)
                deltas[i][1] = random.randint(-mag, mag)
            else:
                for i in range(NUM_MICE_MAX):
                    deltas[i][0] = random.randint(-mag, mag)
                    deltas[i][1] = random.randint(-mag, mag)

            ser.write(make_packet(deltas))
            packets += 1

            if args.duration > 0 and (time.monotonic() - start) >= args.duration:
                break

            next_until = start + (packets + 1) * interval
            sleep_time = next_until - time.monotonic()
            if sleep_time > 0:
                time.sleep(sleep_time)
    except KeyboardInterrupt:
        pass

    elapsed = time.monotonic() - start
    print(f"Sent {packets} packets in {elapsed:.1f} s ({packets / elapsed:.0f} Hz)")


if __name__ == "__main__":
    main()

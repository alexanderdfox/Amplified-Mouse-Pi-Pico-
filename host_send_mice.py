#!/usr/bin/env python3
"""
Read 6 mice from Linux evdev and send aggregated packets over serial to the Pico.
Usage: python3 host_send_mice.py /dev/ttyUSB0 [--baud 115200]
Requires: pyserial, evdev (pip install pyserial evdev). Run with access to /dev/input (e.g. user in group input).
"""
import argparse
import select
import struct
import sys

try:
    import evdev
except ImportError:
    print("Install evdev: pip install evdev", file=sys.stderr)
    sys.exit(1)
try:
    import serial
except ImportError:
    print("Install pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

SYNC = 0xAA
# Packet matches firmware: 1 sync + 6*(dx,dy) + buttons + wheel = 15 bytes. Firmware uses first config.NUM_MICE.
PACKET_LEN = 15
NUM_MICE_MAX = 6


def find_mice(limit=6):
    devices = []
    for path in evdev.list_devices():
        try:
            dev = evdev.InputDevice(path)
            caps = dev.capabilities()
            if evdev.ecodes.EV_REL in caps and evdev.ecodes.EV_KEY in caps:
                rels = caps[evdev.ecodes.EV_REL]
                if evdev.ecodes.REL_X in rels and evdev.ecodes.REL_Y in rels:
                    devices.append(dev)
                    if len(devices) >= limit:
                        break
        except (OSError, PermissionError):
            continue
    return devices


def main():
    ap = argparse.ArgumentParser(description="Send 6 mouse inputs to Pico over UART")
    ap.add_argument("port", help="Serial port (e.g. /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate")
    args = ap.parse_args()

    mice = find_mice(NUM_MICE_MAX)
    if len(mice) < NUM_MICE_MAX:
        print(f"Warning: found {len(mice)} mice (firmware may use fewer; check config.yaml).", file=sys.stderr)
    while len(mice) < NUM_MICE_MAX:
        mice.append(None)

    ser = serial.Serial(args.port, args.baud, timeout=0)
    state = [[0, 0, 0, 0] for _ in range(NUM_MICE_MAX)]

    def make_packet():
        buf = bytearray(PACKET_LEN)
        buf[0] = SYNC
        for i in range(NUM_MICE_MAX):
            dx, dy, btns, wheel = state[i]
            buf[1 + i * 2] = dx & 0xFF
            buf[2 + i * 2] = dy & 0xFF
        buf[13] = 0
        for i in range(NUM_MICE_MAX):
            buf[13] |= state[i][2] & 0x07
        buf[14] = 0
        for i in range(NUM_MICE_MAX):
            w = state[i][3]
            if w > 127:
                w = 127
            elif w < -128:
                w = -128
            buf[14] = (buf[14] + w) & 0xFF  # simple sum; or use first non-zero
        if buf[14] >= 128:
            buf[14] -= 256  # signed
        return buf

    for i in range(NUM_MICE_MAX):
        state[i][0] = state[i][1] = 0
        state[i][3] = 0

    active = [m for m in mice if m is not None]
    if not active:
        print("No mice found.", file=sys.stderr)
        sys.exit(1)

    print(f"Using {len(active)} mice. Sending to {args.port} at {args.baud}. Ctrl+C to stop.")
    while True:
        r, _, _ = select.select(active + [ser], [], [], 0.02)
        for i, dev in enumerate(mice):
            if dev is None:
                continue
            if dev in r:
                for event in dev.read():
                    if event.type == evdev.ecodes.EV_REL:
                        if event.code == evdev.ecodes.REL_X:
                            state[i][0] += event.value
                        elif event.code == evdev.ecodes.REL_Y:
                            state[i][1] += event.value
                        elif event.code == evdev.ecodes.REL_WHEEL:
                            state[i][3] += event.value
                    elif event.type == evdev.ecodes.EV_KEY:
                        if event.code in (evdev.ecodes.BTN_LEFT, evdev.ecodes.BTN_RIGHT, evdev.ecodes.BTN_MIDDLE):
                            bit = {evdev.ecodes.BTN_LEFT: 0, evdev.ecodes.BTN_RIGHT: 1, evdev.ecodes.BTN_MIDDLE: 2}[event.code]
                            if event.value:
                                state[i][2] |= 1 << bit
                            else:
                                state[i][2] &= ~(1 << bit)
        for i in range(NUM_MICE_MAX):
            for j in (0, 1):
                if state[i][j] > 127:
                    state[i][j] = 127
                elif state[i][j] < -128:
                    state[i][j] = -128
        pkt = make_packet()
        ser.write(pkt)
        for i in range(NUM_MICE_MAX):
            state[i][0] = state[i][1] = 0
            state[i][3] = 0


if __name__ == "__main__":
    main()

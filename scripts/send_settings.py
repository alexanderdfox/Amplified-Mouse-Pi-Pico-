#!/usr/bin/env python3
"""
Send runtime settings to the Pico over UART or USB CDC (setting file on device).
Reads config/config.yaml by default; optional CLI overrides. Use --no-save to apply
in RAM only (lost on reboot); default is to save to flash.

  python3 scripts/send_settings.py --port /dev/ttyACM0
  python3 scripts/send_settings.py --port /dev/cu.usbmodem101 --num-mice 4 --save
"""
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
CONFIG_YAML = ROOT / "config" / "config.yaml"

LOGIC_MODES = {
    "sum": 0, "average": 1, "max": 2,
    "min": 3, "and": 4, "or": 5, "xor": 6,
    "nand": 7, "nor": 8, "xnor": 9,
}
INPUT_MODES = {"uart": 0, "quadrature": 1, "both": 2}
OUTPUT_MODES = {"combined": 0, "separate": 1}

# UART config packet: 0x55 0xCF 0x01 N L I O A Q_lo Q_hi save (8 bytes payload)
UART_CONFIG_SYNC1 = 0x55
UART_CONFIG_SYNC2 = 0xCF
UART_CONFIG_CMD = 0x01


def load_yaml(path: Path) -> dict:
    out = {}
    if not path.exists():
        return out
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"(\w+):\s*(.+)", line)
            if m:
                key, val = m.group(1), m.group(2).strip().split("#")[0].strip()
                if not val:
                    continue
                if val.isdigit():
                    out[key] = int(val)
                else:
                    try:
                        out[key] = float(val)
                    except ValueError:
                        out[key] = val
    return out


def build_packet(num_mice: int, logic_mode: int, input_mode: int, output_mode: int,
                 amplify: float, quad_scale: int, save: bool) -> bytes:
    amplify_x100 = max(10, min(1000, int(round(amplify * 100))))
    q_lo = quad_scale & 0xFF
    q_hi = (quad_scale >> 8) & 0xFF
    return bytes([
        UART_CONFIG_SYNC1, UART_CONFIG_SYNC2, UART_CONFIG_CMD,
        num_mice & 0xFF, logic_mode & 0xFF, input_mode & 0xFF, output_mode & 0xFF,
        amplify_x100 & 0xFF, q_lo, q_hi,
        1 if save else 0,
    ])


def main() -> None:
    ap = argparse.ArgumentParser(description="Send settings to Pico over UART (setting file on device)")
    ap.add_argument("--port", "-p", required=True, metavar="DEV", help="Serial port (e.g. /dev/ttyACM0 or /dev/tty.usbmodem101)")
    ap.add_argument("--num-mice", type=int, metavar="N", help="Number of mice (2-6)")
    ap.add_argument("--logic-mode", choices=list(LOGIC_MODES), metavar="MODE", help="Logic mode")
    ap.add_argument("--input-mode", choices=list(INPUT_MODES), metavar="MODE", help="Input mode")
    ap.add_argument("--output-mode", choices=list(OUTPUT_MODES), metavar="MODE", help="Output: combined (1 mouse) or separate (6 mice)")
    ap.add_argument("--amplify", type=float, metavar="F", help="Amplification factor")
    ap.add_argument("--quad-scale", type=int, metavar="N", help="Quadrature scale")
    ap.add_argument("--no-save", action="store_true", help="Apply in RAM only (do not save to flash)")
    ap.add_argument("--baud", type=int, default=115200, help="UART baud (default 115200)")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        print("pip install pyserial", file=sys.stderr)
        raise SystemExit(1)

    cfg = load_yaml(CONFIG_YAML)
    num_mice = args.num_mice if args.num_mice is not None else int(cfg.get("num_mice", 6))
    logic_mode = LOGIC_MODES[args.logic_mode] if args.logic_mode is not None else LOGIC_MODES.get(
        str(cfg.get("logic_mode", "sum")).lower(), 0
    )
    input_mode = INPUT_MODES[args.input_mode] if args.input_mode is not None else INPUT_MODES.get(
        str(cfg.get("input_mode", "uart")).lower(), 0
    )
    output_mode = OUTPUT_MODES[args.output_mode] if args.output_mode is not None else OUTPUT_MODES.get(
        str(cfg.get("output_mode", "combined")).lower(), 0
    )
    amplify = args.amplify if args.amplify is not None else float(cfg.get("amplify", 1.0))
    quad_scale = args.quad_scale if args.quad_scale is not None else int(cfg.get("quad_scale", 2))

    if num_mice < 2 or num_mice > 6:
        raise SystemExit("num_mice must be 2-6")
    if quad_scale < 1:
        quad_scale = 1

    packet = build_packet(num_mice, logic_mode, input_mode, output_mode, amplify, quad_scale, save=not args.no_save)
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        ser.write(packet)
    print(f"Sent settings to {args.port}: num_mice={num_mice} logic={logic_mode} input={input_mode} output={output_mode} amplify={amplify} quad_scale={quad_scale} save={not args.no_save}")


if __name__ == "__main__":
    main()

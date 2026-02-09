# 6-Input Amplified Mouse (Raspberry Pi Pico)

Aggregates **6 mouse inputs** into a single USB HID mouse with optional **amplification** (scale factor on combined movement), or presents them as **6 separate HID mice** (one cursor per input). The Pico appears as either one or six mice to the PC; you can feed it from 6 physical mice via a host (UART or USB host hardware).

## Use cases

**Accessibility / limited mobility**  
Use several small movements (e.g. from different fingers, or light taps on multiple trackballs) that add up to normal cursor motion. Set `AMPLIFY` > 1 so small combined motion still moves the cursor enough.

**Collaborative control**  
Several people each have a mouse; all movements sum to one cursor (e.g. teaching, pair demos, or a shared kiosk). Buttons/wheel can be OR’d so any participant can click or scroll.

**Precision or “super” control**  
Run 2–6 low-DPI or low-sensitivity devices in parallel; the sum gives finer resolution or a higher effective sensitivity. Tune `AMPLIFY` in `main.c` to get the feel you want.

**Quick test (Option A)**  
1. Plug the Pico into the PC via USB (it shows up as one mouse).  
2. On the same PC, plug in a USB–UART adapter and connect its TX/RX/GND to the Pico’s UART (see wiring below).  
3. Plug up to 6 mice into the PC and run `host_send_mice.py /dev/ttyUSB0` (or your serial port). The “amplified” cursor is the one from the Pico; move any of the 6 mice to move it.

## Hardware options

### Option A: Pico + host PC/Raspberry Pi (UART)

- **Pico** USB → PC (enumerates as the amplified mouse).
- **Pico UART** (GP0 TX, GP1 RX) ↔ host (PC or Raspberry Pi).
- Host has 6 USB mice (or multiple hubs). A small daemon on the host reads all 6 mice, sums deltas, and sends one packet per frame over UART to the Pico.
- **Wiring:** Connect host UART (e.g. USB–UART adapter or RPi UART) to Pico: host TX → GP1 (RX), host RX → GP0 (TX), GND → GND. 3.3 V only.

### Option B: Standalone Pico + USB host (6 mice directly)

- **USB host chip** (e.g. MAX3421E on SPI) + **USB hub** (e.g. 7‑port) so the Pico can talk to 6 mice as USB host.
- Pico’s **built-in USB** = device (the single “amplified” mouse to the PC).
- This requires implementing (or porting) a USB host + HID driver for the MAX3421E and parsing 6 HID mouse reports, then feeding the same aggregation logic. Not included in this repo yet.

### Option C: 6 ball/optical sensors wired directly to Pico

Wire **6 mouse ball encoders** (quadrature) or **6 optical flow sensors** (SPI) straight to the Pico. No host PC or USB host chip needed: the Pico reads motion from GPIO/SPI and outputs one combined USB HID mouse.

**C1 – Quadrature encoders (classic ball mice)**  
Each ball mouse has two axes (X, Y); each axis uses a quadrature encoder (two pins: A, B). So **4 pins per mouse** (X_A, X_B, Y_A, Y_B). For 6 mice you need **24 GPIOs**.

| Mouse | X_A | X_B | Y_A | Y_B | Pico GPIO |
|-------|-----|-----|-----|-----|-----------|
| 0     | pin 1 | pin 2 | pin 3 | pin 4 | GP2, GP3, GP4, GP5 |
| 1     | pin 1 | pin 2 | pin 3 | pin 4 | GP6, GP7, GP8, GP9 |
| 2     | pin 1 | pin 2 | pin 3 | pin 4 | GP10, GP11, GP12, GP13 |
| 3     | pin 1 | pin 2 | pin 3 | pin 4 | GP14, GP15, GP16, GP17 |
| 4     | pin 1 | pin 2 | pin 3 | pin 4 | GP18, GP19, GP20, GP21 |
| 5     | pin 1 | pin 2 | pin 3 | pin 4 | GP22, GP23, GP24, GP25 |

- **Wiring:** From each ball-mouse encoder PCB (or salvaged encoder):  
  - **VCC** → 3.3 V (Pico `3V3`), **GND** → GND.  
  - **Channel A (X)** → X_A GPIO, **Channel B (X)** → X_B GPIO; same for Y.  
- Use **3.3 V only**; do not connect 5 V to GPIO.  
- The firmware enables internal pull-ups on the quadrature pins; if your encoders are open-collector, that’s enough. If they drive 3.3 V, pull-ups are optional.  
- **Build:** Set `input_mode: quadrature` in `config.yaml`, run `python3 configure.py`, then build. UART is unused in this mode. Tune `quad_scale` in config if the cursor is too fast or too slow.

**C2 – Optical flow sensors (e.g. ADNS-2610, PMW3360) over SPI**  
One shared SPI bus plus one chip-select (CS) per sensor: e.g. SPI0 on default pins, CS on GP2–GP7 for 6 sensors. Firmware would read motion registers and fill `g_mice[]`; this variant can be added as a separate build option if you use such sensors.

## Full workflow

1. **Configure** – Edit `config.yaml` (num_mice, logic_mode, input_mode, amplify, quad_scale) or use CLI, then run `python3 configure.py`.
2. **Build** – `mkdir -p build && cd build && cmake .. && make` (set `PICO_SDK_PATH` if the SDK is not in the repo).
3. **Flash** – Copy `build/amplified_mouse.uf2` onto the Pico (hold BOOTSEL, plug USB, then drag the file).
4. **UART option** – For Option A, run `python3 host_send_mice.py /dev/ttyUSB0` (or your port). The host sends 15-byte packets; the firmware uses the first N mice from **runtime settings** (from flash or config.h defaults).
5. **Optional: push settings to Pico** – Run `python3 send_settings.py --port /dev/ttyACM0` to write the current config to the Pico (and optionally save to flash). No reflash needed.

## Build (Pico SDK)

If the repo has a local `pico-sdk` clone, you don’t need to set `PICO_SDK_PATH`. After changing settings, run **`python3 configure.py`** so `config.h` is up to date. Then:

```bash
mkdir -p build && cd build
cmake ..
make
```

Flash: copy `amplified_mouse.uf2` to the Pico (bootsel mode).

### macOS: fix “cannot read spec file 'nosys.specs'”

Homebrew’s `arm-none-eabi-gcc` does not include newlib, so the Pico SDK build can fail with that error. Use the **gcc-arm-embedded** cask instead (full toolchain with newlib):

```bash
# Remove the incomplete toolchain
brew uninstall arm-none-eabi-gcc arm-none-eabi-binutils 2>/dev/null

# Install the full ARM embedded toolchain (includes nosys.specs)
brew install --cask gcc-arm-embedded
```

The cask installs under `/Applications/ArmGNUToolchain/`. If `arm-none-eabi-gcc` is not on your `PATH` after that, add the bin directory and point CMake at it when configuring:

```bash
export PATH="/Applications/ArmGNUToolchain/$(ls /Applications/ArmGNUToolchain/ 2>/dev/null | head -1)/bin:$PATH"
cd build && cmake .. -DCMAKE_C_COMPILER=arm-none-eabi-gcc -DCMAKE_CXX_COMPILER=arm-none-eabi-g++ && make
```

If the cask uses a versioned folder (e.g. `13.3.rel1`), use that in `PATH` instead of the `ls` above.

## Configuring firmware (configure.py)

Instead of editing `main.c`, you can change settings via **config.yaml** and regenerate **config.h**:

```bash
# Edit config.yaml (num_mice, logic_mode, input_mode, amplify, quad_scale), then:
python3 configure.py

# Or override from the command line:
python3 configure.py --num-mice 2 --logic-mode or --input-mode quadrature
python3 configure.py --list   # show logic/input options
```

Then build as usual (`cd build && make`). **config.h** is generated from **config.yaml** (or CLI) and is included by `main.c`.

- **config.yaml** – `num_mice` (2–6), `logic_mode` (sum, average, max, min, and, or, xor, nand, nor, xnor), `input_mode` (uart, quadrature, both), `amplify`, `quad_scale`.

### Setting file on the Pico (runtime + flash)

The firmware keeps a **runtime settings** block in the Pico’s flash (last 4 KB). At boot it loads defaults from **config.h**, then overwrites with saved settings from flash if present. You can change settings **without reflashing** by sending a config packet over UART:

1. **Apply only in RAM** (lost on reboot): send the config packet with “save” = 0.
2. **Apply and save to flash**: send with “save” = 1; the new values persist across reboots.

Use **send_settings.py** to push current config (from `config.yaml` or CLI) to the Pico:

```bash
# Save to flash (persist across reboot)
python3 send_settings.py --port /dev/ttyACM0

# Apply in RAM only
python3 send_settings.py --port /dev/tty.usbmodem101 --no-save

# Override and save
python3 send_settings.py --port /dev/ttyACM0 --num-mice 4 --logic-mode or
```

Config packet format (UART): sync `0x55` `0xCF`, command `0x01`, then 8 bytes: `num_mice`, `logic_mode`, `input_mode`, `output_mode`, `amplify_x100`, `quad_scale` (2 bytes low/high), `save` (0 or 1). Total 11 bytes. Normal mouse packets still use sync `0xAA`; the Pico distinguishes the two.

## Configuration reference

Settings are in **config.yaml** (then `configure.py` → **config.h**). Reference:
  - **`logic_mode`** – How inputs are combined:
    - **All inputs:** `LOGIC_MODE_SUM` (default), `LOGIC_MODE_AVERAGE`, `LOGIC_MODE_MAX`.
    - **2-ball only** (uses only mouse 0 and mouse 1): `LOGIC_MODE_2_MIN`, `LOGIC_MODE_2_AND`, `LOGIC_MODE_2_OR`, `LOGIC_MODE_2_XOR`, `LOGIC_MODE_2_NAND`, `LOGIC_MODE_2_NOR`, `LOGIC_MODE_2_XNOR`. See table below.
  - **2-ball logic (per axis, A = mouse 0, B = mouse 1):**

    | Mode   | Result |
    |--------|--------|
    | MIN    | Value with smaller magnitude (signed). |
    | AND    | Output only when both non-zero and same sign; value = smaller \|v\|. |
    | OR     | A + B (either or both). |
    | XOR    | If one zero → other; if both non-zero → A − B. |
    | NAND   | Both non-zero → 0; else A + B. |
    | NOR    | Always 0. |
    | XNOR   | Same sign and both non-zero → (A+B)/2; opposite sign → 0; one zero → the other. |

  - **`input_mode`** – `uart`, `quadrature`, or `both`.
  - **`output_mode`** – `combined` (one aggregated HID mouse) or `separate` (six independent HID mice; host sees 6 cursors). When `separate`, input 0→mouse 0, input 1→mouse 1, etc.
  - **`num_mice`** – 2–6. Quadrature and UART use the first N inputs.
  - **`amplify`** – Scale factor (e.g. 1.5 = 50% more movement); applies in `combined` mode only.
  - **`quad_scale`** – Quadrature counts per HID step (quadrature mode only).
- Custom quadrature pins: edit **`QUAD_PINS`** in `main.c` if your wiring differs from the default (Mouse 0 = GP2–GP5 … Mouse 5 = GP22–GP25).
- **`tusb_config.h`** – TinyUSB HID buffer size if you change report size.

## UART protocol (Option A)

One packet per “frame”:

| Byte(s)   | Content |
|-----------|--------|
| 0         | Sync `0xAA` |
| 1–12      | 6 × (dx, dy) as signed 8‑bit (mouse 0..5) |
| 13        | Buttons (bit 0 = left, 1 = right, 2 = middle) |
| 14        | Wheel (signed 8‑bit) |

Total **15 bytes** per packet (sync + 12 + 1 + 1). The Pico sums the first N (dx, dy) from runtime settings, applies the current amplify factor, clamps to one HID report, and sends one combined mouse report (buttons and wheel from the single bytes).

**Config packet** (separate from mouse data): sync `0x55` `0xCF`, cmd `0x01`, then 8 bytes (num_mice, logic_mode, input_mode, output_mode, amplify_x100, quad_scale low/high, save). See **send_settings.py** and “Setting file on the Pico” above.

## Example: host script (Linux, 6 mice → UART)

A Python script that reads 6 mice from `/dev/input/event*` and sends the above packet over serial is in `host_send_mice.py`. Requires `pyserial` and access to input devices (e.g. add user to `input` group).

```bash
pip install pyserial
python3 host_send_mice.py /dev/ttyUSB0
```

(Use the correct serial port for your UART adapter.)

## Summary

- **6 inputs** → summed (dx, dy) + one buttons byte + one wheel byte.
- **Amplification** = multiply combined movement by `AMPLIFY` in `main.c`.
- **Output** = single USB HID mouse (TinyUSB device on Pico).

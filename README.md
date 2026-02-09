# 6-Input Amplified Mouse (Raspberry Pi Pico)

Aggregates **6 mouse inputs** into a single USB HID mouse with optional **amplification** (scale factor on combined movement). The Pico appears as one mouse to the PC; you can feed it from 6 physical mice via a host (UART or USB host hardware).

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
- **Build:** In `main.c` set `INPUT_MODE` to `INPUT_MODE_QUADRATURE` (see Configuration). Rebuild and flash; UART is unused in this mode. Tune `QUAD_SCALE` (default 2) if cursor is too fast or too slow.

**C2 – Optical flow sensors (e.g. ADNS-2610, PMW3360) over SPI**  
One shared SPI bus plus one chip-select (CS) per sensor: e.g. SPI0 on default pins, CS on GP2–GP7 for 6 sensors. Firmware would read motion registers and fill `g_mice[]`; this variant can be added as a separate build option if you use such sensors.

## Build (Pico SDK)

If the repo has a local `pico-sdk` clone, you don’t need to set `PICO_SDK_PATH`. Otherwise:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
mkdir build && cd build
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

## Configuration (in code)

- **`main.c`**
  - **`INPUT_MODE`** – `INPUT_MODE_UART` (default), `INPUT_MODE_QUADRATURE` (6 ball encoders on GPIO), or `INPUT_MODE_BOTH` (UART + quadrature).
  - **`QUAD_PINS`** – For quadrature mode: 6 mice × 4 pins (X_A, X_B, Y_A, Y_B). Default: Mouse 0 = GP2–GP5 … Mouse 5 = GP22–GP25. Change if your wiring differs.
  - `NUM_MICE` = 6
  - `AMPLIFY` = scale factor (e.g. `1.5f` = 50% more movement)
  - `UART_BAUD`, `UART_TX_PIN`, `UART_RX_PIN` for UART input (ignored when `INPUT_MODE` is quadrature-only).
- **`tusb_config.h`** – TinyUSB HID buffer size if you change report size

## UART protocol (Option A)

One packet per “frame”:

| Byte(s)   | Content |
|-----------|--------|
| 0         | Sync `0xAA` |
| 1–12      | 6 × (dx, dy) as signed 8‑bit (mouse 0..5) |
| 13        | Buttons (bit 0 = left, 1 = right, 2 = middle) |
| 14        | Wheel (signed 8‑bit) |

Total **15 bytes** per packet (sync + 12 + 1 + 1). The Pico sums the 6 (dx, dy), applies `AMPLIFY`, clamps to one HID report, and sends one combined mouse report (buttons and wheel from the single bytes).

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

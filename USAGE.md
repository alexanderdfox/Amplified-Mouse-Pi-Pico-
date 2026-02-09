# Usage guide: 6-input amplified mouse

This document describes **possible use cases**, **hardware setups**, and **configuration choices** for the 6-input amplified mouse firmware on Raspberry Pi Pico. Use it to decide how to wire, configure, and operate the device for your scenario.

---

## What this device does

The firmware turns **multiple mouse-like inputs** (2–6) into **one USB HID mouse**. The Pico appears as a single mouse to the computer. You choose:

- **Where input comes from:** UART (host PC/RPi sending motion), quadrature encoders (e.g. ball mice wired to GPIO), or both.
- **How inputs are combined:** sum, average, max, or one of several 2-input logic modes (min, and, or, xor, etc.).
- **How strong the output is:** an amplification factor (e.g. 1.5×) so small combined motion still moves the cursor enough, or to tune feel.

Settings can be baked in at build time (**config.yaml** → **config.h** → reflash) or pushed at runtime over UART and optionally saved in the Pico’s flash (**send_settings.py**), so you can change behavior without reflashing.

---

## Use case 1: Accessibility and limited mobility

**Goal:** Use several small or low-effort movements that add up to normal cursor motion, so someone with limited range or strength can still control the pointer.

### Scenario A: Multiple fingers or limbs

- **Idea:** Several small trackballs or touchpads (or one device with multiple “zones”) each send motion. Combined movement reaches the target; amplification makes small total motion sufficient.
- **Hardware:** Option A (UART): host PC has several USB mice or evdev sources; `host_send_mice.py` aggregates them and sends to the Pico. Or Option C (quadrature): several ball encoders wired to the Pico.
- **Settings:**
  - **logic_mode:** `sum` (all contributions add).
  - **num_mice:** 2–6 depending on how many input devices you have.
  - **amplify:** Start at 1.5–2.0; increase if the cursor still feels sluggish for small movements.
- **Workflow:**
  1. Build and flash with `num_mice` and `input_mode` matching your setup.
  2. Run `host_send_mice.py` (UART) or use quadrature wiring.
  3. Tune `amplify` via `config.yaml` + `configure.py` + reflash, or push at runtime with `send_settings.py --port /dev/ttyACM0` and optionally save to flash.

### Scenario B: One strong and one weak input

- **Idea:** One input (e.g. dominant hand) does most of the work; the other (e.g. other hand or foot) adds fine correction or extra range. Sum still works; you can also try **average** so the cursor doesn’t run away if one input is noisy.
- **Settings:**
  - **logic_mode:** `sum` for “both add”; `average` if you want to dampen the effect of one strong input.
  - **amplify:** Slightly above 1.0 (e.g. 1.2) can help small corrections become visible.

### Scenario C: Two inputs, “either or both” (OR)

- **Idea:** Use either input alone or both together; movement is the sum when both move. Good when either hand might be used.
- **Settings:** **logic_mode:** `or` (2-ball mode; only mouse 0 and 1 are used). **num_mice:** 2.

### Tips

- Use **runtime settings** (`send_settings.py` and save to flash) to adjust amplification or logic without reflashing when testing with the user.
- For quadrature ball mice, increase **quad_scale** if the cursor is too fast (more encoder ticks per HID step).

---

## Use case 2: Collaborative control (shared cursor)

**Goal:** Several people share one cursor—e.g. teaching, pair programming, or a kiosk where multiple inputs influence the same pointer.

### Scenario A: Everyone’s movement adds (teaching / demos)

- **Idea:** Instructor and student(s) each have a mouse; all movements sum so anyone can move the cursor. Buttons/wheel are OR’d (anyone can click or scroll).
- **Hardware:** Option A (UART): one PC, multiple USB mice, UART to Pico; Pico USB = the “shared” mouse.
- **Settings:**
  - **logic_mode:** `sum`.
  - **num_mice:** Number of participants (2–6).
  - **amplify:** Often 1.0; if the summed motion is too strong, use `average` so the cursor speed stays moderate.
- **Workflow:** Flash once; optionally use `send_settings.py` to switch between 2/4/6 mice or sum vs average without reflashing.

### Scenario B: “Average” so no one dominates

- **Idea:** Cursor moves in the average direction of all inputs, so one strong push doesn’t overwhelm the others.
- **Settings:** **logic_mode:** `average`. **num_mice:** 2–6.

### Scenario C: “Largest movement wins” (MAX)

- **Idea:** Only the input with the biggest motion on each axis drives the cursor (e.g. “whoever moves most this frame wins”). Useful for games or turn-taking styles.
- **Settings:** **logic_mode:** `max`. **num_mice:** 2–6.

### Scenario D: Two-person collaboration (2-ball modes)

- **Idea:** Exactly two inputs with special semantics (e.g. MIN = smaller movement wins; AND = both must agree in direction; XOR = subtract one from the other for cancellation effects).
- **Settings:** **logic_mode:** `min` | `and` | `or` | `xor` | `nand` | `nor` | `xnor`. **num_mice:** 2.

### Tips

- For a **kiosk**, set **num_mice** and **logic_mode** once (e.g. sum or average), save to flash with `send_settings.py`, so the unit keeps the same behavior after power cycle.
- Use **input_mode: uart** when the host PC is doing the aggregation; use **quadrature** or **both** only if you have physical encoders on the Pico.

---

## Use case 3: Precision and “super” control

**Goal:** Get finer resolution or higher effective sensitivity by combining several low-DPI or low-sensitivity devices, with optional amplification.

### Scenario A: Higher effective DPI (sum of 2–6 devices)

- **Idea:** Run 2–6 low-DPI mice in parallel; the sum of their deltas gives finer steps. Amplification can then scale the combined motion to match desired cursor speed.
- **Hardware:** Option A: host reads multiple evdev mice, sends one UART packet per frame.
- **Settings:**
  - **logic_mode:** `sum`.
  - **num_mice:** 2–6.
  - **amplify:** Tune to taste (e.g. 1.0–2.0) so cursor speed feels right.
- **Workflow:** Build/flash; run `host_send_mice.py`. Use `send_settings.py` to tweak `amplify` at runtime and save to flash when satisfied.

### Scenario B: Two inputs, “smaller movement wins” (MIN)

- **Idea:** Use the input with the smaller magnitude on each axis—e.g. for very fine, conservative control when two devices are available.
- **Settings:** **logic_mode:** `min`. **num_mice:** 2.

### Scenario C: Average for smoothing

- **Idea:** Several noisy or jittery inputs; averaging reduces jitter while keeping responsiveness.
- **Settings:** **logic_mode:** `average`. **num_mice:** 2–6.

### Tips

- **quad_scale** (quadrature mode): Increase for finer steps (more encoder counts per HID step); decrease if the cursor is too slow.

---

## Use case 4: Custom hardware (quadrature ball mice)

**Goal:** Wire 2–6 ball-mouse encoders (or other quadrature sources) directly to the Pico; no host PC in the loop. The Pico is the only “computer” between encoders and the USB mouse.

### Scenario A: Salvaged ball mice

- **Idea:** Take quadrature outputs (X_A, X_B, Y_A, Y_B) from 2–6 ball mice, connect to Pico GPIO (see README pinout). One USB cable from Pico to PC = one combined mouse.
- **Hardware:** Option C1 in README: 4 pins per mouse (e.g. GP2–GP5 for mouse 0, … GP22–GP25 for mouse 5). 3.3 V only; internal pull-ups on.
- **Settings:**
  - **input_mode:** `quadrature` (or `both` if you also send UART).
  - **num_mice:** 2–6.
  - **quad_scale:** Start at 2; increase if cursor is too fast, decrease if too slow.
  - **logic_mode:** `sum` (or any other mode as needed).
- **Workflow:**
  1. Edit `config.yaml`: `input_mode: quadrature`, set `num_mice`, `quad_scale`, `logic_mode`, `amplify`.
  2. Run `configure.py`, build, flash.
  3. Optionally push changes later with `send_settings.py` (UART must be connected for config packets; you can use `input_mode: both` and leave host UART idle if you only use quadrature for motion).

### Scenario B: Mixed UART + quadrature

- **Idea:** Some inputs from host over UART (e.g. virtual or USB mice), some from quadrature encoders on the Pico. Both contribute to the same combined mouse.
- **Settings:** **input_mode:** `both`. Configure **num_mice** and **logic_mode** as needed; UART and quadrature fill the same `g_mice[]` slots (see firmware: UART fills first N, quadrature fills its indices).

### Tips

- Pinout is in README; **QUAD_PINS** in `main.c` can be changed if your wiring differs.
- **quad_scale** is “encoder ticks per HID step”; larger = slower cursor for the same ball movement.

---

## Use case 5: Testing and development

**Goal:** Quickly try different logic modes, mouse counts, and amplification without reflashing every time.

### Scenario A: Quick UART test with 6 mice

- **Idea:** Pico as USB mouse + UART connected to host. Host runs `host_send_mice.py`; you move 6 USB mice and see the combined cursor. Change behavior by pushing new settings over UART.
- **Workflow:**
  1. Flash a build that includes UART (`input_mode: uart` or `both`).
  2. Run `host_send_mice.py /dev/ttyUSB0` (or your UART port).
  3. Change `config.yaml` (or use CLI) and run `send_settings.py --port /dev/ttyACM0` to apply and optionally save. No reflash.

### Scenario B: Try logic modes without reflash

- **Idea:** Cycle through sum, average, max, or 2-ball modes (or, xor, min, etc.) by sending config packets.
- **Workflow:** Use `send_settings.py` with different `--logic-mode` and `--num-mice` (e.g. 2 for 2-ball modes). Use `--no-save` to test in RAM; omit it to save to flash when happy.

### Scenario C: Tune amplification live

- **Idea:** Adjust `amplify` until cursor speed feels right, then save to flash.
- **Workflow:** `send_settings.py --port /dev/ttyACM0 --amplify 1.5` (then 1.2, 2.0, etc.). When satisfied, run again without `--no-save` so it persists.

### Tips

- **send_settings.py** reads **config.yaml** by default; use CLI flags to override. The Pico’s serial port (USB CDC) is often `/dev/ttyACM0` (Linux) or `/dev/tty.usbmodem*` (macOS); the UART adapter is a different port (e.g. `/dev/ttyUSB0`).

---

## Use case 6: Kiosks and fixed installations

**Goal:** One configuration that persists across power cycles; no keyboard or display on the device.

### Scenario

- **Idea:** Pico is built into a kiosk with N mice (UART host or quadrature). You set num_mice, logic_mode, amplify, and input_mode once; they must survive reboot.
- **Workflow:**
  1. Build and flash with sensible defaults in config.h.
  2. Connect UART (if using UART input or for config). Run `send_settings.py --port /dev/ttyACM0` with the desired options (no `--no-save`). Settings are written to flash.
  3. On every boot, the Pico loads from flash and uses those settings. No need to run send_settings again unless you want to change behavior.

### Tips

- Use **input_mode: uart** or **quadrature** (not necessarily **both**) to match the installation. Save to flash so no host script is required after power-up.

---

## Use case 7: Education and experimentation

**Goal:** Understand multi-input aggregation, logic modes, and embedded USB HID.

### Scenarios

- **Sum vs average vs max:** Flash with 2 mice (UART), try `sum`, `average`, `max` via `send_settings.py` and observe how cursor response changes.
- **2-ball logic:** Set **num_mice: 2**, try `or`, `xor`, `and`, `min`, `nand`, `nor`, `xnor` and document the resulting behavior (e.g. “XOR gives cancellation when both move”).
- **Quadrature:** Wire one or two ball encoders to the Pico, set **input_mode: quadrature**, **num_mice: 2**, and **quad_scale**; see how scale affects cursor speed.
- **Runtime vs compile-time:** Change config in config.yaml, run configure.py, but don’t reflash; instead use send_settings to push the same values. Then change again and reflash to see that flash-stored settings override config.h at boot.

---

## Choosing logic mode (quick reference)

| Goal | logic_mode | num_mice |
|------|------------|----------|
| All inputs add | `sum` | 2–6 |
| Dampen; average direction | `average` | 2–6 |
| Largest movement wins | `max` | 2–6 |
| Smaller magnitude wins | `min` | 2 |
| Either or both add | `or` | 2 |
| Both must agree (same sign) | `and` | 2 |
| One cancels the other | `xor` | 2 |
| Both non-zero → 0; else sum | `nand` | 2 |
| Always 0 | `nor` | 2 |
| Same sign → average; else 0 / other | `xnor` | 2 |

---

## Choosing input mode

| input_mode | Use when |
|------------|----------|
| `uart` | Host (PC/RPi) sends motion over UART (e.g. host_send_mice.py). |
| `quadrature` | 2–6 quadrature encoders wired to Pico GPIO; no UART motion. |
| `both` | You have both UART and quadrature connected; both feed the same combined mouse. |

---

## Compile-time vs runtime settings

- **Compile-time:** Edit **config.yaml**, run **configure.py**, then **build and reflash**. Defines defaults and is required for a valid build (e.g. NUM_MICE 2–6).
- **Runtime:** Send a config packet over UART (e.g. via **send_settings.py**). Applied immediately. With **save** = 1, settings are stored in the Pico’s flash and loaded on every boot, so they override the compile-time defaults until you overwrite them again or reflash.

Use **runtime + save** when you want to change behavior without reflashing (e.g. accessibility tuning, kiosk setup, or trying different modes during development).

---

## Summary by scenario

| Scenario | Typical input_mode | Typical logic_mode | Notes |
|----------|--------------------|--------------------|-------|
| Accessibility (multiple small inputs) | uart or quadrature | sum | Increase amplify. |
| Collaborative (shared cursor) | uart | sum or average | num_mice = number of users. |
| Precision (combined DPI) | uart | sum | Tune amplify. |
| Salvaged ball mice | quadrature | sum | Set quad_scale. |
| Testing / try modes | uart or both | any | Use send_settings.py, --no-save to experiment. |
| Kiosk | uart or quadrature | sum or average | Save settings to flash once. |

For build and wiring details, see **README.md**. For config field reference, see **Configuration reference** in README.

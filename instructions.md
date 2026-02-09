# Instructions: config after UF2 firmware is installed

After you have flashed **amplified_mouse.uf2** onto the Pico (hold BOOTSEL, plug USB, drag the file to the removable drive), you can change settings **without reflashing** by sending a config packet over serial. Optionally you can save settings to the Pico’s flash so they persist across reboots.

---

## 1. Which serial port is the Pico?

The firmware exposes **USB CDC (serial)** as well as the HID mice over the **same USB cable**. So once the Pico is plugged in and the firmware is running, the host should create a serial port for it.

- **Linux:** The Pico usually shows up as `/dev/ttyACM0` (or `ttyACM1`, etc.). List with:  
  `ls /dev/ttyACM* /dev/ttyUSB*`
- **macOS:** Usually `/dev/cu.usbmodem*` or `/dev/tty.usbmodem*` (e.g. `/dev/cu.usbmodem101`). List with:  
  `ls /dev/cu.usbmodem* /dev/tty.usbmodem*`
- **Windows:** In Device Manager, look for a “USB Serial Device” or “Pico” COM port (e.g. `COM3`).

**If the Pico does not show in /dev:** Reflash the latest firmware (the build includes USB CDC). Use the **Pico’s USB serial port** for **scripts/send_settings.py**.  
If you also use a **USB–UART adapter** for mouse data (Option A), that adapter has a **different** port (e.g. `/dev/ttyUSB0` on Linux). **Config** = Pico’s USB serial; **mouse data** = UART adapter (if used).

---

## 2. Configure once the firmware is running

You can change **num_mice**, **logic_mode**, **input_mode**, **output_mode**, **amplify**, and **quad_scale** at runtime.

### Option A: Push settings from your PC (no reflash)

1. Install dependency (if needed):  
   `pip install pyserial`

2. Edit **config/config.yaml** in the project with the values you want (or skip and use CLI flags).

3. From the project root, run **scripts/send_settings.py** with the **Pico’s USB serial port** (see section 1):

   ```bash
   # Use config/config.yaml and save to Pico flash (persists after power cycle)
   python3 scripts/send_settings.py --port /dev/ttyACM0

   # macOS example
   python3 scripts/send_settings.py --port /dev/cu.usbmodem101
   ```

4. To **apply in RAM only** (lost on reboot), add `--no-save`:

   ```bash
   python3 scripts/send_settings.py --port /dev/ttyACM0 --no-save
   ```

5. To override **config/config.yaml** from the command line:

   ```bash
   python3 scripts/send_settings.py --port /dev/ttyACM0 --num-mice 4 --logic-mode sum --output-mode separate
   ```

After a successful run, the Pico uses the new settings immediately. If you did not use `--no-save`, they are stored in flash and will still be used after the next power cycle.

---

## 3. Saving settings to flash (persist after reboot)

- **With save (default):**  
  `python3 scripts/send_settings.py --port /dev/ttyACM0`  
  Settings are written to the Pico’s flash and loaded automatically on every boot.

- **Without save:**  
  `python3 scripts/send_settings.py --port /dev/ttyACM0 --no-save`  
  Settings apply only until the next reset or power cycle; flash is unchanged.

---

## 4. If you prefer to bake settings into the firmware (reflash)

1. Edit **config/config.yaml** (num_mice, logic_mode, input_mode, output_mode, amplify, quad_scale).
2. Run **scripts/configure.py** to regenerate **config/config.h**:  
   `python3 scripts/configure.py`
3. Rebuild and copy UF2:  
   `./build.sh`
4. Flash the new **firmware/amplified_mouse.uf2** to the Pico again (BOOTSEL, drag UF2).

These values become the **defaults** at boot. Any settings previously saved to flash (via send_settings with save) will still **override** these defaults until you send a new config with save, or reflash and clear the settings sector.

---

## 5. Quick reference

| Goal                         | Command / step |
|-----------------------------|----------------|
| Apply config and save to flash | `python3 scripts/send_settings.py -p /dev/ttyACM0` |
| Apply config, don’t save    | `python3 send_settings.py -p /dev/ttyACM0 --no-save` |
| Six separate mice          | `--output-mode separate` (or set in config/config.yaml) |
| Change defaults and reflash| Edit config/config.yaml → `python3 scripts/configure.py` → `./build.sh` → flash UF2 |

For all config options (logic_mode, input_mode, etc.), see **README.md** (Configuration reference) and **config/config.yaml** comments.

---

## 6. macOS notes

- **Pico serial port:** Use `/dev/cu.usbmodem*` or `/dev/tty.usbmodem*` (e.g. `/dev/cu.usbmodem101`). Prefer `cu` for scripting:  
  `ls /dev/cu.usbmodem*`

- **send_settings.py on macOS:**  
  `python3 scripts/send_settings.py --port /dev/cu.usbmodem101`

- **host_send_mice.py** is **Linux-only** (evdev). On macOS you can **test the 6 mice** with **test_random_mice.py** over the Pico's USB serial (no UART adapter): `python3 scripts/test_random_mice.py --port /dev/cu.usbmodem101`

- **List USB devices (including mice) on macOS:**  
  `system_profiler SPUSBDataType`  
  Or:  
  `ioreg -p IOUSB -l -w 0 | grep -E '"Product"|"Vendor"'`

# Instructions: config after UF2 firmware is installed

After you have flashed **amplified_mouse.uf2** onto the Pico (hold BOOTSEL, plug USB, drag the file to the removable drive), you can change settings **without reflashing** by sending a config packet over serial. Optionally you can save settings to the Pico’s flash so they persist across reboots.

---

## 1. Which serial port is the Pico?

The Pico has **one USB connection**. When the firmware is running, that USB is used as the **HID mouse** (and optionally as a **USB serial / CDC** port for configuration).

- **Linux:** The Pico often shows up as `/dev/ttyACM0` (or `ttyACM1`, etc.). List with:  
  `ls /dev/ttyACM* /dev/ttyUSB*`
- **macOS:** Usually `/dev/tty.usbmodem101` or similar. List with:  
  `ls /dev/tty.usbmodem* /dev/cu.usbmodem*`
- **Windows:** Check Device Manager for a “USB Serial Device” or “Pico” COM port (e.g. `COM3`).

Use this **Pico USB serial port** for **send_settings.py** (and for any serial console).  
If you also use a **USB–UART adapter** to send mouse data (Option A), that adapter will have a **different** port (e.g. `/dev/ttyUSB0` on Linux). Don’t confuse the two: **config** = Pico’s USB serial; **mouse data** = UART adapter (if used).

---

## 2. Configure once the firmware is running

You can change **num_mice**, **logic_mode**, **input_mode**, **output_mode**, **amplify**, and **quad_scale** at runtime.

### Option A: Push settings from your PC (no reflash)

1. Install dependency (if needed):  
   `pip install pyserial`

2. Edit **config.yaml** in the project with the values you want (or skip and use CLI flags).

3. From the project root, run **send_settings.py** with the **Pico’s USB serial port** (see section 1):

   ```bash
   # Use config.yaml and save to Pico flash (persists after power cycle)
   python3 send_settings.py --port /dev/ttyACM0

   # macOS example
   python3 send_settings.py --port /dev/tty.usbmodem101
   ```

4. To **apply in RAM only** (lost on reboot), add `--no-save`:

   ```bash
   python3 send_settings.py --port /dev/ttyACM0 --no-save
   ```

5. To override **config.yaml** from the command line:

   ```bash
   python3 send_settings.py --port /dev/ttyACM0 --num-mice 4 --logic-mode sum --output-mode separate
   ```

After a successful run, the Pico uses the new settings immediately. If you did not use `--no-save`, they are stored in flash and will still be used after the next power cycle.

---

## 3. Saving settings to flash (persist after reboot)

- **With save (default):**  
  `python3 send_settings.py --port /dev/ttyACM0`  
  Settings are written to the Pico’s flash and loaded automatically on every boot.

- **Without save:**  
  `python3 send_settings.py --port /dev/ttyACM0 --no-save`  
  Settings apply only until the next reset or power cycle; flash is unchanged.

---

## 4. If you prefer to bake settings into the firmware (reflash)

1. Edit **config.yaml** (num_mice, logic_mode, input_mode, output_mode, amplify, quad_scale).
2. Run **configure.py** to regenerate **config.h**:  
   `python3 configure.py`
3. Rebuild and copy UF2:  
   `./build.sh`
4. Flash the new **firmware/amplified_mouse.uf2** to the Pico again (BOOTSEL, drag UF2).

These values become the **defaults** at boot. Any settings previously saved to flash (via send_settings with save) will still **override** these defaults until you send a new config with save, or reflash and clear the settings sector.

---

## 5. Quick reference

| Goal                         | Command / step |
|-----------------------------|----------------|
| Apply config and save to flash | `python3 send_settings.py -p /dev/ttyACM0` |
| Apply config, don’t save    | `python3 send_settings.py -p /dev/ttyACM0 --no-save` |
| Six separate mice          | `--output-mode separate` (or set in config.yaml) |
| Change defaults and reflash| Edit config.yaml → `python3 configure.py` → `./build.sh` → flash UF2 |

For all config options (logic_mode, input_mode, etc.), see **README.md** (Configuration reference) and **config.yaml** comments.

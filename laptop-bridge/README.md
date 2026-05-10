# Laptop Bridge

Python scripts for reading USB-connected gas analysers on a Windows or Linux laptop and pushing their data to the Raspberry Pi hub. This is an optional component — only needed if you have analysers connected via USB rather than the ESP32-based sensor units.

In this lab the laptop monitors two mixing bottle supplies:
- **Minir-5** (orange bottle) — 1–5% CO₂ mix, COM3 / `/dev/ttyMINIR`
- **K30 1%** (clear bottle) — ambient/low CO₂ supply, COM4 / `/dev/ttyK30`

---

## Files

| File | Purpose |
|---|---|
| `minir_pusher.py` | Reads Minir-5 via ASCII protocol and pushes to Pi |
| `k30_pusher.py` | Reads K30 via Modbus binary protocol and pushes to Pi |
| `minir_calibration.py` | GUI calibration tool for the Minir-5 |
| `k30_calibration.py` | GUI calibration tool for the K30 |

---

## Requirements

```bash
pip install pyserial requests
```

On Linux, also add your user to the `dialout` group and log out/in:
```bash
sudo usermod -a -G dialout $USER
```

---

## Configuration

Edit the top of each pusher script to match your setup:

```python
# minir_pusher.py
SERIAL_PORT   = "COM3"          # Windows
# SERIAL_PORT = "/dev/ttyMINIR" # Linux (with udev rule)
PI_HOST       = "192.168.50.1"
PI_PORT       = 5000
SENSOR_ID     = "1-5% Mix"
LOCATION      = "Orange mixing bottle"

# k30_pusher.py
SERIAL_PORT   = "COM4"          # Windows
# SERIAL_PORT = "/dev/ttyK30"   # Linux (with udev rule)
PI_HOST       = "192.168.50.1"
PI_PORT       = 5000
SENSOR_ID     = "<400 ppm Mix"
LOCATION      = "Clear mixing bottle"
```

---

## Running

### Windows — run both scripts together

Create `start_sensors.bat`:
```batch
@echo off
cd /d C:\path\to\laptop-bridge
start "Minir5" python minir_pusher.py
start "K30"   python k30_pusher.py
```

Drop a shortcut to this file in `shell:startup` to run on login.

### Linux — run as systemd services

Create `/etc/systemd/system/minir-pusher.service`:
```ini
[Unit]
Description=Minir-5 Gas Sensor Pusher
After=network.target

[Service]
ExecStart=/usr/bin/python3 /home/user/laptop-bridge/minir_pusher.py
Restart=always
RestartSec=10
User=user

[Install]
WantedBy=multi-user.target
```

Repeat for `k30-pusher.service`. Then:
```bash
sudo systemctl enable --now minir-pusher k30-pusher
```

---

## Linux: Persistent Serial Port Names (recommended)

On Linux the USB serial ports can be assigned different device names (`/dev/ttyUSB0`, `/dev/ttyUSB1`) depending on plug order. Use udev rules to assign stable names.

Find the USB serial numbers:
```bash
udevadm info -a -n /dev/ttyUSB0 | grep -E "serial|idVendor|idProduct"
udevadm info -a -n /dev/ttyUSB1 | grep -E "serial|idVendor|idProduct"
```

Create `/etc/udev/rules.d/99-gas-sensors.rules`:
```
SUBSYSTEM=="tty", ATTRS{idVendor}=="XXXX", ATTRS{serial}=="MINIR_SERIAL_HERE", SYMLINK+="ttyMINIR"
SUBSYSTEM=="tty", ATTRS{idVendor}=="XXXX", ATTRS{serial}=="K30_SERIAL_HERE",   SYMLINK+="ttyK30"
```

Reload:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The devices will now always appear as `/dev/ttyMINIR` and `/dev/ttyK30`.

---

## Protocols

### Minir-5 (ASCII)

| Command | Sends | Response | Description |
|---|---|---|---|
| `Z\r\n` | — | `  Z XXXXX` | Read CO₂ — value × 10 = ppm |
| `U\r\n` | — | `  U XXXXX` | Unlock for calibration |
| `X NNNN\r\n` | — | `  X XXXXX` | Calibrate — NNNN = target_ppm / 10 |

Example: `Z 03050` → 30,500 ppm

### K30 (Modbus binary)

| Bytes | Description |
|---|---|
| `FE 44 00 08 02 9F 25` | Read CO₂ command (7 bytes) |
| `FE 44 02 HH LL XX XX` | Response — CO₂ = (HH × 256 + LL) ppm |

Example response `FE 44 02 00 61 79 0C` → bytes[3:5] = `0x00 0x61` = 97 ppm

---

## Calibration Tools

The calibration tools (`minir_calibration.py` and `k30_calibration.py`) are standalone GUI applications built with Python's tkinter — no additional packages required beyond pyserial.

**Important:** The pusher script and calibration tool cannot both have the serial port open simultaneously. Stop the relevant pusher before running the calibration tool, then restart it afterwards.

### K30 calibration tool

- Connects to K30 on the selected COM port
- Shows live CO₂ reading (updates every 5 seconds)
- Provides 0 ppm and 400 ppm calibration buttons with confirmation dialogs
- Calibration sequence: sends unlock command → calibration command → 8-second settle → reads back result
- Includes on-screen reminders about the departmental compressed air line limitation

### Minir-5 calibration tool

- Connects to Minir-5 on the selected COM port
- Shows live CO₂ reading (updates every 5 seconds)
- Accepts any target concentration (free entry + presets at 1k, 5k, 10k, 20k, 30k, 50k ppm)
- Calibration sequence: `U` unlock → `X NNNN` calibration command → settle → verify confirmed value
- Warns if the sensor's confirmed calibration point differs from the target by more than 100 ppm

---

## Troubleshooting

| Problem | Action |
|---|---|
| `Cannot open COM3/COM4` | Check Device Manager — port may have changed number. Update `SERIAL_PORT` in script. |
| No data in Grafana | Confirm laptop is connected to `GasMonitor` WiFi. Check script log output for POST errors. |
| Readings are ×10 wrong | Verify Minir-5 multiplier — raw value × 10 = ppm for the Minir protocol. |
| K30 returns -1 or no response | Check USB cable and level shifter wiring. Try power-cycling the K30. |
| Calibration tool won't connect | Stop the pusher script first — both tools cannot share the serial port. |
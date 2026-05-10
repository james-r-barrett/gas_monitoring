# CO₂ Monitor Firmware

Arduino sketch for the **CO₂-only** sensor variant. For the combined CO₂ + O₂ variant see `../co2_o2_monitor/`.

## File

`co2_monitor.ino`

---

## What this firmware does

- Reads CO₂ concentration from a SenseAir K30 sensor over UART every 1–60 seconds (configurable)
- Displays live readings, trend indicator, and sparkline on the 1.47" touchscreen
- Logs all readings to a monthly CSV file on the SD card
- Hosts a local web interface at `192.168.4.1` (accessible from the sensor's own WiFi hotspot)
- Connects to the Raspberry Pi hub (`GasMonitor` WiFi) and pushes readings via HTTP POST
- Syncs time from the Pi on connection, or from a browser as fallback
- Stores min/max statistics over a rolling 12-hour window
- Supports K30 calibration (0 ppm and 400 ppm) from the web interface

---

## Setup

See the [firmware README](../README.md) for:
- Arduino IDE and board package installation
- Required libraries
- Board settings (PSRAM must be enabled)
- Flashing instructions

---

## Sensor-specific notes

### K30 UART wiring
The K30 runs at 5V UART logic and must be connected via a level shifter. See `hardware/schematics/` for the full wiring diagram.

### ABC (Automatic Baseline Correction)
The firmware disables ABC on every boot by sending the ABC-off command to the K30. This is essential for growth chamber use where the sensor is never exposed to outdoor air concentrations for the sustained periods ABC requires. Do not re-enable ABC.

### Calibration
Two calibration points are supported:
- **0 ppm** — using pure N₂ or a certified 0 ppm CO₂ / 21% O₂ / 79% N₂ mix
- **400 ppm** — using outdoor ambient air (preferred) or a certified reference gas

See the [calibration documentation](../../docs/user-guide.md#6-calibration) for full procedure including the warning about using the departmental compressed air line.

---

## Display pages

Tap the main reading area to cycle through pages:

| Page | Content |
|---|---|
| Reading view | Live CO₂ value, trend arrow, 12h min/max, uptime/clock, sparkline |
| CO₂ graph | Full-resolution graph for the configured window (up to 96h) |

---

## Settings (configurable at runtime via web UI)

| Setting | Default | Description |
|---|---|---|
| Read interval | 5s | How often to poll the K30 |
| Graph window | 30 min | Time window for device display and web graph |
| Device name | CO2 Monitor | Shown on screen and used as Grafana sensor ID |
| Location | chamber1 | Location tag in Grafana |
| WiFi SSID | CO2Monitor | Sensor's own hotspot SSID |
| WiFi password | co2monitor123 | Sensor's own hotspot password |

All settings are saved to `settings.json` on the SD card and persist across reboots. WiFi changes require a reboot to take effect.
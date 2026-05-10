# CO₂ + O₂ Monitor Firmware

Arduino sketch for the **combined CO₂ and O₂** sensor variant. For the CO₂-only variant see `../co2_monitor/`.

## File

`co2_o2_monitor.ino`

---

## What this firmware does

Everything the CO₂-only firmware does, plus:

- Reads O₂ concentration from a DFRobot SEN0322 sensor over I²C every read cycle
- Displays O₂ alongside CO₂ on the reading view with independent trend indicator
- Logs O₂ to the monthly CSV alongside CO₂ (`o2_pct` column)
- Includes O₂ in data pushed to the Raspberry Pi and Grafana
- Adds an O₂ graph as a third display page (cycle: reading → CO₂ graph → O₂ graph)
- Handles O₂ sensor absence gracefully — shows `--` if sensor not detected at boot

---

## Setup

See the [firmware README](../README.md) for:
- Arduino IDE and board package installation
- Required libraries — **DFRobot_OxygenSensor is additionally required for this variant**
- Board settings (PSRAM must be enabled)
- Flashing instructions

---

## Additional library

Install via Arduino IDE Library Manager:

| Library | Author |
|---|---|
| `DFRobot_OxygenSensor` | DFRobot |

---

## Sensor-specific notes

### SEN0322 I²C address
The O₂ sensor address is set by a dial switch on the board. **Set the dial to position 3** (I²C address `0x73`). This avoids conflict with the AXS5106L touch controller at `0x63`.

### SEN0322 power supply
The SEN0322 **must be powered from 5V (VBUS)**. Connecting VCC to the 3.3V rail will cause the electrochemical cell to give incorrect readings or fail to respond. The I²C SDA/SCL lines are 3.3V compatible and connect directly to the ESP32 GPIO.

### O₂ sensor initialisation
The firmware attempts to initialise the O₂ sensor with up to 5 retries at 500ms intervals during boot. If the sensor is not detected after all retries, the firmware continues without O₂ — all CO₂ functionality is unaffected. A retry loop is necessary because the SEN0322 requires ~2 seconds to stabilise after power-on.

### O₂ sensor lifetime
The electrochemical cell has a lifetime of approximately **2 years** from first use. Monitor for readings that drift consistently low or become unstable — this indicates the cell is near end of life. Replacement sensors are available from DFRobot.

### O₂ calibration
O₂ sensor calibration is performed by holding the **physical button on the back of the SEN0322 board** while the sensor is exposed to clean outdoor air (20.9% O₂). This is a hardware calibration and is not performed via the web interface. Under normal operating conditions recalibration should rarely be needed.

---

## Display pages

Tap the main reading area to cycle through three pages:

| Page | Content |
|---|---|
| Reading view | Live CO₂ and O₂ values, trend arrows, sparklines for both |
| CO₂ graph | Full-resolution CO₂ graph for the configured window |
| O₂ graph | Full-resolution O₂ graph for the configured window (auto-scaled) |

If the Pi is not connected and time has not been synced, tapping to the graph pages is blocked until time sync is complete.

---

## CSV log format

Monthly log files contain the following columns:

```
datetime,uptime_s,co2_ppm,o2_pct
2026-05-10 14:23:01,3601,842,20.91
```

---

## Settings (configurable at runtime via web UI)

| Setting | Default | Description |
|---|---|---|
| Read interval | 5s | How often to poll both sensors |
| Graph window | 30 min | Time window for device display and web graph |
| Device name | CO2 Monitor | Shown on screen and used as Grafana sensor ID |
| Location | chamber1 | Location tag in Grafana |
| WiFi SSID | CO2Monitor | Sensor's own hotspot SSID |
| WiFi password | co2monitor123 | Sensor's own hotspot password |

All settings are saved to `settings.json` on the SD card and persist across reboots.

# Firmware

Two Arduino sketches are provided. Choose the one matching your hardware variant.

| Folder | Variant | Sketch file |
|---|---|---|
| `co2_monitor/` | CO₂ only | `co2_monitor.ino` |
| `co2_o2_monitor/` | CO₂ + O₂ | `co2_o2_monitor.ino` |

---

## Prerequisites

### Arduino IDE

Download and install [Arduino IDE 2.x](https://www.arduino.cc/en/software) (2.0 or later recommended).

### ESP32 Board Package

1. Open Arduino IDE → **File → Preferences**
2. In *Additional boards manager URLs*, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Board → Boards Manager**
4. Search for `esp32` and install **esp32 by Espressif Systems** (version 3.x recommended)

### Board Selection

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM** (required — the firmware allocates history buffers in PSRAM)
- Flash size: **16MB**
- Partition scheme: **Default 4MB with spiffs** (or any scheme with ≥1.5MB app)
- Upload speed: **921600**

> **Important:** PSRAM must be enabled. Without it the large history buffers (up to 345,600 readings) will exhaust internal SRAM immediately.

---

## Required Libraries

Install all of the following via **Tools → Manage Libraries** in Arduino IDE:

| Library | Author | Install name |
|---|---|---|
| Arduino_GFX_Library | moononournation | `Arduino_GFX_Library` |
| esp_lcd_touch_axs5106l | — | Search `axs5106l` |
| DFRobot_OxygenSensor | DFRobot | `DFRobot_OxygenSensor` (CO₂+O₂ variant only) |

The following libraries are included with the ESP32 Arduino core and do not need separate installation:

- `WiFi`
- `WebServer`
- `Wire`
- `SD_MMC`
- `HTTPClient`

---

## Configuration

Before flashing, review the top of the sketch and adjust as needed:

```cpp
// WiFi access point credentials for this sensor's own hotspot
char apSSID[32] = "CO2Monitor";
char apPass[32] = "co2monitor123";

// Raspberry Pi network credentials (if using Pi hub)
const char* PI_SSID = "GasMonitor";
const char* PI_PASS = "*set your own*";
const char* PI_HOST = "192.168.50.1";
const int   PI_PORT = 5000;

// Sensor identity (also configurable via web UI without reflashing)
char deviceName[32] = "CO2 Monitor";
char sensorID[32]   = "sensor1";
char location[32]   = "chamber1";
```

All of these values can also be changed at runtime via the sensor's web interface at `192.168.4.1` — you do not need to reflash to rename a sensor or change WiFi credentials.

---

## Flashing

1. Connect the ESP32-S3 dev kit to your computer via USB-C
2. Open the sketch folder in Arduino IDE (`co2_monitor/co2_monitor.ino` or `co2_o2_monitor/co2_o2_monitor.ino`)
3. Select the correct port under **Tools → Port**
4. Click **Upload** (→)

If the board is not detected, hold the **BOOT** button on the ESP32 while pressing **Reset**, then release both and retry upload.

### First boot

On first boot with a blank SD card:
- The device will create `settings.json` on the SD card with default values
- Time sync will show a splash screen — connect to the sensor's WiFi hotspot and open `192.168.4.1` to sync time from your browser, or connect to the Raspberry Pi network to sync automatically

---

## SD Card Setup

Format the SD card as **FAT32** before first use. The firmware creates all required files automatically on first boot:

| File | Contents |
|---|---|
| `YYYY-MM.csv` | Monthly CO₂ (and O₂) log |
| `fallback.csv` | Readings taken before time sync (backdated on sync) |
| `settings.json` | Device configuration |
| `lasttime.txt` | Saved timestamp for reboot persistence |
| `lastcal.txt` | Most recent calibration record |
| `cal_log.csv` | Full calibration history |

---

## Operational Modes

The firmware operates in two modes automatically — no configuration required:

### Mode 1: Pi-connected (normal operation)
- Sensor connects to `GasMonitor` WiFi at boot
- Time is synced automatically from the Pi
- Readings are pushed to the Pi/Grafana every read cycle
- Green dot indicator shown on device display

### Mode 2: Standalone (Pi unavailable)
- Sensor shows a splash screen prompting time sync via browser
- Connect any device to the sensor's own WiFi hotspot
- Navigate to `192.168.4.1` — time syncs automatically from your browser
- Full local functionality continues (logging, web UI, graphs)
- Data recorded to `fallback.csv` until time sync; backdated automatically on sync

---

## Pin Reference

### Common to both variants

| Function | GPIO |
|---|---|
| Display SPI MOSI | 39 |
| Display SPI SCLK | 38 |
| Display CS | 21 |
| Display DC | 45 |
| Display RST | 40 |
| Display backlight | 46 |
| Touch SDA | 42 |
| Touch SCL | 41 |
| Touch RST | 47 |
| Touch INT | 48 |
| K30 TX (to sensor) | 43 |
| K30 RX (from sensor) | 44 |
| SD CLK | 16 |
| SD CMD | 15 |
| SD D0 | 17 |
| SD D1 | 18 |
| SD D2 | 13 |
| SD D3 | 14 |

### CO₂ + O₂ variant only

| Function | GPIO                     |
|---|--------------------------|
| O₂ sensor SDA | 42 (shared with touch)   |
| O₂ sensor SCL | 41 (shared with touch)   |
| O₂ sensor VCC | 3V3                      |

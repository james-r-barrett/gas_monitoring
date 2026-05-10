# User Guide

This guide covers day-to-day operation of the gas monitoring system for lab members. For build and setup instructions see the [assembly guide](assembly/README.md) and [firmware README](../firmware/README.md).

---

## Contents

1. [System Overview](#1-system-overview)
2. [The Grafana Dashboard](#2-the-grafana-dashboard)
3. [Individual Sensor Web Interface](#3-individual-sensor-web-interface)
4. [Time Synchronisation](#4-time-synchronisation)
5. [Calibration](#5-calibration)
6. [Downloading Data](#6-downloading-data)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. System Overview

The monitoring network has three tiers:

```
ESP32 sensor units          Raspberry Pi hub          Grafana Cloud
(one per chamber)    ──►   (GasMonitor WiFi)   ──►   (remote dashboard)
                            InfluxDB (local)
                            
Windows/Linux laptop ──►   (GasMonitor WiFi)
(Minir-5 + K30 1%)
```

**Each ESP32 sensor unit:**
- Measures CO₂ every 1–60 seconds (configurable)
- Optionally measures O₂ (combined units only)
- Displays live readings on a 1.47" touchscreen
- Logs data to an onboard SD card (monthly CSV files, indefinite retention)
- Hosts its own web interface at `192.168.4.1` when you connect to its hotspot
- Pushes data to the Raspberry Pi for central monitoring

**The Raspberry Pi hub:**
- Broadcasts the `GasMonitor` WiFi network
- Receives data from all sensors
- Forwards to Grafana Cloud (14-day retention)
- Provides automatic time sync to sensors on connection

**The laptop (mixing bottle monitors only):**
- Reads the Minir-5 (orange bottle, 1–5% CO₂) and K30 1% (clear bottle, ambient CO₂) via USB
- Pushes readings to the Pi via GasMonitor WiFi
- Displays the Grafana dashboard full-screen for room-level monitoring

---

## 2. The Grafana Dashboard

### Accessing the dashboard

**In the growth room:** The laptop displays the dashboard full-screen at all times. No login required.

**From your own device:**
- Open the dashboard URL (ask the lab manager for the link)
- Must be on the university network, university WiFi, or VPN

**As a home screen app (recommended):**

*Chrome (desktop or Android):*
1. Open the dashboard URL in Chrome
2. Click the three-dot menu → **Cast, save and share** → **Install as app**
3. Click **Install** — the dashboard appears as an app icon

*Safari (iPhone / iPad):*
1. Open the dashboard URL in Safari
2. Tap the **Share** button (square with arrow)
3. Tap **Add to Home Screen** → **Add**
4. Opens as a full-screen app without browser toolbars

### What the dashboard shows

- All sensors displayed as separate panels
- Mixing bottle sensors (Minir-5 and K30 1%) at the top, colour-coded by bottle colour (orange and clear)
- Chamber sensors below
- Up to 14 days of history
- Adjustable time window using the time picker in the top-right corner of the Grafana page

### Dashboard vs local interface

| Feature | Grafana dashboard | Local web interface (192.168.4.1) |
|---|---|---|
| All sensors at once | ✓ | One sensor only |
| Remote access | ✓ | Only when connected to sensor hotspot |
| History | 14 days | Up to 10,000 readings in browser; full history on SD card |
| Download CSV | — | ✓ |
| Calibration | — | ✓ |
| Settings | — | ✓ |
| Works without Pi | — | ✓ |

---

## 3. Individual Sensor Web Interface

Each sensor has its own web interface, accessible by connecting to its WiFi hotspot.

### Connecting

1. On your phone or laptop, open WiFi settings
2. Connect to the sensor's network — the SSID is the device name (e.g. `Bournemouth`, `Chamber 4`)
3. Open a browser and go to `192.168.4.1`

> **Phone users:** disable mobile data if the page doesn't load — your phone may be using mobile data instead of the sensor WiFi.

### Reading view

The main page shows:
- Live CO₂ reading in ppm with a trend arrow (▴ rising / ▾ falling / · stable)
- Live O₂ reading in % with trend arrow (combined units only)
- Last calibration date and type
- A graph of recent readings for the configured window

### Graph

The graph shows real timestamps (HH:MM) on the x-axis once the device has been time-synced. The time window is configurable in Settings. Tap the graph area on the device touchscreen to switch between CO₂ and O₂ graph pages (combined units).

### Settings

Navigate to **Settings** in the web interface to change:

| Setting | Description |
|---|---|
| Graph window | Time period shown on device and web graph (1 min to 96 hours) |
| Read interval | How often the sensor takes a measurement (1s to 30 min) |
| Device name | Shown on screen and used as the sensor ID in Grafana |
| Location | Location tag in Grafana |
| WiFi SSID | The sensor's own hotspot network name |
| WiFi password | The sensor's own hotspot password |

> WiFi changes require a device reboot to take effect. Make a note of any new credentials before saving.

### Calibration log

The web interface shows the three most recent calibration events — type, date/time, and CO₂ reading at the time. A full log is stored on the SD card as `cal_log.csv`.

---

## 4. Time Synchronisation

The sensor hardware has no real-time clock. Time must be provided after each restart.

### Automatic (Pi connected — normal operation)

When a sensor starts and connects to `GasMonitor` WiFi, the Pi automatically provides the current time. No user action required. A green dot appears on the device screen to confirm the Pi connection.

### Manual (Pi unavailable)

If the Pi is offline or the sensor is being used away from the growth room, the sensor displays a splash screen:

```
TIME SYNC REQUIRED
1. Connect to WiFi: [sensor SSID]
2. Open in browser: 192.168.4.1
```

To sync time:
1. Connect to the sensor's own WiFi hotspot
2. Open `192.168.4.1` in a browser
3. Time syncs automatically from your device — the splash screen clears

> **Data during unsync period:** Readings are still recorded to a `fallback.csv` file on the SD card. Once time is synced, this file is automatically backdated and merged into the correct monthly log. No data is lost.

---

## 5. Calibration

### CO₂ sensor (K30) — when to calibrate

Calibrate the K30 if:
- Readings appear consistently offset from expected values
- The sensor has been stored for a long period
- As part of routine quality control

### Calibration methods

#### 0 ppm calibration (recommended)

The most reliable method. Expose the sensor to a gas containing 0 ppm CO₂ and calibrate to zero.

**Suitable gas sources available in the growth room:**
- Pure nitrogen (N₂)
- 21% O₂ / 79% N₂ (0 ppm CO₂) gas mix plumbed into the growth room

**Procedure:**
1. Connect the sensor to N₂ or the zero gas mix
2. Wait for the reading to stabilise — at least 2–3 minutes
3. Connect to the sensor's web interface and click **Calibrate → 0 ppm**
4. Confirm in the dialog
5. Wait for calibration to complete (~10 seconds)

#### 400 ppm calibration (use with caution)

> ⚠️ **Warning — departmental compressed air line:** The compressed air supply takes outdoor air through desiccation and delivers it at approximately 400 ppm CO₂ on average. However, outdoor CO₂ varies diurnally and with atmospheric conditions, and the compression/desiccation process can shift concentrations. **The actual concentration at any given moment may be ±100 ppm from 400 ppm.** Do not use this supply for 400 ppm calibration without first verifying the concentration independently.

**Preferred method — outdoor calibration:**
1. Power the sensor from a USB battery pack or laptop
2. Take the sensor outdoors, away from building exhausts and busy roads
3. Allow 5 minutes for the reading to stabilise
4. Connect to the sensor's web interface and click **Calibrate → 400 ppm**
5. Confirm in the dialog

### Confirming calibration

After any calibration, allow 2–3 minutes for the reading to fully stabilise, then verify against a known reference.

### O₂ sensor (SEN0322) — combined units only

The O₂ sensor does not require calibration via the web interface. If recalibration is needed:
1. Take the sensor outdoors to clean air (20.9% O₂)
2. Hold the **physical button on the back of the SEN0322 board** until the reading stabilises at ~20.9%

Under normal laboratory conditions O₂ recalibration is rarely necessary. The sensor has a lifetime of approximately **2 years** — readings drifting consistently low or becoming unstable indicate the cell needs replacement.

### Mixing bottle analysers (Minir-5 and K30 1%) — experienced users only

These sensors are calibrated using dedicated Python GUI tools on the laptop. See the [laptop bridge README](../laptop-bridge/README.md) for full instructions. The GasLab software is no longer used.

**Important:** Stop the relevant pusher script before running the calibration tool, and restart it afterwards.

---

## 6. Downloading Data

### From the web interface

1. Connect to the sensor's WiFi hotspot
2. Open `192.168.4.1`
3. Scroll to **Logs**
4. Click **Download** next to the month you want
5. A CSV file will download to your device

### CSV file format

```
datetime,uptime_s,co2_ppm[,o2_pct]
2026-05-10 14:23:01,3601,842[,20.91]
```

- `datetime` — UTC timestamp (ISO 8601)
- `uptime_s` — seconds since last device restart
- `co2_ppm` — CO₂ concentration in ppm
- `o2_pct` — O₂ percentage (combined units only)

### Storage

Log files are stored indefinitely on the SD card. At a 5-second read interval the card has capacity for over 20 years of continuous data. Files are never automatically deleted.

---

## 7. Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| Splash screen asking to sync time | Pi unavailable at startup | Connect to sensor hotspot → open 192.168.4.1 |
| Sensor not in Grafana | Not connected to GasMonitor WiFi | Check green dot on device screen; check Pi is powered |
| `CO₂ disconnected` on screen | K30 not responding | Power-cycle the sensor; check K30 cable |
| O₂ shows `--` | SEN0322 not detected at boot | Power-cycle; check VCC is on 5V not 3.3V |
| O₂ readings drifting low | Electrochemical cell near end of life | Replace SEN0322 (~2 year lifetime) |
| Web interface won't load at 192.168.4.1 | Not connected to sensor hotspot | Connect to sensor WiFi and disable mobile data |
| Settings not saved after reboot | SD card missing or not FAT32 | Check SD card is seated; reformat as FAT32 if needed |
| Missing months in CSV log | Device ran without time sync | Check SD card for `fallback.csv` — data may be there, backdated on next sync |
| Mixing bottle sensors absent from Grafana | Laptop powered off or scripts stopped | Check laptop is on and logged in; restart pusher scripts if closed |
| Readings offset after calibration | Calibration performed with unstable reading | Wait for full stabilisation (2–3 min) and recalibrate |
| 400 ppm calibration gives wrong result | Departmental air line CO₂ variation | Use outdoor calibration or N₂/zero gas instead |

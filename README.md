# Canary Gas Monitoring

An open-source, networked gas monitoring system for laboratory growth chambers, built around the ESP32-S3 microcontroller. Measures CO₂ (and optionally O₂) continuously, logs data to an onboard SD card, and pushes readings to a central Grafana dashboard via a Raspberry Pi hub.

---

## Features

- **Two sensor variants** — CO₂-only and combined CO₂/O₂
- **Local web interface** — connect directly to each sensor's WiFi hotspot to view live readings, graphs, calibration logs, and download CSV data
- **Networked monitoring** — sensors push to a Raspberry Pi hub, which forwards to Grafana Cloud for remote access
- **Offline capable** — all data logged to SD card indefinitely; Pi connection is optional
- **Automatic time sync** — from Raspberry Pi on connection, or from any browser as fallback
- **Configurable** — read interval, graph window, device name, location, WiFi credentials all adjustable from the web interface without reflashing
- **Calibration** — K30 CO₂ sensor calibration (0 ppm and 400 ppm) via web interface and touchscreen; O₂ sensor via hardware button
- **Optional solenoid control** — for automated CO₂ dosing (hardware provision included)

---

## Repository Structure

```
gas-monitor/
├── hardware/
│   ├── bom/                    # Bills of materials (CSV)
│   └── schematics/             # Wiring schematics (SVG/PDF)
├── firmware/
│   ├── co2_monitor/            # CO₂-only firmware (Arduino sketch)
│   └── co2_o2_monitor/         # CO₂ + O₂ firmware (Arduino sketch)
├── server/                     # Raspberry Pi server (Python/Flask)
├── laptop-bridge/              # USB sensor bridge scripts (Windows/Linux)
└── docs/
    └── assembly/               # Step-by-step build guide with images
```

---

## Sensor Variants

| Feature | CO₂ only | CO₂ + O₂ |
|---|---|---|
| CO₂ measurement | ✓ K30 NDIR (0–10,000 ppm) | ✓ K30 NDIR (0–10,000 ppm) |
| O₂ measurement | — | ✓ SEN0322 (0–25%) |
| Display | ✓ 1.47" touchscreen | ✓ 1.47" touchscreen |
| SD logging | ✓ | ✓ |
| Local web UI | ✓ | ✓ |
| Pi push | ✓ | ✓ |
| O₂ sensor lifetime | — | ~2 years (replaceable) |

---

## Quick Start

1. See [`hardware/bom/`](hardware/bom/) for the parts list
2. See [`docs/assembly/`](docs/assembly/) for the build guide
3. See [`firmware/`](firmware/) for Arduino IDE setup and flashing instructions
4. See [`server/`](server/) for Raspberry Pi setup
5. See [`laptop-bridge/`](laptop-bridge/) for the USB analyser bridge (optional)

---

## System Overview

```
┌─────────────────────┐     WiFi (GasMonitor AP)    ┌──────────────────────┐
│  ESP32 sensor unit  │ ──────────────────────────► │   Raspberry Pi hub   │
│  (one per chamber)  │                             │                      │
└─────────────────────┘                             │  InfluxDB  (local)   │
                                                    │  Flask receiver      │
┌─────────────────────┐     USB serial + WiFi       │  → Grafana Cloud     │
│  Windows/Linux      │ ──────────────────────────► │                      │
│  laptop (optional)  │                             └──────────────────────┘
│  Minir-5 + K30 1%   │
└─────────────────────┘
```

---

## Licence

see [LICENSE](LICENSE)

## Citation

If you use this system in published research, please cite this repository:

> Barrett, J.R. (2026). Lab Gas Monitor. GitHub. https://github.com/james-r-barrett/gas-monitor

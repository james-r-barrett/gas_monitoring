# Hardware

This folder contains all hardware documentation for the gas monitor sensor units.

```
hardware/
├── bom/            # Bills of materials (CSV) — one per sensor variant
├── schematics/     # Wiring schematics
└── 3d-print/       # 3D print files and print settings
```

## Sensor Variants

There are two hardware variants. They share the same ESP32-S3 base platform and differ only in the addition of the O₂ sensor and a modified enclosure lid.

### CO₂-only

The standard variant. Measures CO₂ concentration using the SenseAir K30 NDIR sensor. Suitable for the majority of growth chamber applications.

### CO₂ + O₂

Adds a DFRobot SEN0322 electrochemical oxygen sensor to the I²C bus. The O₂ sensor shares the same I²C bus as the touchscreen controller (both run at 3.3V logic but the SEN0322 requires a 5V VCC supply). The enclosure lid has an additional port for the O₂ sensor body.

## Key Design Decisions

**Level shifter for K30:** The SenseAir K30 operates at 5V UART logic. The ESP32-S3 GPIO is 3.3V. A Pololu 4-channel bidirectional level shifter is used between them. The LV reference must be connected to the ESP32's 3.3V rail and the HV reference to the USB 5V (VBUS) rail.

**O₂ sensor power:** The SEN0322 operates at 3.3-5.5V VCC with  3.3V-compatible I²C logic. It may be powered from VBUS (5V), or the 3.3V regulator. The I²C SDA/SCL lines connect directly to the ESP32 GPIO without a level shifter.

**SD card interface:** The ESP32-S3 uses 4-bit SDMMC mode for SD card access, which is significantly faster than SPI mode. The pin assignments are fixed by the SDMMC peripheral and cannot be remapped.

**Power budget:** At peak load the system draws approximately 500–600 mA:
- K30 CO₂ sensor: ~200 mA
- ESP32-S3 + display: ~300 mA  
- SEN0322 O₂ sensor: <10 mA
- Other: <20 mA

A quality USB-C supply rated at ≥2A is recommended. Cheap supplies cause intermittent brown-outs that can corrupt SD card writes.

## I²C Bus

The I²C bus (SDA GPIO42, SCL GPIO41) is shared between:

- AXS5106L touch controller (address 0x63)
- SEN0322 O₂ sensor (address 0x73, set by dial switch position 3) — CO₂+O₂ variant only

There are no address conflicts between these devices.
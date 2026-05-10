# 3D Print Files

Enclosure files for both sensor variants. Source files are Autodesk Fusion projects; STLs are provided for direct slicing.

```
3d-print/
├── README.md           (this file)
├── fusion/             # Autodesk Fusion source projects (.f3d)
│   ├── co2_only/
│   └── co2_o2/
├── stl/
│   ├── co2_only/
│   │   ├── co2_base.stl
│   │   └── co2_fascia.stl
│   │   └── co2_plate.stl
│   │   └── co2_insert.stl
│   └── co2_o2/
│   │   ├── co2_o2_base.stl
│   │   └── co2_o2_fascia.stl
│   │   └── co2_o2_plate.stl
│   │   └── co2_o2_insert.stl
└── renders/            # Reference images
```

---

## Print Settings

These settings were used for production units on Bambu Labs printers. Other configurations will work but may require tolerance adjustments.

| Setting | Recommended value |
|---|-------------------|
| Material | PLA               |
| Layer height | 0.2 mm            |
| Infill | 20% gyroid        |
| Perimeters / walls | 3                 |
| Top/bottom layers | 4                 |
| Supports | Tree              |
| Bed adhesion | Brim (base only)  |
| Nozzle temp (PETG) | 235°C             |
| Bed temp (PETG) | 60°C              |

### Material notes

**PETG** is preferred for growth chamber use — it tolerates higher humidity and temperatures than PLA without deforming. It is also slightly more flexible, which helps with clip-fit lids.

**PLA** works fine for ambient lab environments. Avoid if the enclosure will be exposed to sustained temperatures above 50°C or high humidity condensation.

---

## Enclosure Design
Each sensor enclosure consists of two main parts and an additional part for the base which can be either a standard base (self-supporting) or a low-profile insert.

### Plate
The plate contains the main recess for the push-fit of the K30 as well as the 4 mounting points for the ESP32 board which also serve to connect the fascia to the plate. For the CO₂+O₂ variant the plate also contains cutouts for push-fit of the SEN0322 standoffs that are used to mount the sensor, as well as a cutout for the SEN0322 sensor body:
- K30 sensor mounting recess
- Routing for the wiring from the ESP32 board to the K30.
- **CO₂+O₂ variant only:** cutouts for push-fit of the SEN0322 standoffs
- **CO₂+O₂ variant only:** additional circular port on the side for the SEN0322 body; the electrical connections pass through internally


### Fascia

The fascia is attached to the plate using the 4x M2x8mm screws and contains cutouts for the display, the USB-C charger Features:
- Display aperture (the Waveshare board's display faces upward through the lid)
- Touch-accessible surface over the display area
- **CO₂+O₂ variant only:** routing for the SEN0322 wiring to the ESP32

### Bases
The base is either a standard base (self-supporting) or a low-profile insert. Both operate on push-fit tolerances, where the insert leaves a flush finish but may not support the sensor standing upright and is intended for alternative mounting options.

---

## Dimensional Notes

- The display aperture is sized for the Waveshare ESP32-S3 1.47" display (172×320px panel, 35.5×71.0mm active area)
- K30 dimensions: 57×57×15mm — the mounting recess matches this with 0.3mm clearance
- SEN0322 dimensions: 22×27×13mm — the side port matches the 22×13mm profile
---


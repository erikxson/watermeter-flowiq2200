# WaterMeter-FlowIQ2200 (ESP32 + CC1101 + Home Assistant MQTT Discovery)

ESP32 + CC1101 receiver for Kamstrup FlowIQ 2200 (Wireless M-Bus). Publishes meter data via MQTT for Home Assistant.


Read **Kamstrup FlowIQ 2200** (wM-Bus) using an **ESP32 + CC1101**, publish values over **MQTT**, and let **Home Assistant** create the device + sensors automatically via **MQTT Discovery**.

---
<img align="right" height="250" src="images/flowiq2200.png">
## What you get in Home Assistant

A single MQTT device with sensors such as:

- **Water Meter Usage** (m³) – total consumption
- **Water Meter Month Start Value** (m³) – month baseline
- **Water Meter Flow** (l/h) – current flow as an **integer l/h**

## Status
- Works with FlowIQ 2200 telegrams where AES key and meter ID are known.
- Publishes JSON to MQTT for easy HA sensors.

## License
This project is **GNU GPL v3 (or later)**.  
Original codebase: https://github.com/pthalin/esp32-multical21  
See: `LICENSE` and `CREDITS.md`.

**Important:** Original copyright headers are preserved in derived files.  
All modifications are marked with: `modified by erikxson, 2026:`.

---

# Hardware

- ESP32 Dev board
- CC1101 module (868 MHz)
- Antenna tuned for 868 MHz
- 3.3V power (do NOT use 5V for CC1101)

## Wiring (ESP32 ↔ CC1101)

**Important:** CC1101 is **3.3V only**. Do **not** power it from 5V.

This pin order follows the module silk-screen from **VCC** downward (as on the common green CC1101 boards):

| CC1101 pin (top → bottom) | Connect to ESP32 | Notes |
|---|---:|---|
| **VCC** | **3V3** | 3.3V only |
| **GND** | **GND** | common ground |
| **MOSI** | **GPIO 23** | SPI MOSI |
| **SCLK** | **GPIO 18** | SPI SCK |
| **MISO** | **GPIO 19** | SPI MISO |
| **GDO2** | *(optional / not used)* | leave unconnected unless your build uses it |
| **GDO0** | **GPIO 4** | data/interrupt pin used by firmware |
| **CSN** | **GPIO 5** | SPI CS |

Right-side pads on many modules:

| CC1101 pad | Connection | Notes |
|---|---|---|
| **GND** | GND | (optional) |
| **ANT** | Antenna wire | solder antenna here |
| **GND** | GND | (optional) |

---


If this project saved you time, consider buying me a coffee

<a href="https://buymeacoffee.com/erikxson">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="42" alt="Buy me a coffee">
</a>

---

# Build & Flash (PlatformIO)

## 1) Clone
```bash
git clone https://github.com/erikxson/WaterMeter-FlowIQ2200.git
cd WaterMeter-FlowIQ2200




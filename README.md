# WaterMeter-FlowIQ2200 (ESP32 + CC1101 + Home Assistant)

<a href="https://buymeacoffee.com/erikxson">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="42" alt="Buy me a coffee">
</a>

ESP32 + CC1101 receiver for Kamstrup FlowIQ 2200 (Wireless M-Bus). Publishes meter data via MQTT for Home Assistant.

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

## Required
- ESP32 Dev board
- CC1101 module (868 MHz)
- Antenna tuned for 868 MHz
- 3.3V power (do NOT use 5V for CC1101)

## Wiring (ESP32)
CC1101 → ESP32
- VCC  → 3V3
- GND  → GND
- MOSI → GPIO23
- SCK  → GPIO18
- MISO → GPIO19
- GDO0 → GPIO32
- CSN  → GPIO4
- GDO2 → not connected

---

# Build & Flash (PlatformIO)

## 1) Clone
```bash
git clone https://github.com/erikxson/WaterMeter-FlowIQ2200.git
cd WaterMeter-FlowIQ2200

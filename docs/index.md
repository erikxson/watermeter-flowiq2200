---
title: WaterMeter-FlowIQ2200
---

# WaterMeter-FlowIQ2200 (ESP32 + CC1101 + Home Assistant)

<a href="https://buymeacoffee.com/erikxson">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="42" alt="Buy me a coffee">
</a>

ESP32 + CC1101 (868 MHz) Wireless M-Bus receiver for Kamstrup FlowIQ 2200. Publishes meter data via MQTT for Home Assistant.

## What you get
- Total usage (m³)
- Month start value (m³)
- Flow (l/h, integer)
- Stable MQTT availability (`online`) and heartbeat (`online_ts`)
- Firmware metadata topics (`fw`, `fw_version`)

---

## Hardware
**Required**
- ESP32 dev board
- CC1101 module (868 MHz)
- 868 MHz antenna
- 3.3V power (do **not** use 5V for CC1101)

**Wiring (ESP32)**
- VCC  → 3V3  
- GND  → GND  
- MOSI → GPIO23  
- SCK  → GPIO18  
- MISO → GPIO19  
- GDO0 → GPIO32  
- CSN  → GPIO4  
- GDO2 → not connected  

---

## Firmware setup (PlatformIO)

### 1) Create credentials
Copy:
- `src/credentials.example.h` → `src/credentials.h`

Fill in:
- Wi-Fi SSID + password
- MQTT broker IP/host
- MQTT username/password (or empty strings)
- `meterId` and AES `key`

### 2) Build + flash
In VS Code (PlatformIO):
- Build
- Upload
- Monitor @ **115200**

---

## MQTT topics

**Published**
- `watermeter/0/online` (retained): `true` / `false`
- `watermeter/0/online_ts` (not retained): uptime seconds (heartbeat)
- `watermeter/0/ipaddr` (retained)
- `watermeter/0/fw` (retained)
- `watermeter/0/fw_version` (retained)
- `watermeter/0/sensor/mydata` (retained): numeric string (m³)
- `watermeter/0/sensor/mydatajson` (retained): JSON payload

Example JSON:
```json
{
  "MeterId":"53480878",
  "MeterIdConfig":"53480878",
  "CurrentValue":153.466,
  "MonthStartValue":146.759,
  "FlowLph":93
}

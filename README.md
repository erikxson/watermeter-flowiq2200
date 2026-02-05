# WaterMeter-FlowIQ2200 (ESP32 + CC1101 + Home Assistant MQTT Discovery)

Read **Kamstrup FlowIQ 2200** (wM-Bus) using an **ESP32 + CC1101**, publish values over **MQTT**, and let **Home Assistant** create the device + sensors automatically via **MQTT Discovery**.

---

<img align="right" height="250" src="images/flowiq2200.png">

## What you get in Home Assistant

A single MQTT device with sensors such as:

- **Water Meter Usage** (m³) – total consumption
- **Water Meter Month Start** (m³) – month baseline
- **Water Meter Flow** (L/h) – current flow as an **integer L/h** (flow over 256 L/h may be temporarily wrong between full frames)
- **Water Meter Flow (Full)** (L/h) – flow from full frames (2‑byte), may be sporadic

See **Flow behavior (compact vs full frames)** below for how these values are derived.

## Status

- Works with FlowIQ 2200 telegrams where AES key and meter ID are known.
- Publishes JSON to MQTT for easy HA sensors.
- ESP32: tested.
- ESP8266: experimental / untested.

---

## Flow behavior (compact vs full frames)

FlowIQ 2200 sends two frame types:
- Compact frames (CI=0x79) contain flow in 1 byte (0..255).
- Full frames (CI=0x78) contain flow in 2 bytes and are often much less frequent (minutes between).
  - Observed: full frames can be even more sporadic during evenings/weekends.

Readouts:
- `FlowLph` from compact frames (1‑byte VIF 0x32).
- `FlowLphFull` from full frames (2‑byte VIF 0x3B).

To get a usable flow value above 255, the firmware uses this rule:
- When a full frame arrives, its flow value is cached.
- While waiting for the next full frame, compact flow is extended by a +256 offset based on the last full frame.
- When a new full frame arrives with flow <256, the offset is reset.

This means FlowLph can look "stuck" high or low between full frames if the meter is quiet. This is expected and will update as soon as a new full frame arrives.
- Observed: full frames can be even more sporadic during evenings/weekends.
---

## License

This project is based on code from:

1) **esp32-multical21** by pthalin  
   Repository: https://github.com/pthalin/esp32-multical21  
   License: GNU GPL v3 (or later)

That project is itself derived from:

2) **esp-multical21** by chester4444  
   Repository: https://github.com/chester4444/esp-multical21  
   License: GNU GPL v3 (or later)

This project is **GNU GPL v3 (or later)**.  
See: `LICENSE`, `CREDITS.md`, and `CHANGELOG.md`.

**Important:** Original copyright headers are preserved in derived files.  
All modifications are marked with: `modified by erikxson, 2026`.

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
|---|---|---|
| **VCC** | **3V3** | 3.3V only |
| **GND** | **GND** | common ground |
| **MOSI** | **GPIO 23** | SPI MOSI |
| **SCK/SCLK** | **GPIO 18** | SPI SCK |
| **MISO** | **GPIO 19** | SPI MISO |
| **GDO2** | *(optional / not used)* | leave unconnected unless your build uses it |
| **GDO0** | **GPIO 32** | data/interrupt pin used by firmware |
| **CSN** | **GPIO 4** | SPI CS |

Right-side pads on many modules:

| CC1101 pad | Connection | Notes |
|---|---|---|
| **GND** | GND | (optional) |
| **ANT** | Antenna wire | solder antenna here |
| **GND** | GND | (optional) |

---

# Build & Flash (PlatformIO)

## Open the project in PlatformIO

- Open VS Code
- Install PlatformIO (Extensions → search “PlatformIO”)
- In VS Code: File → Open Folder… and select the repo folder (watermeter-flowiq2200)
- Wait for PlatformIO to finish indexing dependencies

## Create your credentials file (required)

- Rename `src/credentials.example.h` → `src/credentials.h`
- Fill in:
  - Wi-Fi SSID + password
  - MQTT broker host/IP
  - MQTT username/password (or empty strings if not used)
  - meterId (4 bytes)
  - key (16 bytes AES-128)

## Verify wiring matches the firmware

- Confirm your wiring matches `include/hwconfig.h`

## Select build target

- **ESP32 (tested)**
- **ESP8266 (experimental / untested)**

## Build the firmware

- PlatformIO → Project tasks → select environment (`esp32` recommended) → **Build**

## Flash the firmware

- PlatformIO → Project tasks → select environment → **Upload**

## Monitor serial output

- PlatformIO → Project tasks → select environment → **Monitor**

---

# Home Assistant

## Enable MQTT Discovery and verify the device appears

Home Assistant must have the MQTT integration configured and connected to the broker.

Verify in Home Assistant:
- Settings → Devices & Services → MQTT
- Open the Devices list
- Look for a new device named similar to: **Water Meter**

You should get sensors such as:
- Water Meter Usage (m³)
- Water Meter Month Start (m³)
- Water Meter Flow (L/h)

## MQTT topics (for verification)

Home Assistant: Settings → Devices & Services → MQTT → Configure → **Listen to a topic**

Listen to:
- `watermeter/0/sensor/mydatajson`
- `watermeter/0/online`
- `watermeter/0/online_ts`

## MQTT control topics

### Reset (new)
- Command: `watermeter/0/cmd/reset`  
  Payload: `true`
- Status (retained): `watermeter/0/cmd/reset/status`  
  Payload set back to `false` after processing.

### Reset (legacy, still accepted during transition)
- Command: `espmeter/reset`  
  Payload: `true`

---

## Runtime behavior (robustness)

- WiFi connection: retries up to 5 times with ~10s delay between attempts, then reboots.
- MQTT reconnect: 5s backoff between connection attempts.

---

If this project saved you time, consider buying me a coffee

<a href="https://buymeacoffee.com/erikxson">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="42" alt="Buy me a coffee">
</a>

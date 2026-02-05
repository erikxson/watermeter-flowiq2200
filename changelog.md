# Changelog

All notable changes to this project will be documented in this file.

## 0.3.2 - 2026-02-05
### Added
- Full-frame flow cache and JSON field `FlowLphFull`, plus HA sensor "Water Meter Flow (Full)".
- Raw RF dump topic `watermeter/0/debug/rf` (pre-decrypt) for troubleshooting.
- Extended CRC diagnostics (EN13757 + X25 tests and block scans) for full frames.

### Changed
- Flow parsing is now record-based (no fixed offsets) and supports VIF `0x3B` in full frames.
- Compact flow can be extended using the last full-frame flow (>256 offset) until the next full frame.
- Non-compact frames can be parsed/dumped even when CRC fails (for troubleshooting).

## 0.3.1 - 2026-01-26
### Changed
- Home Assistant MQTT Discovery: flow sensor unit updated from `l/h` to `L/h` for standardized SI-style unit formatting.
- Firmware version bumped to `0.3.1`.
- MQTT reset command topics migrated to project namespace:
  - New: `watermeter/0/cmd/reset`
  - New: `watermeter/0/cmd/reset/status` (retained)
  - Backward compatible: still accepts `espmeter/reset` during transition.
- Removed unused MQTT subscriptions (`watermeter/0/liveData`, `/smarthomeNG/start`).
- WiFi connect behavior: retry up to 5 times with ~10s delay between attempts, then reboot.
- MQTT reconnect backoff set to 5s.

### Fixed
- MQTT Discovery JSON builder now guards optional field appends to avoid potential payload overflow/corruption.
- Reset status topics are now explicitly managed (retained): set to `true` during reset processing and back to `false` after reboot/reconnect (`watermeter/0/cmd/reset/status` and legacy `espmeter/reset/status`).

### Docs
- Updated README.md
- Updated CHANGELOG.md


## 0.3.0 - 2026-01-01
### Added
- FlowIQ 2200 decoding path (based on original esp32-multical21 codebase).
- MQTT online heartbeat topics and firmware metadata topics.
- MeterId included in JSON payload.
- Flow output normalized to l/h at source (no HA-side scaling needed).

### Changed
- Codebase adapted from Multical 21 focus to FlowIQ 2200 support.

### Notes
- This project remains GPL-3.0-or-later (see LICENSE).

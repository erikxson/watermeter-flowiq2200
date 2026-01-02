# Changelog

All notable changes to this project will be documented in this file.

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
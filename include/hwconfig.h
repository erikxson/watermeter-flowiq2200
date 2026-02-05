#ifndef __HWCONFIG_H__
#define __HWCONFIG_H__

#if defined(ESP8266)
// CC1101 <-> ESP8266
// VCC   => 3V3
// GND   => GND
// CSN   => D8
// MOSI  => D7
// MISO  => D6
// SCK   => D5
// GDO0  => D2
// GDO2  => not connected

  #define CC1101_CSN          D8
  #define CC1101_MOSI         D7
  #define CC1101_MISO         D6
  #define CC1101_SCK          D5
  #define CC1101_GDO0         D2

  #define PIN_LED_BUILTIN     D4

#elif defined(ESP32)
// CC1101 <-> ESP32 (din koppling)
// VCC   => 3V3
// GND   => GND
// CSN   => GPIO4   (D4)
// MOSI  => GPIO23  (D23)
// MISO  => GPIO19  (D19)
// SCK   => GPIO18  (D18)
// GDO0  => GPIO32  (D32)
// GDO2  => not connected

  #define CC1101_CSN          4
  #define CC1101_MOSI         23
  #define CC1101_MISO         19
  #define CC1101_SCK          18
  #define CC1101_GDO0         32

  #define PIN_LED_BUILTIN     2
#endif

// =============================================================================
// Debug: wM-Bus record dump
// =============================================================================
// Set to 1 to publish decrypted payload + parsed records to MQTT for troubleshooting.
// Topics: watermeter/0/debug/raw and watermeter/0/debug/records
#define WMBUS_DEBUG_RECORD_DUMP 1

// If 1, only publish dump when FlowLph parsing fails (flow == 0).
// Set to 0 to dump on every received frame.
#define WMBUS_DEBUG_RECORD_DUMP_ON_FAIL 0

// If 1, print current FlowLph to Serial (VSCode/PlatformIO monitor).
#define WMBUS_DEBUG_SERIAL_FLOW 1

// If 1, also print debug/raw + debug/records to Serial.
#define WMBUS_DEBUG_SERIAL_DUMP 1

// If 1, print any records whose final VIF is in range 0x30..0x37.
#define WMBUS_DEBUG_SERIAL_VIF_RANGE 1

// If 1, publish raw RF bytes (pre-decrypt) to MQTT.
// Topic: watermeter/0/debug/rf
#define WMBUS_DEBUG_RF_DUMP 1

// If 1, apply 8-bit wrap correction for flow (>255 L/h) by adding 256
// when a low byte wrap is detected.
#define WMBUS_FLOW_WRAP_CORRECTION 1

// Enable caching of non-compact (full) frames to improve flow parsing.
#define WMBUS_FULL_FRAME_CACHE 1

// Max age (ms) to use cached full-frame flow on compact frames.
#define WMBUS_FULL_FRAME_MAX_AGE_MS 900000UL

// If 1, prefer cached full-frame flow when available (vlen >= 2).
#define WMBUS_FLOW_PREFER_FULL 1

// If 1, print CI byte and length for every decrypted frame.
#define WMBUS_DEBUG_SERIAL_CI 1

// If 1, log CRC mismatches (Serial + MQTT debug).
#define WMBUS_DEBUG_CRC 1

// If 1, allow non-compact frames to be parsed/dumped even if CRC fails.
#define WMBUS_ALLOW_NONCOMPACT_CRC_FAIL 1

#endif // __HWCONFIG_H__

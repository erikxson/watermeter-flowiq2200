#ifndef CREDENTIALS_H
#define CREDENTIALS_H
#include <Arduino.h>

// =============================================================================
// Wi-Fi + MQTT configuration
// =============================================================================
// 1) Copy this file to: src/credentials.h
// 2) Fill in your values below
//
// NOTE: Do NOT commit src/credentials.h (it contains secrets). The .gitignore
// already excludes it.
//
// credentials[i] = { WIFI_SSID, WIFI_PASSWORD, MQTT_BROKER_IP_OR_HOSTNAME }
#define NUM_SSID_CREDENTIALS  1
static const char *credentials[NUM_SSID_CREDENTIALS][3] =
{
  { "YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD", "YOUR_MQTT_BROKER_IP_OR_HOST" }
};

// MQTT authentication (leave empty strings if your broker does not require auth)
const char mqtt_user[] = "YOUR_MQTT_USERNAME";
const char mqtt_pass[] = "YOUR_MQTT_PASSWORD";

// =============================================================================
// Meter configuration (FlowIQ 2200 / Kamstrup)
// =============================================================================
// meterId and key are provided by your utility / IR readout.
//
// - meterId: 4 bytes, written as hex (0x..). Example below is 0x53 0x48 0x08 0x78
// - key:     16 bytes AES-128 key, written as hex (0x..)
//
// If your utility gives values like "53480878", convert to bytes:
//   "53 48 08 78" -> { 0x53, 0x48, 0x08, 0x78 }
//
// If your utility gives the AES key as 32 hex characters (e.g. "76A30C2C..."):
// split into byte pairs: 76 A3 0C 2C ... and prefix each with 0x.
const uint8_t meterId[4] = { 0x53, 0x48, 0x08, 0x78 };

const uint8_t key[16] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

#endif // CREDENTIALS_H
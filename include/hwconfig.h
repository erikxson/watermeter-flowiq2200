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

#endif // __HWCONFIG_H__

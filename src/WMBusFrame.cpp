/*
 Copyright (C) 2020 chester4444@wolke7.net
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

Modifications:
Modified by erikxson, 2026:
- FlowIQ 2200 support (volume + month start + flow in l/h)
- MQTT Home Assistant discovery support
- Robust MQTT availability/heartbeat topics
- Removed unused temperature fields
*/

#include <Arduino.h>
#include <string.h>

#include "WMbusFrame.h"
#include "credentials.h"

// Implementeras i main.cpp
void mqttMyData(const char* str);
void mqttMyDataJson(const char* str);

// ---- Helpers ---------------------------------------------------------

static void meterIdBcdToString(const uint8_t id[4], char out[9])
{
  // Ex: {0x53,0x48,0x08,0x78} => "53480878"
  snprintf(out, 9, "%02X%02X%02X%02X", id[0], id[1], id[2], id[3]);
}

static uint32_t readLEu32(const uint8_t* p)
{
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

static uint32_t bcdLeToUInt(const uint8_t* p, int bytes, bool* ok)
{
  // Little-endian BCD: low nibble first digit, high nibble second digit
  uint32_t v = 0;
  uint32_t mul = 1;

  for (int i = 0; i < bytes; i++)
  {
    uint8_t b = p[i];
    uint8_t lo = b & 0x0F;
    uint8_t hi = (b >> 4) & 0x0F;

    if (lo > 9 || hi > 9)
    {
      if (ok) *ok = false;
      return 0;
    }

    v += (uint32_t)lo * mul; mul *= 10;
    v += (uint32_t)hi * mul; mul *= 10;
  }

  if (ok) *ok = true;
  return v;
}

// Rimlighetsfilter för flöde i l/h (justerbart vid behov)
static bool isPlausibleFlowLph(long v)
{
  // 0..20000 l/h är rimligt i hushåll. Justera om du vill.
  return (v >= 0 && v <= 20000);
}

// Parse records (DIF/DIFE + VIF/VIFE) från startPos och leta efter FlowIQ2200 flöde.
// Enligt din debug: DIF=0x41, VIF=0x31, vlen=1, value=flöde i l/h.
static bool tryParseFlowLph_FlowIQ2200(const uint8_t* data, size_t len, size_t startPos, long* flowLphOut)
{
  if (!data || startPos >= len) return false;

  size_t pos = startPos;

  while (pos + 2 <= len)
  {
    // --- DIF + DIFE chain ---
    uint8_t dif0 = data[pos++];
    uint8_t lfield = (uint8_t)(dif0 & 0x0F);

    bool hasDife = (dif0 & 0x80) != 0;
    while (hasDife && pos < len)
    {
      uint8_t dife = data[pos++];
      hasDife = (dife & 0x80) != 0;
    }
    if (pos >= len) break;

    // --- VIF + VIFE chain (vi tar sista byte i kedjan som "slutlig") ---
    uint8_t vif = data[pos++];
    while ((vif & 0x80) && pos < len)
    {
      vif = data[pos++];
    }
    vif &= 0x7F; // 7-bit VIF

    // --- Determine value length from DIF L-field ---
    int vlen = -1;
    bool isBcd = false;

    if (lfield <= 4) vlen = (int)lfield;
    else if (lfield == 5) { vlen = 2; isBcd = true; }
    else if (lfield == 6) { vlen = 3; isBcd = true; }
    else if (lfield == 7) { vlen = 4; isBcd = true; }
    else if (lfield == 8)
    {
      if (pos >= len) break;
      vlen = (int)data[pos++];
    }
    else if (lfield == 0x0F)
    {
      // No data / special function, fortsätt
      continue;
    }
    else
    {
      // Okänd/special – fortsätt skanna i stället för att avbryta
      continue;
    }

    if (vlen < 0) continue;
    if (pos + (size_t)vlen > len) break;

    // --- FlowIQ 2200: Flow i l/h verkar ligga som VIF 0x31 med 1 byte ---
    if (vif == 0x31)
    {
      uint32_t raw = 0;

      if (isBcd)
      {
        bool ok = false;
        raw = bcdLeToUInt(&data[pos], vlen, &ok);
        if (!ok)
        {
          pos += (size_t)vlen;
          continue;
        }
      }
      else
      {
        for (int i = 0; i < vlen && i < 4; i++)
          raw |= ((uint32_t)data[pos + i] << (8 * i));
      }

      long flow = (long)raw; // Ingen /10. Din debug visar att 0x55 => 85 l/h direkt.

      if (isPlausibleFlowLph(flow))
      {
        if (flowLphOut) *flowLphOut = flow;
        return true;
      }
    }

    // hoppa förbi värdet
    pos += (size_t)vlen;
  }

  return false;
}

// Snabb “direktmatch” på det mönster du såg i debug:
// vid offset pos_tg+4: DIF=0x41, VIF=0x31, value=1 byte (flöde).
static bool tryParseFlowAtFixedOffset(const uint8_t* data, size_t len, size_t offset, long* flowLphOut)
{
  // behöver minst DIF + VIF + 1 byte värde
  if (!data) return false;
  if (offset + 2 >= len) return false;

  uint8_t dif = data[offset + 0];
  uint8_t vif = (uint8_t)(data[offset + 1] & 0x7F);

  // DIF=0x41 => lfield=1 byte (stämmer med din REC[6])
  if (dif == 0x41 && vif == 0x31)
  {
    long flow = (long)data[offset + 2];
    if (isPlausibleFlowLph(flow))
    {
      if (flowLphOut) *flowLphOut = flow;
      return true;
    }
  }

  return false;
}

// ---- Class -----------------------------------------------------------

WMBusFrame::WMBusFrame()
{
  aes128.setKey(key, sizeof(key));
}

void WMBusFrame::check()
{
  // check meterId (original logic)
  for (uint8_t i = 0; i < 4; i++)
  {
    if (meterId[i] != payload[6 - i])
    {
      isValid = false;
      return;
    }
  }
  isValid = true;
}

void WMBusFrame::printMeterInfo(uint8_t *data, size_t len)
{
  // FlowIQ 2200 compact frame indicator
  if (!data || len < 20) return;
  if (data[2] != 0x79) return;

  // CRC check
  uint16_t calc_crc = crc16_EN13757(data + 2, len - 2);
  uint16_t read_crc = ((uint16_t)data[1] << 8) | (uint16_t)data[0];
  if (calc_crc != read_crc) return;

  // Offsets for FlowIQ2200 compact (validated earlier)
  const int pos_tt = 11; // CurrentValue (liter)
  const int pos_tg = 15; // MonthStartValue (liter)
  if ((size_t)(pos_tg + 3) >= len) return;

  uint32_t tt = readLEu32(&data[pos_tt]);
  uint32_t tg = readLEu32(&data[pos_tg]);

  // MeterId string from credentials
  char meterIdStr[9];
  meterIdBcdToString(meterId, meterIdStr);

  // Publish simple sensor state (m3, retained)
  char stateStr[32];
  snprintf(stateStr, sizeof(stateStr), "%u.%03u", (unsigned)(tt / 1000U), (unsigned)(tt % 1000U));
  mqttMyData(stateStr);

  // --- Flow parsing ---
  long flowLph = 0;
  const size_t flowStart = (size_t)(pos_tg + 4);

  // 1) Försök direktmatch på det mönster vi såg i din debug
  if (!tryParseFlowAtFixedOffset(data, len, flowStart, &flowLph))
  {
    // 2) Fallback: skanna records från efter volymfälten och leta efter VIF 0x31
    if (!tryParseFlowLph_FlowIQ2200(data, len, flowStart, &flowLph))
    {
      flowLph = 0;
    }
  }

  // JSON (no temperatures)
  char json[240];
  snprintf(
    json,
    sizeof(json),
    "{\"MeterId\":\"%s\",\"MeterIdConfig\":\"%s\",\"CurrentValue\":%u.%03u,\"MonthStartValue\":%u.%03u,\"FlowLph\":%ld}",
    meterIdStr,
    meterIdStr,
    (unsigned)(tt / 1000U), (unsigned)(tt % 1000U),
    (unsigned)(tg / 1000U), (unsigned)(tg % 1000U),
    flowLph
  );

  mqttMyDataJson(json);
}

void WMBusFrame::decode()
{
  // check meterId
  check();
  if (!isValid) return;

  // cipher starts at index 16, remove 2 crc bytes and 16 bytes header
  uint8_t cipherLength = length - 2 - 16;
  memcpy(cipher, &payload[16], cipherLength);

  memset(iv, 0, sizeof(iv));   // padding with 0
  memcpy(iv, &payload[1], 8);
  iv[8] = payload[10];
  memcpy(&iv[9], &payload[12], 4);

  aes128.setIV(iv, sizeof(iv));
  aes128.decrypt(plaintext, (const uint8_t *)cipher, cipherLength);

  printMeterInfo(plaintext, cipherLength);
}

uint16_t WMBusFrame::crc16_EN13757(uint8_t *data, size_t len)
{
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; ++i)
    crc = crc16_EN13757_per_byte(crc, data[i]);
  return (uint16_t)(~crc);
}

#define CRC16_EN_13757 0x3D65

uint16_t WMBusFrame::crc16_EN13757_per_byte(uint16_t crc, uint8_t b)
{
  for (unsigned char i = 0; i < 8; i++)
  {
    if (((crc & 0x8000) >> 8) ^ (b & 0x80))
      crc = (crc << 1) ^ CRC16_EN_13757;
    else
      crc = (crc << 1);
    b <<= 1;
  }
  return crc;
}

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
#include "hwconfig.h"

// Implementeras i main.cpp
void mqttMyData(const char* str);
void mqttMyDataJson(const char* str);
void mqttDebugRaw(const char* str);
void mqttDebugRecord(const char* str);

#ifndef WMBUS_DEBUG_RECORD_DUMP
#define WMBUS_DEBUG_RECORD_DUMP 0
#endif

#ifndef WMBUS_DEBUG_RECORD_DUMP_ON_FAIL
#define WMBUS_DEBUG_RECORD_DUMP_ON_FAIL 1
#endif

#ifndef WMBUS_DEBUG_SERIAL_FLOW
#define WMBUS_DEBUG_SERIAL_FLOW 0
#endif

#ifndef WMBUS_DEBUG_SERIAL_DUMP
#define WMBUS_DEBUG_SERIAL_DUMP 0
#endif

#ifndef WMBUS_DEBUG_SERIAL_VIF_RANGE
#define WMBUS_DEBUG_SERIAL_VIF_RANGE 0
#endif

#ifndef WMBUS_FLOW_WRAP_CORRECTION
#define WMBUS_FLOW_WRAP_CORRECTION 0
#endif

#ifndef WMBUS_DEBUG_SERIAL_CI
#define WMBUS_DEBUG_SERIAL_CI 0
#endif

#ifndef WMBUS_DEBUG_CRC
#define WMBUS_DEBUG_CRC 0
#endif

#ifndef WMBUS_ALLOW_NONCOMPACT_CRC_FAIL
#define WMBUS_ALLOW_NONCOMPACT_CRC_FAIL 0
#endif

#ifndef WMBUS_FLOW_USE_FIXED_OFFSET
#define WMBUS_FLOW_USE_FIXED_OFFSET 0
#endif

#ifndef WMBUS_FULL_FRAME_CACHE
#define WMBUS_FULL_FRAME_CACHE 0
#endif

#ifndef WMBUS_FULL_FRAME_MAX_AGE_MS
#define WMBUS_FULL_FRAME_MAX_AGE_MS 900000UL
#endif

#ifndef WMBUS_FLOW_PREFER_FULL
#define WMBUS_FLOW_PREFER_FULL 0
#endif

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

// Rimlighetsfilter fÃ¶r flÃ¶de i l/h (justerbart vid behov)
static bool isPlausibleFlowLph(long v)
{
  // 0..20000 l/h Ã¤r rimligt i hushÃ¥ll. Justera om du vill.
  return (v >= 0 && v <= 20000);
}

#if WMBUS_FLOW_WRAP_CORRECTION
// Wrap correction for 8-bit flow values (0..255). If value wraps, add 256.
static bool g_flowWrapNoVolume = false;

static long applyFlowWrapCorrection(long flowRaw)
{
  static bool add256 = false;
  static long prevRaw = -1;

  const long hi = 192; // hysteresis high threshold
  const long lo = 64;  // hysteresis low threshold

  // If volume didn't change, don't assume wrap (prevents false >256 when flow stops).
  if (g_flowWrapNoVolume)
  {
    add256 = false;
  }
  else if (prevRaw >= 0)
  {
    if (prevRaw >= hi && flowRaw <= lo)
    {
      add256 = true;
    }
    else if (prevRaw <= lo && flowRaw >= hi)
    {
      add256 = false;
    }
  }

  prevRaw = flowRaw;

  if (add256)
  {
    return flowRaw + 256;
  }

  return flowRaw;
}
#endif

// ---- Full frame cache -------------------------------------------------
#if WMBUS_FULL_FRAME_CACHE
struct FullFrameCache
{
  bool valid = false;
  uint32_t tsMs = 0;
  size_t len = 0;
  uint8_t ci = 0;
  long flowLph = 0;
  int flowVlen = 0;
  uint8_t flowVif = 0;
  bool flowValid = false;
  uint8_t data[WMBusFrame::MAX_LENGTH];
};

static FullFrameCache g_fullFrame;

static void cacheFullFrame(const uint8_t* data, size_t len, uint8_t ci, bool flowValid, long flowLph, int flowVlen, uint8_t flowVif)
{
  if (!data || len == 0) return;

  if (len > WMBusFrame::MAX_LENGTH) len = WMBusFrame::MAX_LENGTH;

  g_fullFrame.len = len;
  g_fullFrame.ci = ci;
  g_fullFrame.tsMs = millis();
  g_fullFrame.valid = true;

  g_fullFrame.flowValid = flowValid;
  if (flowValid)
  {
    g_fullFrame.flowLph = flowLph;
    g_fullFrame.flowVlen = flowVlen;
    g_fullFrame.flowVif = flowVif;
  }

  memcpy(g_fullFrame.data, data, len);
}

static bool getCachedFullFlow(long* flowOut, int* vlenOut, uint8_t* vifOut)
{
  if (!flowOut) return false;
  if (!g_fullFrame.valid || !g_fullFrame.flowValid) return false;

  const uint32_t now = millis();
  if ((uint32_t)(now - g_fullFrame.tsMs) > (uint32_t)WMBUS_FULL_FRAME_MAX_AGE_MS) return false;

  *flowOut = g_fullFrame.flowLph;
  if (vlenOut) *vlenOut = g_fullFrame.flowVlen;
  if (vifOut) *vifOut = g_fullFrame.flowVif;
  return true;
}
#endif

// Compact flow extension (use last full-flow to extend compact 8-bit values)
static long g_compactOffset = 0;
static bool g_compactOffsetValid = false;

static long computeCompactOffsetFromFull(long fullFlow)
{
  if (fullFlow < 256) return 0;
  long off = (fullFlow / 256L) * 256L;
  if (off < 0) off = 0;
  return off;
}

static void updateCompactOffsetFromFull(long fullFlow, int fullVlen)
{
  if (fullVlen < 2) return;
  const long off = computeCompactOffsetFromFull(fullFlow);
  g_compactOffset = off;
  g_compactOffsetValid = (off > 0);
}

static bool applyCompactOffsetIfValid(long* flowLph)
{
  if (!flowLph) return false;
  if (!g_compactOffsetValid) return false;
  *flowLph += g_compactOffset;
  return true;
}

// ---- Record parsing helpers ------------------------------------------

static uint16_t crc16_EN13757_buf(const uint8_t* data, size_t len)
{
  if (!data) return 0;

  uint16_t crc = 0x0000;
  const uint16_t poly = 0x3D65;

  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
    {
      if (crc & 1)
        crc = (uint16_t)((crc >> 1) ^ poly);
      else
        crc = (uint16_t)(crc >> 1);
    }
  }

  return (uint16_t)(~crc);
}

static uint16_t crc_mirror(uint16_t crc, uint8_t bits)
{
  uint16_t i;
  uint16_t j = 1;
  uint16_t out = 0;
  for (i = (uint16_t)1 << (bits - 1); i; i >>= 1)
  {
    if (crc & i)
      out |= j;
    j <<= 1;
  }
  return out;
}

static uint16_t crc_internal(const uint8_t* p, uint16_t len, uint16_t poly, uint16_t init, bool revIn, bool revOut)
{
  uint16_t i, j, c, bit, crc;

  crc = init;
  for (i = 0; i < 16; i++)
  {
    bit = crc & 1;
    if (bit) crc ^= poly;
    crc >>= 1;
    if (bit) crc |= 0x8000;
  }

  for (i = 0; i < len; i++)
  {
    c = (uint16_t)*p++;
    if (revIn) c = crc_mirror(c, 8);

    for (j = 0x80; j; j >>= 1)
    {
      bit = crc & 0x8000;
      crc <<= 1;
      if (c & j) crc |= 1;
      if (bit) crc ^= poly;
    }
  }

  for (i = 0; i < 16; i++)
  {
    bit = crc & 0x8000;
    crc <<= 1;
    if (bit) crc ^= poly;
  }

  if (revOut) crc = crc_mirror(crc, 16);
  crc ^= 0xffff;

  return crc;
}

static uint16_t crc16_X25_buf(const uint8_t* data, size_t len)
{
  if (!data) return 0;
  return crc_internal(data, (uint16_t)len, 0x1021, 0xffff, true, true);
}

static void checkCrcBlocks(const uint8_t* data, size_t len, size_t offset, int* blocksOut, int* okOut)
{
  int blocks = 0;
  int ok = 0;
  if (!data || len == 0 || offset >= len) { if (blocksOut) *blocksOut = 0; if (okOut) *okOut = 0; return; }

  size_t pos = offset;
  while (pos + 16 + 2 <= len)
  {
    uint16_t calc = crc16_EN13757_buf(&data[pos], 16);
    uint16_t read = ((uint16_t)data[pos + 17] << 8) | (uint16_t)data[pos + 16];
    if (calc == read) ok++;
    blocks++;
    pos += 18;
  }

  if (blocksOut) *blocksOut = blocks;
  if (okOut) *okOut = ok;
}

static void checkLinkCrcBlocks(const uint8_t* data, size_t len, size_t offset, int* blocksOut, int* okLEOut, int* okBEOut)
{
  int blocks = 0;
  int okLE = 0;
  int okBE = 0;
  if (!data || len == 0 || offset >= len) { if (blocksOut) *blocksOut = 0; if (okLEOut) *okLEOut = 0; if (okBEOut) *okBEOut = 0; return; }

  size_t pos = offset;
  while (pos + 16 + 2 <= len)
  {
    uint16_t calc = crc16_EN13757_buf(&data[pos], 16);
    uint16_t readLE = ((uint16_t)data[pos + 17] << 8) | (uint16_t)data[pos + 16];
    uint16_t readBE = ((uint16_t)data[pos + 16] << 8) | (uint16_t)data[pos + 17];
    if (calc == readLE) okLE++;
    if (calc == readBE) okBE++;
    blocks++;
    pos += 18;
  }

  if (blocksOut) *blocksOut = blocks;
  if (okLEOut) *okLEOut = okLE;
  if (okBEOut) *okBEOut = okBE;
}

static void findBestCrcBlocks(const uint8_t* data, size_t len, bool crcAfter, int* bestOff, int* bestOkLE, int* bestOkBE, int* bestBlocks)
{
  int bestScore = -1;
  int bestOffset = -1;
  int bestLE = 0;
  int bestBE = 0;
  int bestBlk = 0;

  if (!data || len < 18)
  {
    if (bestOff) *bestOff = -1;
    if (bestOkLE) *bestOkLE = 0;
    if (bestOkBE) *bestOkBE = 0;
    if (bestBlocks) *bestBlocks = 0;
    return;
  }

  const int maxOff = (len < 20) ? (int)len - 18 : 18;
  for (int off = 0; off < maxOff; off++)
  {
    int blocks = 0;
    int okLE = 0;
    int okBE = 0;
    size_t pos = (size_t)off;

    while (pos + 18 <= len)
    {
      const uint8_t* block = crcAfter ? &data[pos] : &data[pos + 2];
      uint16_t calc = crc16_EN13757_buf(block, 16);
      uint16_t readLE = crcAfter
        ? (uint16_t)(((uint16_t)data[pos + 17] << 8) | (uint16_t)data[pos + 16])
        : (uint16_t)(((uint16_t)data[pos + 1] << 8) | (uint16_t)data[pos + 0]);
      uint16_t readBE = crcAfter
        ? (uint16_t)(((uint16_t)data[pos + 16] << 8) | (uint16_t)data[pos + 17])
        : (uint16_t)(((uint16_t)data[pos + 0] << 8) | (uint16_t)data[pos + 1]);
      if (calc == readLE) okLE++;
      if (calc == readBE) okBE++;
      blocks++;
      pos += 18;
    }

    const int score = (okLE > okBE) ? okLE : okBE;
    if (score > bestScore)
    {
      bestScore = score;
      bestOffset = off;
      bestLE = okLE;
      bestBE = okBE;
      bestBlk = blocks;
    }
  }

  if (bestOff) *bestOff = bestOffset;
  if (bestOkLE) *bestOkLE = bestLE;
  if (bestOkBE) *bestOkBE = bestBE;
  if (bestBlocks) *bestBlocks = bestBlk;
}

struct RecordDumpInfo
{
  size_t pos = 0;
  uint8_t dif = 0;
  uint8_t difeCount = 0;
  uint32_t storage = 0;
  uint8_t vifBytes[8];
  uint8_t vifCount = 0;
  uint8_t vifStored = 0;
  uint8_t lfield = 0;
  int vlen = 0;
  bool isBcd = false;
  const uint8_t* value = nullptr;
};

static bool parseRecordForDump(const uint8_t* data, size_t len, size_t* posInOut, RecordDumpInfo* out)
{
  if (!data || !posInOut || !out) return false;

  size_t pos = *posInOut;
  if (pos >= len) return false;

  const size_t startPos = pos;
  const uint8_t dif0 = data[pos++];
  const uint8_t lfield = (uint8_t)(dif0 & 0x0F);
  uint32_t storage = (uint32_t)((dif0 >> 6) & 0x03);
  uint8_t storageShift = 2;

  // DIFEs
  uint8_t difeCount = 0;
  bool hasDife = (dif0 & 0x80) != 0;
  while (hasDife && pos < len)
  {
    const uint8_t dife = data[pos++];
    difeCount++;
    storage |= (uint32_t)(dife & 0x0F) << storageShift;
    storageShift = (uint8_t)(storageShift + 4);
    hasDife = (dife & 0x80) != 0;
  }
  if (pos >= len) return false;

  // VIF + VIFE chain
  uint8_t vifCount = 0;
  uint8_t vifStored = 0;
  while (pos < len)
  {
    const uint8_t vif = data[pos++];
    if (vifStored < sizeof(out->vifBytes))
    {
      out->vifBytes[vifStored++] = vif;
    }
    vifCount++;
    if ((vif & 0x80) == 0) break;
  }
  if (pos > len) return false;

  int vlen = -1;
  bool isBcd = false;

  if (lfield <= 4) vlen = (int)lfield;
  else if (lfield == 5) { vlen = 2; isBcd = true; }
  else if (lfield == 6) { vlen = 3; isBcd = true; }
  else if (lfield == 7) { vlen = 4; isBcd = true; }
  else if (lfield == 8)
  {
    if (pos >= len) return false;
    vlen = (int)data[pos++];
  }
  else if (lfield == 0x0F)
  {
    vlen = 0; // no data
  }
  else
  {
    return false; // unknown/special
  }

  if (vlen < 0) return false;
  if (pos + (size_t)vlen > len) return false;

  out->pos = startPos;
  out->dif = dif0;
  out->difeCount = difeCount;
  out->storage = storage;
  out->vifCount = vifCount;
  out->vifStored = vifStored;
  out->lfield = lfield;
  out->vlen = vlen;
  out->isBcd = isBcd;
  out->value = &data[pos];

  pos += (size_t)vlen;
  if (pos <= startPos) return false;
  *posInOut = pos;

  return true;
}

static size_t findRecordStartCandidate(const uint8_t* data, size_t len, size_t hintPos)
{
  const int kMinRecords = 2;
  if (!data || hintPos >= len) return hintPos;

  for (size_t start = hintPos; start + 2 < len; start++)
  {
    size_t pos = start;
    int count = 0;
    while (count < kMinRecords)
    {
      RecordDumpInfo rec;
      const size_t before = pos;
      if (!parseRecordForDump(data, len, &pos, &rec)) break;
      if (pos <= before) break;
      count++;
    }
    if (count >= kMinRecords) return start;
  }

  return hintPos;
}

static bool getRecordVifLast(const RecordDumpInfo& rec, uint8_t* vifOut)
{
  if (!vifOut) return false;
  if (rec.vifStored == 0) return false;
  *vifOut = (uint8_t)(rec.vifBytes[rec.vifStored - 1] & 0x7F);
  return true;
}

static bool decodeRecordValue(const RecordDumpInfo& rec, uint32_t* rawOut)
{
  if (!rawOut) return false;
  if (rec.vlen <= 0 || !rec.value) return false;

  if (rec.isBcd)
  {
    bool ok = false;
    uint32_t v = bcdLeToUInt(rec.value, rec.vlen, &ok);
    if (!ok) return false;
    *rawOut = v;
    return true;
  }

  uint32_t v = 0;
  for (int i = 0; i < rec.vlen && i < 4; i++)
  {
    v |= ((uint32_t)rec.value[i] << (8 * i));
  }
  *rawOut = v;
  return true;
}

static bool isFlowVif(uint8_t vifLast)
{
  return ((vifLast >= 0x30 && vifLast <= 0x37) || (vifLast == 0x3B));
}

static uint8_t difFunction(uint8_t dif)
{
  return (uint8_t)((dif >> 4) & 0x03);
}

static int scoreFlowRecord(const RecordDumpInfo& rec, bool preferWide)
{
  int score = 0;
  const uint8_t func = difFunction(rec.dif);

  // Prefer instantaneous values (function = 0) and current storage.
  if (func == 0) score += 100;
  if (rec.storage == 0) score += 20;

  // Prefer wider values if requested.
  if (preferWide)
  {
    if (rec.vlen >= 2) score += 60;
    else score -= 10;
  }

  // Slight tie-breaker.
  score += rec.vlen;
  return score;
}

static bool considerFlowCandidate(const RecordDumpInfo& rec, bool preferWide, long* bestFlow, int* bestScore, int* bestVlen, uint8_t* bestVif)
{
  if (!bestFlow || !bestScore || !bestVlen) return false;
  if (rec.vlen <= 0) return false;
  if (preferWide && rec.vlen < 2) return false;

  uint8_t vifLast = 0;
  if (!getRecordVifLast(rec, &vifLast)) return false;
  if (!isFlowVif(vifLast)) return false;

  uint32_t raw = 0;
  if (!decodeRecordValue(rec, &raw)) return false;

  long flow = (long)raw;
#if WMBUS_FLOW_WRAP_CORRECTION
  if (rec.vlen == 1)
  {
    flow = applyFlowWrapCorrection(flow);
  }
#endif

  if (!isPlausibleFlowLph(flow)) return false;

  const int score = scoreFlowRecord(rec, preferWide);
  if (score > *bestScore)
  {
    *bestScore = score;
    *bestFlow = flow;
    *bestVlen = rec.vlen;
    if (bestVif) *bestVif = vifLast;
    return true;
  }

  return false;
}

#if WMBUS_DEBUG_RECORD_DUMP
static void buildHexList(const uint8_t* bytes, size_t count, char* out, size_t outSize)
{
  if (!out || outSize == 0) return;
  out[0] = '\0';

  if (!bytes || count == 0)
  {
    snprintf(out, outSize, "-");
    return;
  }

  char* p = out;
  size_t left = outSize;

  for (size_t i = 0; i < count; i++)
  {
    int n = snprintf(p, left, (i == 0) ? "%02X" : " %02X", bytes[i]);
    if (n <= 0 || (size_t)n >= left) break;
    p += n;
    left -= (size_t)n;
  }
}

static void dumpRawPayload(const uint8_t* data, size_t len)
{
  char buf[900];
  int n = snprintf(buf, sizeof(buf), "len=%u data=", (unsigned)len);
  if (n <= 0 || (size_t)n >= sizeof(buf)) return;

  size_t pos = (size_t)n;
  for (size_t i = 0; i < len; i++)
  {
    int w = snprintf(buf + pos, sizeof(buf) - pos, (i + 1 < len) ? "%02X " : "%02X", data[i]);
    if (w <= 0 || (size_t)w >= sizeof(buf) - pos) break;
    pos += (size_t)w;
  }

  mqttDebugRaw(buf);

#if WMBUS_DEBUG_SERIAL_DUMP
  Serial.print("RAW ");
  Serial.println(buf);
#endif
}

static void dumpRecords(const uint8_t* data, size_t len, size_t startPos)
{
  const int kMaxRecords = 12;
  char line[200];

  snprintf(line, sizeof(line), "dump start=%u len=%u", (unsigned)startPos, (unsigned)len);
  mqttDebugRecord(line);
#if WMBUS_DEBUG_SERIAL_DUMP
  Serial.print("REC ");
  Serial.println(line);
#endif

  size_t pos = startPos;
  for (int idx = 0; idx < kMaxRecords && pos < len; idx++)
  {
    RecordDumpInfo rec;
    const size_t before = pos;
    if (!parseRecordForDump(data, len, &pos, &rec)) break;
    if (pos <= before) break;

    char vifs[64];
    buildHexList(rec.vifBytes, rec.vifStored, vifs, sizeof(vifs));

    char vals[64];
    const size_t valMax = 8;
    const size_t valCount = (rec.vlen < 0) ? 0 : (size_t)rec.vlen;
    const size_t valShown = (valCount < valMax) ? valCount : valMax;
    buildHexList(rec.value, valShown, vals, sizeof(vals));

    snprintf(
      line,
      sizeof(line),
      "rec=%02d pos=%u dif=0x%02X dife=%u l=%u vifs=%s vifs_n=%u vlen=%d bcd=%u val=%s val_n=%u",
      idx,
      (unsigned)rec.pos,
      rec.dif,
      rec.difeCount,
      rec.lfield,
      vifs,
      rec.vifCount,
      rec.vlen,
      rec.isBcd ? 1 : 0,
      vals,
      (unsigned)valCount
    );
    mqttDebugRecord(line);
#if WMBUS_DEBUG_SERIAL_DUMP
    Serial.print("REC ");
    Serial.println(line);
#endif
  }
}

static void dumpVifRangeRecords(const uint8_t* data, size_t len, size_t startPos, uint8_t vifMin, uint8_t vifMax)
{
#if WMBUS_DEBUG_SERIAL_VIF_RANGE
  char line[200];
  snprintf(line, sizeof(line), "VIFR range=0x%02X..0x%02X start=%u len=%u", vifMin, vifMax, (unsigned)startPos, (unsigned)len);
  Serial.println(line);

  size_t pos = startPos;
  for (int idx = 0; idx < 32 && pos < len; idx++)
  {
    RecordDumpInfo rec;
    const size_t before = pos;
    if (!parseRecordForDump(data, len, &pos, &rec)) break;
    if (pos <= before) break;

    if (rec.vifStored == 0) continue;
    const uint8_t vifLast = (uint8_t)(rec.vifBytes[rec.vifStored - 1] & 0x7F);
    if (vifLast < vifMin || vifLast > vifMax) continue;

    char vifs[64];
    buildHexList(rec.vifBytes, rec.vifStored, vifs, sizeof(vifs));

    char vals[64];
    const size_t valMax = 8;
    const size_t valCount = (rec.vlen < 0) ? 0 : (size_t)rec.vlen;
    const size_t valShown = (valCount < valMax) ? valCount : valMax;
    buildHexList(rec.value, valShown, vals, sizeof(vals));

    snprintf(
      line,
      sizeof(line),
      "VIFR rec=%02d pos=%u dif=0x%02X l=%u vif=0x%02X vifs=%s vlen=%d bcd=%u val=%s val_n=%u",
      idx,
      (unsigned)rec.pos,
      rec.dif,
      rec.lfield,
      vifLast,
      vifs,
      rec.vlen,
      rec.isBcd ? 1 : 0,
      vals,
      (unsigned)valCount
    );
    Serial.println(line);
  }
#else
  (void)data; (void)len; (void)startPos; (void)vifMin; (void)vifMax;
#endif
}
#endif // WMBUS_DEBUG_RECORD_DUMP


// Parse records (DIF/DIFE + VIF/VIFE) frÃ¥n startPos och leta efter FlowIQ2200 flÃ¶de.
// Enligt din debug: DIF=0x41, VIF=0x31, vlen=1, value=flÃ¶de i l/h.
#if 0
static bool tryParseFlowLph_FlowIQ2200Ex(const uint8_t* data, size_t len, size_t startPos, bool preferWide, long* flowLphOut, int* vlenOut, uint8_t* vifOut)
{
  if (!data || len == 0) return false;

  bool found = false;
  long bestFlow = 0;
  int bestScore = -100000;
  int bestVlen = 0;
  uint8_t bestVif = 0;

  // 1) Try parsing from a plausible record start (near startPos).
  const size_t hint = (startPos < len) ? startPos : 0;
  size_t pos = findRecordStartCandidate(data, len, hint);
  if (pos < len)
  {
    while (pos + 2 <= len)
    {
      RecordDumpInfo rec;
      const size_t before = pos;
      if (!parseRecordForDump(data, len, &pos, &rec)) break;
      if (pos <= before) break;

      if (considerFlowCandidate(rec, preferWide, &bestFlow, &bestScore, &bestVlen, &bestVif))
      {
        found = true;
      }
    }
  }

  // 2) Fallback: brute-force scan for any flow record.
  if (!found)
  {
    for (size_t i = 0; i + 2 <= len; i++)
    {
      size_t p = i;
      RecordDumpInfo rec;
      if (!parseRecordForDump(data, len, &p, &rec)) continue;
      if (considerFlowCandidate(rec, preferWide, &bestFlow, &bestScore, &bestVlen, &bestVif))
      {
        found = true;
      }
    }
  }

  if (found)
  {
    if (flowLphOut) *flowLphOut = bestFlow;
    if (vlenOut) *vlenOut = bestVlen;
    if (vifOut) *vifOut = bestVif;
    return true;
  }

  return false;
}

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
      // No data / special function, fortsÃ¤tt
      continue;
    }
    else
    {
      // OkÃ¤nd/special â€“ fortsÃ¤tt skanna i stÃ¤llet fÃ¶r att avbryta
      continue;
    }

    if (vlen < 0) continue;
    if (pos + (size_t)vlen > len) break;

    // --- FlowIQ 2200: Flow i l/h verkar ligga som VIF 0x31 med 1 byte ---
    if (vif == 0x31 || vif == 0x32)
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
#if WMBUS_FLOW_WRAP_CORRECTION
      flow = applyFlowWrapCorrection(flow);
#endif

      if (isPlausibleFlowLph(flow))
      {
        if (flowLphOut) *flowLphOut = flow;
        return true;
      }
    }

    // hoppa fÃ¶rbi vÃ¤rdet
    pos += (size_t)vlen;
  }

  return false;
}

// Snabb â€œdirektmatchâ€ pÃ¥ det mÃ¶nster du sÃ¥g i debug:
// vid offset pos_tg+4: DIF=0x41, VIF=0x31, value=1 byte (flÃ¶de).
#endif

// Parse records (DIF/DIFE + VIF/VIFE) frÃƒÂ¥n startPos och leta efter flÃƒÂ¶de.
// Vi matchar VIF i intervallet 0x30..0x37 (volymflÃƒÂ¶de).
static bool tryParseFlowLph_FlowIQ2200Ex(const uint8_t* data, size_t len, size_t startPos, bool preferWide, long* flowLphOut, int* vlenOut, uint8_t* vifOut)
{
  if (!data || len == 0) return false;

  bool found = false;
  long bestFlow = 0;
  int bestScore = -100000;
  int bestVlen = 0;
  uint8_t bestVif = 0;

  // 1) Try parsing from a plausible record start (near startPos).
  const size_t hint = (startPos < len) ? startPos : 0;
  size_t pos = findRecordStartCandidate(data, len, hint);
  if (pos < len)
  {
    while (pos + 2 <= len)
    {
      RecordDumpInfo rec;
      const size_t before = pos;
      if (!parseRecordForDump(data, len, &pos, &rec)) break;
      if (pos <= before) break;

      if (considerFlowCandidate(rec, preferWide, &bestFlow, &bestScore, &bestVlen, &bestVif))
      {
        found = true;
      }
    }
  }

  // 2) Fallback: brute-force scan for any flow record.
  if (!found)
  {
    for (size_t i = 0; i + 2 <= len; i++)
    {
      size_t p = i;
      RecordDumpInfo rec;
      if (!parseRecordForDump(data, len, &p, &rec)) continue;
      if (considerFlowCandidate(rec, preferWide, &bestFlow, &bestScore, &bestVlen, &bestVif))
      {
        found = true;
      }
    }
  }

  if (found)
  {
    if (flowLphOut) *flowLphOut = bestFlow;
    if (vlenOut) *vlenOut = bestVlen;
    if (vifOut) *vifOut = bestVif;
    return true;
  }

  return false;
}

#if WMBUS_FLOW_USE_FIXED_OFFSET
static bool tryParseFlowAtFixedOffset(const uint8_t* data, size_t len, size_t offset, long* flowLphOut)
{
  // behÃ¶ver minst DIF + VIF + 1 byte vÃ¤rde
  if (!data) return false;
  if (offset + 2 >= len) return false;

  uint8_t dif = data[offset + 0];
  uint8_t vif = (uint8_t)(data[offset + 1] & 0x7F);

  // DIF=0x41 => lfield=1 byte (stÃ¤mmer med din REC[6])
  if (dif == 0x41 && (vif == 0x31 || vif == 0x32))
  {
    long flow = (long)data[offset + 2];
#if WMBUS_FLOW_WRAP_CORRECTION
    flow = applyFlowWrapCorrection(flow);
#endif
    if (isPlausibleFlowLph(flow))
    {
      if (flowLphOut) *flowLphOut = flow;
      return true;
    }
  }

  return false;
}
#endif

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
  // FlowIQ 2200 frame handling
  if (!data || len < 3) return;

#if WMBUS_DEBUG_SERIAL_CI
  Serial.printf("CI=0x%02X len=%u\n", data[2], (unsigned)len);
#endif

  const uint8_t ci = data[2];
  const bool isCompact = (ci == 0x79);

  if (len < 4) return;

  // CRC check
  uint16_t calc_crc = crc16_EN13757(data + 2, len - 2);
  uint16_t read_crc = ((uint16_t)data[1] << 8) | (uint16_t)data[0];
  const bool crcOk = (calc_crc == read_crc);

#if WMBUS_DEBUG_CRC
  if (!crcOk)
  {
    char line[120];
    snprintf(
      line,
      sizeof(line),
      "CRC_FAIL ci=0x%02X len=%u calc=0x%04X read=0x%04X",
      ci,
      (unsigned)len,
      (unsigned)calc_crc,
      (unsigned)read_crc
    );
    mqttDebugRecord(line);
#if WMBUS_DEBUG_SERIAL_DUMP
    Serial.println(line);
#endif

    if (!isCompact)
    {
      uint16_t tailReadLE = 0;
      uint16_t tailReadBE = 0;
      uint16_t tailCalcAll = 0;
      uint16_t tailCalcSkip2 = 0;
      bool tailOkLE = false;
      bool tailOkBE = false;
      bool tailOkSkip2LE = false;
      bool tailOkSkip2BE = false;

      if (len >= 4)
      {
        tailReadLE = ((uint16_t)data[len - 1] << 8) | (uint16_t)data[len - 2];
        tailReadBE = ((uint16_t)data[len - 2] << 8) | (uint16_t)data[len - 1];
        tailCalcAll = crc16_EN13757(data, len - 2);
        tailCalcSkip2 = crc16_EN13757(data + 2, len - 4);
        tailOkLE = (tailCalcAll == tailReadLE);
        tailOkBE = (tailCalcAll == tailReadBE);
        tailOkSkip2LE = (tailCalcSkip2 == tailReadLE);
        tailOkSkip2BE = (tailCalcSkip2 == tailReadBE);
      }

      int blk0 = 0;
      int ok0 = 0;
      int blk2 = 0;
      int ok2 = 0;
      checkCrcBlocks(data, len, 0, &blk0, &ok0);
      checkCrcBlocks(data, len, 2, &blk2, &ok2);

      int lblk0 = 0;
      int lok0le = 0;
      int lok0be = 0;
      int lblk16 = 0;
      int lok16le = 0;
      int lok16be = 0;
      checkLinkCrcBlocks(payload, length, 0, &lblk0, &lok0le, &lok0be);
      checkLinkCrcBlocks(payload, length, 16, &lblk16, &lok16le, &lok16be);

      char line2[160];
      snprintf(
        line2,
        sizeof(line2),
        "CRC_DBG tailLE=%d tailBE=%d tail2LE=%d tail2BE=%d blk0=%d/%d blk2=%d/%d",
        tailOkLE ? 1 : 0,
        tailOkBE ? 1 : 0,
        tailOkSkip2LE ? 1 : 0,
        tailOkSkip2BE ? 1 : 0,
        ok0,
        blk0,
        ok2,
        blk2
      );
      mqttDebugRecord(line2);
#if WMBUS_DEBUG_SERIAL_DUMP
      Serial.println(line2);
#endif

      char line3[160];
      snprintf(
        line3,
        sizeof(line3),
        "LCRC off0 le=%d/%d be=%d/%d off16 le=%d/%d be=%d/%d",
        lok0le,
        lblk0,
        lok0be,
        lblk0,
        lok16le,
        lblk16,
        lok16be,
        lblk16
      );
      mqttDebugRecord(line3);
#if WMBUS_DEBUG_SERIAL_DUMP
      Serial.println(line3);
#endif

      const uint16_t headReadLE = read_crc;
      const uint16_t headReadBE = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
      const uint16_t x25Head = crc16_X25_buf(data + 2, len - 2);
      bool x25HeadLE = (x25Head == headReadLE);
      bool x25HeadBE = (x25Head == headReadBE);
      uint16_t x25Tail = 0;
      bool x25TailLE = false;
      bool x25TailBE = false;
      if (len >= 4)
      {
        x25Tail = crc16_X25_buf(data, len - 2);
        x25TailLE = (x25Tail == tailReadLE);
        x25TailBE = (x25Tail == tailReadBE);
      }

      char line5[180];
      snprintf(
        line5,
        sizeof(line5),
        "CRC_X25 head=0x%04X le=%d be=%d tail=0x%04X le=%d be=%d",
        (unsigned)x25Head,
        x25HeadLE ? 1 : 0,
        x25HeadBE ? 1 : 0,
        (unsigned)x25Tail,
        x25TailLE ? 1 : 0,
        x25TailBE ? 1 : 0
      );
      mqttDebugRecord(line5);
#if WMBUS_DEBUG_SERIAL_DUMP
      Serial.println(line5);
#endif

      int bestOffA = -1, bestOkALE = 0, bestOkABE = 0, bestBlkA = 0;
      int bestOffB = -1, bestOkBLE = 0, bestOkBBE = 0, bestBlkB = 0;
      findBestCrcBlocks(payload, length, true, &bestOffA, &bestOkALE, &bestOkABE, &bestBlkA);
      findBestCrcBlocks(payload, length, false, &bestOffB, &bestOkBLE, &bestOkBBE, &bestBlkB);

      char line4[180];
      snprintf(
        line4,
        sizeof(line4),
        "LCRC_BEST post off=%d le=%d/%d be=%d/%d pre off=%d le=%d/%d be=%d/%d",
        bestOffA,
        bestOkALE,
        bestBlkA,
        bestOkABE,
        bestBlkA,
        bestOffB,
        bestOkBLE,
        bestBlkB,
        bestOkBBE,
        bestBlkB
      );
      mqttDebugRecord(line4);
#if WMBUS_DEBUG_SERIAL_DUMP
      Serial.println(line4);
#endif
    }
  }
#endif

#if WMBUS_ALLOW_NONCOMPACT_CRC_FAIL
  if (!crcOk && isCompact) return;
#else
  if (!crcOk) return;
#endif

  const bool allowPublish = crcOk;

#if WMBUS_FLOW_WRAP_CORRECTION
  if (!isCompact)
  {
    g_flowWrapNoVolume = false;
  }
#endif

  long fullFlowLph = 0;
  int fullFlowVlen = 0;
  uint8_t fullFlowVif = 0;
  bool gotFullFlow = false;

#if WMBUS_FULL_FRAME_CACHE
  if (!isCompact)
  {
    gotFullFlow = tryParseFlowLph_FlowIQ2200Ex(data, len, 0, true, &fullFlowLph, &fullFlowVlen, &fullFlowVif);
    if (gotFullFlow && (crcOk || WMBUS_ALLOW_NONCOMPACT_CRC_FAIL))
    {
      cacheFullFrame(data, len, ci, gotFullFlow, fullFlowLph, fullFlowVlen, fullFlowVif);
    }
  }
#endif

  // Volume values (from compact frame offsets, if available)
  static uint32_t lastTt = 0;
  static uint32_t lastTg = 0;
  static bool haveVol = false;
  bool volumeUpdated = false;
  uint32_t tt = lastTt;
  uint32_t tg = lastTg;

  if (isCompact)
  {
    if (len < 20) return;
    const int pos_tt = 11; // CurrentValue (liter)
    const int pos_tg = 15; // MonthStartValue (liter)
    if ((size_t)(pos_tg + 3) >= len) return;

    tt = readLEu32(&data[pos_tt]);
    tg = readLEu32(&data[pos_tg]);
    lastTt = tt;
    lastTg = tg;
    haveVol = true;
    volumeUpdated = true;
  }
  else
  {
    // For non-compact CI: use last known volumes (if any)
    if (!haveVol) return;
  }

#if WMBUS_FLOW_WRAP_CORRECTION
  static bool prevTtValid = false;
  static uint32_t prevTt = 0;
  if (volumeUpdated)
  {
    g_flowWrapNoVolume = (prevTtValid && tt == prevTt);
    prevTt = tt;
    prevTtValid = true;
  }
  else
  {
    g_flowWrapNoVolume = false;
  }
#endif

  // MeterId string from credentials
  char meterIdStr[9];
  meterIdBcdToString(meterId, meterIdStr);

  // Publish simple sensor state (m3, retained)
  char stateStr[32];
  snprintf(stateStr, sizeof(stateStr), "%u.%03u", (unsigned)(tt / 1000U), (unsigned)(tt % 1000U));
  if (allowPublish)
  {
    mqttMyData(stateStr);
  }

  // --- Flow parsing ---
  long flowLph = 0;
  int flowVlen = 0;
  uint8_t flowVif = 0;
  long flowLphFull = -1;
  const size_t recordHint = isCompact ? (size_t)(15 + 4) : 0;
  bool gotFlow = false;

  if (!isCompact)
  {
#if WMBUS_FULL_FRAME_CACHE
    gotFlow = gotFullFlow;
    flowLph = fullFlowLph;
    flowVlen = fullFlowVlen;
    flowVif = fullFlowVif;
#else
    gotFlow = tryParseFlowLph_FlowIQ2200Ex(data, len, 0, true, &flowLph, &flowVlen, &flowVif);
#endif
    if (!gotFlow)
    {
      gotFlow = tryParseFlowLph_FlowIQ2200Ex(data, len, 0, false, &flowLph, &flowVlen, &flowVif);
    }

    if (gotFlow)
    {
      flowLphFull = flowLph;
      updateCompactOffsetFromFull(flowLph, flowVlen);
    }
  }
  else
  {
#if WMBUS_FLOW_USE_FIXED_OFFSET
    gotFlow = tryParseFlowAtFixedOffset(data, len, (size_t)(15 + 4), &flowLph);
    if (gotFlow) flowVlen = 1;
#endif

    if (!gotFlow)
    {
      gotFlow = tryParseFlowLph_FlowIQ2200Ex(data, len, recordHint, false, &flowLph, &flowVlen, &flowVif);
    }

#if WMBUS_FLOW_PREFER_FULL && WMBUS_FULL_FRAME_CACHE
    {
      long cachedFlow = 0;
      int cachedVlen = 0;
      uint8_t cachedVif = 0;
      if (getCachedFullFlow(&cachedFlow, &cachedVlen, &cachedVif))
      {
        bool allow = true;
#if WMBUS_FLOW_WRAP_CORRECTION
        allow = !g_flowWrapNoVolume;
#endif
        if (allow && cachedVlen >= 2)
        {
          flowLph = cachedFlow;
          flowVlen = cachedVlen;
          gotFlow = true;
        }
      }
    }
#endif

    if (gotFlow && flowVlen == 1)
    {
      applyCompactOffsetIfValid(&flowLph);
    }
  }

  if (flowLphFull < 0)
  {
#if WMBUS_FULL_FRAME_CACHE
    long cachedFlow = 0;
    int cachedVlen = 0;
    uint8_t cachedVif = 0;
    if (getCachedFullFlow(&cachedFlow, &cachedVlen, &cachedVif))
    {
      flowLphFull = cachedFlow;
    }
#endif
  }

  if (!gotFlow)
  {
    flowLph = 0;
  }

#if WMBUS_DEBUG_SERIAL_FLOW
  if (!isCompact && gotFlow && flowVif == 0x3B)
  {
    Serial.printf("FULLFLOW vif=0x%02X vlen=%d lph=%ld\n", flowVif, flowVlen, flowLph);
  }
#endif
#if WMBUS_DEBUG_RECORD_DUMP
  {
    const bool dumpNow = (!WMBUS_DEBUG_RECORD_DUMP_ON_FAIL) || (flowLph == 0);
    if (dumpNow)
    {
      dumpRawPayload(data, len);
      const size_t dumpStart = findRecordStartCandidate(data, len, recordHint);
      dumpRecords(data, len, dumpStart);
      dumpVifRangeRecords(data, len, dumpStart, 0x30, 0x37);
      dumpVifRangeRecords(data, len, dumpStart, 0x3B, 0x3B);
    }
  }
#endif

#if WMBUS_DEBUG_SERIAL_FLOW
  Serial.printf("ts_ms=%lu FlowLph=%ld\n", (unsigned long)millis(), flowLph);
#endif

  // JSON (no temperatures)
  char json[300];
  snprintf(
    json,
    sizeof(json),
    "{\"MeterId\":\"%s\",\"MeterIdConfig\":\"%s\",\"CurrentValue\":%u.%03u,\"MonthStartValue\":%u.%03u,\"FlowLph\":%ld,\"FlowLphFull\":%ld}",
    meterIdStr,
    meterIdStr,
    (unsigned)(tt / 1000U), (unsigned)(tt % 1000U),
    (unsigned)(tg / 1000U), (unsigned)(tg % 1000U),
    flowLph,
    flowLphFull
  );

  if (allowPublish)
  {
    mqttMyDataJson(json);
  }
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


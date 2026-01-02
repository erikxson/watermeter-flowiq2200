/*
 Copyright (C) 2020 chester4444@wolke7.net
 GPLv3

 Modifications:
Modified by erikxson, 2026:
- FlowIQ 2200 support (volume + month start + flow in l/h)
- MQTT Home Assistant discovery support
- Robust MQTT availability/heartbeat topics
- Removed unused temperature fields
*/

#include <Arduino.h>
#include <SPI.h>
#include "WaterMeter.h"
#include "hwconfig.h"

#if defined(ESP32)
  #define ISR_ATTR IRAM_ATTR
#else
  #define ISR_ATTR ICACHE_RAM_ATTR
#endif

WaterMeter::WaterMeter() {}

// ChipSelect assert
inline void WaterMeter::selectCC1101(void)
{
  digitalWrite(CC1101_CSN, LOW);
}

// ChipSelect deassert
inline void WaterMeter::deselectCC1101(void)
{
  digitalWrite(CC1101_CSN, HIGH);
}

// wait for MISO pulling down
inline void WaterMeter::waitMiso(void)
{
  while (digitalRead(CC1101_MISO) == HIGH) { /* wait */ }
}

// write a single register of CC1101
void WaterMeter::writeReg(uint8_t regAddr, uint8_t value)
{
  selectCC1101();
  waitMiso();
  SPI.transfer(regAddr);
  SPI.transfer(value);
  deselectCC1101();
}

// send a strobe command to CC1101
void WaterMeter::cmdStrobe(uint8_t cmd)
{
  selectCC1101();
  delayMicroseconds(5);
  waitMiso();
  SPI.transfer(cmd);
  delayMicroseconds(5);
  deselectCC1101();
}

// read CC1101 register (status or configuration)
uint8_t WaterMeter::readReg(uint8_t regAddr, uint8_t regType)
{
  uint8_t addr = regAddr | regType;
  selectCC1101();
  waitMiso();
  SPI.transfer(addr);
  uint8_t val = SPI.transfer(0x00);
  deselectCC1101();
  return val;
}

void WaterMeter::readBurstReg(uint8_t *buffer, uint8_t regAddr, uint8_t len)
{
  uint8_t addr = regAddr | READ_BURST;

  selectCC1101();
  delayMicroseconds(5);
  waitMiso();
  SPI.transfer(addr);

  for (uint8_t i = 0; i < len; i++)
  {
    buffer[i] = SPI.transfer(0x00);
  }

  delayMicroseconds(2);
  deselectCC1101();
}

// power on reset
void WaterMeter::reset(void)
{
  deselectCC1101();
  delayMicroseconds(3);

  pinMode(CC1101_MOSI, OUTPUT);
  pinMode(CC1101_SCK, OUTPUT);

  digitalWrite(CC1101_MOSI, LOW);
  digitalWrite(CC1101_SCK, HIGH); // CC1101 datasheet 11.3

  selectCC1101();
  delayMicroseconds(3);
  deselectCC1101();
  delayMicroseconds(45);

  selectCC1101();
  waitMiso();
  SPI.transfer(CC1101_SRES);
  waitMiso();
  deselectCC1101();
}

// set IDLE state, flush FIFO and (re)start receiver
void WaterMeter::startReceiver(void)
{
  cmdStrobe(CC1101_SIDLE);
  while (readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) != MARCSTATE_IDLE)
  {
    delay(1);
  }

  cmdStrobe(CC1101_SFRX); // flush RX FIFO

  cmdStrobe(CC1101_SRX);
  while (readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) != MARCSTATE_RX)
  {
    delay(1);
  }
}

// initialize all the CC1101 registers
void WaterMeter::initializeRegisters(void)
{
  writeReg(CC1101_IOCFG2,   CC1101_DEFVAL_IOCFG2);
  writeReg(CC1101_IOCFG0,   CC1101_DEFVAL_IOCFG0);
  writeReg(CC1101_FIFOTHR,  CC1101_DEFVAL_FIFOTHR);
  writeReg(CC1101_PKTLEN,   CC1101_DEFVAL_PKTLEN);
  writeReg(CC1101_PKTCTRL1, CC1101_DEFVAL_PKTCTRL1);
  writeReg(CC1101_PKTCTRL0, CC1101_DEFVAL_PKTCTRL0);
  writeReg(CC1101_SYNC1,    CC1101_DEFVAL_SYNC1);
  writeReg(CC1101_SYNC0,    CC1101_DEFVAL_SYNC0);
  writeReg(CC1101_ADDR,     CC1101_DEFVAL_ADDR);
  writeReg(CC1101_CHANNR,   CC1101_DEFVAL_CHANNR);
  writeReg(CC1101_FSCTRL1,  CC1101_DEFVAL_FSCTRL1);
  writeReg(CC1101_FSCTRL0,  CC1101_DEFVAL_FSCTRL0);
  writeReg(CC1101_FREQ2,    CC1101_DEFVAL_FREQ2);
  writeReg(CC1101_FREQ1,    CC1101_DEFVAL_FREQ1);
  writeReg(CC1101_FREQ0,    CC1101_DEFVAL_FREQ0);
  writeReg(CC1101_MDMCFG4,  CC1101_DEFVAL_MDMCFG4);
  writeReg(CC1101_MDMCFG3,  CC1101_DEFVAL_MDMCFG3);
  writeReg(CC1101_MDMCFG2,  CC1101_DEFVAL_MDMCFG2);
  writeReg(CC1101_MDMCFG1,  CC1101_DEFVAL_MDMCFG1);
  writeReg(CC1101_MDMCFG0,  CC1101_DEFVAL_MDMCFG0);
  writeReg(CC1101_DEVIATN,  CC1101_DEFVAL_DEVIATN);
  writeReg(CC1101_MCSM1,    CC1101_DEFVAL_MCSM1);
  writeReg(CC1101_MCSM0,    CC1101_DEFVAL_MCSM0);
  writeReg(CC1101_FOCCFG,   CC1101_DEFVAL_FOCCFG);
  writeReg(CC1101_BSCFG,    CC1101_DEFVAL_BSCFG);
  writeReg(CC1101_AGCCTRL2, CC1101_DEFVAL_AGCCTRL2);
  writeReg(CC1101_AGCCTRL1, CC1101_DEFVAL_AGCCTRL1);
  writeReg(CC1101_AGCCTRL0, CC1101_DEFVAL_AGCCTRL0);
  writeReg(CC1101_FREND1,   CC1101_DEFVAL_FREND1);
  writeReg(CC1101_FREND0,   CC1101_DEFVAL_FREND0);
  writeReg(CC1101_FSCAL3,   CC1101_DEFVAL_FSCAL3);
  writeReg(CC1101_FSCAL2,   CC1101_DEFVAL_FSCAL2);
  writeReg(CC1101_FSCAL1,   CC1101_DEFVAL_FSCAL1);
  writeReg(CC1101_FSCAL0,   CC1101_DEFVAL_FSCAL0);
  writeReg(CC1101_FSTEST,   CC1101_DEFVAL_FSTEST);
  writeReg(CC1101_TEST2,    CC1101_DEFVAL_TEST2);
  writeReg(CC1101_TEST1,    CC1101_DEFVAL_TEST1);
  writeReg(CC1101_TEST0,    CC1101_DEFVAL_TEST0);
}

static volatile bool packetAvailable = false;

void ISR_ATTR GD0_ISR(void)
{
  packetAvailable = true;
}

bool WaterMeter::isFrameAvailable(void)
{
  if (packetAvailable)
  {
    detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
    packetAvailable = false;

    WMBusFrame frame;
    receive(&frame);

    attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), GD0_ISR, FALLING);
    return frame.isValid;
  }
  return false;
}

void WaterMeter::begin()
{
  pinMode(CC1101_CSN, OUTPUT);
  deselectCC1101();

#if defined(ESP32)
  SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);
#else
  SPI.begin();
#endif

  pinMode(CC1101_GDO0, INPUT);

  reset();
  initializeRegisters();

  cmdStrobe(CC1101_SCAL);
  delay(1);

  attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), GD0_ISR, FALLING);
  startReceiver();
}

uint8_t WaterMeter::readByteFromFifo(void)
{
  return readReg(CC1101_RXFIFO, CC1101_CONFIG_REGISTER);
}

void WaterMeter::receive(WMBusFrame *frame)
{
  uint8_t p1 = readByteFromFifo();
  uint8_t p2 = readByteFromFifo();
  uint8_t payloadLength = readByteFromFifo();

  if ((payloadLength < WMBusFrame::MAX_LENGTH) && (p1 == 0x54) && (p2 == 0x3D))
  {
    frame->length = payloadLength;

    for (int i = 0; i < payloadLength; i++)
    {
      frame->payload[i] = readByteFromFifo();
    }

    frame->decode();
  }

  startReceiver();
}

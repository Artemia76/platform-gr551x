/*
 * I2C pour GR551x — version provisoire : aucune transaction n'est émise, toute adresse répond NACK.
 * Permet de compiler les bibliothèques qui sondent le bus (découverte d'horloges RTC de MeshCore).
 * TODO : pilote app_i2c (capteurs du T1000-A sur SCL=GPIO8 / SDA=GPIO9).
 */
#pragma once

#include "Arduino.h"
#include "api/HardwareI2C.h"

class TwoWire : public arduino::HardwareI2C {
public:
  void begin() override {}
  void begin(uint8_t) override {}
  void end() override {}
  void setClock(uint32_t) override {}
  void beginTransmission(uint8_t) override {}
  uint8_t endTransmission(bool) override { return 2; }   /* 2 = NACK sur l'adresse */
  uint8_t endTransmission(void) override { return 2; }
  size_t requestFrom(uint8_t, size_t, bool) override { return 0; }
  size_t requestFrom(uint8_t, size_t) override { return 0; }
  void onReceive(void (*)(int)) override {}
  void onRequest(void (*)(void)) override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t *, size_t) override { return 0; }
  using Print::write;
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

extern TwoWire Wire;

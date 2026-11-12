/*
 * SPI maître pour GR551x (pilote app_spi du SDK, SPIM). Broches et mux définis par la variante
 * (PIN_SPI_SCK/MOSI/MISO, PIN_SPI_MUX). La sélection d'esclave est pilotée par l'utilisateur
 * (digitalWrite), comme sur les autres cœurs Arduino.
 */
#pragma once

#include "Arduino.h"
#include "api/HardwareSPI.h"

class SPIClass : public arduino::HardwareSPI {
public:
  uint8_t transfer(uint8_t data) override;
  uint16_t transfer16(uint16_t data) override;
  void transfer(void *buf, size_t count) override;
  void transfer(const void *tx, void *rx, size_t count);

  void usingInterrupt(int) override {}
  void notUsingInterrupt(int) override {}
  void beginTransaction(arduino::SPISettings settings) override;
  void endTransaction(void) override {}
  void attachInterrupt() override {}
  void detachInterrupt() override {}

  void begin() override;
  void end() override;

  int lastError() const { return err; }

private:
  void configure(uint32_t clock, arduino::SPIMode mode);
  bool ready = false;
  uint32_t cur_clock = 0;
  arduino::SPIMode cur_mode = arduino::SPI_MODE0;
  int err = 0;
};

extern SPIClass SPI;

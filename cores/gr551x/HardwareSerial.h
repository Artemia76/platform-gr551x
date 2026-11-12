/*
 * Serial : le GR551x du T1000-A n'a pas de port série accessible. Serial est une console de débogage dont
 * les octets écrits sont confiés à gr551x_debug_write() (définition faible : rien) ; la variante ou
 * l'application peut la remplacer (console BLE, journal en RAM…). Aucune réception.
 */
#pragma once

#include "api/HardwareSerial.h"

extern "C" void gr551x_debug_write(const uint8_t *data, size_t len);

class DebugSerial : public arduino::HardwareSerial {
public:
  void begin(unsigned long) override {}
  void begin(unsigned long, uint16_t) override {}
  void end() override {}
  int available(void) override { return 0; }
  int peek(void) override { return -1; }
  int read(void) override { return -1; }
  void flush(void) override {}
  size_t write(uint8_t c) override { gr551x_debug_write(&c, 1); return 1; }
  size_t write(const uint8_t *buf, size_t n) override { gr551x_debug_write(buf, n); return n; }
  using arduino::Print::write;
  operator bool() override { return true; }
};

extern DebugSerial Serial;

#include "Uart.h"

/*
 * Port série matériel (UART0/UART1 du GR551x, pilote app_uart du SDK).
 * Réception par interruption avec délai de fin de caractère (rx_timeout_mode) vers un tampon circulaire ;
 * émission synchrone.
 */
#pragma once

#include "api/HardwareSerial.h"

class GR551xUart : public arduino::HardwareSerial {
public:
  GR551xUart(uint8_t id, uint8_t tx_pin, uint8_t rx_pin, uint8_t mux)
    : _id(id), _tx(tx_pin), _rx(rx_pin), _mux(mux) {}

  void begin(unsigned long baud) override { begin(baud, 0); }
  void begin(unsigned long baud, uint16_t config) override;
  void end() override;
  int available(void) override;
  int peek(void) override;
  int read(void) override;
  void flush(void) override {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override;
  using arduino::Print::write;
  operator bool() override { return _open; }

  uint32_t rxBytes() const { return _rx_total; }   /* octets reçus depuis l'ouverture (diagnostic) */
  void setPins(int tx, int rx) { if (tx >= 0) _tx = (uint8_t)tx; if (rx >= 0) _rx = (uint8_t)rx; }

  /* interne (contexte d'interruption) */
  void onRxChunk(uint16_t n);
  void rearm();

  static const uint16_t RX_RING = 1024;
  static const uint16_t RX_CHUNK = 64;

private:
  uint8_t _id, _tx, _rx, _mux;
  bool _open = false;
  uint8_t _chunk[RX_CHUNK];
  uint8_t _ring[RX_RING];
  volatile uint16_t _head = 0, _tail = 0;
  volatile uint32_t _rx_total = 0;
};

#if defined(PIN_SERIAL1_TX) && defined(PIN_SERIAL1_RX)
extern GR551xUart Serial1;
#endif

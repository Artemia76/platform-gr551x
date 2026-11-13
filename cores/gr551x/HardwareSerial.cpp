#include "Arduino.h"

DebugSerial Serial;

extern "C" __attribute__((weak)) void gr551x_debug_write(const uint8_t *data, size_t len)
{
  (void)data;
  (void)len;
}

/* ------------------------------------------------------------------ réception de Serial */

#define DEBUG_RX_SIZE 256
static uint8_t s_rx[DEBUG_RX_SIZE];
static volatile uint16_t s_rx_r, s_rx_w;
static uint32_t s_last_read;
static bool s_read_once;

static void note_reader(void) { s_last_read = millis(); s_read_once = true; }

extern "C" size_t gr551x_debug_feed(const uint8_t *data, size_t len)
{
  size_t n = 0;
  for (; n < len; n++) {
    uint16_t next = (uint16_t)((s_rx_w + 1) % DEBUG_RX_SIZE);
    if (next == s_rx_r) break;          /* plein : le reste est perdu */
    s_rx[s_rx_w] = data[n];
    s_rx_w = next;
  }
  return n;
}

extern "C" bool gr551x_debug_reader_active(void)
{
  return s_read_once && millis() - s_last_read < 500;
}

int DebugSerial::available(void)
{
  note_reader();
  return (int)((s_rx_w + DEBUG_RX_SIZE - s_rx_r) % DEBUG_RX_SIZE);
}

int DebugSerial::peek(void)
{
  note_reader();
  return s_rx_r == s_rx_w ? -1 : s_rx[s_rx_r];
}

int DebugSerial::read(void)
{
  note_reader();
  if (s_rx_r == s_rx_w) return -1;
  uint8_t c = s_rx[s_rx_r];
  s_rx_r = (uint16_t)((s_rx_r + 1) % DEBUG_RX_SIZE);
  return c;
}

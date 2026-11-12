#include "Arduino.h"

DebugSerial Serial;

extern "C" __attribute__((weak)) void gr551x_debug_write(const uint8_t *data, size_t len)
{
  (void)data;
  (void)len;
}

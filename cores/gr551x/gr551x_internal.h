#pragma once
#include "Arduino.h"
#include "gr551x_sdk.h"

bool gr551x_pin_decode(pin_size_t n, app_io_type_t *type, uint32_t *mask);
uint64_t gr551x_cycles(void);

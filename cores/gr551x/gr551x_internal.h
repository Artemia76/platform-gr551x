#pragma once
#include "Arduino.h"
#include "gr551x_sdk.h"

bool gr551x_pin_decode(pin_size_t n, app_io_type_t *type, uint32_t *mask);
uint64_t gr551x_cycles(void);
extern "C" uint64_t gr551x_ticks32k(void);
extern volatile uint32_t gr551x_wake_events;   /* incrémenté par les interruptions utiles (réveil) */

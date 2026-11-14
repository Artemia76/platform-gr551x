#pragma once
#include "Arduino.h"
#include "gr551x_sdk.h"

bool gr551x_pin_decode(pin_size_t n, app_io_type_t *type, uint32_t *mask);
uint64_t gr551x_cycles(void);
extern "C" uint64_t gr551x_ticks32k(void);
extern volatile uint32_t gr551x_wake_events;   /* incrémenté par les interruptions utiles (réveil) */
void gr551x_tone_poll(void);                  /* tone.cpp : fin des sons à durée fixée */
bool gr551x_tone_busy(void);                  /* tone.cpp : un son joue (pas de veille) */
extern uint8_t gr551x_pwm_held;               /* tone.cpp : broches tenues par analogWrite() */
void gr551x_pwm_pin_release(pin_size_t pin);  /* tone.cpp : rend une broche analogWrite() au GPIO */

/* power_stats.cpp : alimentés par main.cpp (veille) et gr551x_ble.cpp (connexions, annonces) */
void gr551x_pstat_loop(void);
void gr551x_pstat_idle(uint64_t ticks);
void gr551x_pstat_refused(bool awake);
void gr551x_pstat_ble(const ble_evt_t *evt);

/*
 * Statistiques d'énergie : temps passé en veille, en connexion et en annonce BLE (base de temps à 32 kHz du
 * calendrier AON, qui tourne aussi en veille). Les événements BLE arrivent en interruption : les accès
 * se font interruptions masquées.
 */
#include "Arduino.h"
#include "gr551x_internal.h"

static uint64_t s_t0;                       /* début de la période mesurée */
static uint64_t s_idle;
static uint64_t s_conn, s_conn_since;
static uint64_t s_adv, s_adv_since;
static uint8_t  s_conns;                    /* connexions ouvertes */
static bool     s_adv_on;
static uint32_t s_entries, s_ref_event, s_ref_awake, s_loops, s_wake0;

struct Lock {
  uint32_t primask;
  Lock() : primask(__get_PRIMASK()) { __disable_irq(); }
  ~Lock() { __set_PRIMASK(primask); }
};

void gr551x_pstat_loop(void) { s_loops++; }

void gr551x_pstat_idle(uint64_t ticks)
{
  Lock l;
  s_idle += ticks;
  s_entries++;
}

void gr551x_pstat_refused(bool awake)
{
  if (awake) s_ref_awake++; else s_ref_event++;
}

static void adv_set(bool on, uint64_t now)
{
  if (on && !s_adv_on) s_adv_since = now;
  if (!on && s_adv_on) s_adv += now - s_adv_since;
  s_adv_on = on;
}

void gr551x_pstat_ble(const ble_evt_t *evt)
{
  Lock l;
  uint64_t now = gr551x_ticks32k();
  switch (evt->evt_id) {
    case BLE_GAPM_EVT_ADV_START:
      if (evt->evt_status == BLE_SUCCESS) adv_set(true, now);
      break;
    case BLE_GAPM_EVT_ADV_STOP:
      adv_set(false, now);
      break;
    case BLE_GAPC_EVT_CONNECTED:
      if (evt->evt_status != BLE_SUCCESS) break;
      adv_set(false, now);                  /* une connexion arrête l'annonce connectable */
      if (s_conns++ == 0) s_conn_since = now;
      break;
    case BLE_GAPC_EVT_DISCONNECTED:
      if (s_conns && --s_conns == 0) s_conn += now - s_conn_since;
      break;
    default:
      break;
  }
}

extern "C" void gr551x_power_stats_get(gr551x_power_stats_t *s)
{
  Lock l;
  uint64_t now = gr551x_ticks32k();
  s->total_ticks = now - s_t0;
  s->idle_ticks = s_idle;
  s->ble_conn_ticks = s_conn + (s_conns ? now - s_conn_since : 0);
  s->ble_adv_ticks = s_adv + (s_adv_on ? now - s_adv_since : 0);
  s->idle_entries = s_entries;
  s->idle_refused_event = s_ref_event;
  s->idle_refused_awake = s_ref_awake;
  s->wake_events = gr551x_wake_events - s_wake0;
  s->loops = s_loops;
}

extern "C" void gr551x_power_stats_reset(void)
{
  Lock l;
  uint64_t now = gr551x_ticks32k();
  s_t0 = s_conn_since = s_adv_since = now;
  s_idle = s_conn = s_adv = 0;
  s_entries = s_ref_event = s_ref_awake = s_loops = 0;
  s_wake0 = gr551x_wake_events;
}

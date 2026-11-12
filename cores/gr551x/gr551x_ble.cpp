#include "Arduino.h"
#include "gr551x_ble.h"
#include "gr551x_internal.h"

extern "C" {
STACK_HEAP_INIT(heaps_table);
}

static gr551x_ble_handler_t s_handlers[GR551X_BLE_MAX_HANDLERS];
static void (*s_ready_cbs[GR551X_BLE_MAX_HANDLERS])(void);
static volatile int s_ready;
static sdk_err_t s_init_err;

static void ble_evt_handler(const ble_evt_t *evt)
{
  gr551x_wake_events++;
  if (evt->evt_id == BLE_COMMON_EVT_STACK_INIT && !s_ready) {
    s_ready = 1;
    gr551x_boot_step(GR551X_STEP_BLE_READY);
    for (auto cb : s_ready_cbs) if (cb) cb();
  }
  for (auto h : s_handlers) if (h) h(evt);
}

extern "C" int gr551x_ble_add_handler(gr551x_ble_handler_t handler)
{
  for (auto &h : s_handlers) {
    if (h == handler) return 0;
    if (!h) { h = handler; return 0; }
  }
  return -1;
}

extern "C" void gr551x_ble_on_ready(void (*cb)(void))
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  bool now = s_ready;
  if (!now) {
    for (auto &c : s_ready_cbs) if (!c) { c = cb; break; }
  }
  __set_PRIMASK(primask);
  if (now) cb();
}

extern "C" int gr551x_ble_ready(void) { return s_ready; }
extern "C" sdk_err_t gr551x_ble_init_error(void) { return s_init_err; }

extern "C" void gr551x_ble_begin(void)
{
  s_init_err = ble_stack_init(ble_evt_handler, &heaps_table);
}

static volatile int s_stopped;

extern "C" void gr551x_ble_stop(void)
{
  s_stopped = 1;
  ble_gap_adv_stop(0);
  for (uint8_t i = 0; i < CFG_MAX_CONNECTIONS; i++) ble_gap_disconnect(i);
}

extern "C" int gr551x_ble_stopped(void) { return s_stopped; }

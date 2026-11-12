#include "Arduino.h"
#include "gr551x_sdk.h"
#include "gr551x_internal.h"

extern "C" {
#include "app_uart.h"
}

static GR551xUart *s_uart[2];
static app_uart_params_t s_params[2];
static uint8_t s_txbuf[2][256];

static void uart_evt(uint8_t id, app_uart_evt_t *e)
{
  GR551xUart *u = s_uart[id];
  gr551x_wake_events++;
  if (!u) return;
  if (e->type == APP_UART_EVT_RX_DATA) {
    u->onRxChunk(e->data.size);
    u->rearm();
  } else if (e->type == APP_UART_EVT_ERROR) {
    u->rearm();
  }
}
static void uart0_evt(app_uart_evt_t *e) { uart_evt(0, e); }
static void uart1_evt(app_uart_evt_t *e) { uart_evt(1, e); }

void GR551xUart::begin(unsigned long baud, uint16_t config)
{
  (void)config;   /* 8N1 */
  if (_open) end();
  app_uart_params_t &p = s_params[_id];
  memset(&p, 0, sizeof(p));
  p.id = (app_uart_id_t)_id;
  p.pin_cfg.tx.type = APP_IO_TYPE_NORMAL;
  p.pin_cfg.tx.mux  = (app_io_mux_t)_mux;
  p.pin_cfg.tx.pin  = 1u << _tx;
  p.pin_cfg.tx.pull = APP_IO_PULLUP;
  p.pin_cfg.rx.type = APP_IO_TYPE_NORMAL;
  p.pin_cfg.rx.mux  = (app_io_mux_t)_mux;
  p.pin_cfg.rx.pin  = 1u << _rx;
  p.pin_cfg.rx.pull = APP_IO_PULLUP;
  p.init.baud_rate       = baud;
  p.init.data_bits       = UART_DATABITS_8;
  p.init.stop_bits       = UART_STOPBITS_1;
  p.init.parity          = UART_PARITY_NONE;
  p.init.hw_flow_ctrl    = UART_HWCONTROL_NONE;
  p.init.rx_timeout_mode = UART_RECEIVER_TIMEOUT_ENABLE;

  app_uart_tx_buf_t tx = { s_txbuf[_id], sizeof(s_txbuf[_id]) };
  s_uart[_id] = this;
  _head = _tail = 0;
  if (app_uart_init(&p, _id ? uart1_evt : uart0_evt, &tx) != APP_DRV_SUCCESS) {
    s_uart[_id] = nullptr;
    return;
  }
  _open = true;
  rearm();
}

void GR551xUart::end()
{
  if (!_open) return;
  _open = false;
  app_uart_deinit((app_uart_id_t)_id);
  s_uart[_id] = nullptr;
}

void GR551xUart::rearm()
{
  if (_open) app_uart_receive_async((app_uart_id_t)_id, _chunk, RX_CHUNK);
}

void GR551xUart::onRxChunk(uint16_t n)
{
  _rx_total += n;
  for (uint16_t i = 0; i < n && i < RX_CHUNK; i++) {
    uint16_t next = (uint16_t)((_head + 1) % RX_RING);
    if (next == _tail) break;                    /* plein : on perd la fin */
    _ring[_head] = _chunk[i];
    _head = next;
  }
}

int GR551xUart::available(void)
{
  return (int)((_head + RX_RING - _tail) % RX_RING);
}

int GR551xUart::peek(void)
{
  return _head == _tail ? -1 : _ring[_tail];
}

int GR551xUart::read(void)
{
  if (_head == _tail) return -1;
  uint8_t c = _ring[_tail];
  _tail = (uint16_t)((_tail + 1) % RX_RING);
  return c;
}

size_t GR551xUart::write(const uint8_t *buf, size_t n)
{
  if (!_open || !n) return 0;
  return app_uart_transmit_sync((app_uart_id_t)_id, (uint8_t *)buf, (uint16_t)n, 1000) == APP_DRV_SUCCESS ? n : 0;
}

#if defined(PIN_SERIAL1_TX) && defined(PIN_SERIAL1_RX)
GR551xUart Serial1(1, PIN_SERIAL1_TX, PIN_SERIAL1_RX, PIN_SERIAL1_MUX);
#endif

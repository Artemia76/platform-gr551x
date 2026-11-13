#include "BleConsole.h"
#include "gr551x_ble.h"
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "gus.h"
}

BleConsoleClass BleConsole;

static const char *s_name = "GR551x";
static const char *s_version = "";
static bool s_advertise;
static volatile uint16_t s_err;

static void note_err(uint16_t code) { if (code && !s_err) s_err = code; }
/* Note l'erreur e (non nulle) avec l'étape « where » dans l'octet de poids fort. */
static void chk(uint16_t where, uint32_t e) { if (e) note_err((uint16_t)(where | (e & 0xFF))); }

/* ---------------------------------------------------------------- émission */

#define TX_BUF_SIZE 2048
static uint8_t  s_tx_buf[TX_BUF_SIZE];
static volatile uint32_t s_tx_head, s_tx_tail;
static volatile uint16_t s_tx_inflight;
static volatile int s_conn_idx = -1;
static volatile int s_tx_open;
static volatile uint16_t s_mtu = 23;

/* Appelé depuis loop() et depuis les événements BLE : le paquet est réservé en section critique. */
static void tx_kick(void)
{
  static uint8_t chunk[244];
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (s_conn_idx < 0 || !s_tx_open || s_tx_inflight || s_tx_head == s_tx_tail) {
    __set_PRIMASK(primask);
    return;
  }
  uint16_t max = (uint16_t)(s_mtu - 3);
  if (max > sizeof(chunk)) max = sizeof(chunk);
  uint16_t n = 0;
  uint32_t t = s_tx_tail;
  while (t != s_tx_head && n < max) {
    chunk[n++] = s_tx_buf[t];
    t = (t + 1) % TX_BUF_SIZE;
  }
  s_tx_inflight = n;
  __set_PRIMASK(primask);
  if (gus_tx_data_send((uint8_t)s_conn_idx, chunk, n) != SDK_SUCCESS) s_tx_inflight = 0;
}

size_t BleConsoleClass::write(const uint8_t *buf, size_t n)
{
  if (s_conn_idx < 0 || !s_tx_open) return n;            /* personne n'écoute : on jette */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  for (size_t i = 0; i < n; i++) {
    uint32_t next = (s_tx_head + 1) % TX_BUF_SIZE;
    if (next == s_tx_tail) break;                        /* plein : on tronque */
    s_tx_buf[s_tx_head] = buf[i];
    s_tx_head = next;
  }
  __set_PRIMASK(primask);
  tx_kick();
  return n;
}

/* Serial → console */
extern "C" void gr551x_debug_write(const uint8_t *data, size_t len) { BleConsole.write(data, len); }

bool BleConsoleClass::connected() const { return s_conn_idx >= 0 && s_tx_open; }
uint16_t BleConsoleClass::lastError() const { return s_err; }

/* ---------------------------------------------------------------- réception */

#define RX_RING_SIZE 512
#define RX_LINE_MAX  128
static uint8_t s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_w, s_rx_r;
static char s_rx_line[RX_LINE_MAX];
static uint32_t s_rx_len;

static void rx_bytes(const uint8_t *p, uint16_t len)
{
  for (uint16_t i = 0; i < len; i++) {
    uint16_t next = (uint16_t)((s_rx_w + 1) % RX_RING_SIZE);
    if (next == s_rx_r) break;
    s_rx_ring[s_rx_w] = p[i];
    s_rx_w = next;
  }
}

static void exec_line(char *line, BleConsoleClass::CommandHandler handler)
{
  char raw[RX_LINE_MAX + 2];
  strncpy(raw, line, RX_LINE_MAX);
  raw[RX_LINE_MAX] = 0;
  char *argv[10] = {0};
  int argc = 0;
  for (char *tok = strtok(line, " "); tok && argc < 10; tok = strtok(NULL, " ")) argv[argc++] = tok;
  if (!argc) return;

  if (handler && handler(argc, argv)) return;
  if (!strcmp(argv[0], "help")) {
    BleConsole.print("commandes : help, info, reboot, dfu (+ commandes de l'application ; les autres lignes vont a Serial quand l'application le lit)\r\n");
  } else if (!strcmp(argv[0], "info")) {
    BleConsole.printf("%s %s  uptime=%lus  ble_err=0x%04x\r\n", s_name, s_version, millis() / 1000, s_err);
  } else if (!strcmp(argv[0], "reboot")) {
    BleConsole.print("redemarrage...\r\n");
    delay(200);
    gr551x_reboot();
  } else if (!strcmp(argv[0], "dfu")) {
    BleConsole.print("passage en DFU...\r\n");
    delay(200);
    gr551x_enter_bootloader();
  } else if (gr551x_debug_reader_active()) {
    /* l'application lit Serial (ex. CLI de secours MeshCore) : la ligne lui revient, fin de ligne « \r » */
    size_t n = strlen(raw);
    raw[n++] = '\r';
    gr551x_debug_feed((const uint8_t *)raw, n);
  } else {
    BleConsole.print("commande inconnue (help)\r\n");
  }
}

void BleConsoleClass::poll()
{
  while (s_rx_r != s_rx_w) {
    char c = (char)s_rx_ring[s_rx_r];
    s_rx_r = (uint16_t)((s_rx_r + 1) % RX_RING_SIZE);
    if (c == '\r' || c == '\n') {
      if (s_rx_len) {
        s_rx_line[s_rx_len] = 0;
        s_rx_len = 0;
        exec_line(s_rx_line, handler);
      }
    } else if (s_rx_len < RX_LINE_MAX - 1) {
      s_rx_line[s_rx_len++] = c;
    }
  }
  tx_kick();
}

/* ---------------------------------------------------------------- BLE */

static ble_gap_adv_param_t s_adv_param;
static ble_gap_adv_time_param_t s_adv_time;

static void gus_evt(gus_evt_t *e)
{
  switch (e->evt_type) {
    case GUS_EVT_TX_PORT_OPENED:
      s_tx_open = 1;
      BleConsole.printf("%s %s - tapez help\r\n", s_name, s_version);
      break;
    case GUS_EVT_TX_PORT_CLOSED:
      s_tx_open = 0;
      break;
    case GUS_EVT_RX_DATA_RECEIVED:
      rx_bytes(e->p_data, e->length);
      break;
    case GUS_EVT_TX_DATA_SENT:
      s_tx_tail = (s_tx_tail + s_tx_inflight) % TX_BUF_SIZE;
      s_tx_inflight = 0;
      tx_kick();
      break;
    default:
      break;
  }
}

static void adv_start(void)
{
  if (s_advertise) chk(0x500, ble_gap_adv_start(0, &s_adv_time));
}

static void on_ready(void)
{
  gus_init_t init = { .evt_handler = gus_evt };
  chk(0x100, gus_service_init(&init));
  if (!s_advertise) return;

  static const uint8_t adv_data[] = { 0x11, BLE_GAP_AD_TYPE_COMPLETE_LIST_128_BIT_UUID, GUS_SERVICE_UUID };
  static uint8_t scan_rsp[31];
  size_t n = strlen(s_name);
  if (n > sizeof(scan_rsp) - 2) n = sizeof(scan_rsp) - 2;
  scan_rsp[0] = (uint8_t)(n + 1);
  scan_rsp[1] = BLE_GAP_AD_TYPE_COMPLETE_NAME;
  memcpy(&scan_rsp[2], s_name, n);

  ble_gap_device_name_set(BLE_GAP_WRITE_PERM_DISABLE, (uint8_t *)s_name, (uint16_t)strlen(s_name));
  s_adv_param.adv_intv_min = 160;   /* 100 ms */
  s_adv_param.adv_intv_max = 320;   /* 200 ms */
  s_adv_param.adv_mode     = BLE_GAP_ADV_TYPE_ADV_IND;
  s_adv_param.chnl_map     = BLE_GAP_ADV_CHANNEL_37_38_39;
  s_adv_param.disc_mode    = BLE_GAP_DISC_MODE_GEN_DISCOVERABLE;
  s_adv_param.filter_pol   = BLE_GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
  chk(0x200, ble_gap_adv_param_set(0, BLE_GAP_OWN_ADDR_STATIC, &s_adv_param));
  chk(0x300, ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_DATA, adv_data, sizeof(adv_data)));
  chk(0x400, ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_SCAN_RSP, scan_rsp, (uint16_t)(n + 2)));
  ble_gap_l2cap_params_set(247, 247, 1);
  ble_gap_data_length_set(251, 2120);
  ble_gap_pref_phy_set(BLE_GAP_PHY_ANY, BLE_GAP_PHY_ANY);
  s_adv_time.duration = 0;
  s_adv_time.max_adv_evt = 0;
  adv_start();
}

static void ble_evt(const ble_evt_t *e)
{
  switch (e->evt_id) {
    case BLE_GAPM_EVT_ADV_START:
      if (e->evt_status) chk(0x600, e->evt_status);
      break;
    case BLE_GAPC_EVT_CONNECTED:
      s_conn_idx = e->evt.gapc_evt.index;
      s_mtu = 23;
      s_tx_inflight = 0;
      break;
    case BLE_GAPC_EVT_DISCONNECTED:
      s_conn_idx = -1;
      s_tx_open = 0;
      s_tx_inflight = 0;
      adv_start();
      break;
    case BLE_GAPC_EVT_CONN_PARAM_UPDATE_REQ:
      ble_gap_conn_param_update_reply(e->evt.gapc_evt.index, true);
      break;
    case BLE_GATT_COMMON_EVT_MTU_EXCHANGE:
      s_mtu = e->evt.gatt_common_evt.params.mtu_exchange.mtu;
      break;
    default:
      break;
  }
}

void BleConsoleClass::begin(const char *deviceName, const char *version, bool advertise)
{
  s_name = deviceName;
  s_version = version;
  s_advertise = advertise;
  gr551x_ble_add_handler(ble_evt);
  gr551x_ble_on_ready(on_ready);
}

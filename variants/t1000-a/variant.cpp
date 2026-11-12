/*
 * Initialisation et services de la carte SenseCAP T1000-A.
 *
 * Le T1000-A n'a ni port série ni SWD accessible : le seul moyen de le reprogrammer est le DFU BLE du
 * bootloader Seeed. Cette variante garantit qu'on peut toujours y retourner (voir safety.h) :
 *   - plantages répétés au démarrage → DFU automatique ;
 *   - bouton maintenu 6 s → DFU ;
 *   - chien de garde AON de 8 s (rafraîchi par loop() et les attentes du cœur).
 */
#include "Arduino.h"
#include "gr551x_ble.h"

extern "C" {
#include "t1000a_board.h"
#include "safety.h"
#include "diag.h"
}

#define BTN_DFU_MS 6000u

extern "C" void board_led_set(int on)
{
  app_io_write_pin(BOARD_LED_TYPE, BOARD_LED_PIN, on ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

extern "C" int board_button_pressed(void)
{
  return app_io_read_pin(BOARD_BTN_TYPE, BOARD_BTN_PIN) == APP_IO_PIN_SET;
}

static void board_init(void)
{
  app_io_init_t io;
  io.pin  = BOARD_LED_PIN;
  io.mode = APP_IO_MODE_OUTPUT;
  io.pull = APP_IO_NOPULL;
  io.mux  = BOARD_GPIO_MUX;
  app_io_init(BOARD_LED_TYPE, &io);
  board_led_set(0);

  io.pin  = BOARD_BTN_PIN;
  io.mode = APP_IO_MODE_INPUT;
  io.pull = APP_IO_NOPULL;        /* comme le firmware d'origine (pull-down externe) */
  io.mux  = BOARD_GPIO_MUX;
  app_io_init(BOARD_BTN_TYPE, &io);
}

static void ble_evt(const ble_evt_t *evt)
{
  if (evt->evt_id == BLE_GAPC_EVT_CONNECTED) safety_ble_connected();
}

/* Fil d'Ariane du démarrage dans le secteur de diagnostic (lisible après coup, ex. par hello « diag »).
 * Désactivé par défaut : chaque étape écrit en Flash. */
extern "C" void gr551x_boot_step(uint8_t id)
{
#ifdef T1000A_BOOT_TRACE
  diag_step(id);
#else
  (void)id;
#endif
}

extern "C" void initVariantEarly(void)
{
  board_init();
#ifdef T1000A_BOOT_TRACE
  diag_boot();
  diag_step(diag_last_reset_wdt() ? 0x4F : 0x40);    /* début de main() ; 0x4F : après chien de garde */
#endif
  safety_boot();                  /* peut basculer en DFU après plusieurs démarrages ratés */
  diag_boot();
  gr551x_ble_add_handler(ble_evt);
}

extern "C" int variantPinInverted(pin_size_t pin)
{
  return pin == PIN_LORA_IRQ;     /* IRQ du LR1110 : active à l'état bas, collecteur ouvert */
}

extern "C" void variantPoll(void)
{
  static uint32_t btn_since;
  static bool btn_down;
  uint32_t now = millis();

  safety_poll(now);
  if (board_button_pressed()) {
    if (!btn_down) { btn_down = true; btn_since = now; }
    else if (now - btn_since >= BTN_DFU_MS) safety_enter_dfu();
  } else {
    btn_down = false;
  }
}

extern "C" void gr551x_enter_bootloader(void) { safety_enter_dfu(); }
extern "C" void gr551x_reboot(void) { safety_reboot(); }

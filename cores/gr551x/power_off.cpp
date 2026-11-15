/*
 * Arrêt véritable : veille ultra-profonde du GR551x (pwr_mgmt_ultra_sleep), réveillée par une broche AON et/ou
 * par le comparateur MSIO. Ni horloge ni RAM ne sont conservées : le réveil repasse par le bootloader puis par un
 * démarrage complet de l'application, qui retrouve l'état laissé par la variante (gr551x_power_off_state()).
 */
#include "gr551x_internal.h"
#include "gr551x_ble.h"
#include "app_comp.h"

extern "C" __attribute__((weak)) void variantPowerOff(const gr551x_wake_cfg_t *cfg) { (void)cfg; }
extern "C" __attribute__((weak)) bool gr551x_power_off_state(gr551x_off_info_t *info) { (void)info; return false; }
extern "C" __attribute__((weak)) void gr551x_power_off_clear(void) {}

static app_comp_params_t s_comp;     /* le pilote garde un pointeur dessus */

static uint32_t comp_input(int8_t msio)
{
  switch (msio) {
    case 1: return COMP_INPUT_SRC_IO1;
    case 2: return COMP_INPUT_SRC_IO2;
    case 3: return COMP_INPUT_SRC_IO3;
    case 4: return COMP_INPUT_SRC_IO4;
    default: return COMP_INPUT_SRC_IO0;
  }
}

extern "C" void gr551x_power_off(const gr551x_wake_cfg_t *cfg)
{
  gr551x_ble_stop();
  variantPowerOff(cfg);

  uint32_t sources = 0;
  if (cfg->wake_aon_pin >= 0 && cfg->wake_aon_pin <= 5) {
    hal_pwr_config_ext_wakeup(1u << cfg->wake_aon_pin, cfg->wake_aon_high ? PWR_EXTWKUP_TYPE_HIGH : PWR_EXTWKUP_TYPE_LOW);
    sources |= PWR_WKUP_COND_EXT;
  }
  if (cfg->wake_msio_pin >= 0 && cfg->wake_msio_pin <= 4) {
    memset(&s_comp, 0, sizeof(s_comp));
    s_comp.pin_cfg.input.type = APP_IO_TYPE_MSIO;
    s_comp.pin_cfg.input.mux = APP_IO_MUX_7;
    s_comp.pin_cfg.input.pin = 1u << cfg->wake_msio_pin;
    s_comp.init.input_source = comp_input(cfg->wake_msio_pin);
    s_comp.init.ref_source = COMP_REF_SRC_VREF;
    uint32_t level = cfg->wake_msio_mv / 30;           /* Vref = 30 mV × niveau */
    s_comp.init.ref_value = level > 63 ? 63 : level;
    if (app_comp_init(&s_comp, NULL) == APP_DRV_SUCCESS && app_comp_start() == APP_DRV_SUCCESS)
      sources |= PWR_WKUP_COND_MSIO_COMP;              /* app_comp_start() déclare aussi cette source */
  }
  pwr_mgmt_wakeup_source_setup(sources);
  pwr_mgmt_ultra_sleep(0);                             /* pas de minuterie : seul un réveil externe */

  NVIC_SystemReset();                                  /* ne devrait pas revenir */
  while (1) {}
}

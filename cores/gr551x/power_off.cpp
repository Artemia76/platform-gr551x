/*
 * Arrêt véritable : veille ultra-profonde du GR551x (pwr_mgmt_ultra_sleep), réveillée par des broches AON.
 * Ni horloge ni RAM ne sont conservées : le réveil repasse par le bootloader puis par un démarrage complet de
 * l'application, qui retrouve l'état laissé par la variante (gr551x_power_off_state()).
 * Constaté sur le T1000-A : le comparateur MSIO ne réveille pas depuis ce mode (seules les broches AON le font).
 */
#include "gr551x_internal.h"
#include "gr551x_ble.h"

extern "C" __attribute__((weak)) void variantPowerOff(const gr551x_wake_cfg_t *cfg) { (void)cfg; }
extern "C" __attribute__((weak)) bool gr551x_power_off_state(gr551x_off_info_t *info) { (void)info; return false; }
extern "C" __attribute__((weak)) void gr551x_power_off_clear(void) {}

extern "C" void gr551x_power_off(const gr551x_wake_cfg_t *cfg)
{
  gr551x_ble_stop();
  variantPowerOff(cfg);

  uint8_t high = cfg->wake_aon_high & 0x3F, low = cfg->wake_aon_low & 0x3F & ~cfg->wake_aon_high;
  if (high) hal_pwr_config_ext_wakeup(high, PWR_EXTWKUP_TYPE_HIGH);   /* bit n = PWR_EXTWKUP_PINn = AON n */
  if (low)  hal_pwr_config_ext_wakeup(low, PWR_EXTWKUP_TYPE_LOW);
  pwr_mgmt_wakeup_source_setup(PWR_WKUP_COND_EXT);
  pwr_mgmt_ultra_sleep(0);                             /* pas de minuterie : seul un réveil externe */

  NVIC_SystemReset();                                  /* ne devrait pas revenir */
  while (1) {}
}

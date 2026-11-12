/*
 * Mesure analogique (ADC « sense » du GR551x) sur les broches MSIO.
 *
 * Réglages relevés dans le firmware Seeed du T1000-A (fonctions 0x01036188 / 0x010349CC) :
 * entrée référencée à la masse, référence interne tamponnée 1,28 V, horloge 1 MHz ; N échantillons
 * convertis en volts par app_adc_voltage_intern (étalonnage d'usine), arrondis au mV et moyennés.
 */
#include "Arduino.h"
#include "gr551x_sdk.h"

extern "C" {
#include "app_adc.h"
}

#define ADC_MAX_SAMPLES 32

extern "C" int analogReadMilliVolts(pin_size_t pin)
{
  if (pin < 0x30 || pin > 0x34) return -1;          /* MSIO0..4 uniquement */
  uint32_t n = pin - 0x30;

  static app_adc_params_t p;
  memset(&p, 0, sizeof(p));
  p.pin_cfg.channel_p.type = APP_IO_TYPE_MSIO;
  p.pin_cfg.channel_p.mux  = APP_IO_MUX_7;
  p.pin_cfg.channel_p.pin  = 1u << n;
  p.pin_cfg.channel_n = p.pin_cfg.channel_p;
  p.init.channel_p  = LL_ADC_INPUT_SRC_IO0 + n;
  p.init.channel_n  = LL_ADC_INPUT_SRC_IO0 + n;
  p.init.input_mode = LL_ADC_INPUT_SINGLE;
  p.init.ref_source = LL_ADC_REF_SRC_BUF_INT;
  p.init.ref_value  = LL_ADC_REF_VALUE_1P2;
  p.init.clock      = LL_ADC_CLK_1;

  if (app_adc_init(&p, NULL) != APP_DRV_SUCCESS) return -1;

  static uint16_t raw[ADC_MAX_SAMPLES];
  static double volts[ADC_MAX_SAMPLES];
  const uint32_t count = 20;
  int result = -1;
  if (app_adc_conversion_sync(raw, count, 1000) == APP_DRV_SUCCESS &&
      app_adc_voltage_intern(raw, volts, count) == APP_DRV_SUCCESS) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < count; i++) sum += (uint32_t)(volts[i] * 1000.0 + 0.5);
    result = (int)(sum / count);
  }
  app_adc_deinit();
  return result;
}

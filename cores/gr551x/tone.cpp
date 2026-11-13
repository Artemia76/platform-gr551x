/*
 * tone() / noTone() sur les modules PWM du GR551x (PWM0, PWM1 ; voies A, B, C).
 *
 * Un son à la fois par module, rapport cyclique 50 %. Seules les broches qui ont une fonction PWM sont prises en
 * charge (table ci-dessous, d'après gr55xx_hal_*_ex.h) ; les autres sont ignorées. Pendant un son, la veille
 * légère est suspendue : le CPU peut y dormir jusqu'à 1 s, ce qui casserait le rythme des mélodies (RTTTL).
 */
#include "gr551x_internal.h"
#include "app_pwm.h"

struct PwmPin {
  uint8_t pin;          /* numérotation Arduino du core */
  uint8_t id;           /* APP_PWM_ID_0 / APP_PWM_ID_1 */
  uint8_t channel;      /* 0 = A, 1 = B, 2 = C */
  uint8_t mux;          /* APP_IO_MUX_n */
};

static const PwmPin k_pwm_pins[] = {
  {2, 0, 0, 5},  {3, 0, 1, 5},  {4, 0, 2, 5},
  {7, 1, 0, 5},  {8, 1, 1, 5},  {9, 1, 2, 5},  {10, 1, 1, 5}, {11, 1, 2, 5},
  {26, 0, 2, 3}, {30, 0, 1, 3}, {31, 0, 0, 3},
  {0x22, 0, 2, 5}, {0x23, 1, 0, 5}, {0x24, 1, 1, 5}, {0x25, 1, 2, 5},
  {0x30, 0, 0, 0}, {0x31, 0, 1, 0}, {0x32, 0, 2, 0}, {0x33, 1, 0, 0}, {0x34, 1, 1, 0},
};

struct ToneState {
  app_pwm_params_t params;   /* doit rester en mémoire : le pilote garde un pointeur dessus */
  int16_t pin;               /* broche en cours, -1 si le module est libre */
  uint32_t stop_at;          /* millis() de fin, 0 = sans durée */
};
static ToneState s_tone[APP_PWM_ID_MAX] = {{{}, -1, 0}, {{}, -1, 0}};
static uint32_t s_last_use;    /* millis() du dernier tone()/noTone() : les silences d'une mélodie comptent */

static const PwmPin *find_pin(uint8_t pin)
{
  for (const PwmPin &p : k_pwm_pins)
    if (p.pin == pin) return &p;
  return nullptr;
}

static void release(uint8_t id)
{
  ToneState &s = s_tone[id];
  if (s.pin < 0) return;
  app_pwm_stop((app_pwm_id_t)id);
  app_pwm_deinit((app_pwm_id_t)id);
  pin_size_t pin = (pin_size_t)s.pin;
  s.pin = -1;
  s.stop_at = 0;
  pinMode(pin, OUTPUT);          /* comme sur AVR : la broche reste en sortie, à l'état bas */
  digitalWrite(pin, LOW);
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
  const PwmPin *p = find_pin(pin);
  if (!p) return;
  s_last_use = millis();
  if (frequency == 0) { noTone(pin); return; }

  ToneState &s = s_tone[p->id];
  if (s.pin != pin) {
    release(p->id);

    app_pwm_params_t &prm = s.params;
    memset(&prm, 0, sizeof(prm));
    app_io_type_t type;
    uint32_t mask;
    gr551x_pin_decode(pin, &type, &mask);
    app_pwm_pin_t io = {type, (app_io_mux_t)p->mux, mask, APP_IO_NOPULL, APP_PWM_PIN_ENABLE};
    app_pwm_pin_t *chan[3] = {&prm.pin_cfg.channel_a, &prm.pin_cfg.channel_b, &prm.pin_cfg.channel_c};
    pwm_channel_init_t *cfg[3] = {&prm.init.channel_a, &prm.init.channel_b, &prm.init.channel_c};
    *chan[p->channel] = io;
    cfg[p->channel]->duty = 50;
    cfg[p->channel]->drive_polarity = PWM_DRIVEPOLARITY_POSITIVE;

    prm.id = (app_pwm_id_t)p->id;
    prm.active_channel = (app_pwm_active_channel_t)(1u << p->channel);
    prm.init.mode = PWM_MODE_FLICKER;
    prm.init.align = PWM_ALIGNED_EDGE;
    prm.init.freq = frequency;
    if (app_pwm_init(&prm) != APP_DRV_SUCCESS) return;
    if (app_pwm_start(prm.id) != APP_DRV_SUCCESS) { app_pwm_deinit(prm.id); return; }
    s.pin = pin;
  } else {
    app_pwm_update_freq((app_pwm_id_t)p->id, frequency);
  }

  s.stop_at = duration ? (uint32_t)millis() + duration : 0;
  if (duration && s.stop_at == 0) s.stop_at = 1;
}

void noTone(uint8_t pin)
{
  const PwmPin *p = find_pin(pin);
  if (!p) return;
  s_last_use = millis();
  if (s_tone[p->id].pin == pin) release(p->id);
}

/* Appelé par gr551x_service() : fin des sons à durée fixée. */
void gr551x_tone_poll(void)
{
  for (uint8_t id = 0; id < APP_PWM_ID_MAX; id++) {
    ToneState &s = s_tone[id];
    if (s.pin >= 0 && s.stop_at && (int32_t)((uint32_t)millis() - s.stop_at) >= 0) release(id);
  }
}

/* Vrai pendant un son et dans la seconde qui suit (silences entre deux notes). */
bool gr551x_tone_busy(void)
{
  return s_tone[0].pin >= 0 || s_tone[1].pin >= 0 || (s_last_use && millis() - s_last_use < 1000);
}

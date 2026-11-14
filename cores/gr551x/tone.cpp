/*
 * tone() / noTone() et analogWrite() sur les modules PWM du GR551x (PWM0, PWM1 ; voies A, B, C).
 *
 * Une broche à la fois par module. Seules les broches qui ont une fonction PWM sont prises en charge (table
 * ci-dessous, d'après gr55xx_hal_*_ex.h).
 * - tone() : rapport cyclique 50 %. Pendant un son, la veille légère est suspendue : le CPU peut y dormir jusqu'à
 *   1 s, ce qui casserait le rythme des mélodies (RTTTL). Les broches sans PWM sont ignorées.
 * - analogWrite() : 1 kHz, rapport cyclique 0..255 écrit dans le comparateur (gr551x_pwm_set_level). Le PWM
 *   continue pendant la veille ; il ne la bloque pas. pinMode() / digitalWrite() sur la broche la rendent au GPIO.
 *   Sans PWM sur la broche, ou module déjà pris par tone() : tout ou rien (seuil 128).
 * Un module tenu par analogWrite() n'est pas pris par tone(), et inversement.
 */
#include "gr551x_internal.h"
#include "app_pwm.h"
#include "gr55xx_ll_aon_gpio.h"
#include "gr55xx_ll_pwm.h"

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
  bool analog;               /* tenu par analogWrite() */
  uint8_t level;             /* analogWrite() : niveau courant (0..255) */
};
static ToneState s_tone[APP_PWM_ID_MAX] = {{{}, -1, 0, false, 0}, {{}, -1, 0, false, 0}};
uint8_t gr551x_pwm_held;       /* nombre de broches tenues par analogWrite() (test rapide dans wiring.cpp) */
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
  if (s.analog) { s.analog = false; gr551x_pwm_held--; }   /* avant pinMode() : pas de récursion */
  pinMode(pin, OUTPUT);          /* comme sur AVR : la broche reste en sortie, à l'état bas */
  digitalWrite(pin, LOW);
}

/* Rapport cyclique écrit directement dans le comparateur (CMPx1 = PRD × niveau / 255, aligné sur front) :
   app_pwm_config_channel() reste sans effet une fois le module démarré (vu sur T1000-A), et ne fait que du 1 %.
   UPDATE.SA déjà à 1 ne recopie rien : on le repasse à 0 avant d'écrire, puis à 1 (comme les HAL Goodix).
   Fonction à part (non inline) : le jumeau numérique l'intercepte pour suivre le rapport cyclique. */
extern "C" __attribute__((noinline)) void gr551x_pwm_set_level(uint8_t id, uint8_t channel, uint8_t level)
{
  pwm_regs_t *r = id ? PWM1 : PWM0;
  uint32_t cmp = (uint32_t)((uint64_t)READ_REG(r->PRD) * level / 255);
  ll_pwm_disable_update_all(r);       /* la recopie en fin de période se fait sur le front montant de SA */
  switch (channel) {
    case 0: ll_pwm_set_compare_a1(r, cmp); break;
    case 1: ll_pwm_set_compare_b1(r, cmp); break;
    default: ll_pwm_set_compare_c1(r, cmp); break;
  }
  ll_pwm_enable_update_all(r);
}

static bool start(const PwmPin &p, uint8_t pin, uint32_t freq, uint8_t duty)
{
  ToneState &s = s_tone[p.id];
  app_pwm_params_t &prm = s.params;
  memset(&prm, 0, sizeof(prm));
  app_io_type_t type;
  uint32_t mask;
  gr551x_pin_decode(pin, &type, &mask);
  app_pwm_pin_t io = {type, (app_io_mux_t)p.mux, mask, APP_IO_NOPULL, APP_PWM_PIN_ENABLE};
  app_pwm_pin_t *chan[3] = {&prm.pin_cfg.channel_a, &prm.pin_cfg.channel_b, &prm.pin_cfg.channel_c};
  pwm_channel_init_t *cfg[3] = {&prm.init.channel_a, &prm.init.channel_b, &prm.init.channel_c};
  *chan[p.channel] = io;
  cfg[p.channel]->duty = duty;
  cfg[p.channel]->drive_polarity = PWM_DRIVEPOLARITY_POSITIVE;

  prm.id = (app_pwm_id_t)p.id;
  prm.active_channel = (app_pwm_active_channel_t)(1u << p.channel);
  prm.init.mode = PWM_MODE_FLICKER;
  prm.init.align = PWM_ALIGNED_EDGE;
  prm.init.freq = freq;
  if (app_pwm_init(&prm) != APP_DRV_SUCCESS) return false;
  if (app_pwm_start(prm.id) != APP_DRV_SUCCESS) { app_pwm_deinit(prm.id); return false; }
  /* AON : en mode MUX, le HAL laisse la sortie du pad coupée (AON_PAD_CTL1.AON_GPO_OE_N = 1, vu sur T1000-A :
     PWM1 tournait, LED éteinte). On la rouvre ; le mux n'est pas touché. */
  if (type == APP_IO_TYPE_AON) ll_aon_gpio_set_pin_mode(mask, LL_AON_GPIO_MODE_OUTPUT);
  return true;
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration)
{
  const PwmPin *p = find_pin(pin);
  if (!p) return;
  s_last_use = millis();
  if (frequency == 0) { noTone(pin); return; }
  if (frequency > 20000) return;      /* hors audio (ex. note RTTTL hors tableau) : ignoré */

  ToneState &s = s_tone[p->id];
  if (s.analog) return;               /* module tenu par analogWrite() */
  if (s.pin != pin) {
    release(p->id);

    if (!start(*p, pin, frequency, 50)) return;
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
  if (s_tone[p->id].pin == pin && !s_tone[p->id].analog) release(p->id);
}

void analogWrite(pin_size_t pin, int value)
{
  const PwmPin *p = find_pin(pin);
  ToneState *s = p ? &s_tone[p->id] : nullptr;
  if (!p || (s->pin >= 0 && !s->analog)) {        /* pas de PWM ici, ou module occupé par tone() */
    pinMode(pin, OUTPUT);
    digitalWrite(pin, value > 127 ? HIGH : LOW);
    return;
  }
  if (value < 0) value = 0;
  if (value > 255) value = 255;
  if (s->pin != (int16_t)pin) {
    release(p->id);                               /* autre voie du même module : on la rend au GPIO */
    if (!start(*p, pin, 1000, (uint8_t)((value * 100 + 127) / 255))) return;
    s->pin = pin;
    s->analog = true;
    gr551x_pwm_held++;
  } else if (value == s->level) {
    return;
  }
  gr551x_pwm_set_level(p->id, p->channel, (uint8_t)value);
  s->level = (uint8_t)value;
}

/* pinMode() / digitalWrite() : une broche tenue par analogWrite() revient au GPIO. */
void gr551x_pwm_pin_release(pin_size_t pin)
{
  for (uint8_t id = 0; id < APP_PWM_ID_MAX; id++)
    if (s_tone[id].analog && s_tone[id].pin == (int16_t)pin) release(id);
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
  for (const ToneState &s : s_tone)
    if (s.pin >= 0 && !s.analog) return true;     /* analogWrite() ne bloque pas la veille */
  return s_last_use && millis() - s_last_use < 1000;
}

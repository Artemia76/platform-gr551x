/*
 * Point d'entrée du programme pour le cœur Arduino GR551x.
 *
 * Avant main() : startup_gr55xx.s → SystemInit → main_init (SDK) → __main : copie .data, mise à zéro .bss,
 * system_platform_init() (horloges, Flash, NVDS, chien de garde de démarrage de 3 s armé par le SDK)…
 *
 * Le SDK n'exécute pas .init_array (il y range lui-même system_platform_init) : les constructeurs C++
 * globaux sont lancés ici, en sautant system_platform_init (déjà exécutée ; la relancer réinitialiserait
 * la plateforme).
 */
#include "Arduino.h"
#include "gr551x_internal.h"
#include "gr551x_ble.h"

extern "C" {
extern void (*__preinit_array_start[])(void);
extern void (*__preinit_array_end[])(void);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
void system_platform_init(void);
}

static void run_constructors(void)
{
  for (auto p = __preinit_array_start; p < __preinit_array_end; ++p) (*p)();
  for (auto p = __init_array_start; p < __init_array_end; ++p) {
    if (*p != system_platform_init) (*p)();
  }
}

static app_timer_id_t s_tick;

/* Minuteur d'une seconde : réveille la boucle en veille (tâches périodiques, chien de garde) et lit les
 * compteurs pour absorber leurs débordements 32 bits. */
static void tick_cb(void *ctx)
{
  (void)ctx;
  gr551x_ticks32k();
  gr551x_wake_events++;
}

static volatile bool s_idle_req;
extern "C" void gr551x_idle_request(void) { s_idle_req = true; }

__attribute__((weak)) void initVariant(void) {}

extern "C" int main(void)
{
  initVariantEarly();             /* variante : init. carte, sécurités de démarrage */
  run_constructors();
  gr551x_boot_step(GR551X_STEP_CTORS);
  gr551x_ble_begin();
  gr551x_boot_step(gr551x_ble_init_error() ? GR551X_STEP_BLE_ERR | (gr551x_ble_init_error() & 0xF)
                                           : GR551X_STEP_BLE_INIT);
  pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);   /* veille seulement sur demande (gr551x_idle_request) */
  gr551x_ticks32k();                         /* démarre la base de temps */
  app_timer_create(&s_tick, ATIMER_REPEAT, tick_cb);
  app_timer_start(s_tick, 1000, NULL);
  gr551x_boot_step(GR551X_STEP_TIMER);
  initVariant();

  gr551x_boot_step(GR551X_STEP_SETUP);
  setup();
  gr551x_boot_step(GR551X_STEP_SETUP_DONE);
  bool first = true;
  for (;;) {
    uint32_t ev = gr551x_wake_events;
    loop();
    if (first) { first = false; gr551x_boot_step(GR551X_STEP_LOOP); }
    gr551x_service();
    if (s_idle_req) {
      s_idle_req = false;
      if (ev == gr551x_wake_events) {           /* rien n'est arrivé pendant le tour : on peut dormir */
        pwr_mgmt_mode_set(PMR_MGMT_IDLE_MODE);
        pwr_mgmt_schedule();
        pwr_mgmt_mode_set(PMR_MGMT_ACTIVE_MODE);
        continue;
      }
    }
    pwr_mgmt_schedule();
  }
  return 0;
}

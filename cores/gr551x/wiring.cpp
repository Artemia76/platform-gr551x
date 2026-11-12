/*
 * Fonctions Arduino de base pour GR551x : broches (app_io), interruptions (app_io_event), temps.
 *
 * Temps : compteur de cycles DWT (64 MHz) étendu à 64 bits. Le compteur s'arrête quand le cœur dort :
 * le cœur Arduino maintient donc le GR551x en mode actif (pas de WFI) — voir main.cpp.
 * Le débordement 32 bits (67 s) est absorbé par le minuteur périodique de main.cpp, qui lit le compteur
 * toutes les 10 ms.
 */
#include "Arduino.h"
#include "gr551x_sdk.h"
#include "gr551x_internal.h"

/* ------------------------------------------------------------------ hooks faibles de la variante */

extern "C" __attribute__((weak)) void initVariantEarly(void) {}
extern "C" __attribute__((weak)) void variantPoll(void) {}
extern "C" __attribute__((weak)) int variantPinInverted(pin_size_t pin) { (void)pin; return 0; }
extern "C" __attribute__((weak)) void gr551x_boot_step(uint8_t id) { (void)id; }
extern "C" __attribute__((weak)) void gr551x_reboot(void) { NVIC_SystemReset(); while (1) {} }
extern "C" __attribute__((weak)) void gr551x_enter_bootloader(void) { gr551x_reboot(); }

/* ------------------------------------------------------------------ broches */

bool gr551x_pin_decode(pin_size_t n, app_io_type_t *type, uint32_t *mask)
{
  if (n < 32)                 { *type = APP_IO_TYPE_NORMAL; *mask = 1u << n; return true; }
  if (n >= 0x20 && n < 0x28)  { *type = APP_IO_TYPE_AON;    *mask = 1u << (n - 0x20); return true; }
  if (n >= 0x30 && n < 0x35)  { *type = APP_IO_TYPE_MSIO;   *mask = 1u << (n - 0x30); return true; }
  return false;
}

void pinMode(pin_size_t pin, PinMode mode)
{
  app_io_type_t t;
  uint32_t m;
  if (!gr551x_pin_decode(pin, &t, &m)) return;
  app_io_init_t io;
  io.pin  = m;
  io.mux  = APP_IO_MUX_7;     /* fonction GPIO */
  io.mode = (mode == OUTPUT || mode == OUTPUT_OPENDRAIN) ? APP_IO_MODE_OUTPUT : APP_IO_MODE_INPUT;
  io.pull = (mode == INPUT_PULLUP) ? APP_IO_PULLUP : (mode == INPUT_PULLDOWN) ? APP_IO_PULLDOWN : APP_IO_NOPULL;
  app_io_init(t, &io);
}

void digitalWrite(pin_size_t pin, PinStatus val)
{
  app_io_type_t t;
  uint32_t m;
  if (!gr551x_pin_decode(pin, &t, &m)) return;
  app_io_write_pin(t, m, val == LOW ? APP_IO_PIN_RESET : APP_IO_PIN_SET);
}

PinStatus digitalRead(pin_size_t pin)
{
  app_io_type_t t;
  uint32_t m;
  gr551x_service();               /* les pilotes (RadioLib) attendent souvent une broche en boucle */
  if (!gr551x_pin_decode(pin, &t, &m)) return LOW;
  bool v = app_io_read_pin(t, m) == APP_IO_PIN_SET;
  if (variantPinInverted(pin)) v = !v;
  return v ? HIGH : LOW;
}

int analogRead(pin_size_t pin) { (void)pin; return 0; }        /* TODO : ADC (batterie) */
void analogReference(uint8_t mode) { (void)mode; }
void analogWrite(pin_size_t pin, int value) { pinMode(pin, OUTPUT); digitalWrite(pin, value > 127 ? HIGH : LOW); }

/* ------------------------------------------------------------------ interruptions */

#define N_IRQ_PINS (32 + 8 + 5)

struct IrqSlot {
  voidFuncPtrParam cb;
  void *param;
  bool with_param;
};
static IrqSlot s_irq[N_IRQ_PINS];

static int irq_index(app_io_type_t t, uint32_t mask)
{
  int bit = __builtin_ctz(mask);
  switch (t) {
    case APP_IO_TYPE_NORMAL: return bit;
    case APP_IO_TYPE_AON:    return 32 + bit;
    case APP_IO_TYPE_MSIO:   return 40 + bit;
    default:                 return -1;
  }
}

static void io_evt(app_io_evt_t *e)
{
  uint32_t pins = e->pin;
  while (pins) {
    uint32_t m = pins & (~pins + 1);
    pins &= ~m;
    int i = irq_index(e->type, m);
    if (i < 0 || !s_irq[i].cb) continue;
    if (s_irq[i].with_param) s_irq[i].cb(s_irq[i].param);
    else ((voidFuncPtr)s_irq[i].cb)();
  }
}

static void attach(pin_size_t pin, voidFuncPtrParam cb, PinStatus mode, void *param, bool with_param)
{
  app_io_type_t t;
  uint32_t m;
  if (!gr551x_pin_decode(pin, &t, &m)) return;
  int i = irq_index(t, m);
  app_io_init_t io;
  io.pin = m;
  io.mux = APP_IO_MUX_7;
  bool inverted = variantPinInverted(pin) != 0;
  if (inverted) {
    /* inverseur à collecteur ouvert sur la carte : pull-up obligatoire, fronts inversés */
    io.pull = APP_IO_PULLUP;
    if (mode == RISING) mode = FALLING;
    else if (mode == FALLING) mode = RISING;
    else if (mode == HIGH) mode = LOW;
    else if (mode == LOW) mode = HIGH;
  } else {
    io.pull = (mode == FALLING || mode == LOW) ? APP_IO_PULLUP : APP_IO_PULLDOWN;
  }
  switch (mode) {
    case RISING:  io.mode = APP_IO_MODE_IT_RISING; break;
    case FALLING: io.mode = APP_IO_MODE_IT_FALLING; break;
    case HIGH:    io.mode = APP_IO_MODE_IT_HIGH; break;
    case LOW:     io.mode = APP_IO_MODE_IT_LOW; break;
    default:      io.mode = APP_IO_MODE_IT_RISING; break;   /* CHANGE non géré par le GR551x */
  }
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  s_irq[i].cb = cb;
  s_irq[i].param = param;
  s_irq[i].with_param = with_param;
  __set_PRIMASK(primask);
  app_io_event_register_cb(t, &io, io_evt, NULL);
}

void attachInterrupt(pin_size_t pin, voidFuncPtr cb, PinStatus mode)
{
  attach(pin, (voidFuncPtrParam)cb, mode, nullptr, false);
}

void attachInterruptParam(pin_size_t pin, voidFuncPtrParam cb, PinStatus mode, void *param)
{
  attach(pin, cb, mode, param, true);
}

void detachInterrupt(pin_size_t pin)
{
  app_io_type_t t;
  uint32_t m;
  if (!gr551x_pin_decode(pin, &t, &m)) return;
  app_io_event_unregister(t, m);
  s_irq[irq_index(t, m)].cb = nullptr;
}

/* ------------------------------------------------------------------ temps */

static uint32_t s_cyc_last;
static uint64_t s_cyc_total;
static bool s_dwt_on;

/* Le SDK coupe le compteur DWT hors de ses propres attentes (hal_dwt_enable/disable comptent les
 * références) : on en prend une référence permanente. */
static void dwt_ensure(void)
{
  if (!s_dwt_on) {
    hal_dwt_enable();
    s_cyc_last = DWT->CYCCNT;
    s_dwt_on = true;
  }
}

uint64_t gr551x_cycles(void)
{
  dwt_ensure();
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  uint32_t now = DWT->CYCCNT;
  s_cyc_total += (uint32_t)(now - s_cyc_last);
  s_cyc_last = now;
  uint64_t t = s_cyc_total;
  __set_PRIMASK(primask);
  return t;
}

unsigned long micros(void) { return (unsigned long)(gr551x_cycles() / (SystemCoreClock / 1000000u)); }
unsigned long millis(void) { return (unsigned long)(gr551x_cycles() / (SystemCoreClock / 1000u)); }

void delay(unsigned long ms)
{
  uint64_t end = gr551x_cycles() + (uint64_t)ms * (SystemCoreClock / 1000u);
  while (gr551x_cycles() < end) gr551x_service();
}

void delayMicroseconds(unsigned int us)
{
  uint64_t end = gr551x_cycles() + (uint64_t)us * (SystemCoreClock / 1000000u);
  while (gr551x_cycles() < end) {}
}

void yield(void) { gr551x_service(); }

/* Appelé depuis toute attente : limité à un passage toutes les 10 ms (variantPoll peut écrire en Flash). */
extern "C" void gr551x_service(void)
{
  static uint64_t last;
  static bool busy;
  uint64_t now = gr551x_cycles();
  if (busy || now - last < (uint64_t)SystemCoreClock / 100u) return;
  busy = true;
  last = now;
  variantPoll();
  busy = false;
}

unsigned long pulseIn(pin_size_t pin, uint8_t state, unsigned long timeout)
{
  unsigned long start = micros();
  while (digitalRead(pin) != state) if (micros() - start > timeout) return 0;
  unsigned long t0 = micros();
  while (digitalRead(pin) == state) if (micros() - start > timeout) return 0;
  return micros() - t0;
}

unsigned long pulseInLong(pin_size_t pin, uint8_t state, unsigned long timeout) { return pulseIn(pin, state, timeout); }

/* ------------------------------------------------------------------ hasard (WMath) */

void randomSeed(unsigned long seed) { if (seed) srand(seed); }
long random(long howbig) { return howbig ? rand() % howbig : 0; }
long random(long lo, long hi) { return hi > lo ? lo + random(hi - lo) : lo; }

void tone(uint8_t pin, unsigned int frequency, unsigned long duration) { (void)pin; (void)frequency; (void)duration; }
void noTone(uint8_t pin) { (void)pin; }

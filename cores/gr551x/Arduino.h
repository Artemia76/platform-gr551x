/*
 * Cœur Arduino pour Goodix GR551x — point d'entrée des croquis.
 *
 * Numérotation des broches (identique au firmware Seeed du T1000-A) :
 *   0–31  : GPIO 0–31
 *   32–39 : AON 0–7   (0x20 + n)
 *   48–52 : MSIO 0–4  (0x30 + n)
 */
#pragma once

#include "api/ArduinoAPI.h"

#if defined(__cplusplus) && !defined(c_plusplus)
using namespace arduino;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GR551X_PIN_AON(n)   (0x20u + (n))
#define GR551X_PIN_MSIO(n)  (0x30u + (n))

/* Accès direct aux ports (utilisé par certaines bibliothèques, ex. Adafruit BusIO en SPI logiciel).
 * GPIO 0–15 : port 0 (GPIO0), GPIO 16–31 : port 1 (GPIO1), registres DATA (lecture) / DATAOUT (écriture).
 * Les broches AON et MSIO n'ont pas d'accès direct : masque nul, registre factice (sans effet). */
extern volatile uint32_t gr551x_no_port_reg;
#define GR551X_GPIO0_BASE           0xA0010000UL
#define GR551X_GPIO1_BASE           0xA0011000UL
#define digitalPinToPort(p)         ((p) < 16 ? 0 : (p) < 32 ? 1 : 2)
#define digitalPinToBitMask(p)      ((p) < 32 ? (1UL << ((p) & 15)) : 0UL)
#define portInputRegister(port)     ((port) == 0 ? (volatile uint32_t *)(GR551X_GPIO0_BASE + 0x0) : \
                                     (port) == 1 ? (volatile uint32_t *)(GR551X_GPIO1_BASE + 0x0) : &gr551x_no_port_reg)
#define portOutputRegister(port)    ((port) == 0 ? (volatile uint32_t *)(GR551X_GPIO0_BASE + 0x4) : \
                                     (port) == 1 ? (volatile uint32_t *)(GR551X_GPIO1_BASE + 0x4) : &gr551x_no_port_reg)

/* Une interruption par broche : le numéro d'interruption est le numéro de broche. */
#define digitalPinToInterrupt(p)  (p)

/* Attente active bornée : rafraîchit le chien de garde de la variante (si elle en a un). */
void gr551x_service(void);

/* Hooks de la variante (définitions faibles vides dans le cœur). */
void initVariantEarly(void);            /* tout premier appel de main(), avant les constructeurs */
void variantPoll(void);                 /* à chaque tour de loop() et pendant les attentes */
int  variantPinInverted(pin_size_t pin);/* 1 si la broche passe par un inverseur sur la carte */

/* Étape de démarrage franchie (la variante peut l'enregistrer de façon persistante). */
void gr551x_boot_step(uint8_t id);
#define GR551X_STEP_CTORS      0x41
#define GR551X_STEP_BLE_INIT   0x42
#define GR551X_STEP_TIMER      0x43
#define GR551X_STEP_SETUP      0x44
#define GR551X_STEP_SETUP_DONE 0x45
#define GR551X_STEP_LOOP       0x46
#define GR551X_STEP_BLE_READY  0x47
#define GR551X_STEP_BLE_ERR    0x50   /* + code d'erreur de ble_stack_init (4 bits) */

/* Redémarrages (la variante remplace les définitions faibles, ex. drapeau DFU du bootloader). */
void gr551x_reboot(void) __attribute__((noreturn));
void gr551x_enter_bootloader(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#include "pins_arduino.h"

#ifdef __cplusplus
#include "HardwareSerial.h"

/* Surcharges entières : beaucoup de bibliothèques (RadioLib sur une plateforme « inconnue », anciennes
 * bibliothèques AVR) passent des entiers là où ArduinoCore-API attend PinMode / PinStatus. */
inline void pinMode(pin_size_t pin, uint32_t mode) { pinMode(pin, (PinMode)mode); }
inline void digitalWrite(pin_size_t pin, uint32_t val) { digitalWrite(pin, val ? HIGH : LOW); }
inline void attachInterrupt(pin_size_t pin, voidFuncPtr cb, uint32_t mode) { attachInterrupt(pin, cb, (PinStatus)mode); }
#endif

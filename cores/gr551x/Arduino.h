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

/* Une interruption par broche : le numéro d'interruption est le numéro de broche. */
#define digitalPinToInterrupt(p)  (p)

/* Attente active bornée : rafraîchit le chien de garde de la variante (si elle en a un). */
void gr551x_service(void);

/* Hooks de la variante (définitions faibles vides dans le cœur). */
void initVariantEarly(void);            /* tout premier appel de main(), avant les constructeurs */
void variantPoll(void);                 /* à chaque tour de loop() et pendant les attentes */
int  variantPinInverted(pin_size_t pin);/* 1 si la broche passe par un inverseur sur la carte */

/* Redémarrages (la variante remplace les définitions faibles, ex. drapeau DFU du bootloader). */
void gr551x_reboot(void) __attribute__((noreturn));
void gr551x_enter_bootloader(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#include "pins_arduino.h"

#ifdef __cplusplus
#include "HardwareSerial.h"
#endif

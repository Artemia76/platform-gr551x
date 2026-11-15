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
void variantPostSetup(void);            /* après setup() : la configuration des broches par l'appli est faite */
int  variantPinInverted(pin_size_t pin);/* 1 si la broche passe par un inverseur sur la carte */

/* Tension en mV sur une broche MSIO (0x30+n), ADC du GR551x étalonné ; -1 si broche invalide ou erreur. */
int analogReadMilliVolts(pin_size_t pin);

/* Veille légère : demande de dormir (WFI) à la fin du tour de loop() en cours, jusqu'à la prochaine
 * interruption (réveil au plus tard par le minuteur d'une seconde du cœur). Ignorée si une interruption
 * est arrivée pendant le tour de boucle. */
void gr551x_idle_request(void);

/* Empêche la veille légère pendant `ms` millisecondes (prolonge, ne raccourcit jamais). Sert aux entrées
 * lues par scrutation (bouton) : un appui bref passerait inaperçu pendant une seconde de sommeil. */
void gr551x_stay_awake(uint32_t ms);

/* Statistiques d'énergie : durées en ticks de 32 768 Hz depuis le démarrage ou le dernier
 * gr551x_power_stats_reset(). Servent à estimer la consommation (temps passé dans chaque état). */
typedef struct {
  uint64_t total_ticks;          /* durée couverte */
  uint64_t idle_ticks;           /* CPU en veille légère (IDLE, WFI) */
  uint64_t ble_conn_ticks;       /* au moins une connexion BLE ouverte */
  uint64_t ble_adv_ticks;        /* annonces BLE en cours */
  uint32_t idle_entries;         /* entrées en veille */
  uint32_t idle_refused_event;   /* veille demandée, refusée : interruption arrivée pendant le tour */
  uint32_t idle_refused_awake;   /* veille demandée, refusée : gr551x_stay_awake() ou son en cours */
  uint32_t wake_events;          /* interruptions utiles (broches, BLE, UART, minuteur) */
  uint32_t loops;                /* tours de loop() */
  uint8_t  ble_connections;      /* connexions BLE ouvertes à l'instant */
} gr551x_power_stats_t;
void gr551x_power_stats_get(gr551x_power_stats_t *s);
void gr551x_power_stats_reset(void);

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

/* Arrêt véritable : veille ultra-profonde (ni horloge ni RAM conservées), réveil = redémarrage complet.
 * Sources de réveil : une broche AON (niveau) et/ou le comparateur MSIO (tension > seuil, ex. câble de
 * charge). Le BLE est arrêté et variantPowerOff(cfg) est appelé juste avant (chien de garde, drapeau). */
typedef struct {
  int8_t   wake_aon_pin;     /* broche AON 0..5 qui réveille, -1 : aucune */
  uint8_t  wake_aon_high;    /* 1 : réveil au niveau haut, 0 : au niveau bas */
  int8_t   wake_msio_pin;    /* entrée MSIO 0..4 du comparateur, -1 : aucune */
  uint16_t wake_msio_mv;     /* seuil du comparateur (pas de 30 mV, ≤ 1890 mV) */
  uint16_t batt_mv;          /* tension batterie à l'arrêt, mémorisée par la variante */
} gr551x_wake_cfg_t;
void gr551x_power_off(const gr551x_wake_cfg_t *cfg) __attribute__((noreturn));
void variantPowerOff(const gr551x_wake_cfg_t *cfg);

/* État laissé par le dernier arrêt (implémenté par la variante ; faux par défaut). */
typedef struct {
  uint16_t batt_mv;          /* tension batterie au moment de l'arrêt */
  uint32_t calendar;         /* compteur brut du calendrier AON (32 768 Hz) à l'arrêt */
} gr551x_off_info_t;
bool gr551x_power_off_state(gr551x_off_info_t *info);
void gr551x_power_off_clear(void);
/* Compteur brut du calendrier AON (32 768 Hz, 32 bits) : continue de tourner si le domaine AON reste alimenté. */
uint32_t gr551x_calendar_raw(void);

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

/*
 * Mécanismes de sécurité : le T1000-A n'a ni port série ni accès SWD sans ouvrir le boîtier.
 * Le seul moyen de reflasher est le DFU BLE du bootloader Seeed ; ce module garantit qu'on
 * peut toujours y retourner :
 *   - safety_enter_dfu()   : séquence identique à AT+DFU_ENTER du firmware d'origine ;
 *   - compteur de démarrages en Flash, remis à zéro à la première connexion BLE ou après
 *     SAFETY_STABLE_MS de fonctionnement. Au 3e démarrage « court » consécutif → DFU automatique
 *     (couvre plantages au démarrage et blocages détectés par le chien de garde) ;
 *   - chien de garde AON (8 s), coupé avant tout passage en DFU ;
 *   - appui long de 6 s sur le bouton → DFU (variant.cpp) ;
 *   - en dernier recours (matériel) : brancher le câble de charge en maintenant le bouton.
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdint.h>

/* À appeler en tout premier dans main(). Peut ne jamais revenir (passage en DFU). */
void safety_boot(void);
/* À appeler régulièrement depuis la boucle principale. */
void safety_poll(uint32_t uptime_ms);
/* À appeler à chaque connexion BLE : prouve que le firmware est joignable, remet le compteur à zéro. */
void safety_ble_connected(void);
/* Écrit le drapeau DFU et redémarre vers le bootloader. Ne revient pas. */
void safety_enter_dfu(void) __attribute__((noreturn));
/* Redémarrage normal (remet le compteur à zéro avant). Ne revient pas. */
void safety_reboot(void) __attribute__((noreturn));
/* Rafraîchit le chien de garde depuis une attente bornée (pilotes, RadioLib). Sans effet s'il est inactif. */
void safety_wdt_kick(void);
/* Nombre de démarrages rapides consécutifs constaté au démarrage. */
uint32_t safety_boot_count(void);

/* Arrêt véritable : coupe le chien de garde, remet la garde à zéro et enregistre l'état « éteint »
 * (tension du premier arrêt, conservée lors des retours en veille suivants). */
typedef struct {
    uint8_t  magic[4];
    uint16_t batt_mv;
    uint16_t reserved;
    uint32_t unused;
} safety_off_rec_t;
void safety_power_off(uint16_t batt_mv);
int safety_off_read(safety_off_rec_t *rec);
void safety_off_clear(void);
/* Réveils suivis d'un retour en veille depuis l'arrêt (appuis courts, parasites). */
uint32_t safety_off_wakes(void);

#endif

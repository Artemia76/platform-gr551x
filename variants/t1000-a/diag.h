/*
 * Diagnostic persistant (survit aux redémarrages), stocké dans le secteur Flash 0x010E2000 :
 *   - cause du dernier redémarrage (chien de garde AON ou autre) ;
 *   - fil d'Ariane : un octet par étape franchie (diag_step), pour situer un blocage ;
 *   - dernière faute matérielle : PC, LR, CFSR, HFSR, BFAR, MMFAR (gestionnaire HardFault remplacé).
 */
#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_DIAG_ADDR 0x010E2000u

void diag_boot(void);                 /* au démarrage : relève la cause du redémarrage */
void diag_begin(void);                /* efface le fil d'Ariane et la faute (début d'un essai) */
void diag_step(uint8_t id);           /* note qu'une étape est atteinte */
void diag_report(void (*out)(const char *fmt, ...));

#ifdef __cplusplus
}
#endif

#endif

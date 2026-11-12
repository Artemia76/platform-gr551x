/*
 * SenseCAP T1000-A : constantes internes de la variante (C).
 */
#ifndef T1000A_BOARD_H
#define T1000A_BOARD_H

#include "app_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED : AON3, active à l'état haut. Bouton : AON0, actif à l'état haut (pull-down externe). */
#define BOARD_LED_TYPE      APP_IO_TYPE_AON
#define BOARD_LED_PIN       APP_IO_PIN_3
#define BOARD_BTN_TYPE      APP_IO_TYPE_AON
#define BOARD_BTN_PIN       APP_IO_PIN_0
#define BOARD_GPIO_MUX      APP_IO_MUX_7

/* Carte Flash (docs/materiel-t1000a.md) */
#define FLASH_FS_ADDR           0x010C2000u   /* système de fichiers (LittleFS), 128 Ko */
#define FLASH_FS_SIZE           0x00020000u
#define FLASH_BOOT_GUARD_ADDR   0x010E3000u   /* compteur de démarrages (sécurité) */
#define FLASH_DFU_FLAG_ADDR     0x010E4000u   /* drapeau lu par le bootloader Seeed */
#define FLASH_SECTOR_SIZE       0x1000u

void board_led_set(int on);
int  board_button_pressed(void);

#ifdef __cplusplus
}
#endif

#endif

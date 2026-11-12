/*
 * Accès partagé à la pile BLE Goodix (en ROM). Le cœur l'initialise avant setup() ; les bibliothèques
 * (interface série BLE de MeshCore, console…) s'abonnent aux événements.
 *
 * Les services GATT ne peuvent être ajoutés qu'après l'événement BLE_COMMON_EVT_STACK_INIT :
 * gr551x_ble_on_ready() appelle la fonction donnée à ce moment-là (ou tout de suite si la pile est prête).
 * Les événements arrivent en contexte d'interruption.
 */
#pragma once

#include "gr551x_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gr551x_ble_handler_t)(const ble_evt_t *evt);

#define GR551X_BLE_MAX_HANDLERS 4

int  gr551x_ble_add_handler(gr551x_ble_handler_t handler);   /* 0 si ok */
void gr551x_ble_on_ready(void (*cb)(void));
int  gr551x_ble_ready(void);
sdk_err_t gr551x_ble_init_error(void);                       /* erreur de ble_stack_init (0 si ok) */

/* Interne au cœur */
void gr551x_ble_begin(void);

#ifdef __cplusplus
}
#endif

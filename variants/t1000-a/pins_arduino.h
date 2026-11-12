/*
 * Brochage Arduino du Seeed SenseCAP T1000-A (Goodix GR5515 + LR1110 + AG3335).
 * Numéros : 0–31 GPIO, 0x20 + n = AON n, 0x30 + n = MSIO n (comme le firmware Seeed).
 */
#pragma once

#define NUM_DIGITAL_PINS    0x35
#define NUM_ANALOG_INPUTS   0

/* LED et bouton */
#define PIN_LED             0x23          /* AON3, active à l'état haut */
#define LED_BUILTIN         PIN_LED
#define LED_STATE_ON        1
#define PIN_BUTTON1         0x20          /* AON0, actif à l'état haut */

/* SPI (LR1110) : SPIM, mux 2 */
#define PIN_SPI_SCK         3
#define PIN_SPI_MOSI        4
#define PIN_SPI_MISO        5
#define PIN_SPI_MUX         APP_IO_MUX_2
#define SPI_DEFAULT_HZ      4000000u      /* fréquence du firmware Seeed */

/* LR1110 */
#define PIN_LORA_NSS        6
#define PIN_LORA_RESET      12
#define PIN_LORA_BUSY       13
#define PIN_LORA_IRQ        0x21          /* AON1 ; ligne inversée sur la carte (voir variantPinInverted) */
#define PIN_LORA_RF_EN      14            /* = PIN_BATTERY_EN ; mis à 1 par hello avant la radio (rôle exact à confirmer) */

/* GNSS AG3335 : UART1 115200 bauds (Serial1) */
#define PIN_SERIAL1_TX      30
#define PIN_SERIAL1_RX      26
#define PIN_SERIAL1_MUX     1             /* APP_IO_MUX_1 : UART1 sur GPIO26/30 (relevé dans le firmware Seeed) */
#define PIN_GNSS_TX         30
#define PIN_GNSS_RX         26
#define PIN_GNSS_EN         28
#define PIN_GNSS_VRTC       27
#define PIN_GNSS_RESET      16
#define PIN_GNSS_SLEEP      29
#define PIN_GNSS_RTC_INT    24
#define PIN_GNSS_RESETB     25

/* Batterie : MSIO4, pont diviseur par 2, activé par GPIO14 (relevé dans le firmware Seeed, 0x01028DDC) */
#define PIN_BATTERY         0x34
#define PIN_BATTERY_EN      14
#define BATTERY_DIVIDER     2

/* I2C (capteurs) */
#define PIN_WIRE_SCL        8
#define PIN_WIRE_SDA        9

/* Buzzer : PWM0 sur GPIO2, alimentation sur GPIO15 */
#define PIN_BUZZER          2
#define PIN_BUZZER_EN       15

/* Système de fichiers interne (LittleFS) : fin de la zone applicative */
#define GR551X_FS_ADDR      0x010C2000u
#define GR551X_FS_SIZE      0x00020000u

#include "safety.h"
#include "t1000a_board.h"
#include "hal_flash.h"
#include "app_aon_wdt.h"
#include "gr55xx_hal.h"
#include "gr55xx_delay.h"
#include <string.h>

#define BOOT_GUARD_MAX_FAST_BOOTS   3        /* au 3e démarrage court consécutif → DFU */
#ifndef SAFETY_STABLE_MS
#define SAFETY_STABLE_MS            (2u * 60u * 1000u)   /* au-delà, le démarrage est jugé réussi */
#endif
#define WDT_TIMEOUT_TICKS           (8u * 32768u)   /* environ 8 s sur l'horloge lente */

static uint32_t s_boot_count;
static int      s_guard_cleared;
static int      s_ble_seen;
static int      s_wdt_on;
static app_aon_wdt_params_t s_wdt;

/*
 * Format du secteur de garde : 4 octets magiques « T1GD », puis un octet 0x00 par démarrage
 * rapide. Un secteur sans en-tête valide (contenu laissé par un autre firmware, ou effacé)
 * est réinitialisé et compte pour zéro démarrage.
 */
#define GUARD_MAGIC_LEN 4
static const uint8_t s_guard_magic[GUARD_MAGIC_LEN] = { 'T', '1', 'G', 'D' };

static void guard_clear(void)
{
    hal_flash_erase(FLASH_BOOT_GUARD_ADDR, FLASH_SECTOR_SIZE);
    hal_flash_write(FLASH_BOOT_GUARD_ADDR, s_guard_magic, GUARD_MAGIC_LEN);
}

/* Nombre d'octets de comptage consommés après l'en-tête. */
static uint32_t guard_count(void)
{
    uint8_t buf[16];
    uint32_t n = 0;

    hal_flash_read(FLASH_BOOT_GUARD_ADDR, buf, GUARD_MAGIC_LEN);
    for (int i = 0; i < GUARD_MAGIC_LEN; i++) {
        if (buf[i] != s_guard_magic[i]) {
            guard_clear();
            return 0;
        }
    }
    for (uint32_t off = 0; off < 256; off += sizeof(buf)) {
        hal_flash_read(FLASH_BOOT_GUARD_ADDR + GUARD_MAGIC_LEN + off, buf, sizeof(buf));
        for (uint32_t i = 0; i < sizeof(buf); i++) {
            if (buf[i] != 0x00) return n;   /* 0xFF = libre ; autre valeur = fin du comptage */
            n++;
        }
    }
    return n;
}

static void wdt_stop(void)
{
    if (s_wdt_on) {
        app_aon_wdt_deinit();
        s_wdt_on = 0;
    }
}

void safety_enter_dfu(void)
{
    static const uint8_t flag = 0x01;   /* valeur écrite par AT+DFU_ENTER d'origine */

    __disable_irq();
    wdt_stop();
    guard_clear();
    hal_flash_erase(FLASH_DFU_FLAG_ADDR, FLASH_SECTOR_SIZE);
    hal_flash_write(FLASH_DFU_FLAG_ADDR, &flag, 1);
    /* signal visuel : LED allumée fixe 1 s */
    board_led_set(1);
    delay_ms(1000);
    hal_nvic_system_reset();
    while (1) {}
}

void safety_reboot(void)
{
    __disable_irq();
    wdt_stop();
    guard_clear();
    hal_nvic_system_reset();
    while (1) {}
}

void safety_boot(void)
{
    static const uint8_t zero = 0x00;

    s_boot_count = guard_count();
    if (s_boot_count + 1 >= BOOT_GUARD_MAX_FAST_BOOTS) {
        safety_enter_dfu();
    }
    if (s_boot_count >= 256) {          /* garde corrompue : on repart de zéro */
        guard_clear();
        s_boot_count = 0;
    }
    hal_flash_write(FLASH_BOOT_GUARD_ADDR + GUARD_MAGIC_LEN + s_boot_count, &zero, 1);

    s_wdt.init.counter       = WDT_TIMEOUT_TICKS;
    s_wdt.init.alarm_counter = 0x1F;
    if (app_aon_wdt_init(&s_wdt, NULL) == APP_DRV_SUCCESS) {
        s_wdt_on = 1;
    }
}

void safety_ble_connected(void)
{
    s_ble_seen = 1;
    if (!s_guard_cleared) {
        guard_clear();
        s_guard_cleared = 1;
    }
}

void safety_poll(uint32_t uptime_ms)
{
    if (s_wdt_on) {
        app_aon_wdt_refresh();
    }
    if (!s_guard_cleared && uptime_ms >= SAFETY_STABLE_MS) {
        guard_clear();
        s_guard_cleared = 1;
    }
}

void safety_wdt_kick(void)
{
    if (s_wdt_on) {
        app_aon_wdt_refresh();
    }
}

uint32_t safety_boot_count(void)
{
    return s_boot_count;
}

/*
 * Arrêt véritable : enregistrement « éteint » dans le secteur de garde, à l'offset 0x800 (hors de la zone de
 * comptage). Il porte la tension batterie et le calendrier AON du premier arrêt ; les retours en veille qui
 * suivent (appui trop court, câble débranché) le conservent, pour mesurer toute la durée d'arrêt.
 */
#define OFF_REC_ADDR (FLASH_BOOT_GUARD_ADDR + 0x800u)
static const uint8_t s_off_magic[4] = { 'O', 'F', 'F', '1' };

int safety_off_read(safety_off_rec_t *rec)
{
    safety_off_rec_t r;
    hal_flash_read(OFF_REC_ADDR, (uint8_t *)&r, sizeof(r));
    if (memcmp(r.magic, s_off_magic, sizeof(s_off_magic)) != 0) return 0;
    if (rec) *rec = r;
    return 1;
}

void safety_power_off(uint16_t batt_mv, uint32_t calendar)
{
    safety_off_rec_t r;
    int had = safety_off_read(&r);

    wdt_stop();                 /* le chien de garde AON tourne aussi en veille : il redémarrerait le boîtier */
    guard_clear();              /* les réveils sans démarrage complet ne comptent pas comme des échecs */
    if (!had) {
        memcpy(r.magic, s_off_magic, sizeof(s_off_magic));
        r.batt_mv = batt_mv;
        r.reserved = 0xFFFF;
        r.calendar = calendar;
    }
    hal_flash_write(OFF_REC_ADDR, (uint8_t *)&r, sizeof(r));
}

void safety_off_clear(void)
{
    static const uint8_t zero[4] = { 0, 0, 0, 0 };   /* 1 → 0 sans effacement : l'en-tête devient invalide */
    hal_flash_write(OFF_REC_ADDR, zero, sizeof(zero));
}

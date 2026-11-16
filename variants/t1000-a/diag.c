#include "diag.h"
#include "t1000a_board.h"
#include "hal_flash.h"
#include "gr55xx_hal.h"
#include "gr55xx_ll_aon_wdt.h"
#include "gr_soc.h"
#include <string.h>

/* Organisation du secteur :
 *   0x000  "T1DG"
 *   0x004  fil d'Ariane : octets ≠ 0xFF dans l'ordre d'écriture (≤ 252)
 *   0x100  faute : "FALT", pc, lr, cfsr, hfsr, bfar, mmfar, xpsr (8 mots)
 *   0x140  chien de garde : "WDOG", pc, lr, xpsr, sp, 0, ipsr (6 mots) — écrit par la préalerte */
#define OFF_STEPS 0x004u
#define MAX_STEPS 252u
#define OFF_FAULT 0x100u
#define OFF_WDOG  0x140u

static const uint8_t s_magic[4] = { 'T', '1', 'D', 'G' };
static const uint8_t s_fmagic[4] = { 'F', 'A', 'L', 'T' };
static const uint8_t s_wmagic[4] = { 'W', 'D', 'O', 'G' };
static uint32_t s_reset_wdt;          /* 1 si le dernier redémarrage vient du chien de garde AON */

static void sector_init(void)
{
    hal_flash_erase(FLASH_DIAG_ADDR, FLASH_SECTOR_SIZE);
    hal_flash_write(FLASH_DIAG_ADDR, s_magic, sizeof(s_magic));
}

static int sector_valid(void)
{
    uint8_t m[4];
    hal_flash_read(FLASH_DIAG_ADDR, m, sizeof(m));
    return memcmp(m, s_magic, sizeof(m)) == 0;
}

void diag_boot(void)
{
    static int done;
    if (done) return;                 /* idempotent : peut être appelé tôt (trace) puis normalement */
    done = 1;
    s_reset_wdt = ll_aon_wdt_is_active_flag_reboot() ? 1u : 0u;
    if (s_reset_wdt) ll_aon_wdt_clear_flag_reboot();
    if (!sector_valid()) sector_init();
}

int diag_last_reset_wdt(void)
{
    return (int)s_reset_wdt;
}

void diag_begin(void)
{
    sector_init();
}

void diag_step(uint8_t id)
{
    uint8_t buf[16];
    for (uint32_t off = 0; off < MAX_STEPS; off += sizeof(buf)) {
        hal_flash_read(FLASH_DIAG_ADDR + OFF_STEPS + off, buf, sizeof(buf));
        for (uint32_t i = 0; i < sizeof(buf); i++) {
            if (buf[i] == 0xFF && off + i < MAX_STEPS) {
                if (id == 0xFF) id = 0xFE;
                hal_flash_write(FLASH_DIAG_ADDR + OFF_STEPS + off + i, &id, 1);
                return;
            }
        }
    }
}

void diag_report(void (*out)(const char *fmt, ...))
{
    uint8_t steps[MAX_STEPS];
    uint32_t f[8];
    uint8_t fm[4];

    out("dernier redemarrage : %s\r\n", s_reset_wdt ? "CHIEN DE GARDE" : "normal/autre");
    hal_flash_read(FLASH_DIAG_ADDR + OFF_STEPS, steps, sizeof(steps));
    out("etapes :");
    for (uint32_t i = 0; i < MAX_STEPS && steps[i] != 0xFF; i++) out(" %u", steps[i]);
    out("\r\n");
    hal_flash_read(FLASH_DIAG_ADDR + OFF_FAULT, fm, sizeof(fm));
    if (memcmp(fm, s_fmagic, sizeof(fm)) == 0) {
        hal_flash_read(FLASH_DIAG_ADDR + OFF_FAULT + 4, (uint8_t *)f, sizeof(f));
        out("FAUTE pc=%08lx lr=%08lx cfsr=%08lx hfsr=%08lx bfar=%08lx mmfar=%08lx xpsr=%08lx\r\n",
            f[0], f[1], f[2], f[3], f[4], f[5], f[6]);
    } else {
        out("aucune faute enregistree\r\n");
    }
    hal_flash_read(FLASH_DIAG_ADDR + OFF_WDOG, fm, sizeof(fm));
    if (memcmp(fm, s_wmagic, sizeof(fm)) == 0) {
        hal_flash_read(FLASH_DIAG_ADDR + OFF_WDOG + 4, (uint8_t *)f, 6 * sizeof(uint32_t));
        out("CHIEN DE GARDE (prealerte) pc=%08lx lr=%08lx xpsr=%08lx sp=%08lx ipsr=%lu\r\n",
            f[0], f[1], f[2], f[3], f[5]);
    } else {
        out("aucune prealerte du chien de garde enregistree\r\n");
    }
}

/* Préalerte du chien de garde AON (≈ 1 ms avant le redémarrage) : enregistre où le code était bloqué (PC et LR
 * empilés à l'entrée de l'interruption). Une seule fois : l'interruption n'est pas acquittée. */
void diag_wdt_capture(uint32_t sp)
{
    static int done;
    NVIC_DisableIRQ(AON_WDT_IRQn);
    if (done) return;
    done = 1;
    const uint32_t *frame = (const uint32_t *)sp;   /* r0 r1 r2 r3 r12 lr pc xpsr */
    uint32_t rec[6];
    rec[0] = frame[6];
    rec[1] = frame[5];
    rec[2] = frame[7];
    rec[3] = sp + 32u;
    rec[4] = 0;                                      /* réservé */
    rec[5] = frame[7] & 0x1FFu;                      /* interruption en cours au moment du blocage (0 = code normal) */
    hal_flash_write(FLASH_DIAG_ADDR + OFF_WDOG, s_wmagic, sizeof(s_wmagic));
    hal_flash_write(FLASH_DIAG_ADDR + OFF_WDOG + 4, (const uint8_t *)rec, sizeof(rec));
}

__attribute__((naked)) void diag_wdt_irq(void)
{
    __asm volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "b diag_wdt_capture \n");
}

void diag_wdt_install(void)
{
    soc_register_nvic(AON_WDT_IRQn, (uint32_t)diag_wdt_irq);
    NVIC_ClearPendingIRQ(AON_WDT_IRQn);
    NVIC_EnableIRQ(AON_WDT_IRQn);
}

/* Remplace la fonction faible du SDK appelée par HardFault_Handler avec le pointeur de pile fautif. */
void hardfault_trace_handler(uint32_t sp)
{
    const uint32_t *frame = (const uint32_t *)sp;   /* r0 r1 r2 r3 r12 lr pc xpsr */
    uint32_t rec[8];
    rec[0] = frame[6];
    rec[1] = frame[5];
    rec[2] = SCB->CFSR;
    rec[3] = SCB->HFSR;
    rec[4] = SCB->BFAR;
    rec[5] = SCB->MMFAR;
    rec[6] = frame[7];
    rec[7] = 0;
    hal_flash_write(FLASH_DIAG_ADDR + OFF_FAULT, s_fmagic, sizeof(s_fmagic));
    hal_flash_write(FLASH_DIAG_ADDR + OFF_FAULT + 4, (const uint8_t *)rec, sizeof(rec));
    NVIC_SystemReset();
}

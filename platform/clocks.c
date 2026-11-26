/*
 * frank-sdk — clocks, core voltage and flash timing.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * From frank-duke3d / frank-shadowwarrior common/platform/clocks.c (the boot
 * and warm-restart fixes proven on FRANK Next and Core 2) and frank-dos
 * (the flash read-delay rule and the Quad Enable fix-up).
 *
 * At 504 MHz:
 *   PLL_SYS  12 MHz x 126 = 1512 MHz VCO / 3 / 1 = 504 MHz -> clk_sys
 *   clk_peri = clk_sys / 4 = 126 MHz (UART, SPI)
 *   clk_hstx = clk_sys / 4 = 126 MHz: 252 Mbit/s TMDS, 640x480's 25.2 MHz
 */
#include "frank/clocks.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "hardware/uart.h"
#include "hardware/vreg.h"
#include "hardware/xip_cache.h"
#include "pico/runtime_init.h"
#include "pico/stdlib.h"

#include "flash_config.h"

#ifndef FRANK_CPU_MHZ
#define FRANK_CPU_MHZ 252
#endif
#ifndef FRANK_VREG
#define FRANK_VREG VREG_VOLTAGE_1_30
#endif
#ifndef FRANK_FLASH_MHZ
#define FRANK_FLASH_MHZ 66
#endif
#ifndef FRANK_FLASH_RXDELAY
#define FRANK_FLASH_RXDELAY -1
#endif

/* the flash timing the boot ROM / boot2 left, and the clock it was for */
static uint32_t boot_m0_timing, boot_m0_hz;
uint32_t frank_boot_qmi_seen;   /* what the boot found (frank_clocks_report) */

/* The very first initialiser: the XIP cache emptied (a warm restart can
 * leave lines of the previous image) and the core voltage back to its
 * power-on 1.10 V. The regulator survives a watchdog or BOOTSEL restart, and
 * the SDK's clock setup then ran at the 504 MHz voltage: the boot faulted now
 * and then on FRANK Next (a cold boot never). */
static void __no_inline_not_in_flash_func(frank_xip_fresh)(void) {
    xip_cache_invalidate_all();
    if (vreg_get_voltage() != VREG_VOLTAGE_DEFAULT) {
        vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
        for (volatile int i = 0; i < 20000; i++) __asm__ volatile("nop");   /* no timer yet */
    }
}
PICO_RUNTIME_INIT_FUNC_HW(frank_xip_fresh, "00001");

/* Just before the SDK raises clk_sys to 150 MHz: a flash timing safe up to it
 * (/4, read delay 2). After a warm restart the boot ROM can leave a faster
 * one, and the boot then fetched wrong instructions now and then. */
static void __no_inline_not_in_flash_func(frank_flash_safe_timing)(void) {
    uint32_t t = qmi_hw->m[0].timing;
    frank_boot_qmi_seen = t;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (4u << QMI_M0_TIMING_CLKDIV_LSB) | (2u << QMI_M0_TIMING_RXDELAY_LSB);
    qmi_hw->m[0].timing = t;
    __asm__ volatile("dmb" ::: "memory");
    for (volatile int i = 0; i < 100; i++) __asm__ volatile("nop");
}
PICO_RUNTIME_INIT_FUNC_HW(frank_flash_safe_timing, "00499");

static void __no_inline_not_in_flash_func(flash_write_timing)(uint32_t divisor, uint32_t rxdelay) {
    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (divisor << QMI_M0_TIMING_CLKDIV_LSB) |
         ((rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS);
    qmi_hw->m[0].timing = t;
    __asm__ volatile("dmb" ::: "memory");
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("nop");
}

/* The read delay counts half clk_sys cycles in a 3-bit field. Up to divider 7
 * it is the divider (+1 over 100 MHz), as the fleet always had it. Past 7 the
 * old code overflowed into a reserved bit and left 0 (504 MHz / 66 MHz); there
 * it is boot2's delay scaled to the new clock (xrip/pico-386's rule), at most 7.
 * Boards whose flash was tuned (boards/*.yaml flash_rxdelay) pass theirs.
 * No 64-bit division here: __aeabi_uldivmod would be a call into flash. */
void __no_inline_not_in_flash_func(frank_flash_set_timing)(uint32_t sys_hz, unsigned max_mhz, int rxdelay) {
    const uint32_t max_hz = max_mhz * 1000000u;
    uint32_t divisor = (sys_hz + max_hz - (max_hz >> 4) - 1u) / max_hz;
    if (divisor < 1u) divisor = 1u;
    if (divisor == 1u && sys_hz >= 166000000u) divisor = 2u;
    if (divisor > 255u) divisor = 255u;
    if (rxdelay < 0) {
        rxdelay = (int)divisor;
        if (sys_hz / divisor > 100000000u && sys_hz >= 166000000u) rxdelay += 1;
        if (rxdelay > 7) {
            const uint32_t boot_rx = (boot_m0_timing & QMI_M0_TIMING_RXDELAY_BITS) >> QMI_M0_TIMING_RXDELAY_LSB;
            const uint32_t boot_mhz = boot_m0_hz / 1000000u, mhz = sys_hz / 1000000u;
            rxdelay = boot_mhz ? (int)((boot_rx * mhz + boot_mhz - 1u) / boot_mhz) : 7;
            if (rxdelay > 7) rxdelay = 7;
        }
    }
    flash_write_timing(divisor, (uint32_t)rxdelay);
}

/* ---- Quad Enable (frank-dos; the write is xrip's flash_config_init) ---- */

int __no_inline_not_in_flash_func(frank_flash_ensure_qe)(uint8_t *jedec_mfr) {
    uint8_t tx[4], rx[4];
    tx[0] = 0x9F; tx[1] = 0; tx[2] = 0; tx[3] = 0;
    flash_do_cmd(tx, rx, 4);
    *jedec_mfr = rx[1];
    switch (rx[1]) {
    case 0xEF: case 0xC8: case 0x85: case 0x68: case 0x5E:   /* Winbond GigaDevice Puya Boya Zbit */
        break;
    case 0x20:   /* XMC; Micron uses 20h too and has no SR2 QE bit */
        if (rx[2] != 0x40 && rx[2] != 0x70) return -1;
        break;
    default:     /* e.g. Macronix: 35h means "enter QPI" there */
        return -1;
    }
    tx[0] = 0x35; tx[1] = 0;
    flash_do_cmd(tx, rx, 2);
    if (rx[1] & 0x02) return 0;
    return flash_config_init() ? 1 : -2;
}

/* ---- the clock ---- */

static uint32_t peri_div;

bool frank_clocks_init(void) {
    return frank_clocks_apply(FRANK_CPU_MHZ, FRANK_VREG, FRANK_FLASH_MHZ, FRANK_FLASH_RXDELAY);
}

bool frank_clocks_apply(unsigned cpu_mhz, unsigned vreg, unsigned flash_mhz, int flash_rxdelay) {
    static bool first = true;
    if (first) {   /* the boot ROM's / boot2's flash timing, the base for the rules */
        first = false;
        boot_m0_timing = qmi_hw->m[0].timing;
        boot_m0_hz = clock_get_hz(clk_sys);
    }

    /* QE before anything raises a clock (on QE=0 flash both fail), once a boot */
    static bool qe_done;
    if (!qe_done) {
        qe_done = true;
        uint8_t mfr;
        uint32_t irq = save_and_disable_interrupts();
        int qe = frank_flash_ensure_qe(&mfr);
        restore_interrupts(irq);
        if (qe == 1)   /* xrip's writer leaves quad mode at divider 12: the new base */
            boot_m0_timing = qmi_hw->m[0].timing;
    }

    vreg_disable_voltage_limit();
    vreg_set_voltage((enum vreg_voltage)vreg);
    busy_wait_ms(10);

    /* set_sys_clock_khz runs clk_sys from the USB PLL (48 MHz) while it sets the
     * system PLL up: in between, a flash timing right for both 48 and 504 MHz
     * (/5, read delay 3). FRANK Next's final /4 delay 5 misreads at 48 MHz
     * (the delay is over half the slower clock's cycle), which faulted the boot
     * (frank-shadowwarrior). */
    flash_write_timing(5u, 3u);
    bool ok = set_sys_clock_khz(cpu_mhz * 1000u, false);
    if (!ok) set_sys_clock_khz(252u * 1000u, true);
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    frank_flash_set_timing(sys_hz, flash_mhz, flash_rxdelay);

    /* clk_peri from clk_sys, at most 150 MHz: 126 MHz from 252/378/504, so the
     * SD card's SPI is not stuck at the USB PLL's 48 MHz / 2 */
    peri_div = (sys_hz + 150000000u - 1u) / 150000000u;
    clock_configure_int_divider(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz, peri_div);
    frank_console_reclock();
    return ok;
}

void frank_console_reclock(void) {
#if defined(LIB_PICO_STDIO_UART) && defined(PICO_DEFAULT_UART)
    uart_set_baudrate(PICO_DEFAULT_UART ? uart1 : uart0, PICO_DEFAULT_UART_BAUD_RATE);
#endif
}

bool frank_clocks_set_tmds(uint32_t bit_khz) {
    /* HSTX sends two bits per clk_hstx cycle */
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const uint32_t hstx_hz = bit_khz * 500u;
    if (!hstx_hz || sys_hz % hstx_hz) return false;
    return clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz, hstx_hz);
}

void __no_inline_not_in_flash_func(frank_clocks_for_reboot)(void) {
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (!(clocks_hw->clk[clk_sys].selected & 1u)) __asm__ volatile("nop");
    if (boot_m0_timing) qmi_hw->m[0].timing = boot_m0_timing;
    __asm__ volatile("dmb" ::: "memory");
    for (volatile int i = 0; i < 100; i++) __asm__ volatile("nop");
    vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
    for (volatile int i = 0; i < 20000; i++) __asm__ volatile("nop");
}

static unsigned vreg_mv(void) {
    static const uint16_t high[] = {1350, 1400, 1500, 1600, 1650, 1700, 1800, 1900,
                                    2000, 2350, 2500, 2650, 2800, 3000, 3150, 3300};
    unsigned v = (unsigned)vreg_get_voltage();
    return v < 16 ? 550 + 50 * v : high[v - 16];
}

void frank_clocks_report(void) {
    const uint32_t m0 = qmi_hw->m[0].timing, m1 = qmi_hw->m[1].timing;
    printf("clk_sys %lu kHz (measured %lu), clk_peri %lu kHz, clk_hstx %lu kHz\n",
           (unsigned long)(clock_get_hz(clk_sys) / 1000u),
           (unsigned long)frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS),
           (unsigned long)(clock_get_hz(clk_peri) / 1000u),
           (unsigned long)(clock_get_hz(clk_hstx) / 1000u));
    printf("flash /%lu rxdelay %lu (timing %08lx, at boot %08lx), psram /%lu rxdelay %lu, vreg %u mV\n",
           (unsigned long)((m0 & QMI_M0_TIMING_CLKDIV_BITS) >> QMI_M0_TIMING_CLKDIV_LSB),
           (unsigned long)((m0 & QMI_M0_TIMING_RXDELAY_BITS) >> QMI_M0_TIMING_RXDELAY_LSB),
           (unsigned long)m0, (unsigned long)frank_boot_qmi_seen,
           (unsigned long)((m1 & QMI_M1_TIMING_CLKDIV_BITS) >> QMI_M1_TIMING_CLKDIV_LSB),
           (unsigned long)((m1 & QMI_M1_TIMING_RXDELAY_BITS) >> QMI_M1_TIMING_RXDELAY_LSB),
           vreg_mv());
}

/*
 * frank-sdk — QSPI PSRAM (APS6404L, ESP-PSRAM64H) on XIP M1.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * From frank-dos drivers/psram (reset + ID, size detection, the clock
 * fall-back and cap, interrupts off in direct mode), with:
 *   - FRANK_PSRAM_RXDELAY, the per-board read delay (frank-war2/quake2 on M2:
 *     168 MHz reads right with 5..7 and fails with the derived 4);
 *   - FRANK_PSRAM_COOLDOWN (frank-diablo used 3; the rest of the fleet 1);
 *   - the reset to SPI mode before the QPI enable (frank-shadowwarrior): a
 *     warm reset leaves the part in QPI mode, and the SPI-mode QPI enable is
 *     then a garbled command.
 */
#include "frank/psram.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#ifndef FRANK_PSRAM_MHZ
#define FRANK_PSRAM_MHZ 133
#endif
#ifndef FRANK_PSRAM_COOLDOWN
#define FRANK_PSRAM_COOLDOWN 1   /* x64 clk_sys the CS stays low for a sequential access (2 bits) */
#endif

static uint8_t psram_id[2];
static unsigned freq_cap_mhz;
static size_t detected;
static int active_cs = -1;   /* the CS frank_psram_init_cs() set up */

unsigned frank_psram_cs(void) {
    if (active_cs >= 0) return (unsigned)active_cs;
    const bool rp2350a = *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1u;
#ifdef FRANK_PSRAM_CS_RP2350A
    if (rp2350a) return FRANK_PSRAM_CS_RP2350A;
#endif
#ifdef FRANK_PSRAM_CS_RP2350B
    if (!rp2350a) return FRANK_PSRAM_CS_RP2350B;   /* an RP2350B module in an RP2350A socket board */
#endif
    (void)rp2350a;
#ifdef FRANK_PSRAM_CS
    return FRANK_PSRAM_CS;
#else
    return 0xFFu;   /* a run-time board: no PSRAM until frank_psram_init_cs() */
#endif
}

/* One direct-mode transfer with CS1 held for its whole length. In quad mode
 * only the command byte is sent (to leave QPI mode). */
static void __no_inline_not_in_flash_func(direct_xfer)(const uint8_t *tx, uint8_t *rx, int n, bool quad) {
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
    for (int i = 0; i < n; i++) {
        uint32_t w = tx[i];
        if (quad)
            w |= QMI_DIRECT_TX_OE_BITS | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB;
        qmi_hw->direct_tx = w;
        while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
        uint8_t b = (uint8_t)qmi_hw->direct_rx;
        if (rx) rx[i] = b;
    }
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_ASSERT_CS1N_BITS);
}

/* exit QPI (F5h on four lines), reset enable, reset. Direct mode must be on. */
static void __no_inline_not_in_flash_func(qpi_exit_and_reset)(void) {
    uint8_t cmd = 0xF5;
    direct_xfer(&cmd, NULL, 1, true);
    cmd = 0x66;
    direct_xfer(&cmd, NULL, 1, false);
    cmd = 0x99;
    direct_xfer(&cmd, NULL, 1, false);
    for (volatile int i = 0; i < 2000; i++);   /* tRST is 50 ns */
}

void __no_inline_not_in_flash_func(frank_psram_reset_to_spi)(void) {
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    qpi_exit_and_reset();
    qmi_hw->direct_csr = 0;
}

/* Only at boot: a reset does not promise to keep the contents. The flash is
 * unreadable while direct mode is on, so interrupts are off (the USB stdio
 * IRQ ran from flash here once and locked the core up) and the command bytes
 * are filled one by one (an initialiser would be read from .rodata in flash). */
static void __no_inline_not_in_flash_func(reset_and_read_id)(unsigned cs) {
    gpio_set_function(cs, GPIO_FUNC_XIP_CS1);
    uint32_t irq = save_and_disable_interrupts();
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    qpi_exit_and_reset();
    uint8_t tx[6], rx[6];
    tx[0] = 0x9F; tx[1] = 0; tx[2] = 0; tx[3] = 0; tx[4] = 0xFF; tx[5] = 0xFF;
    direct_xfer(tx, rx, 6, false);
    psram_id[0] = rx[4];
    psram_id[1] = rx[5];
    qmi_hw->direct_csr = 0;
    restore_interrupts(irq);
}

static void __no_inline_not_in_flash_func(setup_qpi)(unsigned cs, unsigned mhz) {
    const int clock_hz = (int)clock_get_hz(clk_sys);
    if (freq_cap_mhz && mhz > freq_cap_mhz) mhz = freq_cap_mhz;
    gpio_set_function(cs, GPIO_FUNC_XIP_CS1);
    uint32_t irq = save_and_disable_interrupts();

    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35;   /* QPI enable */
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS);

    const int max_hz = (int)mhz * 1000000;
    int divisor = (clock_hz + max_hz - 1) / max_hz;
    if (divisor == 1 && clock_hz > 100000000) divisor = 2;
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) rxdelay += 1;
#if defined(FRANK_PSRAM_RXDELAY) && !FRANK_RUNTIME_BOARD
    if (!freq_cap_mhz) rxdelay = FRANK_PSRAM_RXDELAY;   /* tuned for the board's own clock */
#endif
    if (rxdelay > 7) rxdelay = 7;                        /* a 3-bit field */
    const int period_fs = (int)(1000000000000000ll / clock_hz);
    const int max_select = (125 * 1000000) / period_fs;   /* tCEM 8 us, in 64-cycle units */
    const int min_deselect = (18 * 1000000 + (period_fs - 1)) / period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = FRANK_PSRAM_COOLDOWN << QMI_M1_TIMING_COOLDOWN_LSB |
                          QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB |
                          max_select << QMI_M1_TIMING_MAX_SELECT_LSB |
                          min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB |
                          rxdelay << QMI_M1_TIMING_RXDELAY_LSB |
                          divisor << QMI_M1_TIMING_CLKDIV_LSB;
    qmi_hw->m[1].rfmt = QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB |
                        QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_RFMT_ADDR_WIDTH_LSB |
                        QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB |
                        QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_RFMT_DUMMY_WIDTH_LSB |
                        QMI_M0_RFMT_DATA_WIDTH_VALUE_Q << QMI_M0_RFMT_DATA_WIDTH_LSB |
                        QMI_M0_RFMT_PREFIX_LEN_VALUE_8 << QMI_M0_RFMT_PREFIX_LEN_LSB |
                        6 << QMI_M0_RFMT_DUMMY_LEN_LSB;
    qmi_hw->m[1].rcmd = 0xEB;   /* fast read quad */
    qmi_hw->m[1].wfmt = QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB |
                        QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q << QMI_M0_WFMT_ADDR_WIDTH_LSB |
                        QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB |
                        QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q << QMI_M0_WFMT_DUMMY_WIDTH_LSB |
                        QMI_M0_WFMT_DATA_WIDTH_VALUE_Q << QMI_M0_WFMT_DATA_WIDTH_LSB |
                        QMI_M0_WFMT_PREFIX_LEN_VALUE_8 << QMI_M0_WFMT_PREFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = 0x38;   /* quad write */
    qmi_hw->direct_csr = 0;
    restore_interrupts(irq);
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);
}

void frank_psram_retime(unsigned max_mhz) {
    setup_qpi(frank_psram_cs(), max_mhz);
}

/* Smaller parts alias at their capacity boundary: 1/2/4/8 MiB probes tell
 * 1..16 MiB apart. Through the uncached window: cached lines would make
 * aliased addresses look independent. */
static size_t detect_size(void) {
    volatile uint32_t *base = (volatile uint32_t *)FRANK_PSRAM_NOCACHE;
    const uint32_t m0 = 0x13579BDFu, m1 = 0x2468ACE0u;
    uint32_t old0 = base[0];
    base[0] = m0;
    __dmb();
    if (base[0] != m0) {
        base[0] = old0;
        return 0;
    }
    for (unsigned sh = 20; sh <= 23; sh++) {
        volatile uint32_t *probe = (volatile uint32_t *)(FRANK_PSRAM_NOCACHE + (1u << sh));
        uint32_t oldp = probe[0];
        base[0] = m0;
        __dmb();
        probe[0] = m1;
        __dmb();
        if (base[0] == m1) {
            base[0] = old0;
            return 1u << sh;
        }
        probe[0] = oldp;
    }
    base[0] = old0;
    return 16u << 20;
}

extern uint8_t __psram_data_start__[], __psram_data_end__[], __psram_data_load__[];
extern uint8_t __psram_bss_start__[], __psram_bss_end__[];

size_t frank_psram_init(void) {
    return frank_psram_init_cs(frank_psram_cs(), FRANK_PSRAM_MHZ);
}

size_t frank_psram_init_cs(unsigned cs, unsigned max_mhz) {
    if (cs >= 48u) return detected = 0;
    active_cs = (int)cs;
    freq_cap_mhz = 0;
    reset_and_read_id(cs);
    setup_qpi(cs, max_mhz);
    detected = detect_size();
    static const unsigned fallback[] = {84, 66, 33};
    for (unsigned i = 0; detected < (1u << 20) && i < sizeof(fallback) / sizeof(fallback[0]); i++) {
        if (fallback[i] >= max_mhz) continue;
        freq_cap_mhz = fallback[i];
        setup_qpi(cs, fallback[i]);
        detected = detect_size();
    }
    if (detected < (1u << 20)) {
        freq_cap_mhz = 0;
        return detected = 0;
    }
    size_t n = (size_t)(__psram_data_end__ - __psram_data_start__);
    if (n) memcpy(__psram_data_start__, __psram_data_load__, n);
    n = (size_t)(__psram_bss_end__ - __psram_bss_start__);
    if (n) memset(__psram_bss_start__, 0, n);
    return detected;
}

size_t frank_psram_size(void) { return detected; }
unsigned frank_psram_mhz_cap(void) { return freq_cap_mhz; }

void frank_psram_id(uint8_t *mfid, uint8_t *kgd) {
    *mfid = psram_id[0];
    *kgd = psram_id[1];
}

bool frank_psram_test(void) {
    if (!detected) return false;
    volatile uint32_t *p = (volatile uint32_t *)FRANK_PSRAM_BASE;
    const size_t words = detected / 4;
    static const uint32_t offs[] = {0, 1024, 256 * 1024};
    for (unsigned i = 0; i < 4; i++) {
        size_t w = i < 3 ? offs[i] : words - 1;
        if (w >= words) continue;
        uint32_t old = p[w];
        p[w] = 0xDEADBEEFu ^ (uint32_t)w;
        __dmb();
        bool ok = p[w] == (0xDEADBEEFu ^ (uint32_t)w);
        p[w] = old;
        if (!ok) return false;
    }
    return true;
}

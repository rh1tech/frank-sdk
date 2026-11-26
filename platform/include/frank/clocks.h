/*
 * frank-sdk — clocks, core voltage and flash timing.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The flash's Quad Enable bit set, once in the chip's life (Puya parts on
 * some Waveshare PiZero batches ship with QE=0: IO2/IO3 are then WP#/HOLD#,
 * and an overclocked board with PSRAM on the same bus locks up). Before the
 * overclock, before PSRAM, before core 1. Returns -1 vendor not handled,
 * 0 already set, 1 set now, -2 the write did not take. */
int frank_flash_ensure_qe(uint8_t *jedec_mfr);

/* Voltage, flash timing, clk_sys = FRANK_CPU_MHZ, clk_peri about 126 MHz
 * from it. False if the PLL cannot make the clock (then 252 MHz). */
bool frank_clocks_init(void);

/* The flash timing for clk_sys = sys_hz, flash at most max_mhz, read delay
 * rxdelay (< 0: the rule). From RAM. */
void frank_flash_set_timing(uint32_t sys_hz, unsigned max_mhz, int rxdelay);

/* clk_hstx for a TMDS bit rate (kbit/s); it must divide clk_sys exactly. */
bool frank_clocks_set_tmds(uint32_t bit_khz);

/* Before a software restart: clk_sys back on clk_ref, the boot ROM's flash
 * timing, the power-on voltage. From RAM. */
void frank_clocks_for_reboot(void);

/* The UART's baud divisor again, after clk_peri moved */
void frank_console_reclock(void);

void frank_clocks_report(void);

#ifdef __cplusplus
}
#endif

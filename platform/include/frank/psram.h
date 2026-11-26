/*
 * frank-sdk — QSPI PSRAM on the QMI's second chip select (XIP M1).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRANK_PSRAM_BASE   0x11000000u   /* cached */
#define FRANK_PSRAM_NOCACHE 0x15000000u  /* the same memory, uncached */

/* Static PSRAM data: these go to .psram_bss / .psram_data (zeroed / copied by
 * frank_psram_init, before any C++ constructor). */
#ifdef FRANK_HOST   /* the host build: plain memory */
#define FRANK_PSRAM_BSS
#define FRANK_PSRAM_DATA
#define FRANK_SRAM_BSS
#else
#define FRANK_PSRAM_BSS  __attribute__((section(".psram_bss")))
#define FRANK_PSRAM_DATA __attribute__((section(".psram_data")))
/* keep a variable in SRAM when a firmware moves .bss to PSRAM */
#define FRANK_SRAM_BSS   __attribute__((section(".sram_bss")))
#endif

/* The chip select for this chip's package: FRANK_PSRAM_CS, or the board's
 * FRANK_PSRAM_CS_RP2350A when an RP2350A sits in a socketed board. */
unsigned frank_psram_cs(void);

/* Reset the part, read its ID, set it up at the board's clock, find its size,
 * copy .psram_data and zero .psram_bss. When the part does not answer at the
 * board's clock it steps down (84, 66, 33 MHz) and keeps that as a cap for
 * every later frank_psram_retime(). Returns the size found (0: no PSRAM). */
size_t frank_psram_init(void);

/* The same on a chip select and clock found at run time (FRANK_RUNTIME_BOARD:
 * the firmware has identified the board). Then frank_heap_add_psram() to let
 * malloc use it. Nothing may touch .psram_bss / .psram_data before this. */
size_t frank_psram_init_cs(unsigned cs, unsigned max_mhz);

/* After clk_sys changed: the timing for the new clock, at most max_mhz
 * (and at most the cap). From RAM, with interrupts off inside. */
void frank_psram_retime(unsigned max_mhz);

/* Back to the power-on SPI mode: before every software reboot, with the other
 * core stopped and interrupts off. A QPI-mode PSRAM on the shared QSPI bus
 * garbles the boot ROM's flash probe after a warm reset: the chip then hangs
 * in the ROM until a power cycle. */
void frank_psram_reset_to_spi(void);

size_t frank_psram_size(void);
unsigned frank_psram_mhz_cap(void);         /* 0: none */
void frank_psram_id(uint8_t *mfid, uint8_t *kgd);   /* APS6404: 0D, 5D = good die */
bool frank_psram_test(void);                /* a few patterns through the cache */

#ifdef __cplusplus
}
#endif

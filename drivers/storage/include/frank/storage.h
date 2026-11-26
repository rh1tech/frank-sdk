/*
 * frank-sdk — the microSD card and FatFs.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * The driver follows the board: SPI (hardware SPI on the card's pins, else PIO
 * SPI) or 4-bit SDIO with the SPI driver as its fall-back (FRANK Next). Both
 * check every block's CRC and retry (frank-castle). DMA never writes PSRAM:
 * PSRAM buffers go through an SRAM bounce buffer (frank-react).
 * FatFs R0.15 with exFAT and long names; ffconf.h settings can be overridden
 * with compile definitions (FF_CODE_PAGE, FF_LFN_UNICODE, FF_FS_LOCK, ...).
 */
#pragma once

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the card at "" (0:), retrying with growing pauses (a card just
 * inserted, a slow one). Returns the FatFs result, FR_OK = 0. Adds the "sd"
 * console command. */
FRESULT frank_sd_mount(void);
const char *frank_sd_driver(void);   /* "sdio" or "spi" */

#ifdef __cplusplus
}
#endif

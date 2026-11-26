/*
 * frank-sdk — mount, timestamps, the "sd" console command.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "frank/storage.h"

#include <stdio.h>
#include <stdlib.h>

#include "diskio.h"
#include "frank/platform.h"
#include "pico/stdlib.h"

static FATFS fs;

/* FAT timestamps: 2026-01-01 until a firmware with a clock (the Next's DS3231)
 * supplies its own get_fattime() */
__attribute__((weak)) DWORD get_fattime(void) {
    return ((DWORD)(2026 - 1980) << 25) | (1u << 21) | (1u << 16);
}

const char *frank_sd_driver(void) {
#if FRANK_SD_MODE_SDIO
    return "sdio";
#else
    return "spi";
#endif
}

static void cmd_sd(const char *args) {
    if (args[0] == 'b') {
#if FRANK_SD_MODE_SDIO
        extern void sd_bench(void);
        sd_bench();
#else
        printf("sd bench: SDIO only\n");
#endif
        return;
    }
    DWORD free_clusters;
    FATFS *f;
    FRESULT r = f_getfree("", &free_clusters, &f);
    if (r != FR_OK) {
        printf("sd (%s): not mounted (%d)\n", frank_sd_driver(), r);
        return;
    }
    const unsigned long long total = (unsigned long long)(f->n_fatent - 2) * f->csize * 512;
    const unsigned long long freeb = (unsigned long long)free_clusters * f->csize * 512;
    printf("sd (%s): %s, %llu MB, %llu MB free\n", frank_sd_driver(),
           f->fs_type == FS_EXFAT ? "exFAT" : f->fs_type == FS_FAT32 ? "FAT32" : "FAT", total >> 20, freeb >> 20);
    FFDIR d;
    FILINFO fi;
    if (f_opendir(&d, "/") == FR_OK) {
        int n = 0;
        while (f_readdir(&d, &fi) == FR_OK && fi.fname[0] && n++ < 20)
            printf("  %s%s %lu\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "", (unsigned long)fi.fsize);
        f_closedir(&d);
    }
}

FRESULT frank_sd_mount(void) {
    FRESULT fr = FR_NOT_READY;
    for (int attempt = 0; attempt < 6 && fr != FR_OK; attempt++) {
        fr = f_mount(&fs, "", 1);
        if (fr != FR_OK) {
            frank_watchdog_feed();
            sleep_ms(50 << attempt);
        }
    }
    printf("SD (%s): mount %s (%d)\n", frank_sd_driver(), fr == FR_OK ? "ok" : "FAILED", fr);
    frank_console_add("sd", cmd_sd, "the card: size, free space, the root directory; 'sd bench' (SDIO)");
    return fr;
}

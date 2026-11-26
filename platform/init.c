/*
 * frank-sdk — the boot: everything before main() and frank_init().
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * The early part runs as the first C constructor (priority 101; the SDK's own
 * runtime init is earlier), so the PSRAM is up before any C++ constructor
 * writes a global in .psram_bss (frank-diablo). Not a .preinit_array entry:
 * SDK 2.2 runs those on core 1 as well when it launches, and a second PSRAM
 * init under core 0 bus-faults (frank-blood).
 */
#include "frank/platform.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#if FRANK_CONSOLE_USB && !USB_CDC_CONSOLE
#include "pico/stdio_usb.h"
#endif
#if USB_CDC_CONSOLE
/* the dual-role console (drivers/input/usbhid/usb_console.c): CDC on the
 * native port while the USB host runs on PIO-USB */
extern void usb_console_init(void);
extern bool usb_console_connected(void);
#define stdio_usb_connected usb_console_connected
#endif

extern void frank_boot_guard(void);
extern void frank_report_boot(void);
extern void frank_safe_mode_if_hung(void);
extern void frank_stall_watch_start(void);
extern void frank_heap_init(size_t psram_bytes);
extern void frank_console_builtins(void);

static bool early_done, clock_ok;

#if FRANK_RUNTIME_BOARD
/* in the image's symbols, for frank check: a run-time board, so the boot guard
 * is optional (FRANK_RUNTIME_BOOT_GUARD) */
const char frank_runtime_board[] = "found at run time";
#endif
static size_t psram_bytes;

/* A run-time board (FRANK_RUNTIME_BOARD: the firmware identifies the board
 * itself, as frank-test does) gets nothing board-specific here: no clock, no
 * PSRAM chip select driven, no watchdog unless asked for
 * (FRANK_RUNTIME_BOOT_GUARD). Its detection reads idle pins and its overclock
 * campaign picks the clock; frank_clocks_apply(), frank_psram_init_cs() and
 * frank_heap_add_psram() are its to call. */
static void __attribute__((constructor(101))) frank_early_init(void) {
    if (early_done || get_core_num() != 0) return;
    early_done = true;
#if FRANK_RUNTIME_BOARD
#if FRANK_RUNTIME_BOOT_GUARD
    frank_boot_guard();
#endif
    clock_ok = true;
#else
    frank_boot_guard();
    clock_ok = frank_clocks_init();
#ifdef FRANK_HAS_PSRAM
    psram_bytes = frank_psram_init();
#endif
#endif
    frank_heap_init(psram_bytes);
}

void frank_heap_note_psram(size_t bytes) { psram_bytes = bytes; }

void frank_init(const char *name, const char *version) {
    frank_early_init();
#if USB_CDC_CONSOLE
    usb_console_init();
#endif
    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 0);
#if FRANK_CONSOLE_USB
    /* up to 3 s for the host to open the CDC port, so the boot log is seen */
    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) {
        watchdog_update();
        sleep_ms(100);
    }
#endif
    printf("\n==== %s %s on %s (%s console) ====\n", name, version, FRANK_BOARD_NAME,
           FRANK_CONSOLE_USB ? "USB CDC" : "UART");
    frank_report_boot();
    if (!clock_ok) printf("boot: %d MHz unreachable, running at 252 MHz\n", FRANK_CPU_MHZ);
    frank_clocks_report();
#if FRANK_RUNTIME_BOARD
    printf("board: %s (%s build)\n", frank_runtime_board, FRANK_BOARD_ID);
#elif defined(FRANK_HAS_PSRAM)
    if (psram_bytes) {
        uint8_t mf, kgd;
        frank_psram_id(&mf, &kgd);
        printf("PSRAM: %u KB on CS%u, ID %02X %02X%s\n", (unsigned)(psram_bytes / 1024), frank_psram_cs(),
               mf, kgd, kgd == 0x5D ? "" : " (expected 5D)");
        if (frank_psram_mhz_cap())
            printf("WARNING: PSRAM only works at %u MHz or below on this board\n", frank_psram_mhz_cap());
    } else {
        printf("WARNING: no PSRAM found on CS%u\n", frank_psram_cs());
    }
#endif
    frank_heap_report();
    frank_safe_mode_if_hung();
    frank_console_builtins();
    frank_stall_watch_start();
}

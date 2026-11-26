/*
 * frank-sdk example: runtime — the board comes from the fleet table at run time.
 *
 * Built in run-time mode the platform does nothing board-specific at boot: no
 * clock raised, no PSRAM chip select driven. This is where a firmware like
 * frank-test identifies its board (pin signatures; not in this example); here
 * the console names it:
 *   ~C fleet~         the descriptors this package can be
 *   ~C pins~          GPIO functions of every PSRAM chip select in the fleet:
 *                     all still NULL until a board is chosen
 *   ~C board SLUG~    that descriptor: clocks, then PSRAM on its CS, into the heap
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_desc.h"
#include "frank/platform.h"
#include "hardware/gpio.h"
#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"

static frank_mcu_class_t package(void) {
    return (*((io_ro_32 *)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET)) & 1u) ? FRANK_MCU_RP2350A : FRANK_MCU_RP2350B;
}

static void cmd_fleet(const char *a) {
    (void)a;
    const frank_board_desc_t *d[32];
    unsigned n = frank_boards_for_mcu(package(), d, 32);
    printf("%u descriptors for %s:\n", n, frank_mcu_class_name(package()));
    for (unsigned i = 0; i < n; i++)
        printf("  %-12s %-28s %s, PSRAM CS %d\n", d[i]->slug, d[i]->name, frank_role_name(d[i]->role),
               d[i]->pins.psram_cs);
}

static void cmd_pins(const char *a) {
    (void)a;
    printf("PSRAM chip selects in the fleet, as the boot left them:\n");
    bool seen[48] = {0};
    for (unsigned i = 0; i < frank_board_table_len; i++) {
        int cs = frank_board_table[i].pins.psram_cs;
        if (cs < 0 || cs >= (package() == FRANK_MCU_RP2350A ? 30 : 48) || seen[cs]) continue;
        seen[cs] = true;
        printf("  GPIO%-2d function %d (%s)\n", cs, gpio_get_function((uint)cs),
               gpio_get_function((uint)cs) == GPIO_FUNC_NULL ? "untouched" : "IN USE");
    }
}

static void cmd_board(const char *slug) {
    frank_board_id_t id = frank_board_from_slug(slug);
    const frank_board_desc_t *d = NULL;
    for (unsigned i = 0; i < frank_board_table_len; i++)
        if (frank_board_table[i].id == id && !strcmp(frank_board_table[i].slug, slug)) d = &frank_board_table[i];
    if (!d) {
        printf("no fleet board '%s' (~C fleet~)\n", slug);
        return;
    }
    printf("board: %s\n", d->name);
    /* what frank-test's campaign would choose; here the fleet's stock clock */
    frank_clocks_apply(252, VREG_VOLTAGE_DEFAULT, 66, -1);
    if (d->pins.psram_cs >= 0) {
        size_t n = frank_psram_init_cs((unsigned)d->pins.psram_cs, 133);
        uint8_t mf, kgd;
        frank_psram_id(&mf, &kgd);
        printf("PSRAM on CS%d: %u KB, ID %02X %02X\n", d->pins.psram_cs, (unsigned)(n / 1024), mf, kgd);
        if (n) frank_heap_add_psram(n);
    } else {
        printf("no QMI PSRAM on this board\n");
    }
    frank_heap_report();
}

int main(void) {
    frank_init("runtime", FRANK_SDK_VERSION);
    frank_console_add("fleet", cmd_fleet, "the descriptors this package can be");
    frank_console_add("pins", cmd_pins, "the fleet's PSRAM chip selects: untouched until a board is chosen");
    frank_console_add("board", cmd_board, "SLUG: take that descriptor (clocks, PSRAM, heap)");
    frank_watchdog_start(3000);
    frank_boot_ok();
    for (uint32_t n = 0;; n++) {
        frank_crumb(1, n);
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(NULL, NULL);
    }
}

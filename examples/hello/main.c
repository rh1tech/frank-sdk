/*
 * frank-sdk example: hello — the platform alone.
 *
 * Boots on every board with its clocks, PSRAM and heap, prints the boot
 * report, blinks the LED where there is a plain one, and answers the console:
 * ~C help~ lists the commands, ~C hello NAME~ is this example's own, ~BOOT~
 * reboots into BOOTSEL for the next flash.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>

#include "frank/platform.h"
#include "pico/stdlib.h"

static void cmd_hello(const char *args) {
    printf("hello, %s\n", *args ? args : "world");
}

/* hang on purpose: the watchdog ends it, the next boot's safe mode says where,
 * then BOOTSEL (the recovery path, tested without a hand on the board) */
static void __attribute__((noinline)) cmd_hang(const char *args) {
    (void)args;
    printf("hanging at stage 42\n");
    frank_crumb(42, 0xBADC0DE);
    for (volatile uint32_t spin = 0;; spin++) tight_loop_contents();
}

/* "~C" lines that are not commands of the table */
static void on_cmd(const char *line) {
    printf("unknown command: %s (try ~C help~)\n", line);
}

int main(void) {
    frank_init("hello", FRANK_SDK_VERSION);
    frank_console_add("hello", cmd_hello, "[name] say hello");
    frank_console_add("hang", cmd_hang, "hang on purpose: watchdog, safe mode, BOOTSEL");
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    frank_watchdog_start(2000);
    frank_boot_ok();

    uint32_t last = 0;
    for (unsigned n = 0;; n++) {
        frank_crumb(1, n);
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(NULL, on_cmd);
        frank_prof_dump_if_done();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last >= 1000) {
            last = now;
#ifdef PICO_DEFAULT_LED_PIN
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
#endif
            if (frank_verbose) printf("alive %lu s\n", (unsigned long)(now / 1000));
        }
        sleep_ms(1);
    }
}

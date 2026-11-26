/*
 * frank-sdk example: dual — the master and the slave of FRANK Next / Core 2.
 *
 * The slave answers HELLO and ECHO (and REBOOT: ~C slaveboot~ on the master
 * puts it into BOOTSEL for its own flash). The master says hello, then
 * ~C echo [bytes]~ sends the soak pattern through and back and checks it.
 * One source for both halves: FRANK_ROLE_* comes from the board file.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "pico.h"
#include "frank/link.h"
#include "frank/platform.h"
#include "pico/stdlib.h"

static link_session_t *link;

#if FRANK_ROLE_MASTER
static void cmd_hello(const char *a) {
    (void)a;
    link_node_info_t ni;
    if (frank_link_hello(link, 3000, &ni))
        printf("link: slave '%s', %lu MHz, PSRAM %lu KB, up %lu ms\n", ni.build,
               (unsigned long)(ni.sys_clk_hz / 1000000), (unsigned long)(ni.psram_bytes / 1024),
               (unsigned long)ni.uptime_ms);
    else
        printf("link: no answer from the slave\n");
}

static void cmd_echo(const char *a) {
    uint32_t n = (uint32_t)strtoul(a, NULL, 0), kbps = 0, ok = 0, runs = 20;
    if (!n) n = 16384;
    for (uint32_t i = 0; i < runs; i++) {
        ok += frank_link_echo_test(link, n, &kbps);
        frank_watchdog_feed();
    }
    printf("link echo: %lu/%lu intact, %lu bytes each way, %lu KB/s\n", (unsigned long)ok,
           (unsigned long)runs, (unsigned long)n, (unsigned long)kbps);
}

static void cmd_slaveboot(const char *a) {
    (void)a;
    link_m_send_ctrl(link, LINK_OP_REBOOT, 1, 0, NULL, 0);
    printf("link: slave told to enter BOOTSEL\n");
}
#endif

int main(void) {
    frank_init("dual", FRANK_SDK_VERSION);
    link = frank_link_init();
#if FRANK_ROLE_MASTER
    frank_console_add("hello", cmd_hello, "ask the slave who it is");
    frank_console_add("echo", cmd_echo, "[bytes] the soak pattern there and back, 20 times");
    frank_console_add("slaveboot", cmd_slaveboot, "the slave into BOOTSEL");
#endif
    frank_watchdog_start(3000);
    frank_boot_ok();
    for (uint32_t n = 0;; n++) {
        frank_crumb(1, n);
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(NULL, NULL);
#if !FRANK_ROLE_MASTER
        frank_link_serve(link, 10000, "dual " FRANK_SDK_VERSION);
#endif
    }
}

/*
 * frank-sdk — the master-slave link on the board's pins (frank/link.h).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "frank/link.h"

#include <stdio.h>
#include <string.h>

#include "frank/platform.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#ifndef FRANK_HAS_LINK
#error "frank-sdk link: this board has no link (boards/*.yaml link)"
#endif

static link_t link;
static link_session_t session;
/* a board file names a bus's CLK and VALID only where they are not
 * data+8 and data+9, and reversed only for D7..D0 upwards (FRANK Next
 * rev B's slave) */
#ifndef FRANK_LINK_A_CLK
#define FRANK_LINK_A_CLK (FRANK_LINK_A_DATA + 8)
#endif
#ifndef FRANK_LINK_A_VALID
#define FRANK_LINK_A_VALID (FRANK_LINK_A_DATA + 9)
#endif
#ifndef FRANK_LINK_A_REVERSED
#define FRANK_LINK_A_REVERSED 0
#endif
#ifndef FRANK_LINK_B_CLK
#define FRANK_LINK_B_CLK (FRANK_LINK_B_DATA + 8)
#endif
#ifndef FRANK_LINK_B_VALID
#define FRANK_LINK_B_VALID (FRANK_LINK_B_DATA + 9)
#endif
#ifndef FRANK_LINK_B_REVERSED
#define FRANK_LINK_B_REVERSED 0
#endif

static uint8_t ctrl_tx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
static uint8_t ctrl_rx[LINK_CTRL_BYTES] __attribute__((aligned(4)));
#define ECHO_MAX (16u * 1024u)
static uint32_t bulk[ECHO_MAX / 4];

link_session_t *frank_link_init(void) {
    PIO pio = PIO_INSTANCE(FRANK_PIO_LINK);
#if FRANK_PIO0_GPIO_BASE || FRANK_PIO1_GPIO_BASE || FRANK_PIO2_GPIO_BASE
    if ((FRANK_PIO_LINK == 0 && FRANK_PIO0_GPIO_BASE) || (FRANK_PIO_LINK == 1 && FRANK_PIO1_GPIO_BASE) ||
        (FRANK_PIO_LINK == 2 && FRANK_PIO2_GPIO_BASE))
        pio_set_gpio_base(pio, 16);
#endif
    /* bus A runs master->slave, bus B back: each half sends on one */
    const link_bus_pins_t a = {FRANK_LINK_A_DATA, FRANK_LINK_A_CLK, FRANK_LINK_A_VALID, FRANK_LINK_A_REVERSED};
    const link_bus_pins_t b = {FRANK_LINK_B_DATA, FRANK_LINK_B_CLK, FRANK_LINK_B_VALID, FRANK_LINK_B_REVERSED};
#if FRANK_LINK_SIDE_MASTER
    const link_pins_t p = {a, b, FRANK_LINK_DB_OUT, FRANK_LINK_DB_IN, FRANK_LINK_FS, true};
#else
    const link_pins_t p = {b, a, FRANK_LINK_DB_OUT, FRANK_LINK_DB_IN, FRANK_LINK_FS, false};
#endif
    link_init_pins(&link, pio, &p);
    session.link = &link;
    session.ctrl_tx = ctrl_tx;
    session.ctrl_rx = ctrl_rx;
    session.seq = 0;
    session.handshake_timeout_us = 0;
    return &session;
}

bool frank_link_hello(link_session_t *s, uint32_t timeout_ms, link_node_info_t *info) {
    absolute_time_t end = make_timeout_time_ms(timeout_ms);
    while (!time_reached(end)) {
        frank_watchdog_feed();
        if (link_m_send_ctrl(s, LINK_OP_HELLO, LINK_PROTO_VER, clock_get_hz(clk_sys), NULL, 0) &&
            link_m_recv_ctrl(s) && link_rx_hdr(s)->op == LINK_OP_HELLO_ACK) {
            link_node_info_t ni;
            memcpy(&ni, s->ctrl_rx + sizeof(link_hdr_t), sizeof(ni));
            if (info) *info = ni;
            if (ni.sys_clk_hz != clock_get_hz(clk_sys)) {
                printf("link: the slave runs at %lu Hz, this half at %lu: both must match\n",
                       (unsigned long)ni.sys_clk_hz, (unsigned long)clock_get_hz(clk_sys));
                return false;
            }
            return ni.proto_ver == LINK_PROTO_VER;
        }
        link_m_resync(s);
    }
    return false;
}

uint16_t frank_link_serve(link_session_t *s, uint32_t timeout_us, const char *build) {
    const uint16_t op = link_s_wait_ctrl(s, timeout_us);
    if (!op) return 0;
    const link_hdr_t *h = link_rx_hdr(s);
    switch (op) {
    case LINK_OP_HELLO: {
        link_node_info_t ni = {0};
        ni.sys_clk_hz = clock_get_hz(clk_sys);
        ni.proto_ver = LINK_PROTO_VER;
        ni.psram_bytes = (uint32_t)frank_psram_size();
        ni.uptime_ms = to_ms_since_boot(get_absolute_time());
        strncpy(ni.build, build ? build : "", sizeof(ni.build) - 1);
        link_s_send_ctrl(s, LINK_OP_HELLO_ACK, 0, 0, &ni, sizeof(ni));
        return LINK_OP_HELLO;
    }
    case LINK_OP_ECHO: {
        uint32_t n = LINK_ALIGN4(h->arg0);
        if (n > ECHO_MAX) n = ECHO_MAX;
        bool ok = link_s_bulk_recv(s, bulk, n);
        if (ok) link_s_bulk_send(s, bulk, n);
        return LINK_OP_ECHO;
    }
    case LINK_OP_REBOOT:
        link_s_send_ctrl(s, LINK_OP_ACK, 0, 0, NULL, 0);
        busy_wait_ms(5);
        frank_reboot_clean(h->arg0 == 1);
    default:
        return op;   /* the firmware's */
    }
}

bool frank_link_echo_test(link_session_t *s, uint32_t bytes, uint32_t *kbps) {
    bytes = LINK_ALIGN4(bytes);
    if (bytes > ECHO_MAX) bytes = ECHO_MAX;
    const uint32_t seed = time_us_32();
    for (uint32_t i = 0; i < bytes / 4; i++) bulk[i] = link_soak_word(seed, i);
    const uint32_t t0 = time_us_32();
    if (!link_m_send_ctrl(s, LINK_OP_ECHO, bytes, 0, NULL, 0) || !link_m_bulk_send(s, bulk, bytes)) {
        link_m_resync(s);
        return false;
    }
    memset(bulk, 0, bytes);
    if (!link_m_bulk_recv(s, bulk, bytes)) {
        link_m_resync(s);
        return false;
    }
    const uint32_t us = time_us_32() - t0;
    if (kbps) *kbps = (uint32_t)((uint64_t)bytes * 2 * 1000 / (us + 1));
    for (uint32_t i = 0; i < bytes / 4; i++)
        if (bulk[i] != link_soak_word(seed, i)) return false;
    return true;
}

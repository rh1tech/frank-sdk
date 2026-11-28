/*
 * frank-sdk — the link between the two RP2350s of FRANK Next and Core 2.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * Two 8-bit source-synchronous buses (PIO + DMA), three doorbell/frame-sync
 * wires; control frames with a CRC, bulk data at up to clk_sys/5 bytes a
 * second. From frank-blood / frank-amiga common/link. The pins and the PIO
 * come from the board file (boards/na.yaml, nb.yaml, c2.yaml, sl.yaml: link, pio.link);
 * the link's PIO needs gpio base 16 on the master (bus B reaches GPIO39).
 * Both halves must run the same clk_sys: the slave's receive loop is timed
 * against the master's byte period (gen_boards.py checks the pair).
 *
 * The lower layers stay available: link_bus.h (raw transfers), link_session.h
 * (control frames, bulk phases, resync), link_proto.h (frame format).
 */
#pragma once

#include "link_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The link on this half's pins, and a session on it (the frame buffers are
 * the library's own). */
link_session_t *frank_link_init(void);

/* Master: HELLO until the slave answers (timeout_ms), then check its clock
 * and protocol. Fills *info when given. */
bool frank_link_hello(link_session_t *s, uint32_t timeout_ms, link_node_info_t *info);

/* Slave: answer one frame. HELLO, ECHO and REBOOT are handled here; any
 * other operation (LINK_OP_USER and up) is returned for the firmware, with the
 * frame in link_rx_hdr(s). Returns 0 when nothing came within timeout_us. */
uint16_t frank_link_serve(link_session_t *s, uint32_t timeout_us, const char *build);

/* Master: ECHO `bytes` of the soak pattern; true when it came back intact.
 * *kbps gets the round trip's rate. */
bool frank_link_echo_test(link_session_t *s, uint32_t bytes, uint32_t *kbps);

#ifdef __cplusplus
}
#endif

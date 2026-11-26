/*
 * frank-sdk — the master-slave link's control frames (FRANK Next, Core 2).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * From frank-blood / frank-amiga common/link (link_proto.h), with their game's
 * operations and constants taken out. A control frame is LINK_CTRL_BYTES: the
 * 24-byte header, the payload, zeros; bulk data follows on its own when an
 * operation says so. A firmware's own operations start at LINK_OP_USER.
 */
#ifndef LINK_PROTO_H
#define LINK_PROTO_H

#include <stdint.h>

#ifndef LINK_MAGIC
#define LINK_MAGIC      0x4B4E5246u   /* "FRNK" */
#endif
#define LINK_PROTO_VER  1u
#ifndef LINK_CTRL_BYTES
#define LINK_CTRL_BYTES 128u
#endif

enum {
    LINK_OP_HELLO     = 0x0001,   /* M->S; the slave answers HELLO_ACK + link_node_info_t */
    LINK_OP_HELLO_ACK = 0x0002,
    LINK_OP_ACK       = 0x0003,   /* arg0: 0 or an error */
    LINK_OP_ECHO      = 0x0020,   /* arg0 = bytes: a bulk sent and echoed back (bring-up, soak) */
    LINK_OP_REBOOT    = 0x0030,   /* arg0 = 1: into BOOTSEL (flash the slave over the master) */
    LINK_OP_USER      = 0x0100,   /* the firmware's own from here */
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t op;
    uint16_t proto_ver;
    uint32_t seq;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t crc;      /* CRC-32 of the whole frame with crc treated as 0 */
} link_hdr_t;

#define LINK_PAYLOAD_BYTES (LINK_CTRL_BYTES - sizeof(link_hdr_t))

/* HELLO_ACK's payload: the master refuses a slave at another clock (the link
 * drops bytes one way then) or protocol */
typedef struct __attribute__((packed)) {
    uint32_t sys_clk_hz;
    uint32_t proto_ver;
    uint32_t psram_bytes;
    uint32_t uptime_ms;
    char     build[32];
} link_node_info_t;

/* The PIO FIFOs are 32 bits wide: every bulk transfer is a multiple of 4
 * bytes, from a 4-byte aligned buffer. */
#define LINK_ALIGN4(n) (((n) + 3u) & ~3u)

uint32_t link_crc32(const void *data, uint32_t len);
void link_frame_build(void *frame, uint16_t op, uint32_t seq, uint32_t arg0, uint32_t arg1,
                      const void *payload, uint32_t payload_len);
int link_frame_check(const void *frame);

/* a deterministic soak pattern: word i of a buffer seeded with `seed` */
static inline uint32_t link_soak_word(uint32_t seed, uint32_t i) {
    uint32_t x = seed ^ (i * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

#endif /* LINK_PROTO_H */

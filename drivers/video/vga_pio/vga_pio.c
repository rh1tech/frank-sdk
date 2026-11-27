/*
 * frank-sdk — VGA 640x480@60 through PIO, from a 320x240 8-bit picture.
 *
 * The design of the Murmulator's PIO VGA (vga-nextgen, as frank-quake's
 * quakegeneric ships it), rewritten for frank/video.h:
 *   - a PIO state machine puts one byte a pixel on eight pins (B0 B1 G0 G1
 *     R0 R1 HS VS from the board's VGA base), at clk_sys / 25.2 MHz (a whole
 *     divider at 252, 378 and 504 MHz);
 *   - every line is a whole 800-byte buffer with its syncs in it: a blank
 *     line, a vertical-sync line, and two picture lines in turn;
 *   - a control DMA channel writes the next line's address into the data
 *     channel and starts it; the interrupt after it chooses the line after.
 * So the interrupt has a whole line (31.8 us) to prepare the next one. The
 * HSTX driver (drivers/video/vga) had ~280 ns: under frank-sdk's platform a
 * post came late now and then and no monitor would lock (FRANK PGA).
 *
 * Each source pixel is two output pixels, and its 16-bit palette entry gives
 * them two neighbouring levels of the 2-bit ladder (the original's dither):
 * smoother than 64 colours.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later (after vga-nextgen, GPL-2.0)
 */
#include "vga_pio.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#define LINE_BYTES   800   /* 640 visible + 16 front porch + 96 sync + 48 back porch */
#define HS_BYTES     96
#define PICTURE_AT   (HS_BYTES + 48)   /* the visible area starts after sync and back porch */
#define LINES_TOTAL  525
#define LINES_ACTIVE 480
#define VS_FIRST     490
#define VS_LAST      491

#define SYNC_IDLE 0xC0u   /* HS (bit 6) and VS (bit 7) idle high */

static uint32_t lines[4][LINE_BYTES / 4];   /* blank, vsync, picture A, picture B */
static uint32_t *line_ptr[4];               /* what the control channel reads */
static uint16_t palette[256], palette_next[256];
static volatile bool palette_pending;

static PIO pio;
static int sm = -1, dma_data = -1, dma_ctrl = -1;
static uint32_t screen_line;
volatile uint32_t vga_pio_frame_count;

static vga_pio_row_fn row_fn;
static void (*vsync_fn)(void);

static const uint16_t pio_program_instructions[] = {
    0x6008,   /* out pins, 8 */
};
static const struct pio_program pio_program = {
    .instructions = pio_program_instructions,
    .length = 1,
    .origin = -1,
};

/* 8-bit RGB to the two neighbouring ladder levels, as vga-nextgen does */
static uint16_t entry(uint8_t r8, uint8_t g8, uint8_t b8) {
    static const uint8_t conv0[] = {0, 0, 1, 2, 2, 2, 3, 3};
    static const uint8_t conv1[] = {0, 1, 1, 1, 2, 3, 3, 3};
    const uint8_t r = r8 / 42, g = g8 / 42, b = b8 / 42;   /* 0..6 */
    const uint8_t hi = (uint8_t)(conv0[r] << 4 | conv0[g] << 2 | conv0[b]);
    const uint8_t lo = (uint8_t)(conv1[r] << 4 | conv1[g] << 2 | conv1[b]);
    return (uint16_t)(((hi << 8 | lo) & 0x3F3F) | (SYNC_IDLE << 8 | SYNC_IDLE));
}

void vga_pio_set_palette(const uint8_t *rgb, int stride) {
    while (palette_pending) tight_loop_contents();
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        palette_next[i] = entry(c[0], c[1], c[2]);
    }
    palette_pending = true;
}

void vga_pio_set_row_callback(vga_pio_row_fn fn) { row_fn = fn; }
void vga_pio_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }

/* The control channel has just started the data channel on a line: choose
 * the one after it. Odd lines repeat the even one (each source row twice). */
static void __not_in_flash_func(dma_irq)(void) {
    dma_hw->ints0 = 1u << dma_ctrl;
    if (++screen_line == LINES_TOTAL) {
        screen_line = 0;
        vga_pio_frame_count++;
        if (palette_pending) {
            for (int i = 0; i < 256; i++) palette[i] = palette_next[i];
            palette_pending = false;
        }
        if (vsync_fn) vsync_fn();
    }
    const uint32_t l = screen_line;
    if (l >= LINES_ACTIVE) {
        dma_channel_set_read_addr(dma_ctrl, &line_ptr[l >= VS_FIRST && l <= VS_LAST ? 1 : 0], false);
        return;
    }
    if (l & 1) return;   /* the same buffer again */
    const uint32_t row = l >> 1;
    const int b = 2 + (int)(row & 1);   /* the other one is on the wire */
    uint16_t *out = (uint16_t *)((uint8_t *)lines[b] + PICTURE_AT);
    const uint8_t *src = row_fn ? row_fn(row) : NULL;
    if (src) {
        for (int x = 0; x < 320; x += 4) {
            out[x] = palette[src[x]];
            out[x + 1] = palette[src[x + 1]];
            out[x + 2] = palette[src[x + 2]];
            out[x + 3] = palette[src[x + 3]];
        }
    } else {
        for (int x = 0; x < 320; x++) out[x] = SYNC_IDLE << 8 | SYNC_IDLE;
    }
    dma_channel_set_read_addr(dma_ctrl, &line_ptr[b], false);
}

void vga_pio_init(PIO p, unsigned base_pin) {
    pio = p;
    for (int i = 0; i < 4; i++) line_ptr[i] = lines[i];
    /* blank: idle syncs, HS low for its 96 pixels at the start */
    uint8_t *blank = (uint8_t *)lines[0];
    memset(blank, SYNC_IDLE, LINE_BYTES);
    memset(blank, SYNC_IDLE ^ 0x40, HS_BYTES);
    /* vertical sync: VS low all line, both low during HS */
    uint8_t *vs = (uint8_t *)lines[1];
    memset(vs, SYNC_IDLE ^ 0x80, LINE_BYTES);
    memset(vs, 0, HS_BYTES);
    memcpy(lines[2], lines[0], LINE_BYTES);
    memcpy(lines[3], lines[0], LINE_BYTES);
    for (int i = 0; i < 256; i++) palette[i] = SYNC_IDLE << 8 | SYNC_IDLE;

    const uint offset = pio_add_program(pio, &pio_program);
    sm = pio_claim_unused_sm(pio, true);
    for (unsigned i = 0; i < 8; i++) pio_gpio_init(pio, base_pin + i);
    pio_sm_set_consecutive_pindirs(pio, (uint)sm, base_pin, 8, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, base_pin, 8);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 25200000.0f);
    pio_sm_init(pio, (uint)sm, offset, &c);
    pio_sm_set_enabled(pio, (uint)sm, true);

    dma_ctrl = dma_claim_unused_channel(true);
    dma_data = dma_claim_unused_channel(true);
    dma_channel_config d = dma_channel_get_default_config((uint)dma_data);
    channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
    channel_config_set_read_increment(&d, true);
    channel_config_set_write_increment(&d, false);
    channel_config_set_dreq(&d, pio_get_dreq(pio, (uint)sm, true));
    channel_config_set_chain_to(&d, (uint)dma_ctrl);
    dma_channel_configure((uint)dma_data, &d, &pio->txf[sm], lines[0], LINE_BYTES / 4, false);

    dma_channel_config k = dma_channel_get_default_config((uint)dma_ctrl);
    channel_config_set_transfer_data_size(&k, DMA_SIZE_32);
    channel_config_set_read_increment(&k, false);
    channel_config_set_write_increment(&k, false);
    channel_config_set_chain_to(&k, (uint)dma_data);
    dma_channel_configure((uint)dma_ctrl, &k, &dma_hw->ch[dma_data].read_addr, &line_ptr[0], 1, false);
}

void vga_pio_start(void) {
    /* the interrupt on the calling core (core 1) */
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq);
    irq_set_priority(DMA_IRQ_0, 0);
    dma_channel_set_irq0_enabled((uint)dma_ctrl, true);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_start_channel_mask(1u << dma_data);
}

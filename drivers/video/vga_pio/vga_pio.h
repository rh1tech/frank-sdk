/*
 * frank-sdk — VGA 640x480@60 through PIO (vga_pio.c), for frank_video.c.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

typedef const uint8_t *(*vga_pio_row_fn)(uint32_t row);

extern volatile uint32_t vga_pio_frame_count;

/* After the final clk_sys: a state machine on p, eight pins from base_pin,
 * two DMA channels. Nothing runs before vga_pio_start(). */
void vga_pio_init(PIO p, unsigned base_pin);
/* Starts the scan-out, its DMA interrupt (DMA_IRQ_0) on the calling core. */
void vga_pio_start(void);
void vga_pio_set_row_callback(vga_pio_row_fn fn);   /* rows 0..239, NULL: black */
void vga_pio_set_vsync_callback(void (*fn)(void));
/* 256 RGB triplets, stride bytes apart; shown from the next frame */
void vga_pio_set_palette(const uint8_t *rgb, int stride);

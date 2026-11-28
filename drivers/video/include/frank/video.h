/*
 * frank-sdk — HDMI out on every board, whichever back-end it needs.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * The common mode is 640x480@60 from a 320x240 8-bit picture: each source
 * pixel is shown 2x2, through a 256-entry palette. It works on the HSTX
 * boards (M2, NA, NB, C2, FZ: drivers/video/hstx, pico_hdmi's psx_video) and the
 * PIO boards (Z0, M1: drivers/video/pio, frank-quake2's hdmi_pio) alike, each
 * with sound (HDMI data islands) or without (DVI). An HSTX board can take the
 * PIO back-end too: cmake -DFRANK_HDMI_BACKEND=pio. HSTX boards also have
 * 16-bit lines and 800x600 through psx_video.h itself.
 *
 * The output is chosen per build, as the pins are shared (cmake
 * -DFRANK_VIDEO_OUT=hdmi|vga|composite; hdmi is the default): VGA through the
 * HSTX on the GPIO12-19 ladder (M2, PC: DispHSTX, time-dithered colour) or
 * through PIO on any pins (M1 too: FRANK_VGA_BACKEND=pio, a 2-pixel dither),
 * composite PAL/NTSC through PIO (frank-test's tv-software: M2 family; clk_sys
 * 504 MHz only, see docs/gotchas/composite-504-only.md). Neither
 * carries sound; frank/audio.h's analog outputs do.
 *
 * Rules (each cost hours on some port):
 *   - frank_video_start() binds the line IRQ to the calling core: start it
 *     on core 1, and never mask interrupts on that core (the ping-pong DMA
 *     cannot miss an IRQ; one missed and the sink loses the signal for good);
 *   - the row callback runs in that IRQ: it, and the rows it returns, must
 *     be in SRAM, and it must not call into flash (no newlib memcpy there);
 *   - no DMA to or from PSRAM while the picture runs: all channels share one
 *     bus master, a stalled PSRAM access starves the video channel (frank-castle);
 *   - tight core-0 PSRAM loops starve the scan-out too: keep frame buffers in SRAM.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRANK_VIDEO_W 320
#define FRANK_VIDEO_H 240

/* Source row 0..239 (each shown on two output lines), or NULL for black.
 * In the line IRQ: it and what it returns must be in SRAM. */
typedef const uint8_t *(*frank_video_row_fn)(uint32_t row);

/* After frank_init() (the final clk_sys). Claims DMA channels 0-1 (HSTX) or
 * 0-3 (PIO) and DMA_IRQ_0; PIO also takes all of SCRATCH_X and part of
 * SCRATCH_Y. */
void frank_video_init(void);
void frank_video_set_row_callback(frank_video_row_fn fn);
/* Or a whole 320x240 frame buffer (in SRAM), shown as it is: the only way for
 * composite, which scans out of the buffer itself. */
void frank_video_set_framebuffer(const uint8_t *fb);
void frank_video_set_vsync_callback(void (*fn)(void));
/* 256 RGB triplets, `stride` bytes apart; shown from the next vertical blank */
void frank_video_set_palette(const uint8_t *rgb, int stride);
/* DVI: no HDMI data islands, so no sound over HDMI (for sinks that refuse the
 * packets). Default false: HDMI with audio and info frames. Before start. */
void frank_video_set_dvi(bool dvi);
/* Starts the output, its IRQ bound to the calling core (core 1). */
void frank_video_start(void);
/* Starts the output on the calling core (core 1) and never returns: idle (in
 * SRAM) runs over and over between the output's interrupts. The one way that
 * works for VGA too, whose start never returns. */
void __attribute__((noreturn)) frank_video_run(void (*idle)(void));
/* Composite: PAL (625 lines) instead of NTSC (525, the default). Before the start. */
void frank_video_set_composite_pal(bool pal);
uint32_t frank_video_frame_count(void);
const char *frank_video_backend(void);   /* "hstx", "pio", "vga" or "composite" */

/* ---- HDMI audio: 16-bit stereo at 44.1 kHz ---- */

/* Queue frames (left in the low half, right in the high half). Returns how
 * many fitted; the ring holds FRANK_HDMI_AUDIO_RING frames (1024 = 23 ms). */
unsigned frank_hdmi_audio_write(const uint32_t *frames, unsigned n);
unsigned frank_hdmi_audio_free(void);
/* frames the HDMI output has taken so far (its real rate: 44100 a second) */
uint32_t frank_hdmi_audio_consumed(void);

#ifdef __cplusplus
}
#endif

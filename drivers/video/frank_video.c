/*
 * frank-sdk — the common 320x240 8-bit HDMI mode over either back-end.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "frank/video.h"

#include <string.h>

#include "pico.h"
#include "hardware/sync.h"

#ifndef FRANK_HDMI_AUDIO_RING
#define FRANK_HDMI_AUDIO_RING 1024   /* frames, a power of two */
#endif

/* ---- the audio ring: one writer (the firmware), one reader (the line IRQ) ---- */

static uint32_t ring[FRANK_HDMI_AUDIO_RING];
static volatile uint32_t ring_w, ring_r;

uint32_t frank_hdmi_audio_consumed(void) { return ring_r; }

unsigned frank_hdmi_audio_free(void) {
    return FRANK_HDMI_AUDIO_RING - (ring_w - ring_r);
}

unsigned frank_hdmi_audio_write(const uint32_t *frames, unsigned n) {
    unsigned room = frank_hdmi_audio_free();
    if (n > room) n = room;
    uint32_t w = ring_w;
    for (unsigned i = 0; i < n; i++) ring[(w + i) & (FRANK_HDMI_AUDIO_RING - 1)] = frames[i];
    __dmb();
    ring_w = w + n;
    return n;
}

/* the next frame, silence when the ring ran dry */
static inline uint32_t __not_in_flash_func(ring_take)(void) {
    uint32_t r = ring_r;
    if (r == ring_w) return 0;
    uint32_t f = ring[r & (FRANK_HDMI_AUDIO_RING - 1)];
    ring_r = r + 1;
    return f;
}

static frank_video_row_fn row_fn;
static const uint8_t *fb_ptr;

static const uint8_t *__not_in_flash_func(fb_row)(uint32_t y) { return fb_ptr + y * FRANK_VIDEO_W; }

#if FRANK_VIDEO_OUT_VGA
/* ================= VGA over HSTX: frank-test's pico_vga, RGB222 ================= */
#include "vga_output.h"
#if FRANK_VIDEO_VGA_BASE != 12
#error "frank-sdk VGA: the HSTX VGA needs the ladder on GPIO12-19 (M1's on 6-13 is not supported yet)"
#endif
static uint8_t lut[256], lut_next[256];
static volatile bool lut_pending;
static void (*vsync_fn)(void);

static void __not_in_flash_func(on_scanline)(uint32_t line, uint32_t *buf) {
    const uint32_t row = line >> 1;
    const uint8_t *src = row_fn && row < FRANK_VIDEO_H ? row_fn(row) : NULL;
    if (!src) {
        for (int x = 0; x < 640; x++) buf[x] = VGA_SYNC_IDLE;
        return;
    }
    for (int x = 0; x < FRANK_VIDEO_W; x++) buf[2 * x] = buf[2 * x + 1] = lut[src[x]];
}

static void __not_in_flash_func(on_vsync)(void) {
    if (lut_pending) {
        for (int i = 0; i < 256; i++) lut[i] = lut_next[i];
        lut_pending = false;
    }
    if (vsync_fn) vsync_fn();
}

void frank_video_init(void) {
    vga_output_init();
    vga_output_set_scanline_callback(on_scanline);
    vga_output_set_vsync_callback(on_vsync);
}
void frank_video_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }
void frank_video_set_palette(const uint8_t *rgb, int stride) {
    while (lut_pending) tight_loop_contents();
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        lut_next[i] = vga_pixel_byte(c[0] >> 6, c[1] >> 6, c[2] >> 6, VGA_SYNC_IDLE);
    }
    lut_pending = true;
}
void frank_video_set_dvi(bool dvi) { (void)dvi; }
void frank_video_start(void) { vga_output_core1_run(); }   /* never returns */
uint32_t frank_video_frame_count(void) { return vga_frame_count; }
const char *frank_video_backend(void) { return "vga"; }

#elif FRANK_VIDEO_OUT_COMPOSITE
/* ================= composite PAL/NTSC: frank-test's tv-software, PIO ================= */
#include "graphics.h"
static volatile uint32_t frames;
void frank_video_init(void) {
    graphics_init();
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
}
void frank_video_set_vsync_callback(void (*fn)(void)) { (void)fn; }
void frank_video_set_palette(const uint8_t *rgb, int stride) {
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        graphics_set_palette((uint8_t)i, (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]);
    }
}
void frank_video_set_dvi(bool dvi) { (void)dvi; }
void frank_video_start(void) {}   /* the timer IRQ and DMA run from frank_video_init */
uint32_t frank_video_frame_count(void) { return frames; }   /* tv-software counts no frames: stays 0 */
const char *frank_video_backend(void) { return "composite"; }

/* the back-end: the board's (boards/<id>.yaml video.hdmi.backend), or PIO on an
 * HSTX board when the build asks (cmake -DFRANK_HDMI_BACKEND=pio) */
#elif FRANK_VIDEO_HDMI_BACKEND_HSTX && !FRANK_VIDEO_USE_PIO
/* ================= HSTX: psx_video, 320 BGR555 pixels doubled ================= */
#include "hstx_data_island_queue.h"
#include "psx_video.h"

static uint16_t pal16[256];
static uint16_t pal16_next[256];
static volatile bool pal_pending;
/* the HSTX DMA reads a line one to two lines after its callback: four slots */
static uint16_t lines[4][FRANK_VIDEO_W] __attribute__((aligned(4)));
static uint16_t black[FRANK_VIDEO_W] __attribute__((aligned(4)));
static void (*vsync_fn)(void);

static void __not_in_flash_func(on_line)(uint32_t out_line, psxv_line_t *line) {
    const uint32_t row = out_line >> 1;
    const uint8_t *src = row_fn && row < FRANK_VIDEO_H ? row_fn(row) : NULL;
    line->mode = PSXV_LINE_DOUBLE;
    if (!src) {
        line->pixels = black;
        return;
    }
    uint16_t *dst = lines[out_line & 3];
    for (int x = 0; x < FRANK_VIDEO_W; x += 4) {
        dst[x] = pal16[src[x]];
        dst[x + 1] = pal16[src[x + 1]];
        dst[x + 2] = pal16[src[x + 2]];
        dst[x + 3] = pal16[src[x + 3]];
    }
    line->pixels = dst;
}

static void __not_in_flash_func(on_vsync)(void) {
    if (pal_pending) {
        for (int i = 0; i < 256; i++) pal16[i] = pal16_next[i];
        pal_pending = false;
    }
    if (vsync_fn) vsync_fn();
}

static void __not_in_flash_func(audio_src)(uint32_t frames[4]) {
    for (int i = 0; i < 4; i++) frames[i] = ring_take();
}

void frank_video_init(void) {
    psxv_init();
    psxv_set_mode(640, 480);
    psxv_set_line_callback(on_line);
    psxv_set_vsync_callback(on_vsync);
    psxv_set_audio_sample_rate(44100);
    hstx_di_queue_set_audio_source(audio_src);
}

void frank_video_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }

void frank_video_set_palette(const uint8_t *rgb, int stride) {
    while (pal_pending) tight_loop_contents();
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        pal16_next[i] = (uint16_t)((c[0] >> 3) | (c[1] >> 3) << 5 | (c[2] >> 3) << 10);   /* BGR555 */
    }
    pal_pending = true;
}

void frank_video_set_dvi(bool dvi) { psxv_set_dvi_mode(dvi); }
void frank_video_start(void) { psxv_start(); }
uint32_t frank_video_frame_count(void) { return psxv_frame_count; }
const char *frank_video_backend(void) { return "hstx"; }

#elif FRANK_VIDEO_HDMI_BACKEND_PIO || FRANK_VIDEO_USE_PIO
/* ================= PIO: hdmi_pio, 320 8-bit pixels doubled ================= */
#include "hdmi_pio.h"

static const uint8_t *__not_in_flash_func(on_row)(uint32_t row) {
    return row_fn && row < FRANK_VIDEO_H ? row_fn(row) : NULL;
}

/* hdmi_pio takes one frame at 11025 Hz and interpolates up to 44.1 kHz:
 * the mean of four of the ring's */
static uint32_t __not_in_flash_func(audio_src)(void) {
    int32_t l = 0, r = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t f = ring_take();
        l += (int16_t)(f & 0xFFFF);
        r += (int16_t)(f >> 16);
    }
    return (uint16_t)(l / 4) | (uint32_t)(uint16_t)(r / 4) << 16;
}

void frank_video_init(void) {
    psxv_init();
    hdmi_pio_set_row_callback(on_row);
    hdmi_pio_set_audio_source(audio_src);
}

void frank_video_set_vsync_callback(void (*fn)(void)) { psxv_set_vsync_callback(fn); }

void frank_video_set_palette(const uint8_t *rgb, int stride) {
    hdmi_pio_load_palette(rgb, stride);
    hdmi_pio_show_palette();
}

void frank_video_set_dvi(bool dvi) { hdmi_pio_set_dvi(dvi); }
void frank_video_start(void) { psxv_start(); }
uint32_t frank_video_frame_count(void) { return psxv_frame_count; }
const char *frank_video_backend(void) { return "pio"; }

#else
#error "frank-sdk video: the board has no HDMI back-end (boards/*.yaml video.hdmi.backend)"
#endif

void frank_video_set_row_callback(frank_video_row_fn fn) {
#if FRANK_VIDEO_OUT_COMPOSITE
    (void)fn;   /* composite scans a frame buffer out: frank_video_set_framebuffer() */
#else
    row_fn = fn;
#endif
}

void frank_video_set_framebuffer(const uint8_t *fb) {
    fb_ptr = fb;
#if FRANK_VIDEO_OUT_COMPOSITE
    graphics_set_buffer((uint8_t *)fb, FRANK_VIDEO_W, FRANK_VIDEO_H);
#else
    row_fn = fb_row;
#endif
}

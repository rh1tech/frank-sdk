/*
 * frank-sdk — the common 320x240 8-bit HDMI mode over either back-end.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include "frank/video.h"

#include <stdio.h>
#include <string.h>

#include "pico.h"
#include "hardware/sync.h"

#ifndef FRANK_HDMI_AUDIO_RING
#define FRANK_HDMI_AUDIO_RING 1024   /* frames, a power of two */
#endif

/* ---- the audio ring: one writer (the firmware), one reader (the line IRQ) ---- */

/* frank_audio's ring storage when it is linked (one output plays at a time),
 * else this one */
uint32_t frank_audio_ring_mem[FRANK_HDMI_AUDIO_RING] __attribute__((weak));
#define ring frank_audio_ring_mem
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

#if FRANK_VIDEO_OUT_VGA && FRANK_VIDEO_VGA_PIO
/* ================= VGA through PIO: drivers/video/vga_pio, any pin base ================= */
#include "vga_pio.h"
#include "hardware/pio.h"
static void (*vsync_fn)(void);
static const uint8_t *__not_in_flash_func(on_row)(uint32_t row) {
    return row_fn && row < FRANK_VIDEO_H ? row_fn(row) : NULL;
}
static void __not_in_flash_func(on_vsync)(void) {
    if (vsync_fn) vsync_fn();
}
void frank_video_init(void) {
    vga_pio_init(PIO_INSTANCE(FRANK_PIO_VIDEO), FRANK_VIDEO_VGA_BASE);
    vga_pio_set_row_callback(on_row);
    vga_pio_set_vsync_callback(on_vsync);
}
void frank_video_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }
void frank_video_set_palette(const uint8_t *rgb, int stride) { vga_pio_set_palette(rgb, stride); }
void frank_video_set_dvi(bool dvi) { (void)dvi; }
void frank_video_start(void) { vga_pio_start(); }
uint32_t frank_video_frame_count(void) { return vga_pio_frame_count; }
const char *frank_video_backend(void) { return "vga"; }

#elif FRANK_VIDEO_OUT_VGA
/* ================= VGA over HSTX: DispHSTX (Miroslav Nemecek), time-dithered ================= */
/* DispHSTX renders each line on core 1 from a pattern that repeats every
 * VGA_RING rows (DISPHSTX_FORMAT_PAT_8_PAL): the ring below, which core 1
 * refills from the row callback between the interrupts, ahead of the scan
 * line. So row_fn here runs in that loop, not in an interrupt: it may read
 * PSRAM. clk_sys stays the firmware's; clk_hstx is 252 MHz (DispHSTX's
 * "fast" 640x480 timing) or 126 MHz from it, whichever divides. */
#include "disphstx.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#define VGA_RING 8
static uint8_t vga_ring[VGA_RING][FRANK_VIDEO_W] __attribute__((aligned(4)));
static int32_t ring_row[VGA_RING];
static u16 pal565[256];
static u32 palvga[2 * 256];
static uint16_t pal_next[256];
static volatile bool pal_pending;
#define vmode DispHstxVMode   /* DispHSTX's own descriptor: no second copy */
static void (*vsync_fn)(void);
static void (*user_idle)(void);
static volatile uint32_t frames;

void frank_video_init(void) {
    dma_channel_claim(DISPHSTX_DMA_DATA);   /* DispHSTX's fixed channels: no one else's */
    dma_channel_claim(DISPHSTX_DMA_CMD);
    const uint32_t sys = clock_get_hz(clk_sys);
    const bool fast = sys % 252000000u == 0;
    const sDispHstxVModeTime *t = &DispHstxVModeTimeList[fast ? vmodetime_640x480_fast : vmodetime_640x480];
    clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, sys, t->sysclk * 1000u);
    for (int i = 0; i < VGA_RING; i++) ring_row[i] = -1;
    DispHstxVModeInitTime(&vmode, t);
    DispHstxVModeAddStrip(&vmode, -1);
    DispHstxVModeAddSlot(&vmode, 2, 2, FRANK_VIDEO_W, DISPHSTX_FORMAT_PAT_8_PAL, vga_ring, FRANK_VIDEO_W,
                         pal565, palvga, NULL, VGA_RING, 0, 0);
}
void frank_video_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }
void frank_video_set_palette(const uint8_t *rgb, int stride) {
    while (pal_pending) tight_loop_contents();
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        pal_next[i] = (uint16_t)((c[0] >> 3) << 11 | (c[1] >> 2) << 5 | (c[2] >> 3));   /* RGB565 */
    }
    pal_pending = true;
}
void frank_video_set_dvi(bool dvi) { (void)dvi; }

/* core 1, between DispHSTX's interrupts: the next rows into the ring; the
 * palette and the vsync callback once the picture's last line is out */
static void __not_in_flash_func(vga_idle)(void) {
    static int last_line = -1;
    const int line = vmode.line;                 /* the scan line being shown */
    const int row = line < 480 ? line >> 1 : -1;
    if (line >= 480 && last_line < 480) {        /* the picture is out: vertical blank */
        frames++;
        if (pal_pending) {
            for (int i = 0; i < 256; i++) pal565[i] = pal_next[i];
            DispHstxVgaPal(palvga, pal565, 256);
            pal_pending = false;
        }
        if (vsync_fn) vsync_fn();
    }
    last_line = line;
    /* rows row+1 .. row+VGA_RING-1, or the top of the next frame in the blank */
    const int first = row >= 0 ? row + 1 : 0;
    for (int k = 0; k < VGA_RING - 1; k++) {
        const int r = first + k;
        if (r >= FRANK_VIDEO_H) break;
        const int s = r % VGA_RING;
        if (ring_row[s] == r) continue;
        const uint8_t *src = row_fn ? row_fn((uint32_t)r) : NULL;
        uint32_t *d = (uint32_t *)vga_ring[s];
        if (src) {
            const uint32_t *w = (const uint32_t *)src;
            for (int i = 0; i < FRANK_VIDEO_W / 4; i++) d[i] = w[i];
        } else {
            for (int i = 0; i < FRANK_VIDEO_W / 4; i++) d[i] = 0;
        }
        ring_row[s] = r;
    }
    if (user_idle) user_idle();
}
static void vga_start(void (*idle)(void)) {
    user_idle = idle;
    DispHstxVgaIdle = vga_idle;
    DispHstxVgaRunHere(&vmode);   /* never returns */
}
void frank_video_start(void) { vga_start(NULL); }
uint32_t frank_video_frame_count(void) { return frames; }
const char *frank_video_backend(void) { return "vga"; }

#elif FRANK_VIDEO_OUT_COMPOSITE
/* ================= composite PAL/NTSC: frank-test's tv-software, PIO ================= */
#if FRANK_CPU_MHZ < 504
#error "frank-sdk composite: clk_sys 504 MHz only (docs/gotchas/composite-504-only.md)"
#endif
#include "tv_rename.h"   /* the driver's graphics_* are tv_graphics_* */
#include "graphics.h"
static uint8_t pal_rgb[256 * 3];
static bool pal_set, tv_pal, started;
static const uint8_t *__not_in_flash_func(on_row)(uint32_t row) {
    return row_fn && row < FRANK_VIDEO_H ? row_fn(row) : NULL;
}
static void apply_palette(void) {
    for (int i = 0; i < 256; i++)
        graphics_set_palette((uint8_t)i, (uint32_t)pal_rgb[i * 3] << 16 | (uint32_t)pal_rgb[i * 3 + 1] << 8 | pal_rgb[i * 3 + 2]);
}
void frank_video_init(void) {}   /* the output starts with frank_video_start(), on its core */
void frank_video_set_vsync_callback(void (*fn)(void)) { graphics_set_vsync_callback(fn); }
void frank_video_set_palette(const uint8_t *rgb, int stride) {
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        pal_rgb[i * 3] = c[0];
        pal_rgb[i * 3 + 1] = c[1];
        pal_rgb[i * 3 + 2] = c[2];
    }
    pal_set = true;
    if (started) apply_palette();
}
void frank_video_set_dvi(bool dvi) { (void)dvi; }
void frank_video_set_composite_pal(bool pal) { tv_pal = pal; }
void frank_video_start(void) {
    /* the timer IRQ that makes the lines is bound to this core */
    graphics_set_tv_pal(tv_pal);
    graphics_init();
    graphics_set_mode(GRAPHICSMODE_DEFAULT);
    started = true;
    if (pal_set) apply_palette();   /* graphics_init() wrote its own colours 200-215 */
}
uint32_t frank_video_frame_count(void) { return tv_fields; }
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

extern volatile uint32_t psxv_late, psxv_fifo_low, psxv_resyncs;
/* "~C video~": a dropout on the sink is a resync here (a line IRQ serviced
 * too late broke the ping-pong; rebuilt from the next frame) */
static void cmd_video(const char *args) {
    (void)args;
    printf("video: hstx, frame %lu, late IRQs %lu, FIFO found empty %lu, resyncs %lu\n",
           (unsigned long)psxv_frame_count, (unsigned long)psxv_late, (unsigned long)psxv_fifo_low,
           (unsigned long)psxv_resyncs);
}

void frank_video_init(void) {
    extern void frank_console_add(const char *name, void (*fn)(const char *), const char *help);
    frank_console_add("video", cmd_video, "the HDMI counters: frames, late line IRQs, empty FIFO, resyncs");
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
    row_fn = fn;
#if FRANK_VIDEO_OUT_COMPOSITE
    graphics_set_row_callback(fn ? on_row : NULL);
#endif
}

void frank_video_set_framebuffer(const uint8_t *fb) {
    fb_ptr = fb;
#if FRANK_VIDEO_OUT_COMPOSITE
    graphics_set_row_callback(NULL);
    graphics_set_buffer((uint8_t *)fb, FRANK_VIDEO_W, FRANK_VIDEO_H);
#else
    row_fn = fb_row;
#endif
}

#if !FRANK_VIDEO_OUT_COMPOSITE
void frank_video_set_composite_pal(bool pal) { (void)pal; }
#endif

void __attribute__((noreturn)) __not_in_flash_func(frank_video_run)(void (*idle)(void)) {
#if FRANK_VIDEO_OUT_VGA && !FRANK_VIDEO_VGA_PIO
    vga_start(idle);   /* DispHSTX on this core, idle between its interrupts */
#else
    frank_video_start();
#endif
    for (;;) {
        if (idle) idle();
        else tight_loop_contents();
    }
}

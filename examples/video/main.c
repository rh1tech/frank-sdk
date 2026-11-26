/*
 * frank-sdk example: video — HDMI picture and sound on every board.
 *
 * 320x240 8-bit frame buffer in SRAM, shown 2x2 at 640x480@60 by core 1;
 * colour bars, a grey ramp and a box moving one pixel a frame (a frozen
 * capture shows at once), and a 1 kHz tone over HDMI audio.
 * ~C tone HZ~ changes the tone (0: silence), ~C stat~ prints the frame count.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "frank/platform.h"
#include "frank/video.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

static uint8_t fb[FRANK_VIDEO_H][FRANK_VIDEO_W] __attribute__((aligned(4)));

static const uint8_t *__not_in_flash_func(row)(uint32_t y) {
    return fb[y];
}

/* palette: 0-7 the bars, 16-79 a grey ramp, 255 the box */
static void make_palette(void) {
    static uint8_t rgb[256][3];
    static const uint8_t bars[8][3] = {
        {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
        {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
    };
    for (int i = 0; i < 8; i++)
        for (int c = 0; c < 3; c++) rgb[i][c] = bars[i][c];
    for (int i = 0; i < 64; i++) rgb[16 + i][0] = rgb[16 + i][1] = rgb[16 + i][2] = (uint8_t)(i * 4);
    rgb[255][0] = 255;
    rgb[255][1] = 128;
    rgb[255][2] = 0;
    frank_video_set_palette(&rgb[0][0], 3);
}

static void draw_background(void) {
    for (int y = 0; y < FRANK_VIDEO_H; y++)
        for (int x = 0; x < FRANK_VIDEO_W; x++)
            fb[y][x] = y < 160 ? (uint8_t)(x / 40) : (uint8_t)(16 + x / 5);
}

static void box(int bx, int by, bool on) {
    for (int y = by; y < by + 24; y++)
        for (int x = bx; x < bx + 24; x++)
            fb[y][x] = on ? 255 : (y < 160 ? (uint8_t)(x / 40) : (uint8_t)(16 + x / 5));
}

static void core1_main(void) {
    frank_video_start();   /* the line IRQ on this core */
    while (1) __wfi();
}

/* ---- the tone: a triangle wave, written ahead into the HDMI audio ring ---- */
static volatile unsigned tone_hz = 1000;
static uint32_t phase;

static void feed_audio(void) {
    uint32_t buf[64];
    while (frank_hdmi_audio_free() >= 64) {
        const uint32_t step = (uint32_t)(((uint64_t)tone_hz << 32) / 44100u);
        for (int i = 0; i < 64; i++) {
            int32_t tri = (int32_t)(phase >> 16);            /* 0..65535 */
            int16_t s = (int16_t)((tri < 32768 ? tri : 65535 - tri) - 16384);
            if (!tone_hz) s = 0;
            buf[i] = (uint16_t)s | (uint32_t)(uint16_t)s << 16;
            phase += step;
        }
        frank_hdmi_audio_write(buf, 64);
    }
}

static void cmd_tone(const char *a) {
    tone_hz = (unsigned)strtoul(a, NULL, 0);
    printf("tone %u Hz\n", tone_hz);
}

static void cmd_stat(const char *a) {
    (void)a;
    printf("video: %s, frame %lu, audio frames taken %lu, at %lu us\n", frank_video_backend(),
           (unsigned long)frank_video_frame_count(), (unsigned long)frank_hdmi_audio_consumed(),
           (unsigned long)time_us_32());
}

int main(void) {
    frank_init("video", FRANK_SDK_VERSION);
    frank_console_add("tone", cmd_tone, "HZ: the HDMI test tone (0: silence)");
    frank_console_add("stat", cmd_stat, "video back-end and frame count");

    draw_background();
    frank_video_init();
    make_palette();
    frank_video_set_row_callback(row);
    frank_launch_core1(core1_main);
    printf("video: %s back-end, 320x240 at 640x480@60\n", frank_video_backend());

    frank_watchdog_start(2000);
    frank_boot_ok();

    int x = 0, dx = 1, y = 100, dy = 1;
    uint32_t seen = frank_video_frame_count();
    for (;;) {
        frank_crumb(1, seen);
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(NULL, NULL);
        feed_audio();
        uint32_t f = frank_video_frame_count();
        if (f == seen) continue;
        seen = f;
        box(x, y, false);
        x += dx;
        y += dy;
        if (x <= 0 || x >= FRANK_VIDEO_W - 24) dx = -dx;
        if (y <= 0 || y >= FRANK_VIDEO_H - 24) dy = -dy;
        box(x, y, true);
    }
}

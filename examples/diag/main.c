/*
 * frank-sdk example: diag — every driver the board has, checked from the console.
 *
 *   ~C sd~ / ~C sd bench~       the card: size, free, root; SDIO speed
 *   ~C sdtest~                  write, read back and CRC-check a 1 MB file
 *   ~C audio i2s|pwm|hdmi~      switch the tone's output (reboots into it)
 *   ~C tone HZ~                 the test tone (0: silence)
 *   ~C astat~                   audio frames played (the real rate)
 *   ~C keys~                    input events seen (keyboard, mouse, pads)
 * plus the platform's (help, clocks, mem, psram, bootinfo...).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico.h"   /* the board header first: the FRANK_HAS_* below come from it */
#include "frank/audio.h"
#include "frank/platform.h"
#ifdef FRANK_HAS_SD
#include "frank/storage.h"
#endif
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#if __has_include("frank/video.h") && defined(FRANK_HAS_VIDEO)
#include "frank/video.h"
#define DIAG_VIDEO 1
#endif
#if __has_include("frank/input.h")
#include "frank/input.h"
#define DIAG_INPUT 1
#endif

/* ---- the tone ---- */
static volatile unsigned tone_hz = 440;
static uint32_t phase;

static void feed_audio(void) {
    uint32_t buf[64];
    while (frank_audio_free() >= 64) {
        const uint32_t step = (uint32_t)(((uint64_t)tone_hz << 32) / 44100u);
        for (int i = 0; i < 64; i++) {
            int32_t tri = (int32_t)(phase >> 16);
            int16_t s = tone_hz ? (int16_t)((tri < 32768 ? tri : 65535 - tri) - 16384) : 0;
            buf[i] = (uint16_t)s | (uint32_t)(uint16_t)s << 16;
            phase += step;
        }
        frank_audio_write(buf, 64);
    }
}

static void cmd_tone(const char *a) { tone_hz = (unsigned)strtoul(a, NULL, 0); }

static void cmd_astat(const char *a) {
    (void)a;
    static uint32_t last_n, last_t;
    uint32_t n = frank_audio_consumed(), t = time_us_32();
    printf("audio %s: %lu frames played", frank_audio_name(), (unsigned long)n);
    if (last_t) printf(", %lu a second since the last astat", (unsigned long)((uint64_t)(n - last_n) * 1000000u / (t - last_t)));
    printf("\n");
    last_n = n;
    last_t = t;
}

/* the output survives the reboot in watchdog scratch 2 (4-7 are the boot ROM's: watchdog_reboot clears 4) */
#define AUDIO_TAG 0xA0D10000u
static void cmd_audio(const char *a) {
    int o = !strncmp(a, "i2s", 3) ? FRANK_AUDIO_I2S : !strncmp(a, "pwm", 3) ? FRANK_AUDIO_PWM
          : !strncmp(a, "hdmi", 4) ? FRANK_AUDIO_HDMI : FRANK_AUDIO_AUTO;
    watchdog_hw->scratch[2] = AUDIO_TAG | (uint32_t)o;
    printf("audio: rebooting into %s\n", a);
    frank_reboot_clean(false);
}

#ifdef FRANK_HAS_SD
/* ---- the card: a 1 MB file written, read back, compared ---- */
static void cmd_sdtest(const char *a) {
    (void)a;
    static uint32_t blk[1024];
    FIL f;
    uint32_t t0 = time_us_32();
    FRESULT r = f_open(&f, "/frank_diag.bin", FA_CREATE_ALWAYS | FA_WRITE);
    UINT n;
    for (uint32_t k = 0; r == FR_OK && k < 256; k++) {
        for (int i = 0; i < 1024; i++) blk[i] = k * 1024 + i * 2654435761u;
        r = f_write(&f, blk, sizeof(blk), &n);
        frank_watchdog_feed();
    }
    if (r == FR_OK) r = f_close(&f);
    uint32_t t1 = time_us_32();
    uint32_t bad = 0;
    if (r == FR_OK) r = f_open(&f, "/frank_diag.bin", FA_READ);
    for (uint32_t k = 0; r == FR_OK && k < 256; k++) {
        r = f_read(&f, blk, sizeof(blk), &n);
        for (int i = 0; i < 1024; i++) bad += blk[i] != k * 1024 + i * 2654435761u;
        frank_watchdog_feed();
    }
    if (r == FR_OK) f_close(&f);
    uint32_t t2 = time_us_32();
    f_unlink("/frank_diag.bin");
    printf("sdtest: %s (FatFs %d, %lu bad words); write %lu KB/s, read %lu KB/s\n",
           r == FR_OK && !bad ? "ok" : "FAILED", r, (unsigned long)bad,
           (unsigned long)(1024ull * 1000000 / (t1 - t0 + 1)), (unsigned long)(1024ull * 1000000 / (t2 - t1 + 1)));
}
#endif

#ifdef DIAG_VIDEO
static uint8_t fb[FRANK_VIDEO_H][FRANK_VIDEO_W];
static const uint8_t *__not_in_flash_func(row)(uint32_t y) { return fb[y]; }
static void core1_main(void) {
    frank_video_start();
    while (1) __wfi();
}
#endif

#ifdef DIAG_INPUT
static void cmd_keys(const char *a) {
    (void)a;
    frank_input_report();
}
#endif

int main(void) {
    frank_init("diag", FRANK_SDK_VERSION);
    frank_console_add("tone", cmd_tone, "HZ: the test tone (0: silence)");
    frank_console_add("astat", cmd_astat, "audio frames played and their rate");
    frank_console_add("audio", cmd_audio, "i2s|pwm|hdmi|auto: reboot into that output");
#ifdef FRANK_HAS_SD
    frank_console_add("sdtest", cmd_sdtest, "write, read back and check a 1 MB file");
#endif
#ifdef DIAG_INPUT
    frank_console_add("keys", cmd_keys, "input devices and the last events");
    frank_input_init();
#endif

#ifdef DIAG_VIDEO
    for (int y = 0; y < FRANK_VIDEO_H; y++)
        for (int x = 0; x < FRANK_VIDEO_W; x++) fb[y][x] = (uint8_t)((x / 40) + 8 * (y / 30));
    static uint8_t pal[256][3];
    for (int i = 0; i < 64; i++) {
        pal[i][0] = (uint8_t)((i & 7) * 36);
        pal[i][1] = (uint8_t)((i >> 3) * 36);
        pal[i][2] = (uint8_t)(255 - (i & 7) * 36);
    }
    frank_video_init();
    frank_video_set_palette(&pal[0][0], 3);
    frank_video_set_row_callback(row);
    frank_launch_core1(core1_main);
#endif

#ifdef FRANK_HAS_SD
    frank_sd_mount();
#endif
    int out = FRANK_AUDIO_AUTO;
    if ((watchdog_hw->scratch[2] & 0xFFFF0000u) == AUDIO_TAG) out = (int)(watchdog_hw->scratch[2] & 0xFF);
    frank_audio_init((frank_audio_out_t)out, 44100);

    frank_watchdog_start(3000);
    frank_boot_ok();
#ifdef DIAG_VIDEO
    uint32_t seen = frank_video_frame_count(), bar = 0;
#endif
    for (uint32_t n = 0;; n++) {
#ifdef DIAG_VIDEO
        /* a bar sweeping down, a line a frame: a capture that does not change is a hang */
        if (frank_video_frame_count() != seen) {
            seen = frank_video_frame_count();
            for (int x = 0; x < FRANK_VIDEO_W; x++) fb[bar][x] = (uint8_t)((x / 40) + 8 * (bar / 30));
            bar = (bar + 1) % FRANK_VIDEO_H;
            for (int x = 0; x < FRANK_VIDEO_W; x++) fb[bar][x] = 63;
        }
#endif
        frank_crumb(1, n);
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(NULL, NULL);
        feed_audio();
#ifdef DIAG_INPUT
        frank_input_poll();
#endif
    }
}

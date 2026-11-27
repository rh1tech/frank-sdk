/*
 * frank-sdk host build: the platform, video, audio, input and storage APIs on
 * SDL2, so a firmware's logic runs on the desktop before it meets a board.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * The firmware's main() (renamed frank_app_main) runs on a thread of its own,
 * "core 1" (frank_launch_core1) on another; the process's main thread keeps
 * SDL (macOS wants the window there): 60 times a second it builds the frame
 * from the firmware's rows or frame buffer and palette, then calls the vsync
 * callback. Audio: SDL drains the same ring frank_audio_write() fills.
 * Keys: SDL scancodes are USB HID usage codes, so they come out unchanged.
 * The SD card is a host directory (-sd DIR, default ./sd): ff_host.c.
 * The console is the terminal: type ~C help~ (stdin, read by a thread).
 *
 * Usage: <app> [-sd DIR] [-scale N]
 */
#include <SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frank/audio.h"
#include "frank/input.h"
#include "frank/platform.h"
#include "frank/storage.h"
#include "frank/video.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

host_watchdog_t host_watchdog;
extern const char *host_sd_root;
extern int frank_app_main(void);
extern void frank_console_builtins(void);

/* ================= platform ================= */

volatile bool frank_exit_requested;

void frank_init(const char *name, const char *version) {
    printf("\n==== %s %s on %s (terminal console: ~C help~) ====\n", name, version, FRANK_BOARD_NAME);
    frank_console_builtins();
}
void frank_boot_ok(void) {}
void frank_watchdog_start(uint32_t ms) { (void)ms; }
void frank_watchdog_feed(void) {}
static uint32_t crumb[3];
void frank_crumb(uint32_t stage, uint32_t arg) { crumb[0] = stage; crumb[1] = arg; }
void frank_crumb_tick(void) { crumb[2]++; }
void frank_crumb_report(void) {
    printf("crumb: stage %u arg 0x%08x, %u main loops\n", crumb[0], crumb[1], crumb[2]);
}
void frank_report_boot(void) { printf("boot: host process\n"); }
void frank_reboot(bool bootsel) {
    printf("\n%s: the host build ends here\n", bootsel ? "BOOTSEL" : "reboot");
    frank_exit_requested = true;
    pthread_exit(NULL);
}
void frank_reboot_clean(bool bootsel) { frank_reboot(bootsel); }

static void *core1_thread(void *entry) {
    ((void (*)(void))entry)();
    return NULL;
}
void frank_launch_core1(void (*entry)(void)) {
    pthread_t t;
    pthread_create(&t, NULL, core1_thread, (void *)entry);
    pthread_detach(t);
}
unsigned frank_core1_stack_used(void) { return 0; }
void frank_cyccnt_enable(void) {}
void frank_prof_start(uint32_t us) { (void)us; printf("profiling: not on the host\n"); }
void frank_prof_pairs(bool on) { (void)on; }
void frank_prof_core1_init(void) {}
void frank_prof_start_core1(uint32_t us) { (void)us; }
bool frank_prof_dump_if_done(void) { return false; }
/* console.c calls it when linked: Mach-O has no undefined weak symbols */
__attribute__((weak)) void profiler_rearm(void) {}
void *frank_sram_malloc(size_t n) { return malloc(n); }
void *frank_psram_malloc(size_t n) { return malloc(n); }
void frank_heap_report(void) { printf("heap: the host's\n"); }
size_t frank_sram_free(void) { return 512 * 1024; }
void frank_usb_poll(void (*task)(void)) { if (task) task(); }

unsigned frank_psram_cs(void) { return 0; }
size_t frank_psram_size(void) { return FRANK_PSRAM_BYTES; }
unsigned frank_psram_mhz_cap(void) { return 0; }
void frank_psram_id(uint8_t *mfid, uint8_t *kgd) { *mfid = 0x0D; *kgd = 0x5D; }
bool frank_psram_test(void) { return true; }
void frank_clocks_report(void) { printf("clocks: the host's\n"); }

/* stdin, a byte at a time, for frank_console_poll() */
static uint8_t in_ring[1024];
static volatile unsigned in_w, in_r;
static void *stdin_thread(void *u) {
    (void)u;
    for (int c; (c = getchar()) != EOF;) {
        if (c == '\n') continue;   /* the console protocol has no lines */
        while (in_w - in_r >= sizeof(in_ring)) usleep(1000);
        in_ring[in_w++ % sizeof(in_ring)] = (uint8_t)c;
    }
    return NULL;
}
int getchar_timeout_us(uint32_t us) {
    (void)us;
    if (in_r == in_w) return -1;
    return in_ring[in_r++ % sizeof(in_ring)];
}

/* ================= video ================= */

static frank_video_row_fn row_fn;
static const uint8_t *fb_ptr;
static void (*vsync_fn)(void);
static uint32_t pal[256];
static volatile uint32_t frames;

void frank_video_init(void) {}
void frank_video_set_row_callback(frank_video_row_fn fn) { row_fn = fn; }
void frank_video_set_framebuffer(const uint8_t *fb) { fb_ptr = fb; }
void frank_video_set_vsync_callback(void (*fn)(void)) { vsync_fn = fn; }
void frank_video_set_palette(const uint8_t *rgb, int stride) {
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = rgb + i * stride;
        pal[i] = 0xFF000000u | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
    }
}
void frank_video_set_dvi(bool dvi) { (void)dvi; }
void frank_video_start(void) {
    for (;;) usleep(100000);   /* "core 1": the window is drawn by the main thread */
}
uint32_t frank_video_frame_count(void) { return frames; }
const char *frank_video_backend(void) { return "sdl"; }

/* ================= audio: one ring, SDL drains it ================= */

#define RING 8192
static uint32_t aring[RING];
static volatile uint32_t aw, ar;
static int audio_out = -1;
static SDL_AudioDeviceID audio_dev;

int frank_audio_init(frank_audio_out_t out, uint32_t rate) {
    SDL_AudioSpec want = {0}, have;
    want.freq = (int)rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        printf("audio: SDL has no device (%s)\n", SDL_GetError());
        return -1;
    }
    SDL_PauseAudioDevice(dev, 0);
    audio_dev = dev;
    audio_out = out == FRANK_AUDIO_AUTO ? FRANK_AUDIO_I2S : (int)out;
    printf("audio: SDL at %u Hz\n", rate);
    return audio_out;
}
const char *frank_audio_name(void) { return audio_out < 0 ? "none" : "sdl"; }
bool frank_audio_available(frank_audio_out_t out) { return out == FRANK_AUDIO_AUTO || out == FRANK_AUDIO_I2S; }
int frank_audio_switch(frank_audio_out_t out, uint32_t rate) { return frank_audio_init(out, rate); }
unsigned frank_audio_free(void) { return audio_out < 0 ? 0 : RING - (aw - ar); }
uint32_t frank_audio_consumed(void) { return ar; }
void frank_audio_set_volume(unsigned shift) { (void)shift; }
unsigned frank_audio_write(const uint32_t *f, unsigned n) {
    unsigned room = frank_audio_free();
    if (n > room) n = room;
    for (unsigned i = 0; i < n; i++) aring[(aw + i) % RING] = f[i];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    aw += n;
    return n;
}
unsigned frank_hdmi_audio_write(const uint32_t *f, unsigned n) { return frank_audio_write(f, n); }
unsigned frank_hdmi_audio_free(void) { return frank_audio_free(); }
uint32_t frank_hdmi_audio_consumed(void) { return ar; }

/* ================= input: SDL scancodes are HID usages ================= */

static frank_key_t keys[64];
static volatile unsigned kw, kr;
static uint8_t mods;
static int mdx, mdy, mwheel;
static uint8_t mbuttons;
static bool mmoved;

void frank_input_init(void) { printf("input: SDL keyboard and mouse\n"); }
void frank_input_poll(void) {}
bool frank_input_key(frank_key_t *ev) {
    if (kr == kw) return false;
    *ev = keys[kr++ % 64];
    return true;
}
bool frank_input_mouse(int *dx, int *dy, int *wheel, uint8_t *buttons) {
    *dx = mdx; *dy = mdy; *wheel = mwheel; *buttons = mbuttons;
    bool m = mmoved;
    mdx = mdy = mwheel = 0;
    mmoved = false;
    return m;
}
uint32_t frank_input_pad(int n) { (void)n; return 0; }
void frank_input_pad_axes(int n, int8_t axes[4]) { (void)n; for (int i = 0; i < 4; i++) axes[i] = 0; }
bool frank_input_pad_connected(void) { return false; }
uint32_t frank_input_pad_usb(void) { return 0; }
uint32_t frank_input_pad_nes(int n) { (void)n; return 0; }
void frank_input_report(void) { printf("input: SDL; last %u keys queued\n", kw); }

/* ================= storage ================= */

static void cmd_sd(const char *a) {
    (void)a;
    FFDIR d;
    FILINFO fi;
    printf("sd (host): %s/\n", host_sd_root);
    if (f_opendir(&d, "/") != FR_OK) return;
    for (int n = 0; f_readdir(&d, &fi) == FR_OK && fi.fname[0] && n < 20; n++)
        printf("  %s%s %lu\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "", (unsigned long)fi.fsize);
    f_closedir(&d);
}

FRESULT frank_sd_mount(void) {
    static FATFS fs;
    printf("SD (host): %s/\n", host_sd_root);
    frank_console_add("sd", cmd_sd, "the host directory standing in for the card");
    return f_mount(&fs, "", 1);
}
const char *frank_sd_driver(void) { return "host"; }

/* ================= the main thread: SDL ================= */

static void *app_thread(void *u) {
    (void)u;
    frank_app_main();
    frank_exit_requested = true;
    return NULL;
}

int main(int argc, char **argv) {
    int scale = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-sd") && i + 1 < argc) host_sd_root = argv[++i];
        else if (!strcmp(argv[i], "-scale") && i + 1 < argc) scale = atoi(argv[++i]);
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("frank-sdk host", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       FRANK_VIDEO_W * scale, FRANK_VIDEO_H * scale, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                         FRANK_VIDEO_W, FRANK_VIDEO_H);
    pthread_t t;
    pthread_create(&t, NULL, stdin_thread, NULL);
    pthread_detach(t);
    pthread_create(&t, NULL, app_thread, NULL);
    pthread_detach(t);

    static uint32_t px[FRANK_VIDEO_H][FRANK_VIDEO_W];
    while (!frank_exit_requested) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) frank_exit_requested = true;
            else if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_KEYUP) {
                uint8_t k = (uint8_t)e.key.keysym.scancode;
                bool down = e.type == SDL_KEYDOWN;
                if (k >= 0xE0 && k <= 0xE7) mods = down ? (mods | 1u << (k - 0xE0)) : (mods & ~(1u << (k - 0xE0)));
                if (kw - kr < 64) keys[kw++ % 64] = (frank_key_t){k, down, mods};
            } else if (e.type == SDL_MOUSEMOTION) {
                mdx += e.motion.xrel; mdy += e.motion.yrel; mmoved = true;
            } else if (e.type == SDL_MOUSEWHEEL) {
                mwheel += e.wheel.y; mmoved = true;
            } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                uint8_t bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : 4;
                mbuttons = e.type == SDL_MOUSEBUTTONDOWN ? (mbuttons | bit) : (mbuttons & ~bit);
                mmoved = true;
            }
        }
        for (int y = 0; y < FRANK_VIDEO_H; y++) {
            const uint8_t *src = fb_ptr ? fb_ptr + y * FRANK_VIDEO_W : row_fn ? row_fn((uint32_t)y) : NULL;
            for (int x = 0; x < FRANK_VIDEO_W; x++) px[y][x] = src ? pal[src[x]] : 0xFF000000u;
        }
        SDL_UpdateTexture(tex, NULL, px, sizeof(px[0]));
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);   /* waits for the display's vsync */
        frames++;
        if (vsync_fn) vsync_fn();
        /* audio: queued to SDL a frame's worth at a time, about 40 ms ahead */
        SDL_AudioDeviceID dev = audio_dev;
        if (dev) {
            uint32_t buf[2048];
            unsigned n = 0;
            while (ar != aw && n < 2048 && SDL_GetQueuedAudioSize(dev) < 8192) buf[n++] = aring[ar++ % RING];
            if (n) SDL_QueueAudio(dev, buf, n * 4);
        }
    }
    SDL_Quit();
    return 0;
}

/*
 * frank-sdk — sound out over I2S, PWM or HDMI (frank/audio.h).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * I2S: frank-quake2/frank-diablo's ping-pong DMA driver (i2s.c) and frank-dos's
 * TLV320DAC3100 set-up (tlv320.c). PWM: new, DMA into the slice's compare
 * register paced by its wrap (frank-dos drove it from a timer IRQ per sample).
 * The I2S/PWM choice on a shared mux: frank-dos's pin probe.
 */
#include "frank/audio.h"

#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "i2s.h"
#if FRANK_AUDIO_I2S_DAC_TLV320DAC3100
#include "tlv320.h"
#endif

#ifndef FRANK_AUDIO_RING
#define FRANK_AUDIO_RING 2048   /* frames, a power of two */
#endif

static uint32_t ring[FRANK_AUDIO_RING];
static volatile uint32_t ring_w, ring_r;
static volatile unsigned vol_shift;

/* HDMI keeps its own ring (frank/video.h): free space and the count follow the
 * output in use, or a writer waiting for room spins for ever (found on the Next) */
extern unsigned frank_hdmi_audio_free(void) __attribute__((weak));
extern uint32_t frank_hdmi_audio_consumed(void) __attribute__((weak));
static int out_kind = -1;

unsigned frank_audio_free(void) {
    if (out_kind == FRANK_AUDIO_HDMI) return frank_hdmi_audio_free ? frank_hdmi_audio_free() : 0;
    return out_kind < 0 ? 0 : FRANK_AUDIO_RING - (ring_w - ring_r);
}
uint32_t frank_audio_consumed(void) {
    if (out_kind == FRANK_AUDIO_HDMI) return frank_hdmi_audio_consumed ? frank_hdmi_audio_consumed() : 0;
    return ring_r;
}
void frank_audio_set_volume(unsigned shift) { vol_shift = shift > 16 ? 16 : shift; }

static unsigned ring_write(const uint32_t *frames, unsigned n) {
    unsigned room = frank_audio_free();
    if (n > room) n = room;
    uint32_t w = ring_w;
    for (unsigned i = 0; i < n; i++) ring[(w + i) & (FRANK_AUDIO_RING - 1)] = frames[i];
    __dmb();
    ring_w = w + n;
    return n;
}

static inline uint32_t __not_in_flash_func(ring_take)(void) {
    uint32_t r = ring_r;
    if (r == ring_w) return 0;
    uint32_t f = ring[r & (FRANK_AUDIO_RING - 1)];
    ring_r = r + 1;
    if (vol_shift) {
        int16_t l = (int16_t)(f & 0xFFFF), rr = (int16_t)(f >> 16);
        f = (uint16_t)(l >> vol_shift) | (uint32_t)(uint16_t)(rr >> vol_shift) << 16;
    }
    return f;
}

/* ---------------- I2S ---------------- */
#ifdef FRANK_HAS_AUDIO_I2S
static i2s_config_t i2s_cfg;

static void __not_in_flash_func(i2s_fill)(int idx, uint32_t *buf, uint32_t frames) {
    (void)idx;
    for (uint32_t i = 0; i < frames; i++) buf[i] = ring_take();
}

static bool start_i2s(uint32_t rate) {
    memset(&i2s_cfg, 0, sizeof(i2s_cfg));
    i2s_cfg.sample_freq = rate;
    i2s_cfg.channel_count = 2;
    i2s_cfg.data_pin = FRANK_AUDIO_I2S_DATA;
    i2s_cfg.clock_pin_base = FRANK_AUDIO_I2S_BCLK;   /* LRCK = BCLK + 1 */
    i2s_cfg.pio = PIO_INSTANCE(FRANK_PIO_I2S);
    i2s_cfg.dma_trans_count = 256;
    i2s_init(&i2s_cfg);
    i2s_set_fill_callback(i2s_fill);
    i2s_start();
#if FRANK_AUDIO_I2S_DAC_TLV320DAC3100
#ifdef FRANK_AUDIO_I2S_RESET
    /* the codec's reset line (Fruit Jam: GPIO22, the ESP32-C6's too): high
     * to run; it holds the codec silent and deaf to I2C until then */
    gpio_init(FRANK_AUDIO_I2S_RESET);
    gpio_set_dir(FRANK_AUDIO_I2S_RESET, GPIO_OUT);
    gpio_put(FRANK_AUDIO_I2S_RESET, 1);
#endif
    sleep_ms(20);   /* the codec's PLL locks to BCLK: set it up once the clocks run */
    if (!tlv320_init(FRANK_I2C_SDA, FRANK_I2C_SCL)) return false;
#endif
    return true;
}
#endif

/* ---------------- PWM ----------------
 * Left and right on one slice (M2, M1, Z0): one DMA ping-pong writes the
 * slice's compare register, both channels at once. On two slices (PICO-PC:
 * GPIO27/28): a ping-pong per slice, each word carrying its channel's level in
 * both halves (the other channel of each slice is not an audio pin). Paced by
 * the PWM wrap; the refill IRQ runs every PWM_BUF samples. */
#ifdef FRANK_HAS_AUDIO_PWM
#define PWM_BUF 256
static uint32_t pwm_buf[2][2][PWM_BUF] __attribute__((aligned(4)));   /* [slice][half] */
static int pwm_dma[2][2];
static uint32_t pwm_wrap;
static uint pwm_slice[2];
static int pwm_nslices;

static inline uint32_t __not_in_flash_func(level)(int16_t s) {
    return ((uint32_t)((int32_t)s + 32768) * pwm_wrap) >> 16;
}

/* the words for frame f: [0] for slice 0, [1] for slice 1 */
static inline void __not_in_flash_func(pwm_words)(uint32_t f, uint32_t *w0, uint32_t *w1) {
    const uint32_t l = level((int16_t)(f & 0xFFFF)), r = level((int16_t)(f >> 16));
    if (pwm_nslices == 1) {
        *w0 = (FRANK_AUDIO_PWM_LEFT & 1) ? (r | l << 16) : (l | r << 16);   /* A = even GPIO */
    } else {
        *w0 = l | l << 16;
        *w1 = r | r << 16;
    }
}

static void __not_in_flash_func(pwm_fill)(int half) {
    uint32_t *a = pwm_buf[0][half], *b = pwm_buf[1][half], dummy;
    for (int i = 0; i < PWM_BUF; i++) pwm_words(ring_take(), &a[i], pwm_nslices > 1 ? &b[i] : &dummy);
}

static void __not_in_flash_func(pwm_dma_irq)(void) {
    for (int h = 0; h < 2; h++) {
        if (!(dma_hw->ints1 & (1u << pwm_dma[0][h]))) continue;
        for (int s = 0; s < pwm_nslices; s++) dma_hw->ints1 = 1u << pwm_dma[s][h];
        pwm_fill(h);
        for (int s = 0; s < pwm_nslices; s++) dma_channel_set_read_addr(pwm_dma[s][h], pwm_buf[s][h], false);
    }
}

static bool start_pwm(uint32_t rate) {
    pwm_slice[0] = pwm_gpio_to_slice_num(FRANK_AUDIO_PWM_LEFT);
    pwm_slice[1] = pwm_gpio_to_slice_num(FRANK_AUDIO_PWM_RIGHT);
    pwm_nslices = pwm_slice[0] == pwm_slice[1] ? 1 : 2;
    const uint32_t sys = clock_get_hz(clk_sys);
    pwm_wrap = sys / rate - 1;           /* 504 MHz / 44.1 kHz: 11428 steps, ~13.5 bits */
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&c, 1);
    pwm_config_set_wrap(&c, (uint16_t)pwm_wrap);
    gpio_set_function(FRANK_AUDIO_PWM_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(FRANK_AUDIO_PWM_RIGHT, GPIO_FUNC_PWM);
    for (int s = 0; s < pwm_nslices; s++) {
        pwm_init(pwm_slice[s], &c, false);
        pwm_set_both_levels(pwm_slice[s], pwm_wrap / 2, pwm_wrap / 2);
    }
    pwm_fill(0);
    pwm_fill(1);
    for (int s = 0; s < pwm_nslices; s++)
        for (int h = 0; h < 2; h++) pwm_dma[s][h] = dma_claim_unused_channel(true);
    for (int s = 0; s < pwm_nslices; s++)
        for (int h = 0; h < 2; h++) {
            dma_channel_config d = dma_channel_get_default_config(pwm_dma[s][h]);
            channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
            channel_config_set_read_increment(&d, true);
            channel_config_set_write_increment(&d, false);
            channel_config_set_dreq(&d, pwm_get_dreq(pwm_slice[s]));
            channel_config_set_chain_to(&d, pwm_dma[s][h ^ 1]);
            dma_channel_configure(pwm_dma[s][h], &d, &pwm_hw->slice[pwm_slice[s]].cc, pwm_buf[s][h], PWM_BUF, false);
            if (s == 0) dma_channel_set_irq1_enabled(pwm_dma[s][h], true);
        }
    /* DMA_IRQ_1 on this core; i2s.c's own handler is not installed in a PWM run */
    irq_add_shared_handler(DMA_IRQ_1, pwm_dma_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);
    /* the slices start together, so their wraps (and DREQs) stay in step */
    uint32_t mask = 0;
    for (int s = 0; s < pwm_nslices; s++) mask |= 1u << pwm_slice[s];
    for (int s = 0; s < pwm_nslices; s++) dma_channel_start(pwm_dma[s][0]);
    hw_set_bits(&pwm_hw->en, mask);   /* only ours: pwm_set_mask_enabled would stop other slices */
    return true;
}
#endif

/* ---------------- HDMI ---------------- */
extern unsigned frank_hdmi_audio_write(const uint32_t *frames, unsigned n) __attribute__((weak));

unsigned frank_audio_write(const uint32_t *frames, unsigned n) {
    if (out_kind == FRANK_AUDIO_HDMI) return frank_hdmi_audio_write ? frank_hdmi_audio_write(frames, n) : 0;
    return ring_write(frames, n);
}

const char *frank_audio_name(void) {
    static const char *names[] = {"none", "i2s", "pwm", "hdmi"};
    return out_kind < 0 ? names[0] : names[out_kind];
}

/* The M2/Z0 audio mux: a DAC's inputs read differently from the RC filter's
 * (frank-dos audio_init; "HIGH under pull-down" is a back-feed, not a DAC) */
extern int testPins(uint32_t pin0, uint32_t pin1);

int frank_audio_init(frank_audio_out_t out, uint32_t rate) {
    if (out == FRANK_AUDIO_AUTO) {
#if defined(FRANK_HAS_AUDIO_I2S) && defined(FRANK_HAS_AUDIO_PWM)
        gpio_init(FRANK_AUDIO_I2S_DATA);
        gpio_set_dir(FRANK_AUDIO_I2S_DATA, GPIO_OUT);
        gpio_put(FRANK_AUDIO_I2S_DATA, 0);
        gpio_init(FRANK_AUDIO_I2S_BCLK);
        gpio_set_dir(FRANK_AUDIO_I2S_BCLK, GPIO_OUT);
        gpio_put(FRANK_AUDIO_I2S_BCLK, 0);
        sleep_ms(50);   /* residual charge drained before the probe */
        gpio_deinit(FRANK_AUDIO_I2S_DATA);
        gpio_deinit(FRANK_AUDIO_I2S_BCLK);
        int code = testPins(FRANK_AUDIO_I2S_DATA, FRANK_AUDIO_I2S_BCLK);
        if (code & 0x14) code = 0;
        out = code ? FRANK_AUDIO_I2S : FRANK_AUDIO_PWM;
#elif defined(FRANK_HAS_AUDIO_I2S)
        out = FRANK_AUDIO_I2S;
#elif defined(FRANK_HAS_AUDIO_PWM)
        out = FRANK_AUDIO_PWM;
#else
        out = FRANK_AUDIO_HDMI;
#endif
    }
    bool ok = false;
    switch (out) {
#ifdef FRANK_HAS_AUDIO_I2S
    case FRANK_AUDIO_I2S: ok = start_i2s(rate); break;
#endif
#ifdef FRANK_HAS_AUDIO_PWM
    case FRANK_AUDIO_PWM: ok = start_pwm(rate); break;
#endif
    case FRANK_AUDIO_HDMI: ok = frank_hdmi_audio_write != 0; break;
    default: break;
    }
    out_kind = ok ? (int)out : -1;
    printf("audio: %s at %lu Hz\n", ok ? frank_audio_name() : "no such output on this board", (unsigned long)rate);
    return out_kind;
}

/*
 * frank-sdk — sound out: I2S DAC, PWM, or HDMI, one API.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * The firmware writes 16-bit stereo frames (left in the low half) into a ring;
 * the output drains it at the sample rate, silence when it runs dry.
 *   I2S   the board's DAC (TDA1387 on M2/C2, PCM5102A module on Z0) or the
 *         TLV320DAC3100 codec (Next, FZ: set up over I2C once BCLK runs);
 *         PIO + ping-pong DMA, its IRQ on the calling core (core 0: never the
 *         video core, the I2S IRQ there breaks HDMI timing);
 *   PWM   the M2/Z0 audio pins through the RC filter: DMA paced by the PWM
 *         wrap feeds both channels' compare register, no IRQ per sample;
 *   HDMI  the HDMI audio ring of frank/video.h (link frank_video too).
 * AUTO picks HDMI-less analog: on the M2 and Z0 the I2S and PWM paths share
 * pins behind the audio mux, and a pin probe tells a DAC from the RC filter
 * (frank-dos); boards with one kind take it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FRANK_AUDIO_AUTO = 0,
    FRANK_AUDIO_I2S = 1,
    FRANK_AUDIO_PWM = 2,
    FRANK_AUDIO_HDMI = 3,
    /* the board's codec (TLV320DAC3100: Next, FRANK Z0, Fruit Jam) to one
     * output only; FRANK_AUDIO_I2S drives both. The speaker is mono: L+R. */
    FRANK_AUDIO_SPEAKER = 4,
    FRANK_AUDIO_HEADPHONES = 5,
    /* a PCM5122 DAC HAT on the Pi header (boards/<id>.yaml audio.hat) */
    FRANK_AUDIO_HAT = 6,
} frank_audio_out_t;

/* Can this board (this build) play through out? HAT: one answers on its I2C. */
bool frank_audio_available(frank_audio_out_t out);

/* Returns the output in use (the one asked, or what AUTO chose), or -1 if
 * the board has none of the kind asked. */
int frank_audio_init(frank_audio_out_t out, uint32_t sample_rate);
/* The same at run time: stops the output that plays (its PIO, DMA, pins and
 * interrupt given back) and starts out. Returns as frank_audio_init. */
int frank_audio_switch(frank_audio_out_t out, uint32_t sample_rate);
const char *frank_audio_name(void);

/* Queue frames; returns how many fitted (FRANK_AUDIO_RING frames, 2048 default). */
unsigned frank_audio_write(const uint32_t *frames, unsigned n);
unsigned frank_audio_free(void);
uint32_t frank_audio_consumed(void);   /* frames played so far: the real rate */

/* 0 (full) .. 16 (silent): a right shift on every sample */
void frank_audio_set_volume(unsigned shift);

#ifdef __cplusplus
}
#endif

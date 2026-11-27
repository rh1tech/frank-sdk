/*
 * frank-386 - TLV320DAC3100 codec bring-up (FRANK Next)
 *
 * The register sequence is frank-test's drivers/tlv320 (heard on a FRANK
 * Next), over the I2C1 peripheral instead of a bit-banged bus.
 *
 * The codec only plays once it is configured, and its PLL locks to BCLK:
 * nothing drives MCLK (GPIO8), so tlv320_init() must run with the I2S clocks
 * already going (audio_init() calls it right after i2s_init()).
 *
 * Clocks: the PIO sends 16-bit stereo, BCLK = 32 fs = 1.4112 MHz at 44.1 kHz.
 *   PLL_CLK = BCLK x R x J.D / P = 1.4112 MHz x 60 = 84.672 MHz
 *   fs      = PLL_CLK / NDAC / MDAC / DOSR = 84.672 MHz / 5 / 3 / 128
 *
 * Both outputs are powered: the headphone drivers (HPL/HPR) and the mono
 * Class-D amp, which takes the left mixer.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */

#include "tlv320.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define TLV320_ADDR 0x18

static i2c_inst_t *bus;

static bool wr(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = { reg, val };
    return i2c_write_timeout_us(bus, TLV320_ADDR, buf, 2, false, 2000) == 2;
}

bool tlv320_init(unsigned sda, unsigned scl) {
    return tlv320_init_route(sda, scl, TLV320_HEADPHONES | TLV320_SPEAKER);
}

bool tlv320_init_route(unsigned sda, unsigned scl, unsigned route) {
    const bool hp = route & TLV320_HEADPHONES, spk = route & TLV320_SPEAKER;
    bus = (sda / 2) & 1 ? i2c1 : i2c0;
    i2c_init(bus, 100 * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);

    /* page 0: clocking, the serial interface, the digital DAC */
    bool ok = wr(0x00, 0);
    if (!ok) {
        printf("TLV320: no answer at 0x%02x\n", TLV320_ADDR);
        return false;
    }
    ok = ok && wr(0x01, 0x01);          /* software reset */
    sleep_ms(2);
    ok = ok && wr(0x04, 0x07);          /* PLL_CLKIN = BCLK, CODEC_CLKIN = PLL */
    ok = ok && wr(0x05, 0x91);          /* PLL on, P = 1, R = 1 */
    ok = ok && wr(0x06, 60);            /* J */
    ok = ok && wr(0x07, 0);             /* D */
    ok = ok && wr(0x08, 0);
    sleep_ms(10);                       /* PLL lock */
    ok = ok && wr(0x0B, 0x80 | 5);      /* NDAC */
    ok = ok && wr(0x0C, 0x80 | 3);      /* MDAC */
    ok = ok && wr(0x0D, 0);             /* DOSR = 128 */
    ok = ok && wr(0x0E, 128);
    ok = ok && wr(0x1B, 0x00);          /* I2S, 16 bit, BCLK/WCLK inputs */

    /* page 1: the analogue side */
    ok = ok && wr(0x00, 1);
    ok = ok && wr(0x23, 0x44);          /* each DAC into its channel's mixer */
    if (hp) {
        ok = ok && wr(0x21, 0x9E);      /* headphone de-pop: 1.53 ms, 3.9 ms steps */
        ok = ok && wr(0x1F, 0xC4);      /* HPL, HPR on, 1.35 V common mode */
        ok = ok && wr(0x24, 0x80);      /* left mixer -> HPL volume, 0 dB */
        ok = ok && wr(0x25, 0x80);      /* right mixer -> HPR volume, 0 dB */
    }
    if (spk) {
        ok = ok && wr(0x26, 0x80);      /* left mixer -> Class-D volume, 0 dB */
        ok = ok && wr(0x20, 0x80);      /* Class-D on */
    }
    sleep_ms(5);
    if (hp) {
        ok = ok && wr(0x28, 0x06);      /* HPL 0 dB, unmuted */
        ok = ok && wr(0x29, 0x06);      /* HPR 0 dB, unmuted */
    }
    if (spk) ok = ok && wr(0x2A, 0x04); /* Class-D 6 dB (its lowest), unmuted */

    /* page 0: the DACs on, unmuted last */
    ok = ok && wr(0x00, 0);
    /* both DACs on, left/right data; the speaker alone: the left DAC (L+R)/2 */
    ok = ok && wr(0x3F, spk && !hp ? 0xF4 : 0xD4);
    ok = ok && wr(0x41, 0x00);          /* 0 dB */
    ok = ok && wr(0x42, 0x00);
    ok = ok && wr(0x40, 0x00);          /* unmute */

    printf("TLV320: %s\n", !ok ? "a write was not acknowledged"
                            : hp && spk ? "configured (headphones and speaker)"
                            : hp ? "configured (headphones)" : "configured (speaker, L+R)");
    return ok;
}

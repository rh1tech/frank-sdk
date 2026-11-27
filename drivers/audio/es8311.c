/*
 * frank-sdk — the Everest ES8311 codec (the Olimex RP2350pc's U8), over I2C.
 *
 * frank-test's es8311.c (proved on the RP2350pc) on the hardware I2C block
 * instead of a bit-banged bus. Slave mode, MCLK from the MCLK pin at 256 fs
 * (frank_audio drives it from clk_sys and locks the I2S clock to it), I2S
 * 16-bit, DAC at 0 dB. The codec is mono: both halves of a frame reach it.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */

#include "es8311.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define ES8311_ADDR 0x18

static i2c_inst_t *bus;

static bool wr(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_write_timeout_us(bus, ES8311_ADDR, buf, 2, false, 2000) == 2;
}

bool es8311_init(unsigned sda, unsigned scl) {
    bus = (sda / 2) & 1 ? i2c1 : i2c0;
    i2c_init(bus, 100 * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);

    /* MCLK = 256 fs: pre-divider 1, multiplier 1, ADC and DAC dividers 1,
     * single speed, oversampling 0x10 (the reference table's 256 fs line) */
    static const struct { uint8_t reg, val; } seq[] = {
        {0x00, 0x1F},   /* reset everything */
        {0x00, 0x00},
        {0x01, 0x30},
        {0x02, 0x00},
        {0x03, 0x10},
        {0x16, 0x24},
        {0x04, 0x10},
        {0x05, 0x00},
        {0x0B, 0x00},
        {0x0C, 0x00},
        {0x10, 0x1F},
        {0x11, 0x7F},
        {0x00, 0x80},   /* on, slave */
        {0x01, 0x3F},   /* every clock on, MCLK from the MCLK pin */
        {0x06, 0x03},   /* BCLK not inverted */
        {0x07, 0x00},
        {0x08, 0xFF},
        {0x13, 0x10},   /* output drive on */
        {0x1B, 0x0A},
        {0x1C, 0x6A},
        {0x09, 0x0C},   /* the DAC's port: I2S, 16-bit */
        {0x0A, 0x0C},
        {0x17, 0xBF},
        {0x0E, 0x02},
        {0x12, 0x00},   /* DAC powered */
        {0x14, 0x1A},
        {0x0D, 0x01},   /* analog powered */
        {0x15, 0x40},
        {0x37, 0x08},   /* DAC equaliser bypassed */
        {0x45, 0x00},
        {0x32, 0xBF},   /* 0 dB */
        {0x31, 0x00},   /* unmuted */
    };
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        if (!wr(seq[i].reg, seq[i].val)) {
            printf("ES8311: no answer at 0x%02x (register 0x%02x)\n", ES8311_ADDR, seq[i].reg);
            return false;
        }
        if (i == 0) sleep_ms(20);   /* the reset needs a moment */
    }
    return true;
}

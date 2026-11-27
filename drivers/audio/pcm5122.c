/*
 * frank-sdk — the TI PCM5122 DAC of a Raspberry Pi DAC HAT, over I2C.
 *
 * frank-test's pcm5122.c (proved on the Waveshare PiZero, as pico-speccy
 * drives the same HAT) on the hardware I2C block. The HAT brings out BCK,
 * LRCK and DIN and no system clock: the PLL takes BCK as its reference and
 * the clock error detector is told to ignore the missing SCK. Left at the
 * datasheet's defaults the DAC waits for a master clock that never comes:
 * every write is acknowledged and nothing is heard.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */

#include "pcm5122.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define PCM5122_ADDR 0x4C   /* the HAT is strapped to the low address */

static i2c_inst_t *bus;

static bool wr(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_write_timeout_us(bus, PCM5122_ADDR, buf, 2, false, 2000) == 2;
}

static void bus_up(unsigned sda, unsigned scl) {
    bus = (sda / 2) & 1 ? i2c1 : i2c0;
    i2c_init(bus, 100 * 1000);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);
}

static void bus_down(unsigned sda, unsigned scl) {
    i2c_deinit(bus);
    gpio_deinit(sda);
    gpio_deinit(scl);
    gpio_disable_pulls(sda);
    gpio_disable_pulls(scl);
}

bool pcm5122_detect(unsigned sda, unsigned scl) {
    bus_up(sda, scl);
    uint8_t b = 0;
    const bool found = i2c_read_timeout_us(bus, PCM5122_ADDR, &b, 1, false, 2000) == 1;
    bus_down(sda, scl);
    return found;
}

bool pcm5122_init(unsigned sda, unsigned scl) {
    bus_up(sda, scl);
    bool ok = wr(0, 0x00);   /* page 0 */
    if (!ok) {
        printf("PCM5122: no answer at 0x%02x\n", PCM5122_ADDR);
        bus_down(sda, scl);
        return false;
    }
    ok = ok && wr(1, 0x11);    /* reset the registers and the modules */
    sleep_ms(100);
    ok = ok && wr(13, 0x10);   /* PLL reference: BCK */
    ok = ok && wr(37, 0x08);   /* ignore a missing SCK */
    ok = ok && wr(2, 0x00);    /* out of standby */
    ok = ok && wr(3, 0x00);    /* unmuted */
    ok = ok && wr(61, 0x30);   /* 0 dB, left */
    ok = ok && wr(62, 0x30);   /* 0 dB, right */
    return ok;
}

/*
 * frank-386 - TLV320DAC3100 codec bring-up (FRANK Next)
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#ifndef TLV320_H
#define TLV320_H

#include <stdbool.h>

/* 16-bit I2S at 44.1 kHz, the PLL on BCLK; call with the I2S clocks running.
 * False if the codec did not acknowledge a write. */
bool tlv320_init(unsigned sda, unsigned scl);

/* The same, with the outputs chosen (frank-test's TLV320_OUT_HP / SPK): the
 * headphone drivers, the mono Class-D speaker amp, or both (tlv320_init).
 * With the speaker alone the left DAC plays (L+R)/2: the amp takes only the
 * left mixer, and a stereo mix would lose its right half. */
#define TLV320_HEADPHONES 1u
#define TLV320_SPEAKER    2u
bool tlv320_init_route(unsigned sda, unsigned scl, unsigned route);

#endif

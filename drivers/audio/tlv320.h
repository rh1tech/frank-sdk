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

#endif

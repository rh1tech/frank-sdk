/*
 * frank-sdk — the Everest ES8311 codec (es8311.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

/* Slave, MCLK at 256 fs on its MCLK pin, I2S 16-bit; once MCLK and BCLK run */
bool es8311_init(unsigned sda, unsigned scl);

/*
 * frank-sdk — the PCM5122 DAC of a Raspberry Pi DAC HAT (pcm5122.c).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

/* Is the HAT there (an answer at 0x4C)? The bus is released again. */
bool pcm5122_detect(unsigned sda, unsigned scl);
/* PLL from BCK, no SCK, unmuted at 0 dB; once BCK runs */
bool pcm5122_init(unsigned sda, unsigned scl);

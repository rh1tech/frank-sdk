/*
 * frank-sdk — the battery-backed wall clock: DS3231 or RV-3032.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * Two chips in the fleet:
 *   DS3231   at 0x68: FRANK Next (GP2/3), MegaFRANK (GP28/29), xt8086 (GP38/39)
 *   RV-3032  at 0x51: the new FRANK mini (GP28/29)
 * The bus is the caller's: two functions that read and write registers, so a
 * firmware on the board's hardware I2C (frank_rtc_use_i2c) and one with its
 * own bit-banged bus (frank-test's i2c_bb) share the chip code.
 *
 * "Lost" is the chip's own record that the time cannot be trusted: the
 * DS3231's oscillator-stop flag (OSF), the RV-3032's power-on reset and
 * voltage-low flags (PORF, VLF). Only frank_rtc_set() and frank_rtc_clear_lost()
 * clear it, never a read or the boot: it is the one evidence of a flat coin
 * cell (frank-test rtc_ds3231.h).
 *
 * From frank-test core/rtc_ds3231.c (DS3231); the RV-3032 half is new and
 * written from Micro Crystal's application manual: not yet tried on a board.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRANK_RTC_DS3231_ADDR 0x68u
#define FRANK_RTC_RV3032_ADDR 0x51u

typedef enum {
    FRANK_RTC_NONE = 0,
    FRANK_RTC_DS3231,
    FRANK_RTC_RV3032,
} frank_rtc_chip_t;

typedef struct {
    uint16_t year;   /* 2000..2099: both chips keep two digits */
    uint8_t mon;     /* 1..12 */
    uint8_t day;     /* 1..31 */
    uint8_t hour;    /* 0..23 */
    uint8_t min;     /* 0..59 */
    uint8_t sec;     /* 0..59 */
} frank_rtc_time_t;

/* n registers from reg on, and a write whose buf[0] is the first register */
typedef bool (*frank_rtc_read_fn)(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf, size_t n);
typedef bool (*frank_rtc_write_fn)(void *ctx, uint8_t addr7, const uint8_t *buf, size_t n);

typedef struct {
    frank_rtc_chip_t chip;
    frank_rtc_read_fn rd;
    frank_rtc_write_fn wr;
    void *ctx;
} frank_rtc_t;

/* Which chip answers on this bus (DS3231 tried first), FRANK_RTC_NONE if
 * neither. Fills r for the calls below. */
frank_rtc_chip_t frank_rtc_attach(frank_rtc_t *r, frank_rtc_read_fn rd, frank_rtc_write_fn wr, void *ctx);

/* The board's hardware I2C (FRANK_I2C_INST/SDA/SCL from the board file) at
 * 100 kHz, then frank_rtc_attach. FRANK_RTC_NONE on a board without I2C. */
frank_rtc_chip_t frank_rtc_init_board(frank_rtc_t *r);

/* False: the bus did not answer (out untouched). lost may be NULL. */
bool frank_rtc_read(const frank_rtc_t *r, frank_rtc_time_t *out, bool *lost);
/* A valid time, the seconds restarted from it, and lost cleared. */
bool frank_rtc_set(const frank_rtc_t *r, const frank_rtc_time_t *t);
bool frank_rtc_clear_lost(const frank_rtc_t *r);

/* One register, for the bench (the RV-3032's PMU byte at 0xC0: backup
 * switchover, trickle charge) */
bool frank_rtc_reg_read(const frank_rtc_t *r, uint8_t reg, uint8_t *v);
bool frank_rtc_reg_write(const frank_rtc_t *r, uint8_t reg, uint8_t v);

bool frank_rtc_time_valid(const frank_rtc_time_t *t);
uint8_t frank_rtc_days_in_month(uint16_t year, uint8_t mon);
/* 0 = Sunday */
uint8_t frank_rtc_weekday(const frank_rtc_time_t *t);
const char *frank_rtc_chip_name(frank_rtc_chip_t chip);

#ifdef __cplusplus
}
#endif

/*
 * frank-sdk — DS3231 and RV-3032 (frank/rtc.h).
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * SPDX-License-Identifier: MIT
 *
 * DS3231 from frank-test core/rtc_ds3231.c. RV-3032 from Micro Crystal's
 * RV-3032-C7 application manual (registers 0x01..0x07 the time in BCD,
 * 0x0D the status: VLF bit 0, PORF bit 1; a flag is cleared by writing 0,
 * a 1 written leaves it as it is). Untried on a board so far.
 */
#include "frank/rtc.h"

#include "pico/stdlib.h"
#if defined(FRANK_HAS_I2C) && FRANK_HAS_I2C
#include "hardware/i2c.h"
#endif

#define DS_SECS 0x00u
#define DS_STAT 0x0Fu
#define DS_OSF 0x80u

#define RV_SECS 0x01u
#define RV_STAT 0x0Du
#define RV_VLF 0x01u
#define RV_PORF 0x02u

static unsigned from_bcd(uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); }
static uint8_t to_bcd(unsigned v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }

static uint8_t addr_of(frank_rtc_chip_t c) {
    return c == FRANK_RTC_DS3231 ? FRANK_RTC_DS3231_ADDR : FRANK_RTC_RV3032_ADDR;
}

const char *frank_rtc_chip_name(frank_rtc_chip_t chip) {
    return chip == FRANK_RTC_DS3231 ? "DS3231" : chip == FRANK_RTC_RV3032 ? "RV-3032" : "none";
}

frank_rtc_chip_t frank_rtc_attach(frank_rtc_t *r, frank_rtc_read_fn rd, frank_rtc_write_fn wr, void *ctx) {
    r->chip = FRANK_RTC_NONE;
    r->rd = rd;
    r->wr = wr;
    r->ctx = ctx;
    uint8_t v;
    if (rd(ctx, FRANK_RTC_DS3231_ADDR, DS_STAT, &v, 1)) r->chip = FRANK_RTC_DS3231;
    else if (rd(ctx, FRANK_RTC_RV3032_ADDR, RV_STAT, &v, 1)) r->chip = FRANK_RTC_RV3032;
    return r->chip;
}

uint8_t frank_rtc_days_in_month(uint16_t year, uint8_t mon) {
    static const uint8_t len[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mon < 1u || mon > 12u) return 31u;
    if (mon == 2u) return ((year % 4u == 0u) && (year % 100u != 0u || year % 400u == 0u)) ? 29u : 28u;
    return len[mon - 1u];
}

bool frank_rtc_time_valid(const frank_rtc_time_t *t) {
    if (!t || t->year < 2000u || t->year > 2099u || t->mon < 1u || t->mon > 12u) return false;
    if (t->day < 1u || t->day > frank_rtc_days_in_month(t->year, t->mon)) return false;
    return t->hour <= 23u && t->min <= 59u && t->sec <= 59u;
}

uint8_t frank_rtc_weekday(const frank_rtc_time_t *t) {
    unsigned days = 0;
    for (uint16_t y = 2000u; y < t->year; y++)
        days += ((y % 4u == 0u) && (y % 100u != 0u || y % 400u == 0u)) ? 366u : 365u;
    for (uint8_t m = 1u; m < t->mon; m++) days += frank_rtc_days_in_month(t->year, m);
    days += (unsigned)(t->day - 1u);
    return (uint8_t)((days + 6u) % 7u);   /* 2000-01-01 was a Saturday */
}

bool frank_rtc_read(const frank_rtc_t *r, frank_rtc_time_t *out, bool *lost) {
    if (!r || r->chip == FRANK_RTC_NONE || !out) return false;
    const bool ds = r->chip == FRANK_RTC_DS3231;
    uint8_t b[7], stat = 0;
    if (!r->rd(r->ctx, addr_of(r->chip), ds ? DS_SECS : RV_SECS, b, sizeof(b))) return false;
    const bool got_stat = r->rd(r->ctx, addr_of(r->chip), ds ? DS_STAT : RV_STAT, &stat, 1);
    out->sec = (uint8_t)from_bcd(b[0] & 0x7Fu);
    out->min = (uint8_t)from_bcd(b[1] & 0x7Fu);
    out->hour = (uint8_t)from_bcd(b[2] & 0x3Fu);   /* DS3231 bit 6 (12-hour mode) masked off */
    out->day = (uint8_t)from_bcd(b[4] & 0x3Fu);
    out->mon = (uint8_t)from_bcd(b[5] & 0x1Fu);    /* DS3231 bit 7 is the century */
    out->year = (uint16_t)(2000u + from_bcd(b[6]));
    if (lost) *lost = got_stat && (stat & (ds ? DS_OSF : (RV_VLF | RV_PORF)));
    return true;
}

bool frank_rtc_clear_lost(const frank_rtc_t *r) {
    if (!r || r->chip == FRANK_RTC_NONE) return false;
    const bool ds = r->chip == FRANK_RTC_DS3231;
    uint8_t stat;
    if (!r->rd(r->ctx, addr_of(r->chip), ds ? DS_STAT : RV_STAT, &stat, 1)) return false;
    /* read-modify-write: the DS3231's register also holds the 32 kHz output
     * enable and the alarm flags */
    const uint8_t w[2] = {ds ? DS_STAT : RV_STAT, (uint8_t)(stat & (uint8_t)~(ds ? DS_OSF : (RV_VLF | RV_PORF)))};
    return r->wr(r->ctx, addr_of(r->chip), w, sizeof(w));
}

bool frank_rtc_reg_read(const frank_rtc_t *r, uint8_t reg, uint8_t *v) {
    return r && r->chip != FRANK_RTC_NONE && r->rd(r->ctx, addr_of(r->chip), reg, v, 1);
}

bool frank_rtc_reg_write(const frank_rtc_t *r, uint8_t reg, uint8_t v) {
    const uint8_t w[2] = {reg, v};
    return r && r->chip != FRANK_RTC_NONE && r->wr(r->ctx, addr_of(r->chip), w, sizeof(w));
}

/* RV-3032 configuration and EEPROM (application manual: the PMU byte's BSM
 * field, bits 5:4, 00/11 off, 01 direct, 10 level switching; Control 1's EERD
 * stops the automatic EEPROM refresh while the EEPROM is written; command
 * 0x11 at 0x3F copies the configuration RAM into it; EEbusy and EEF in the
 * temperature LSB register) */
#define RV_PMU 0xC0u
#define RV_BSM_MASK 0x30u
#define RV_BSM_LEVEL 0x20u
#define RV_CTRL1 0x10u
#define RV_EERD 0x04u
#define RV_EECMD 0x3Fu
#define RV_EE_UPDATE 0x11u
#define RV_TEMP_LSB 0x0Eu
#define RV_EEBUSY 0x04u
#define RV_EEF 0x08u

bool frank_rtc_backup_on(const frank_rtc_t *r) {
    if (!r || r->chip == FRANK_RTC_NONE) return false;
    if (r->chip == FRANK_RTC_DS3231) return true;
    uint8_t pmu;
    if (!frank_rtc_reg_read(r, RV_PMU, &pmu)) return false;
    const uint8_t bsm = pmu & RV_BSM_MASK;
    return bsm == 0x10u || bsm == 0x20u;
}

bool frank_rtc_enable_backup(const frank_rtc_t *r, bool persist) {
    if (!r || r->chip == FRANK_RTC_NONE) return false;
    if (r->chip == FRANK_RTC_DS3231) return true;
    uint8_t pmu, ctrl1, t = 0;
    if (!frank_rtc_reg_read(r, RV_PMU, &pmu) || !frank_rtc_reg_read(r, RV_CTRL1, &ctrl1)) return false;
    pmu = (uint8_t)((pmu & ~RV_BSM_MASK) | RV_BSM_LEVEL);
    if (!persist) return frank_rtc_reg_write(r, RV_PMU, pmu);
    /* the refresh off, the RAM byte, the RAM copied into the EEPROM, wait */
    bool ok = frank_rtc_reg_write(r, RV_CTRL1, (uint8_t)(ctrl1 | RV_EERD)) &&
              frank_rtc_reg_write(r, RV_PMU, pmu) &&
              frank_rtc_reg_write(r, RV_EECMD, RV_EE_UPDATE);
    bool busy = true;
    for (int i = 0; ok && busy && i < 200; i++) {   /* a write takes ~10 ms, up to 50 */
        sleep_ms(1);
        ok = frank_rtc_reg_read(r, RV_TEMP_LSB, &t);
        busy = ok && (t & RV_EEBUSY);
    }
    ok = ok && !busy && !(t & RV_EEF);
    /* the refresh back on whatever happened */
    return frank_rtc_reg_write(r, RV_CTRL1, (uint8_t)(ctrl1 & ~RV_EERD)) && ok;
}

bool frank_rtc_set(const frank_rtc_t *r, const frank_rtc_time_t *t) {
    if (!r || r->chip == FRANK_RTC_NONE || !frank_rtc_time_valid(t)) return false;
    const bool ds = r->chip == FRANK_RTC_DS3231;
    const uint8_t wd = frank_rtc_weekday(t);
    /* seven registers in one burst from the seconds: the whole date lands at
     * once, and the count restarts from a known edge */
    const uint8_t buf[8] = {
        ds ? DS_SECS : RV_SECS,
        to_bcd(t->sec),
        to_bcd(t->min),
        to_bcd(t->hour),                     /* 24-hour */
        (uint8_t)(ds ? wd + 1u : wd),        /* DS3231 1..7, RV-3032 0..6; Sunday first */
        to_bcd(t->day),
        to_bcd(t->mon),
        to_bcd((unsigned)(t->year - 2000u)),
    };
    return r->wr(r->ctx, addr_of(r->chip), buf, sizeof(buf)) && frank_rtc_clear_lost(r);
}

/* ---- the board's hardware I2C ---- */

#if defined(FRANK_HAS_I2C) && FRANK_HAS_I2C
#define BUS (FRANK_I2C_INST ? i2c1 : i2c0)

static bool hw_read(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *buf, size_t n) {
    (void)ctx;
    if (i2c_write_timeout_us(BUS, addr7, &reg, 1, true, 2000) != 1) return false;
    return i2c_read_timeout_us(BUS, addr7, buf, n, false, 2000 + 200 * n) == (int)n;
}

static bool hw_write(void *ctx, uint8_t addr7, const uint8_t *buf, size_t n) {
    (void)ctx;
    return i2c_write_timeout_us(BUS, addr7, buf, n, false, 2000 + 200 * n) == (int)n;
}

frank_rtc_chip_t frank_rtc_init_board(frank_rtc_t *r) {
    i2c_init(BUS, 100 * 1000);
    gpio_set_function(FRANK_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(FRANK_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(FRANK_I2C_SDA);
    gpio_pull_up(FRANK_I2C_SCL);
    return frank_rtc_attach(r, hw_read, hw_write, NULL);
}
#else
frank_rtc_chip_t frank_rtc_init_board(frank_rtc_t *r) {
    r->chip = FRANK_RTC_NONE;
    return FRANK_RTC_NONE;
}
#endif

/*
 * frank-386 — the microSD in 4-bit SDIO (FRANK Next), FatFs's diskio.
 * From frank-react's drivers/sdio (same author).
 *
 * SPDX-License-Identifier: MIT
 *
 * The bus is rabbitholecomputing's SDIO_RP2350 (sdio_rp2350.cpp, MIT); this is the card
 * on top of it, in place of its SdFat glue: the card brought up (CMD0, CMD8, ACMD41,
 * CMD2, CMD3, CMD9, CMD7, the 4-bit bus by ACMD6, high speed by CMD6, the clock dropping
 * a step when the card cannot keep up), then sectors by CMD17/18 and CMD24/25.
 *
 * The SPI driver's (drivers/sdcard) habits are kept: the DMA writes SRAM only (into
 * PSRAM it held the bus the HDMI scanout needs), so PSRAM buffers go through a bounce
 * buffer; single sectors (FatFs's FAT and directories) go through a cache of 64; the
 * platform's sd_wait_hook runs while it waits; "sd" (sd_stats_report) says where the
 * time went.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/time.h"
#include "sdio_rp2350.h"
#if SD_SLAVE_CACHE
#include "sc_master.h"
#endif

int sdio_dma_a = -1, sdio_dma_b = -1;

#define CMD0 0
#define CMD2 2
#define CMD3 3
#define CMD6 6
#define CMD7 7
#define CMD8 8
#define CMD9 9
#define CMD12 12
#define CMD13 13
#define CMD16 16
#define CMD17 17
#define CMD18 18
#define CMD24 24
#define CMD25 25
#define CMD55 55
#define ACMD6 6
#define ACMD41 41

static volatile DSTATUS Stat = STA_NOINIT;
static uint32_t rca, ocr, sectors;
static uint8_t csd[16];
static rp2350_sdio_mode_t mode_max = SDIO_HIGHSPEED_OVERCLOCK, mode_now;
static uint32_t clk_hz;

/* ("sd" console command) */
static uint32_t n_reads, n_blocks, n_writes, w_blocks, n_retries, n_errors;
static uint64_t us_reads, us_writes;
uint32_t sd_read_commands(void) { return n_reads; }

extern void sd_wait_hook(void) __attribute__((weak));
static void waiting(void)
{
	if (sd_wait_hook)
		sd_wait_hook();
}

void sdio_log(const char *txt, uint32_t arg1, uint32_t arg2)
{
	static int told;
	if (told++ < 40)
		printf("sdio: %s (%lu, %lu)\n", txt, (unsigned long)arg1, (unsigned long)arg2);
}

/* the DMA's landing place: SRAM (a DMA into PSRAM waits on the QMI, and the HDMI
 * channel behind it missed its lines) */
#define BOUNCE_SECTORS 8
static uint32_t bounce[BOUNCE_SECTORS * 512 / 4];

/* murm386: the read-ahead. A sector the emulator asks for alone (the IDE
 * disk reads its image through FatFs a sector at a time) costs the card
 * ~0.75 ms however small, and the next seven in the same command nearly
 * nothing more: its 8-sector group is read into the bounce buffer, which
 * then answers for the rest of the group until something else uses it. */
#ifndef SD_READAHEAD
#define SD_READAHEAD 1
#endif
#ifndef LINK_GROUP_SECTORS
#define LINK_GROUP_SECTORS 8u   /* (link_proto.h's, when there is no slave cache) */
#endif
static LBA_t ra_lba;            /* the group in bounce, from its first sector */
static UINT ra_n;               /* how many of its sectors are valid there (0: none) */
static uint32_t n_ra_hits, n_ra_fills, n_slave_hits;

static bool in_sram(const void *p)
{
	uintptr_t a = (uintptr_t)p;
	return a >= 0x20000000u && a < 0x20082000u && !(a & 3);
}


/* ---- the card ---- */

static uint32_t address_of(LBA_t sector)
{
	return (ocr & (1u << 30)) ? (uint32_t)sector : (uint32_t)sector * 512u;     /* (SDHC: sectors; SDSC: bytes) */
}

/* the card not busy (DAT0 high) and out of its data states */
static bool wait_ready(uint32_t timeout_us)
{
	uint64_t t0 = time_us_64();
	while (time_us_64() - t0 < timeout_us) {
		if (gpio_get(SDIO_D0)) {
			uint32_t st = 0;
			if (rp2350_sdio_command_u32(CMD13, rca, &st, SDIO_FLAG_NO_LOGMSG) == SDIO_OK &&
			    ((st >> 9) & 0x0F) != 5 && ((st >> 9) & 0x0F) != 6 && ((st >> 9) & 0x0F) != 7)
				return true;
		}
		waiting();
	}
	return false;
}

static void stop_transmission(void)
{
	uint32_t reply;
	rp2350_sdio_command_u32(CMD12, 0, &reply, SDIO_FLAG_NO_LOGMSG);
	wait_ready(SDIO_TRANSFER_TIMEOUT_US);
}

static sdio_status_t poll_rx(void)
{
	sdio_status_t st;
	uint32_t n = 0;
	do {
		st = rp2350_sdio_rx_poll(NULL);
		if ((++n & 1023) == 0)
			waiting();
	} while (st == SDIO_BUSY);
	return st;
}

static sdio_status_t poll_tx(void)
{
	sdio_status_t st;
	uint32_t n = 0;
	do {
		st = rp2350_sdio_tx_poll(NULL);
		if ((++n & 1023) == 0)
			waiting();
	} while (st == SDIO_BUSY);
	return st;
}

/* One command for a whole request: CMD17/CMD24 for a sector, else CMD18/CMD25 and
 * CMD12 around any number of them.  The data moves in chunks, rx_start/tx_start
 * continuing the same transfer (the card's clock stopped in between): straight in
 * and out of an SRAM buffer, or, for one elsewhere (PSRAM: no DMA there), through
 * the two halves of the bounce buffer, one copied while the other is on the bus. */
#define HALF_SECTORS (BOUNCE_SECTORS / 2)

static bool read_blocks(LBA_t sector, uint8_t *dst, UINT count)
{
	uint32_t reply;
	bool multi = count > 1, direct = in_sram(dst);
	if (!direct || dst == (uint8_t *)bounce)
		ra_n = 0;
	if (rp2350_sdio_command_u32(multi ? CMD18 : CMD17, address_of(sector), &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK)
		goto fail;
	uint8_t *half[2] = { (uint8_t *)bounce, (uint8_t *)bounce + HALF_SECTORS * 512 };
	UINT max = direct ? SDIO_MAX_BLOCKS_PER_REQ : HALF_SECTORS;
	UINT done = 0, n = count < max ? count : max;
	uint8_t *buf = direct ? dst : half[0];
	if (rp2350_sdio_rx_start(buf, n, 512) != SDIO_OK || poll_rx() != SDIO_OK)
		goto fail;
	for (int k = 1;; k ^= 1) {
		UINT next = count - done - n < max ? count - done - n : max;
		uint8_t *nbuf = direct ? dst + (done + n) * 512 : half[k];
		if (next && rp2350_sdio_rx_start(nbuf, next, 512) != SDIO_OK)
			goto fail;
		if (!direct)
			memcpy(dst + done * 512, buf, n * 512);
		done += n;
		if (!next)
			break;
		if (poll_rx() != SDIO_OK)
			goto fail;
		n = next;
		buf = nbuf;
	}
	if (multi)
		stop_transmission();
	else
		rp2350_sdio_stop();
	return true;
fail:
	if (multi)
		stop_transmission();
	else
		rp2350_sdio_stop();
	return false;
}

static bool write_blocks(LBA_t sector, const uint8_t *src, UINT count)
{
	uint32_t reply;
	bool multi = count > 1, direct = in_sram(src);
	if (!direct)
		ra_n = 0;
	uint8_t *half[2] = { (uint8_t *)bounce, (uint8_t *)bounce + HALF_SECTORS * 512 };
	UINT max = direct ? SDIO_MAX_BLOCKS_PER_REQ : HALF_SECTORS;
	UINT done = 0, n = count < max ? count : max;
	const uint8_t *buf = src;
	if (!direct) {
		memcpy(half[0], src, n * 512);
		buf = half[0];
	}
	if (rp2350_sdio_command_u32(multi ? CMD25 : CMD24, address_of(sector), &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK ||
	    rp2350_sdio_tx_start(buf, n, 512) != SDIO_OK)
		goto fail;
	for (int k = 1;; k ^= 1) {
		UINT next = count - done - n < max ? count - done - n : max;
		const uint8_t *nbuf = src + (done + n) * 512;
		if (next && !direct) {
			memcpy(half[k], nbuf, next * 512);
			nbuf = half[k];
		}
		if (poll_tx() != SDIO_OK)
			goto fail;
		done += n;
		if (!next)
			break;
		if (rp2350_sdio_tx_start(nbuf, next, 512) != SDIO_OK)
			goto fail;
		n = next;
	}
	if (multi) {
		stop_transmission();
		return true;
	}
	rp2350_sdio_stop();
	return wait_ready(SDIO_TRANSFER_TIMEOUT_US);
fail:
	if (multi)
		stop_transmission();
	else
		rp2350_sdio_stop();
	return false;
}

/* ACMD-less CMD6: switch the bus speed (arg 0x80FFFF01: high speed, ...00: default) */
static bool card_cmd6(uint32_t arg)
{
	uint32_t reply;
	ra_n = 0;
	if (rp2350_sdio_command_u32(CMD6, arg, &reply, SDIO_FLAG_STOP_CLK) != SDIO_OK) {
		rp2350_sdio_stop();
		return false;
	}
	rp2350_sdio_rx_start((uint8_t *)bounce, 1, 64);
	sdio_status_t st = poll_rx();
	rp2350_sdio_stop();
	busy_wait_us_32(1000);
	return st == SDIO_OK && !(reply & 0x80);
}

static bool claim_dma(void)
{
	if (sdio_dma_a >= 0)
		return true;
	/* Two that are free and no driver's fixed ones (HSTX HDMI 0/1, PIO HDMI
	 * 0-3, I2S 10/11, DispHSTX 14/15): a card mounted before the video
	 * starts took 0/1, and the video panicked on its claim (FRANK Next).
	 * rp2350_sdio_init claims them itself. */
	static const uint8_t pick[] = {13, 12, 9, 8, 7, 6, 5, 4};
	int a = -1, b = -1;
	for (unsigned i = 0; i < sizeof(pick) && b < 0; i++) {
		if (dma_channel_is_claimed(pick[i]))
			continue;
		if (a < 0)
			a = pick[i];
		else
			b = pick[i];
	}
	if (a < 0 || b < 0)
		return false;
	sdio_dma_a = a;
	sdio_dma_b = b;
	return true;
}

static bool card_init(void)
{
	if (!claim_dma())
		return false;
	uint32_t reply = 0;
	sdio_status_t st = SDIO_ERR_RESPONSE_TIMEOUT;
	rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_INITIALIZE));
	busy_wait_us_32(1000);
	for (int k = 0; k < 5; k++) {
		busy_wait_us_32(1000);
		rp2350_sdio_command(CMD0, 0, NULL, 0, SDIO_FLAG_NO_LOGMSG);
		busy_wait_us_32(1000);
		reply = 0;
		st = rp2350_sdio_command_u32(CMD8, 0x1AA, &reply, SDIO_FLAG_NO_LOGMSG);
		if (st == SDIO_OK && reply == 0x1AA)
			break;
	}
	/* an SD 1.x card (2 GB and under) does not answer CMD8 at all: asked
	 * for its OCR without HCS, it answers ACMD41 and is byte addressed */
	bool v1 = st != SDIO_OK || reply != 0x1AA;
	uint64_t t0 = time_us_64();
	do {
		if (rp2350_sdio_command_u32(CMD55, 0, &reply, v1 ? SDIO_FLAG_NO_LOGMSG : 0) != SDIO_OK ||
		    rp2350_sdio_command_u32(ACMD41, v1 ? SDIO_CARD_OCR_MODE & ~(1u << 30) : SDIO_CARD_OCR_MODE, &ocr,
					    SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK) {
			if (v1)
				printf("sdio: no card (CMD8 %d, %08lx; no ACMD41 either)\n", st, (unsigned long)reply);
			return false;
		}
		if (time_us_64() - t0 > SDIO_INIT_TIMEOUT_US) {
			printf("sdio: the card did not get ready\n");
			return false;
		}
	} while (!(ocr & (1u << 31)));
	uint8_t cid[16];
	if (rp2350_sdio_command(CMD2, 0, cid, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK ||
	    rp2350_sdio_command_u32(CMD3, 0, &rca, 0) != SDIO_OK ||
	    rp2350_sdio_command(CMD9, rca, csd, 16, SDIO_FLAG_NO_CRC | SDIO_FLAG_NO_CMD_TAG) != SDIO_OK ||
	    rp2350_sdio_command_u32(CMD7, rca, &reply, 0) != SDIO_OK) {
		printf("sdio: the card stopped answering after ACMD41 (CMD2/3/9/7)\n");
		return false;
	}
	if ((csd[0] >> 6) == 1) {
		uint32_t csize = csd[9] + ((uint32_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
		sectors = csize << 10;
	} else {
		uint32_t n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
		uint32_t csize = (csd[8] >> 6) + ((uint32_t)csd[7] << 2) + ((uint32_t)(csd[6] & 3) << 10) + 1;
		sectors = csize << (n - 9);
	}
	/* the 4-bit bus; 512-byte blocks (an SDSC card's are not fixed) */
	if (rp2350_sdio_command_u32(CMD55, rca, &reply, 0) != SDIO_OK ||
	    rp2350_sdio_command_u32(ACMD6, 2, &reply, 0) != SDIO_OK ||
	    rp2350_sdio_command_u32(CMD16, 512, &reply, 0) != SDIO_OK) {
		printf("sdio: no 4-bit bus (ACMD6/CMD16)\n");
		return false;
	}
	/* the fastest mode that reads sector 0 the same as the slow one did */
	uint32_t *first = bounce + HALF_SECTORS * 128;   /* (the second half: read_blocks fills the first) */
	if (!read_blocks(0, (uint8_t *)first, 1)) {
		printf("sdio: sector 0 unreadable at the initial clock\n");
		return false;
	}
	uint32_t want = 0;
	for (int i = 0; i < 128; i++)
		want = want * 31 + first[i];
	for (rp2350_sdio_mode_t m = mode_max; m >= SDIO_STANDARD; m = (rp2350_sdio_mode_t)(m - 1)) {
		rp2350_sdio_timing_t t = rp2350_sdio_get_timing(m);
		if (!card_cmd6(t.use_high_speed ? 0x80FFFF01u : 0x80FFFF00u) && m > SDIO_STANDARD)
			continue;
		rp2350_sdio_init(t);
		uint32_t sum = 0;
		bool ok = read_blocks(0, (uint8_t *)first, 1);
		for (int i = 0; ok && i < 128; i++)
			sum = sum * 31 + first[i];
		if (ok && sum == want) {
			mode_now = m;
			clk_hz = clock_get_hz(clk_sys) / (uint32_t)t.data_clk_divider;
			printf("sdio: card ready, %lu MB, %lu kHz %s\n", (unsigned long)(sectors / 2048), (unsigned long)(clk_hz / 1000),
			       t.use_high_speed ? "(high speed)" : "");
			return true;
		}
		rp2350_sdio_init(rp2350_sdio_get_timing(SDIO_INITIALIZE));
	}
	printf("sdio: no clock reads sector 0 as the initial one did\n");
	return false;
}

/* ---- the sector cache (as drivers/sdcard's) ---- */
/* frank-react's 64 live in PSRAM; here the PSRAM is all guest memory and the
 * SRAM is what pc_new() allocates from, so none (FatFs's window caches the FAT) */
#ifndef SCACHE_N
#define SCACHE_N 0
#endif
#if SCACHE_N
static uint8_t scache_data[SCACHE_N][512];
static LBA_t scache_lba[SCACHE_N];
static uint8_t scache_valid[SCACHE_N];
static uint32_t scache_age[SCACHE_N], scache_clock;
static uint32_t sd_scache_hits, sd_scache_misses;

static int scache_find(LBA_t lba)
{
	for (int i = 0; i < SCACHE_N; i++)
		if (scache_valid[i] && scache_lba[i] == lba)
			return i;
	return -1;
}

static void scache_put(LBA_t lba, const BYTE *data)
{
	int v = scache_find(lba);
	if (v < 0) {
		v = 0;
		for (int i = 0; i < SCACHE_N; i++) {
			if (!scache_valid[i]) {
				v = i;
				break;
			}
			if (scache_age[i] < scache_age[v])
				v = i;
		}
	}
	memcpy(scache_data[v], data, 512);
	scache_lba[v] = lba;
	scache_valid[v] = 1;
	scache_age[v] = ++scache_clock;
}
#else
static uint32_t sd_scache_hits, sd_scache_misses;
static inline int scache_find(LBA_t lba) { (void)lba; return -1; }
static inline void scache_put(LBA_t lba, const BYTE *data) { (void)lba; (void)data; }
#endif

/* ---- the SPI fallback ----
 *
 * A card stays in SPI mode from the moment it takes CMD0 with CS low until
 * its power goes: one that frank-test (or anything else speaking SPI) has
 * touched never answers SDIO again before a power cycle. Such a card is
 * driven in SPI mode on the same pins (CLK GPIO1, MOSI = CMD GPIO0, MISO =
 * DAT0 GPIO4, CS = DAT3 GPIO7) by drivers/sdcard's PIO SPI, compiled with its
 * disk_* renamed spi_disk_* (CMakeLists.txt). */
#if SD_SPI_FALLBACK
DSTATUS spi_disk_initialize(BYTE drv);
DSTATUS spi_disk_status(BYTE drv);
DRESULT spi_disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count);
DRESULT spi_disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT spi_disk_ioctl(BYTE drv, BYTE cmd, void *buff);
static bool use_spi;
#define SPI_OR(call) do { if (use_spi) return spi_ ## call; } while (0)
#else
#define SPI_OR(call) do { } while (0)
#endif

/* ---- FatFs ---- */

DSTATUS disk_initialize(BYTE drv)
{
	if (drv)
		return STA_NOINIT;
#if SCACHE_N
	memset(scache_valid, 0, sizeof scache_valid);
#endif
	SPI_OR(disk_initialize(drv));
	Stat = card_init() ? 0 : STA_NOINIT;
#if SD_SPI_FALLBACK
	if (Stat & STA_NOINIT) {
		if (!(spi_disk_initialize(drv) & STA_NOINIT)) {
			use_spi = true;
			printf("sdio: the card answers in SPI mode only (a power cycle brings SDIO back): SPI\n");
			return 0;
		}
		printf("sdio: no answer in SPI mode either\n");
	}
#endif
	return Stat;
}

/* clk_sys changed under the bus (main.c, after config.ini's clocks): the
 * dividers are chosen again, the fastest mode that still reads sector 0 */
void sd_clock_changed(void)
{
#if SD_SPI_FALLBACK
	if (use_spi)
		return;
#endif
	if (Stat & STA_NOINIT)
		return;
	ra_n = 0;
	mode_max = SDIO_HIGHSPEED_OVERCLOCK;
	Stat = card_init() ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE drv)
{
	SPI_OR(disk_status(drv));
	return drv ? STA_NOINIT : Stat;
}

/* with retries; a card that keeps failing is set up again a step slower */
static bool transfer(bool write, LBA_t sector, uint8_t *buf, UINT count)
{
	for (int attempt = 0; attempt < 4; attempt++) {
		if (Stat & STA_NOINIT) {
			if (!card_init()) {
				busy_wait_us_32(20000u << attempt);
				continue;
			}
			Stat = 0;
		}
		if (write ? write_blocks(sector, buf, count) : read_blocks(sector, buf, count))
			return true;
		n_retries++;
		printf("sdio: %s %lu+%u failed, retry %d\n", write ? "write" : "read", (unsigned long)sector, count, attempt + 1);
		if (attempt >= 1) {
			if (mode_now > SDIO_STANDARD)
				mode_max = (rp2350_sdio_mode_t)(mode_now - 1);
			Stat |= STA_NOINIT;
		}
		waiting();
	}
	n_errors++;
	return false;
}

/* one sector through the read-ahead, and the slave's cache (link_proto.h)
 * behind it: false when it has to go the ordinary way */
static bool group_read(LBA_t sector, BYTE *buff)
{
	if (ra_n && sector - ra_lba < ra_n) {
		memcpy(buff, (uint8_t *)bounce + (sector - ra_lba) * 512, 512);
		n_ra_hits++;
		return true;
	}
	LBA_t base = sector & ~(LBA_t)(LINK_GROUP_SECTORS - 1);
	if (base + LINK_GROUP_SECTORS > sectors)
		return false;
	ra_n = 0;
#if SD_SLAVE_CACHE
	if (sc_get((uint32_t)(base / LINK_GROUP_SECTORS), (uint8_t *)bounce)) {
		n_slave_hits++;
		goto have;
	}
#endif
	uint64_t t0 = time_us_64();
	n_reads++;
	n_blocks += LINK_GROUP_SECTORS;
	if (!transfer(false, base, (uint8_t *)bounce, LINK_GROUP_SECTORS))
		return false;
	us_reads += time_us_64() - t0;
	n_ra_fills++;
#if SD_SLAVE_CACHE
	sc_put((uint32_t)(base / LINK_GROUP_SECTORS), (uint8_t *)bounce);
	goto have;
have:
#endif
	ra_lba = base;
	ra_n = LINK_GROUP_SECTORS;
	memcpy(buff, (uint8_t *)bounce + (sector - base) * 512, 512);
	return true;
}

DRESULT disk_read(BYTE drv, BYTE *buff, LBA_t sector, UINT count)
{
	SPI_OR(disk_read(drv, buff, sector, count));
	if (drv || !count)
		return RES_PARERR;
	if (SD_READAHEAD && count == 1 && !(Stat & STA_NOINIT) && group_read(sector, buff))
		return RES_OK;
	if (count == 1) {
		int i = scache_find(sector);
#if SCACHE_N
		if (i >= 0) {
			memcpy(buff, scache_data[i], 512);
			scache_age[i] = ++scache_clock;
			sd_scache_hits++;
			return RES_OK;
		}
#else
		(void)i;
#endif
		sd_scache_misses++;
	}
	uint64_t t0 = time_us_64();
	n_reads++;
	n_blocks += count;
	if (!transfer(false, sector, buff, count))
		return RES_ERROR;
	if (count == 1)
		scache_put(sector, buff);
	us_reads += time_us_64() - t0;
	return RES_OK;
}

DRESULT disk_write(BYTE drv, const BYTE *buff, LBA_t sector, UINT count)
{
	SPI_OR(disk_write(drv, buff, sector, count));
	if (drv || !count)
		return RES_PARERR;
	if (Stat & STA_NOINIT)
		return RES_NOTRDY;
	for (UINT k = 0; k < count; k++)
		if (scache_find(sector + k) >= 0)
			scache_put(sector + k, buff + 512 * k);
	if (ra_n && sector < ra_lba + ra_n && ra_lba < sector + count)
		ra_n = 0;
#if SD_SLAVE_CACHE
	sc_drop((uint32_t)(sector / LINK_GROUP_SECTORS),
		(uint32_t)((sector + count - 1) / LINK_GROUP_SECTORS - sector / LINK_GROUP_SECTORS + 1));
#endif
	uint64_t t0 = time_us_64();
	n_writes++;
	w_blocks += count;
	if (!transfer(true, sector, (uint8_t *)buff, count))
		return RES_ERROR;
	us_writes += time_us_64() - t0;
	return RES_OK;
}

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buff)
{
	SPI_OR(disk_ioctl(drv, cmd, buff));
	if (drv)
		return RES_PARERR;
	if (Stat & STA_NOINIT)
		return RES_NOTRDY;
	switch (cmd) {
	case CTRL_SYNC:
		return wait_ready(SDIO_TRANSFER_TIMEOUT_US) ? RES_OK : RES_ERROR;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = sectors;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 128;   /* (erase block, in sectors: a guess FatFs only uses for f_mkfs) */
		return RES_OK;
	case CTRL_TRIM:
		return RES_OK;
	}
	return RES_PARERR;
}

void sd_stats_report(void)
{
	printf("sd: SDIO at %lu kHz; %lu reads, %lu blocks: %llu ms; %lu writes, %lu blocks: %llu ms; %lu retries, %lu errors\n",
	       (unsigned long)(clk_hz / 1000), (unsigned long)n_reads, (unsigned long)n_blocks, us_reads / 1000,
	       (unsigned long)n_writes, (unsigned long)w_blocks, us_writes / 1000, (unsigned long)n_retries,
	       (unsigned long)n_errors);
	printf("sd: sector cache %lu hits, %lu misses\n", (unsigned long)sd_scache_hits, (unsigned long)sd_scache_misses);
	printf("sd: read-ahead %lu sectors from it, %lu groups read, %lu groups from the slave\n",
	       (unsigned long)n_ra_hits, (unsigned long)n_ra_fills, (unsigned long)n_slave_hits);
#if SD_SLAVE_CACHE
	sc_report();
#endif
}

/* "sdbench": reads and writes at the card's end (its last 2 MB: a file system
 * leaves them alone more often than not; what is written there is what was read)
 * into a buffer from the heap (PSRAM, as the file system's are) */
static uint32_t kbps(uint32_t kb, uint64_t us)
{
	return (uint32_t)((uint64_t)kb * 1000000 / (us + 1));
}

void sd_bench(void)
{
	if (Stat & STA_NOINIT)
		return;
	/* murm386: the SRAM heap has tens of KB, not frank-react's PSRAM heap;
	 * the SDK's malloc panics when it fails, __real_malloc does not */
	extern void *__real_malloc(size_t);
	UINT BIG = 256;         /* sectors: 128 KB, or the most there is room for */
	uint8_t *buf = NULL;
	while (BIG >= 16 && !(buf = __real_malloc(BIG * 512)))
		BIG /= 2;
	if (!buf) {
		printf("sdbench: no memory\n");
		return;
	}
	LBA_t base = sectors - 4096;
	bool ok = true;
	const UINT runs[] = { 8, BIG < 64 ? BIG : 64, BIG };
	uint32_t rd[3];
	for (int r = 0; r < 3; r++) {
		uint64_t t0 = time_us_64();
		for (LBA_t s = 0; s < 2048 && ok; s += runs[r])
			ok = read_blocks(base + s, buf, runs[r]);
		rd[r] = kbps(1024, time_us_64() - t0);
	}
	uint64_t t1 = time_us_64();
	for (int k = 0; k < 200 && ok; k++)
		ok = read_blocks(base + (LBA_t)((k * 7919u) % 2048u), buf, 1);
	uint32_t one = (uint32_t)((time_us_64() - t1) / 200);
	/* writes: 128 KB read, then written back 8 times, in 4 KB and in 128 KB requests */
	uint32_t wr[2] = { 0, 0 };
	ok = ok && read_blocks(base, buf, BIG);
	for (int r = 0; r < 2 && ok; r++) {
		UINT n = r ? BIG : 8;
		uint64_t t0 = time_us_64();
		for (int rep = 0; rep < 8 && ok; rep++)
			for (UINT s = 0; s < BIG && ok; s += n)
				ok = write_blocks(base + s, buf + s * 512, n);
		wr[r] = kbps(1024, time_us_64() - t0);
	}
	free(buf);
	printf("sdbench: %s at %lu kHz; read KB/s: %lu (4 KB), %lu (%u KB), %lu (%u KB); one sector %lu us; "
	       "write KB/s: %lu (4 KB), %lu (%u KB)\n",
	       ok ? "ok" : "FAILED", (unsigned long)(clk_hz / 1000), (unsigned long)rd[0], (unsigned long)rd[1],
	       runs[1] / 2, (unsigned long)rd[2], BIG / 2, (unsigned long)one, (unsigned long)wr[0], (unsigned long)wr[1], BIG / 2);
}

/* frank-sdk: get_fattime() is frank_storage.c's */

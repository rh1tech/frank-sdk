/*
 * frank-sdk: the SPI microSD driver's pins, from the board file (boards/<id>.yaml
 * sd, or sd_spi_fallback on a board whose card is SDIO). Hardware SPI where the
 * card sits on an SPI block's pins, PIO SPI (the board's pio.sd) where not.
 */
#ifndef _SDCARD_H_
#define _SDCARD_H_

#include "pico.h"

#if defined(FRANK_HAS_SD_SPI_FALLBACK)
#define SDCARD_PIN_SPI0_SCK  FRANK_SD_SPI_FALLBACK_CLK
#define SDCARD_PIN_SPI0_MOSI FRANK_SD_SPI_FALLBACK_CMD
#define SDCARD_PIN_SPI0_MISO FRANK_SD_SPI_FALLBACK_DAT0
#define SDCARD_PIN_SPI0_CS   FRANK_SD_SPI_FALLBACK_DAT3
#elif defined(FRANK_SD_MODE_SPI)
#define SDCARD_PIN_SPI0_SCK  FRANK_SD_CLK
#define SDCARD_PIN_SPI0_MOSI FRANK_SD_CMD
#define SDCARD_PIN_SPI0_MISO FRANK_SD_DAT0
#define SDCARD_PIN_SPI0_CS   FRANK_SD_DAT3
#else
#error "sd_spi: the board has no SPI microSD (boards/*.yaml sd.mode)"
#endif

/* GPIO n's SPI function is on SPI((n / 8) % 2), as RX when n % 4 == 0, SCK at 2,
 * TX at 3: hardware SPI when the card's pins fit that, on one block. */
#define SDCARD_HW_PINS (SDCARD_PIN_SPI0_SCK % 4 == 2 && SDCARD_PIN_SPI0_MOSI == SDCARD_PIN_SPI0_SCK + 1 && \
                        SDCARD_PIN_SPI0_MISO % 4 == 0 && \
                        (SDCARD_PIN_SPI0_SCK / 8) % 2 == (SDCARD_PIN_SPI0_MISO / 8) % 2)
#if SDCARD_HW_PINS && !defined(FRANK_SD_FORCE_PIO)
#define SDCARD_SPI_BUS ((SDCARD_PIN_SPI0_SCK / 8) % 2 ? spi1 : spi0)
#else
#define SDCARD_PIO PIO_INSTANCE(FRANK_PIO_SD)
#define SDCARD_PIO_SM (-1)        /* claimed at init */
#endif

#endif // _SDCARD_H_

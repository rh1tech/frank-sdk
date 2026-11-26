/*
 * frank-386: SDIO_RP2350's configuration (rabbitholecomputing/SDIO_RP2350, MIT,
 * LICENSE-SDIO_RP2350.md) for FRANK Next: the microSD in 4-bit SDIO on CMD GP0,
 * CLK GP1, DAT0-3 GP4-7. Taken from frank-react's drivers/sdio.
 *
 * PIO0: PIO1 is the HDMI encoder and PIO2 the I2S codec (board_config.h). The
 * inter-processor link would want PIO0 too, which is why the NX build has no
 * REMOTE_MEM / DISK_CACHE. The DMA channels are whichever two were free when the
 * card was first set up (sd_sdio.c); DMA_IRQ_2 (0 is the video's).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void sdio_log(const char *txt, uint32_t arg1, uint32_t arg2);       /* (sd_sdio.c, C) */
extern int sdio_dma_a, sdio_dma_b;
#ifdef __cplusplus
}
#endif
#define SDIO_CRITMSG(txt, arg1, arg2) sdio_log(txt, (uint32_t)(arg1), (uint32_t)(arg2))
#define SDIO_ERRMSG(txt, arg1, arg2) sdio_log(txt, (uint32_t)(arg1), (uint32_t)(arg2))

/* the bounce buffer's half (sd_sdio.c) caps a PSRAM transfer anyway, and the
 * emulator's SRAM buffers are a sector or a few (the driver's state is sized by it) */
#define SDIO_MAX_BLOCKS_PER_REQ 16

/* frank-sdk: the PIO and pins from the board file (boards/*.yaml sd, pio.sd) */
#define SDIO_PIO PIO_INSTANCE(FRANK_PIO_SD)
#define SDIO_SM 0
#define SDIO_GPIO_FUNC (FRANK_PIO_SD == 0 ? GPIO_FUNC_PIO0 : FRANK_PIO_SD == 1 ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO2)
#define SDIO_GPIO_SLEW GPIO_SLEW_RATE_FAST
#define SDIO_GPIO_DRIVE GPIO_DRIVE_STRENGTH_8MA

#define SDIO_DMACH_A sdio_dma_a
#define SDIO_DMACH_B sdio_dma_b
#define SDIO_DMAIRQ_IDX 2
#define SDIO_DMAIRQ DMA_IRQ_2

#define SDIO_CMD FRANK_SD_CMD
#define SDIO_CLK FRANK_SD_CLK
#define SDIO_D0 FRANK_SD_DAT0
#define SDIO_D1 (FRANK_SD_DAT0 + 1)
#define SDIO_D2 (FRANK_SD_DAT0 + 2)
#define SDIO_D3 (FRANK_SD_DAT0 + 3)
#define SDIO_USE_SDFAT 0

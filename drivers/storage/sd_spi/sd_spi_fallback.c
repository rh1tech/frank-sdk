/*
 * frank-sdk: the SPI driver as SDIO's fall-back (FRANK Next): its FatFs entry
 * points renamed spi_disk_*, as sd_sdio.c calls them (frank-dos).
 */
#define disk_initialize spi_disk_initialize
#define disk_status spi_disk_status
#define disk_read spi_disk_read
#define disk_write spi_disk_write
#define disk_ioctl spi_disk_ioctl
#define FRANK_SD_FORCE_PIO 1
#include "sdcard.c"

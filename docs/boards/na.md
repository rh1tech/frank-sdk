# FRANK Next rev A (master) — target `NA`

Generated from [`boards/na.yaml`](../../boards/na.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role master, pair `SL`
- **Covers:** FRANK Next rev A, FRANK Next 1.xx
- **Flash:** 16M W25Q128JVPIQ
- **PSRAM:** 8M on CS GPIO47
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default; UART0 TX GPIO46 / RX GPIO45
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** sd → PIO0, video → PIO1, i2s → PIO1, link → PIO2; PIO0 base 0, PIO1 base 0, PIO2 base 16

| GPIO | Function |
|---:|---|
| 0 | sd / sd_spi_fallback |
| 1 | sd / sd_spi_fallback |
| 2 | i2c |
| 3 | i2c |
| 4 | sd / sd_spi_fallback |
| 5 | sd |
| 6 | sd |
| 7 | sd / sd_spi_fallback |
| 8 | audio.i2s |
| 9 | audio.i2s |
| 10 | audio.i2s |
| 11 | audio.i2s |
| 12 | video.hdmi |
| 13 | video.hdmi |
| 14 | video.hdmi |
| 15 | video.hdmi |
| 16 | video.hdmi |
| 17 | video.hdmi |
| 18 | video.hdmi |
| 19 | video.hdmi |
| 20 | link |
| 21 | link |
| 22 | link |
| 23 | link |
| 24 | link |
| 25 | link |
| 26 | link |
| 27 | link |
| 28 | link |
| 29 | link |
| 30 | link |
| 31 | link |
| 32 | link |
| 33 | link |
| 34 | link |
| 35 | link |
| 36 | link |
| 37 | link |
| 38 | link |
| 39 | link |
| 40 | link |
| 41 | link |
| 42 | link |
| 43 | link |
| 44 | tape_in |
| 45 | console.uart |
| 46 | console.uart |
| 47 | psram |

Shared (a build uses one of each group): sd vs sd_spi_fallback

Sources: murmulator hardware/frank_next (schematic, rev 1.xx); frank-test core/board_table.c FRANK_BOARD_NEXT (netlist); frank-dos src/board_config.h BOARD_NX, boards/frank_next_master.h; frank-duke3d common/platform/clocks.c (504 MHz on both halves)

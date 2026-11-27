# UKTailwind Pico Computer 3 — target `TW`

Generated from [`boards/tw.yaml`](../../boards/tw.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role single
- **Covers:** UKTailwind Pico Computer 3
- **Flash:** 16M W25Q128JVSIQ
- **PSRAM:** 8M on CS GPIO47
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 100 MHz, flash ≤ 66 MHz
- **Console:** UART by default; UART1 TX GPIO8 / RX GPIO9
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** i2s → PIO2, ps2 → PIO0, nespad → PIO2; PIO0 base 0, PIO2 base 0

| GPIO | Function |
|---:|---|
| 0 | ps2 |
| 1 | ps2 |
| 2 | ps2 |
| 3 | ps2 |
| 4 | nespad |
| 6 | nespad |
| 7 | nespad |
| 8 | console.uart |
| 9 | console.uart |
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
| 20 | i2c |
| 21 | i2c |
| 22 | audio.i2s |
| 25 | led |
| 28 | sd |
| 30 | sd |
| 31 | sd |
| 33 | sd |
| 47 | psram |

Sources: UKTailwind/Pico-Computer-3 PicoComputer3.3Schematic-1.pdf (PicoUSBMotherboardV3.3); frank-idtech1 boards/frank_tw_rp2350b.h, src/board_config.h BOARD_TW

# FRANK Z0 — target `FZ`

Generated from [`boards/fz.yaml`](../../boards/fz.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role single
- **Covers:** FRANK Z0
- **Flash:** 16M W25Q128JVPIQ
- **PSRAM:** 8M on CS GPIO47
- **Clocks:** CPU 378 MHz at 1.5 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default; UART1 TX GPIO8 / RX GPIO9
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** video → PIO1, i2s → PIO2, pio_usb → PIO0; PIO0 base 0, PIO1 base 0, PIO2 base 0

| GPIO | Function |
|---:|---|
| 4 | sd |
| 5 | sd |
| 6 | sd |
| 7 | sd |
| 8 | console.uart |
| 9 | console.uart |
| 10 | audio.hat / i2c |
| 11 | audio.hat / i2c |
| 12 | video.hdmi |
| 13 | video.hdmi |
| 14 | video.hdmi |
| 15 | video.hdmi |
| 16 | video.hdmi |
| 17 | video.hdmi |
| 18 | video.hdmi |
| 19 | video.hdmi |
| 20 | led |
| 21 | usb.pio_usb |
| 22 | usb.pio_usb |
| 23 | audio.i2s |
| 24 | audio.i2s |
| 25 | audio.i2s |
| 26 | audio.i2s |
| 27 | audio.hat |
| 30 | audio.hat |
| 31 | audio.hat |
| 47 | psram |

Shared (a build uses one of each group): i2c vs audio.hat

Sources: frank-test core/board_table.c FRANK_BOARD_Z0 (netlist), boards/frank_z0.h

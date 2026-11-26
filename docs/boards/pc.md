# Olimex PICO-PC (Pico 2) — target `PC`

Generated from [`boards/pc.yaml`](../../boards/pc.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350A, role single
- **Covers:** Olimex PICO-PC
- **Flash:** 4M
- **PSRAM:** 8M on CS GPIO8
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default; UART0 TX GPIO0 / RX GPIO1
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** video → PIO1, ps2 → PIO0, nespad → PIO2; PIO0 base 0, PIO1 base 0, PIO2 base 0

| GPIO | Function |
|---:|---|
| 0 | console.uart / ps2 |
| 1 | console.uart / ps2 |
| 2 | ps2 |
| 3 | ps2 |
| 4 | sd |
| 5 | nespad |
| 6 | sd |
| 7 | sd |
| 8 | psram |
| 9 | nespad |
| 12 | video.hdmi / video.vga |
| 13 | video.hdmi / video.vga |
| 14 | video.hdmi / video.vga |
| 15 | video.hdmi / video.vga |
| 16 | video.hdmi / video.vga |
| 17 | video.hdmi / video.vga |
| 18 | video.hdmi / video.vga |
| 19 | video.hdmi / video.vga |
| 20 | nespad |
| 21 | nespad |
| 22 | sd |
| 25 | led |
| 27 | audio.pwm |
| 28 | audio.pwm |

Shared (a build uses one of each group): console.uart vs ps2.kbd; video.hdmi vs video.vga

Sources: pico-speccy cmake/board-pins.cmake PICO_PC; frank-dos src/board_config.h BOARD_PC

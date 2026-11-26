# FRANK Nano — target `FN`

Generated from [`boards/fn.yaml`](../../boards/fn.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350A, role single
- **Covers:** FRANK Nano
- **Flash:** 16M W25Q128JVPIQ
- **PSRAM:** 8M on CS GPIO8
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** USB CDC by default
- **HDMI:** HSTX on GPIO12-19 (clock pair GPIO12)
- **PIO:** video → PIO1, pio_usb → PIO0; PIO0 base 0, PIO1 base 0

| GPIO | Function |
|---:|---|
| 4 | sd |
| 5 | sd |
| 6 | sd |
| 7 | sd |
| 8 | psram |
| 12 | video.hdmi |
| 13 | video.hdmi |
| 14 | video.hdmi |
| 15 | video.hdmi |
| 16 | video.hdmi |
| 17 | video.hdmi |
| 18 | video.hdmi |
| 19 | video.hdmi |
| 20 | usb.pio_usb |
| 21 | usb.pio_usb |
| 25 | led |

Sources: frank-lab frank_nano/frank_nano.kicad_sch netlist (kicad-cli sch export netlist, 2026-11-26)

# FRANK Next / Core 2 (slave) — target `SL`

Generated from [`boards/sl.yaml`](../../boards/sl.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350A, role slave
- **Covers:** FRANK Next slave, FRANK Core 2 slave, FRANK Core 2U slave
- **Flash:** 16M W25Q128JVPIQ
- **PSRAM:** 8M on CS GPIO0
- **Clocks:** CPU 504 MHz at 1.65 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** UART by default; UART1 TX GPIO24 / RX GPIO25
- **PIO:** link → PIO0; PIO0 base 0

| GPIO | Function |
|---:|---|
| 0 | psram |
| 1 | link |
| 2 | link |
| 3 | link |
| 4 | link |
| 5 | link |
| 6 | link |
| 7 | link |
| 8 | link |
| 9 | link |
| 10 | link |
| 11 | link |
| 12 | link |
| 13 | link |
| 14 | link |
| 15 | link |
| 16 | link |
| 17 | link |
| 18 | link |
| 19 | link |
| 20 | link |
| 21 | link |
| 22 | link |
| 23 | link |
| 24 | console.uart |
| 25 | console.uart |
| 26 | led |
| 27 | led |

Sources: frank-test core/board_table.c "Core 2 / FRANK Next (slave)" (netlist); frank-react drivers/link/link_pins.h

# Generic RP2350A (board found at run time) — target `GA`

Generated from [`boards/ga.yaml`](../../boards/ga.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350A, role single
- **Covers:** FRANK (Pico 2), miniFRANK, microFRANK, zeroFRANK, the Core 2 / Next slave
- **Flash:** 4M
- **PSRAM:** ? on CS GPIO?
- **Clocks:** CPU 252 MHz at 1.1 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** UART by default; UART0 TX GPIO0 / RX GPIO1

| GPIO | Function |
|---:|---|
| 0 | console.uart |
| 1 | console.uart |

Sources: frank-test boards/frank_a.h

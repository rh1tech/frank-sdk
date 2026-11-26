# Generic RP2350B (board found at run time) — target `GB`

Generated from [`boards/gb.yaml`](../../boards/gb.yaml) by `tools/gen_boards.py`. Do not edit.

- **MCU:** RP2350B, role single
- **Covers:** FRANK (Pico Plus 2), FRANK PGA, MegaFRANK, OldSkoolFRANK, Core 2 / 2U, Z0pa, FRANK ZX
- **Flash:** 16M
- **PSRAM:** ? on CS GPIO?
- **Clocks:** CPU 252 MHz at 1.1 V, PSRAM ≤ 133 MHz, flash ≤ 66 MHz
- **Console:** UART by default; UART0 TX GPIO0 / RX GPIO1

| GPIO | Function |
|---:|---|
| 0 | console.uart |
| 1 | console.uart |

Sources: frank-test boards/frank_b.h

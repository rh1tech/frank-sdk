# The fleet: frank-test's run-time descriptors

Generated from [`boards/fleet/`](../boards/fleet) by `tools/gen_boards.py`. Do not edit.
Each descriptor is what frank-test's detection can identify; `target` is the
frank-sdk build target it runs as, cross-checked pin for pin.

| # | Board | slug | MCU | role | target | video | signature pins |
|---:|---|---|---|---|---|---|---:|
| 0 | FRANK | `frank` | any | single | `M2` | auto | 4 |
| 1 | FRANK PGA | `pga` | rp2350b | single | `M2` | auto | 10 |
| 2 | MegaFRANK | `mega` | rp2350b | single | — | auto | 6 |
| 3 | microFRANK | `micro` | rp2350a | single | — | auto | 0 |
| 4 | miniFRANK | `mini` | rp2350a | single | `M2` | auto | 4 |
| 5 | zeroFRANK | `zero` | rp2350a | single | — | auto | 4 |
| 6 | OldSkoolFRANK | `oldskool` | rp2350b | single | — | vga | 9 |
| 7 | FRANK Core 2 | `core2` | rp2350b | master | `C2` | auto | 6 |
| 8 | Core 2 / FRANK Next (slave) | `core2-slave` | rp2350a | slave | `SL` | auto | 6 |
| 9 | FRANK Next rev A | `next` | rp2350b | master | `NA` | auto | 9 |
| 10 | Z0pa | `z0pa` | rp2350b | single | `Z0` | auto | 10 |
| 11 | xt8086 | `xt8086` | rp2350b | single | — | auto | 10 |
| 12 | FRANK ZX | `zx` | rp2350b | single | — | auto | 12 |
| 13 | FRANK Z0 | `z0` | rp2350b | single | `FZ` | auto | 10 |
| 14 | Olimex RP2350pc | `rp2350pc` | rp2350b | single | `OL` | auto | 7 |
| 15 | FRANK Nano | `nano` | rp2350a | single | `FN` | auto | 6 |
| 16 | FRANK Next rev B | `next_b` | rp2350b | master | `NB` | auto | 9 |

# Flash read delay: at most what the board was tuned for

**Symptom.** The board boots at power-on but hangs after a soft reset, or misreads code at random after an overclock.

**Cause.** QMI RXDELAY counts half clk_sys cycles in a 3-bit field. The old rule (divider, +1 over 100 MHz) overflowed past divider 7 into a reserved bit (RXDELAY 0 at 504/66), and gave 6 at /5, over half an SCK period, which hung FRANK Next and bricked the Core 2's soft reset.

**Fix.** frank_flash_set_timing(): the divider rule up to 7, boot2's delay scaled past it, at most 7; a board's tuned value (boards/*.yaml flash_rxdelay) wins. Tune new boards with the diag firmware before raising the flash clock. `~C clocks~` prints the timing word (NX: 60007708).

**Found in:** frank-dos, frank-blood, xrip/pico-386.

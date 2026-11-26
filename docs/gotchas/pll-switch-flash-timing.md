# The flash timing must suit both clocks across a PLL switch

**Symptom.** A fault during set_sys_clock_khz() at boot on FRANK Next.

**Cause.** set_sys_clock_khz() runs clk_sys from the 48 MHz USB PLL while it sets the system PLL up. A timing for 504 MHz (/4, delay 5) misreads at 48 MHz.

**Fix.** frank_clocks_init() puts /5 delay 3 (right for both) in before the switch and the final timing after it.

**Found in:** frank-shadowwarrior.

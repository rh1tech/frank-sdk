# Every firmware must recover from a hang with nobody at the board

**Symptom.** The board is unreachable after a bad firmware; someone has to press BOOTSEL.

**Cause.** No watchdog, or a watchdog without a safe path back to BOOTSEL.

**Fix.** frank_init() + frank_boot_ok(); the main loop feeds the watchdog and ticks the crumb. A hang: watchdog → safe mode prints the crumb and the stall PCs for 8 s → BOOTSEL → reflash. Three failed boots: BOOTSEL. Wrap every bench command in a timeout. Verified on FRANK Next.

**Found in:** frank-ecat, frank-diablo.

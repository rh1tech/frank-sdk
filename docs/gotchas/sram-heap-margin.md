# Keep a few KB of SRAM heap free

**Symptom.** The board hangs before HDMI comes up after a small change that added static data.

**Cause.** Boot allocations took the whole SRAM heap ('0 KB free').

**Fix.** `~C mem~` after every SRAM-growing change; move big rarely used statics to .psram_bss (FRANK_PSRAM_BSS). `frank check` warns under 4 KB.

**Found in:** frank-dos.

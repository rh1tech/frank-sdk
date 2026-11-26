# picotool load fails verify: the PSRAM is stuck driving the bus

**Symptom.** In BOOTSEL the flash reads back noisy (different every read); `picotool load` says it did not match; the board seems to hang at boot.

**Cause.** GPIO47 (PSRAM CS1) is pulled low at reset and the PSRAM was stuck mid-transaction (a hardware watchdog reset in QPI mode), driving the shared QSPI data lines.

**Fix.** Load a no_flash RAM program that drives GPIO47 high, then reset_usb_boot; then the flash loads verify. Prevention: frank_reboot() everywhere; the stall watcher records hangs before the hardware watchdog.

**Found in:** frank-react, frank-war1 (tools/psram_unstick).

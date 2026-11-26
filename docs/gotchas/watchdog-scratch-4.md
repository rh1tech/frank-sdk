# Keep your own state in watchdog scratch 0-3, never 4-7

**Symptom.** A value written to a watchdog scratch register before a reboot is gone after it.

**Cause.** Scratch 4-7 are the boot ROM's (its reboot vector); the SDK's watchdog_reboot(0, 0, ...) clears scratch 4.

**Fix.** frank-sdk's platform uses scratch 0, 5 and 6; firmware state goes in 1-3 (examples/diag keeps its audio choice in 2).

**Found in:** frank-sdk (examples/diag on FRANK Next).

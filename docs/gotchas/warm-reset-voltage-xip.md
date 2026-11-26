# After a warm restart: XIP cache flushed, voltage back to 1.10 V

**Symptom.** The boot faults now and then on FRANK Next after a watchdog or BOOTSEL restart, never after a cold one.

**Cause.** The regulator keeps 1.65 V across those restarts, and the SDK's clock setup then ran at the wrong voltage; stale XIP cache lines of the previous image; a fast flash timing left by the boot ROM.

**Fix.** frank-sdk runs frank_xip_fresh (runtime init 00001) and frank_flash_safe_timing (00499) before the SDK's clocks.

**Found in:** frank-duke3d, frank-shadowwarrior.

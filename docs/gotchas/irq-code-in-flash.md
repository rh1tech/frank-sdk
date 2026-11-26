# Interrupt handlers must not call into flash

**Symptom.** Video stops for good when core 0 is busy, or audio crackles under load.

**Cause.** When core 0 saturates XIP, an IRQ that fetches from flash (newlib memcpy/memset from libg.a, irq_set_pending, a 64-bit division) misses its deadline and the DMA chain its reload.

**Fix.** Handlers and everything they call __not_in_flash_func; frank-sdk's linker script puts mem* (libc.a and libg.a) and the 64-bit divisions in RAM. `frank check ELF handler...` lists what a handler reaches in flash.

**Found in:** frank-blood, frank-diablo.

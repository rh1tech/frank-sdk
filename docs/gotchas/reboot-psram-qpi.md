# Every software reboot takes the PSRAM out of QPI mode first

**Symptom.** After a warm reset (watchdog, ~RST~, picotool) the chip hangs in the boot ROM until a power cycle; or PSRAM reads back junk.

**Cause.** The PSRAM keeps QPI mode across a chip reset that does not cycle its power. A QPI-mode PSRAM on the shared QSPI bus garbles the boot ROM's flash probe.

**Fix.** Reboot only through frank_reboot()/frank_reboot_clean(): core 1 forced off, interrupts off, PSRAM to SPI mode (F5h, 66h, 99h), clocks and flash timing back to the boot state. frank_psram_init() also resets the part before the QPI enable. Verified: 20/20 warm reboots on FRANK Next.

**Found in:** frank-ecat, frank-castle, frank-quake2, frank-shadowwarrior.

# No allocation in an interrupt handler

**Symptom.** The HDMI picture stops for good while the firmware allocates.

**Cause.** A PSRAM allocation walks dlmalloc's lists for 40-90 us; with interrupts off behind the heap lock, the line IRQ missed lines.

**Fix.** frank-sdk's heap holds a spinlock with interrupts on: so no handler may allocate.

**Found in:** frank-ecat.

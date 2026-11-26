# No DMA to or from PSRAM while video runs

**Symptom.** The picture stretches, shifts by lines, or the sink loses sync for good, as soon as a DMA copy touches PSRAM.

**Cause.** All DMA channels share one bus master. A DMA read or write that stalls on the QMI (PSRAM) holds it, and the HSTX/PIO video channel misses its reload. Guest-thread stacks in PSRAM count too.

**Fix.** DMA buffers and line buffers in SRAM (static). Copy PSRAM with the CPU. Only the XIP stream FIFO (DREQ-paced) is safe for DMA from PSRAM. `frank check` flags PSRAM objects named like DMA buffers.

**Found in:** frank-castle, frank-react.

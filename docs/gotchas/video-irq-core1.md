# The video IRQ owns core 1: never mask interrupts there

**Symptom.** No signal, permanently, after a short critical section or an extra DMA transfer.

**Cause.** pico_hdmi's ping-pong DMA IRQ cannot miss an interrupt: one missed and it desyncs for good. A tiny extra transfer per line completes before the IRQ re-arms the other channel. An I2S DMA IRQ on the same core breaks the timing.

**Fix.** frank_video_start() on core 1; no save_and_disable_interrupts there; black borders in the SRAM line itself; I2S IRQ on core 0. The HSTX DMA reads a line one to two lines after its callback: keep two or more slots of margin (frank_video uses four).

**Found in:** frank-castle.

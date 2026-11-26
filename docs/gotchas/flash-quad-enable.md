# Flash shipped with Quad Enable off

**Symptom.** An overclocked board with PSRAM locks up or fails the PSRAM test; Waveshare PiZero batches with Puya flash.

**Cause.** QE=0 leaves IO2/IO3 as WP#/HOLD#, not data lines, on the bus the PSRAM shares.

**Fix.** frank_clocks_init() sets QE once (non-volatile) for Winbond, GigaDevice, Puya, Boya, Zbit, XMC, before any overclock, with xrip's one-session writer.

**Found in:** frank-dos issue #6, xrip/pico-386.

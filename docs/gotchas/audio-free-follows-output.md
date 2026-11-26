# Wait for room in the ring the output actually drains

**Symptom.** The firmware hangs in its audio feed loop as soon as HDMI audio is chosen; safe mode reports stage 1 after one main loop.

**Cause.** frank_audio_free() reported the analog ring, which nothing drains in HDMI mode: a writer waiting for room spun for ever.

**Fix.** Fixed in frank_audio.c: free space and the played count follow the output in use. The recovery path found it: watchdog, safe mode, BOOTSEL, with nobody at the board.

**Found in:** frank-sdk (FRANK Next).

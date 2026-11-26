# The capture stick's audio pitch is wrong

**Symptom.** A 1 kHz test tone reads 889-1090 Hz depending on the run and the frequency.

**Cause.** The MS2109 stick resamples the HDMI audio (it takes 44.1 kHz as 48 kHz, and not consistently).

**Fix.** Judge presence and level from the capture; confirm the rate with the firmware's own counter (frank_hdmi_audio_consumed(): exactly 44100 a second on FRANK Next).

**Found in:** frank-dos, frank-sdk.

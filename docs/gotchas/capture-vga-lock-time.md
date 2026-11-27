# The capture stick needs ~15 s to lock on VGA

**Symptom.** A VGA build shows the capture stick's colour bars ("no signal") on every grab, while frank-test's VGA, flashed the same way, shows a picture.

**Cause.** The USB Video capture stick takes more than 6 s to detect a VGA mode after the signal appears. Grabs taken 3-6 s after a flash always see no signal; frank-test was grabbed 8 s after its boot, which also runs longer. The bars it shows are white-yellow-cyan-green-magenta-red-blue-black from the left: a firmware's own test bars in another order are a real picture.

**Fix.** Wait 15 s after a VGA flash before `grab_frame` / `frank watch`.

**Found in:** frank-idtech1 on FRANK PGA (2026-11-27).

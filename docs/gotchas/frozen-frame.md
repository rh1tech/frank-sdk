# The same frame 10 s apart is a frozen board

**Symptom.** The capture shows a picture, but nothing moves; or the counters stopped.

**Cause.** A hang with the video still scanning out the last frame.

**Fix.** `frank watch` grabs two frames 10 s apart: identical = frozen. Drop everything and fix the picture first; check it after every flash and every map or mode load. Look at the frames yourself: the stick shows its own screen without a signal.

**Found in:** frank-amiga, frank-react.

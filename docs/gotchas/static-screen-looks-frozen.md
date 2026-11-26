# Keep something moving on a test screen

**Symptom.** `frank watch` says frozen although the console answers.

**Cause.** The frozen check compares two frames 10 s apart; a static test pattern is identical too.

**Fix.** Every frank-sdk example moves something (the box, diag's sweeping bar), so frozen always means a hang.

**Found in:** frank-sdk.

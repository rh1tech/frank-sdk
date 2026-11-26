# Look at what is on the board before flashing it

**Symptom.** Another project's firmware got overwritten, or an M2 image went onto the Next.

**Cause.** One bench board serves several projects; a manual cmake without -DFRANK_BOARD builds for the default board.

**Fix.** `frank-dev board_info` first; build with the board in the command (./build.sh NX); images are named <board>_<name>.uf2, so the wrong one shows.

**Found in:** frank-react, frank-dos.

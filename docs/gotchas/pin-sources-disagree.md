# When two sources disagree about a pin, stop

**Symptom.** A board file would put two functions on one GPIO.

**Cause.** Ports copy pin tables from each other, comments and all (pico-speccy's Pico DV NES pins carry the Olimex board's UEXT comments and land inside the HDMI pins).

**Fix.** gen_boards.py refuses the conflict. Take pins from the KiCad netlist (kicad-cli sch export netlist, as frank-test does); park a board in boards/unverified/ until a schematic settles it.

**Found in:** frank-sdk.

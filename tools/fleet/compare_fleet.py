#!/usr/bin/env python3
#
# frank-sdk — the generated run-time table against frank-test's own.
#
# Builds the dumper twice on the host (frank-test's core/board_table.c, and
# boards/gen/frank_fleet.c + fleet/fleet.c) and compares the two dumps field by
# field. Prints every difference; exit 1 if any is not in EXPECTED.
#
# Usage: tools/fleet/compare_fleet.py FRANK_TEST_DIR
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: MIT

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fleetlib as fl  # noqa: E402

# Deliberate changes in frank-sdk's copy (boards/fleet/*.yaml says why)
EXPECTED = {
    ("z0pa", "caps"),
    ("z0pa", "pins.ps2_kb_clk"), ("z0pa", "pins.ps2_kb_dat"), ("z0pa", "pins.ps2_ms_clk"),
    ("z0pa", "pins.ps2_ms_dat"), ("z0pa", "pins.pwm_l"), ("z0pa", "pins.pwm_r"),
    ("z0pa", "pins.pad_latch"), ("z0pa", "pins.pad_d1"), ("z0pa", "pins.pad_d2"),
    ("z0pa", "pins.pio_usb_dp"),
    ("mini", "caps"), ("mini", "pins.i2c_sda"), ("mini", "pins.i2c_scl"),
}


def diff(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            yield from diff(a.get(k), b.get(k), f"{path}.{k}" if path else k)
    elif a != b:
        yield path, a, b


def main():
    ft = Path(sys.argv[1])
    old = fl.dump(ft / "core" / "board_table.c")
    gen = fl.ROOT / "boards" / "gen" / "frank_fleet.c"
    # the generated table and the lookups beside it, one translation unit
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(f'#include "{gen}"\n#include "{fl.ROOT / "fleet" / "fleet.c"}"\n')
        both = f.name
    new = fl.dump(both)
    bad = 0
    if len(old["table"]) != len(new["table"]):
        print(f"table length {len(old['table'])} vs {len(new['table'])}")
        return 1
    rows = [(o["slug"], o, n) for o, n in zip(old["table"], new["table"])] + [("fallback", old["fallback"], new["fallback"])]
    for slug, o, n in rows:
        for path, a, b in diff(o, n):
            ok = (slug, path) in EXPECTED
            bad += not ok
            print(f"{'expected' if ok else 'DIFFERS '} {slug}.{path}: {a!r} -> {b!r}")
    print(f"{len(rows)} descriptors compared, {bad} unexpected differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

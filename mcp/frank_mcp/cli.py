"""`frank`: the bench from a shell, with the MCP server's functions.

  frank status                 boards, probes, capture devices
  frank flash BOARD IMAGE      ~BOOT~ + picotool load -v -x + verify
  frank cmd BOARD 'clocks'     one ~C command, up to its OK line
  frank unstick BOARD          release a stuck PSRAM (RAM-only program), back to BOOTSEL
  frank console BOARD [SECS] [SEND...]
  frank watch DIR              two frames 10 s apart and a verdict
  frank audio [HZ]             the capture's audio: RMS and peak
  frank build DIR BOARD        configure + build, errors and memory
  frank check ELF [IRQ...]     the ELF lint
  frank new PATH NAME BOARD... [--video] [--local]
"""

from __future__ import annotations

import json
import sys

from . import bench, lint, scaffold


def main(argv: list[str] | None = None) -> int:
    a = list(sys.argv[1:] if argv is None else argv)
    if not a or a[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    cmd, rest = a[0], a[1:]
    try:
        if cmd == "status":
            print(json.dumps(bench.status(float(rest[0]) if rest else 0), indent=1))
        elif cmd == "flash":
            print(bench.flash(rest[0], rest[1]))
        elif cmd == "unstick":
            print(bench.psram_unstick(rest[0]))
        elif cmd == "cmd":
            print(bench.command(rest[0], " ".join(rest[1:])))
        elif cmd == "console":
            secs = float(rest[1]) if len(rest) > 1 else 3
            print(bench.console(rest[0], rest[2:], secs))
        elif cmd == "watch":
            r = bench.watch(rest[0] if rest else ".")
            print(json.dumps(r.__dict__, indent=1))
        elif cmd == "audio":
            print(json.dumps(bench.grab_audio(3, float(rest[0]) if rest else None)))
        elif cmd == "build":
            r = bench.build(rest[0], rest[1])
            print(json.dumps(r, indent=1))
            return 0 if r["ok"] else 1
        elif cmd == "check":
            r = lint.check(rest[0], rest[1:] or None)
            print(json.dumps(r, indent=1))
            return 0 if r["ok"] else 1
        elif cmd == "new":
            flags = [x for x in rest if x.startswith("--")]
            pos = [x for x in rest if not x.startswith("--")]
            r = scaffold.new_firmware(pos[0], pos[1], pos[2:], features=["video"] if "--video" in flags else [],
                                      sdk="local" if "--local" in flags else "submodule")
            print(json.dumps(r, indent=1))
        else:
            print(__doc__)
            return 2
    except bench.BenchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except IndexError:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

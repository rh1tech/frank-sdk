"""frank-dev: build, flash and watch FRANK firmware on the bench."""

from __future__ import annotations

from dataclasses import asdict

from mcp.server.mcpserver import MCPServer

from . import bench, lint, scaffold

server = MCPServer(
    "frank-dev",
    instructions=(
        "Builds and bench-tests FRANK RP2350 firmware. Boards are named by target id (M1, M2, NX, C2, SL, FZ, FN, Z0, PC: frank-docs list_boards) "
        "and found by USB serial from ~/.config/frank/bench.yaml. Flash only through `flash` (never ask the "
        "user to press BOOTSEL); after every flash, check the console and the picture (`watch`). Every tool "
        "has a timeout; a BenchError says what failed."
    ),
)


def _guard(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except bench.BenchError as e:
        return {"error": str(e)}


@server.tool()
def bench_status(wait_s: float = 30) -> dict:
    """Which configured boards are up (console port, BOOTSEL), probes, capture devices.
    Absent devices are re-checked for wait_s seconds before being reported."""
    return _guard(bench.status, wait_s)


@server.tool()
def board_info(board: str) -> str | dict:
    """picotool info of what is on the board now (the bench board is shared: look before flashing)."""
    return _guard(lambda: bench.picotool_info(bench.board_cfg(board)["serial"]))


@server.tool()
def build(source_dir: str, board: str, build_dir: str = "", defines: dict | None = None) -> dict:
    """Configure and build a frank-sdk firmware (or example) for one board: errors,
    warning count, memory per region, the .uf2."""
    return _guard(bench.build, source_dir, board, build_dir or None, defines)


@server.tool()
def check(elf: str, irq_roots: list[str] | None = None) -> dict:
    """Lint a built ELF against the fleet's traps: mem*/64-bit division in flash, small
    stack, tight SRAM heap, missing recovery path, DMA buffers in PSRAM, and (given the
    IRQ handlers' names) anything they reach that runs from flash."""
    return lint.check(elf, irq_roots)


@server.tool()
def flash(board: str, uf2: str, allow_swd: bool = True) -> str | dict:
    """Flash a .uf2: ~BOOT~ over the console, picotool load -v -x by serial, verify.
    SWD (boot ROM reboot, nothing written) only if the console never answers."""
    return _guard(bench.flash, board, uf2, allow_swd)


@server.tool()
def psram_unstick(board: str) -> str | dict:
    """A flash load failed verify with noisy reads: the PSRAM is stuck on the QSPI bus.
    Runs a RAM-only program (no flash written) that releases it, back to BOOTSEL."""
    return _guard(bench.psram_unstick, board)


@server.tool()
def console(board: str, send: list[str] | None = None, seconds: float = 3.0, until: str = "") -> str | dict:
    """Send strings to the board's console (e.g. ["~C help~"]) and return what it prints."""
    return _guard(bench.console, board, send, seconds, until or None)


@server.tool()
def command(board: str, cmd: str, seconds: float = 5.0) -> str | dict:
    """Run one console command ("clocks", "psram", "mem", "bootinfo", "help", ...) up to its OK line."""
    return _guard(bench.command, board, cmd, seconds)


@server.tool()
def reboot(board: str, bootsel: bool = False) -> str | dict:
    """~RST~ (or ~BOOT~ for BOOTSEL) and the first lines of the next boot."""
    return _guard(bench.console, board, ["~BOOT~" if bootsel else "~RST~"], 2.0)


@server.tool()
def grab_frame(out: str) -> str | dict:
    """One frame from the HDMI capture into `out` (.png). Look at it."""
    return _guard(bench.grab_frame, out)


@server.tool()
def watch(out_dir: str, gap_s: float = 10.0) -> dict:
    """Two frames gap_s apart and a verdict: changing, frozen (identical: a hang) or black.
    Returns both paths: look at them too (the stick shows its own screen without signal)."""
    r = _guard(bench.watch, out_dir, gap_s)
    return asdict(r) if isinstance(r, bench.FrameVerdict) else r


@server.tool()
def grab_audio(seconds: float = 3.0, expect_hz: float = 0) -> dict:
    """Record the capture's audio: RMS, silence, the dominant frequency. The stick's
    pitch is several percent off: confirm rates with the firmware's own counters."""
    return _guard(bench.grab_audio, seconds, expect_hz or None)


@server.tool()
def new_firmware(path: str, name: str, boards: list[str], description: str = "",
                 license: str = "MIT", features: list[str] | None = None) -> dict:
    """Create a new firmware repo from frank-sdk's template: frank-sdk as a submodule (or a
    path for local work), CMakeLists with the chosen features (video, audio, storage, input,
    link), build.sh/flash.sh/
    release.sh, version.txt, the no-AI-attribution git hooks, CLAUDE.md and .mcp.json."""
    return _guard(scaffold.new_firmware, path, name, boards, description, license, features or [])


def main():
    server.run()


if __name__ == "__main__":
    main()

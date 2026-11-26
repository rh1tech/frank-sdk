"""frank-docs: what frank-sdk knows, read-only. Never touches hardware."""

from __future__ import annotations

import json
import re

from mcp.server.mcpserver import MCPServer

from .paths import BOARDS_JSON, DOCS, DRIVERS, EXAMPLES, PLATFORM, SDK

server = MCPServer(
    "frank-docs",
    instructions=(
        "Knowledge of the FRANK RP2350 boards and frank-sdk: pins, clocks, drivers, the console "
        "protocol, examples and the gotchas the fleet learned the hard way. Ask `board` before "
        "touching any pin, and `gotchas` before debugging video, PSRAM, flash or reboots."
    ),
)


def _boards() -> dict:
    return json.loads(BOARDS_JSON.read_text())


@server.tool()
def list_boards() -> list[dict]:
    """Every build target: id, name, MCU, role, the products it covers, its video back-end."""
    out = []
    for bid, b in _boards().items():
        out.append({"id": bid, "name": b["name"], "mcu": b["mcu"], "role": b["role"],
                    "covers": b.get("covers", []), "pair": b.get("pair"),
                    "video": {k: v.get("backend", "dac") for k, v in b.get("video", {}).items()},
                    "console_default": b.get("console", {}).get("default")})
    return out


@server.tool()
def board(board_id: str) -> dict:
    """One board in full: pins by function and by GPIO, clocks, console, PIO plan,
    shared pins, sources. Board ids: M1 (Murmulator 1 + Pico 2, "M1P2"), M2, NX,
    C2, SL (the Next/Core 2 slave), FZ (FRANK Z0), FN (FRANK Nano), Z0 (Waveshare RP2350-PiZero),
    PC (Olimex PICO-PC + Pico 2)."""
    bs = _boards()
    b = bs.get(board_id.upper())
    if not b:
        return {"error": f"no board {board_id}; one of {', '.join(bs)}"}
    return b


@server.tool()
def fleet(slug: str = "") -> list[dict] | dict:
    """frank-test's run-time board descriptors (boards/fleet/*.yaml): every FRANK
    board detection can identify, with all its pins, capabilities, the pin
    signature and the notes from the netlists. No slug lists them."""
    data = json.loads((BOARDS_JSON.parent / "fleet.json").read_text())
    if not slug:
        return [{"slug": d["slug"], "name": d["name"], "mcu": d["mcu"], "role": d["role"],
                 "target": d.get("target")} for d in data]
    for d in data:
        if d["slug"] == slug:
            return d
    return {"error": f"no fleet board {slug}; one of {', '.join(d['slug'] for d in data)}"}


@server.tool()
def pin(board_id: str, gpio: int) -> str:
    """What a GPIO is on a board (and whether something else may claim it)."""
    b = _boards().get(board_id.upper())
    if not b:
        return f"no board {board_id}"
    owners = b["gpio"].get(str(gpio))
    if not owners:
        return f"GPIO{gpio} is free on {board_id.upper()}"
    shared = " (shared: a build uses one of them)" if len(owners) > 1 else ""
    return f"GPIO{gpio} on {board_id.upper()}: {' / '.join(owners)}{shared}"


def _md_files():
    return sorted(DOCS.rglob("*.md"))


@server.tool()
def gotchas(query: str = "") -> list[dict]:
    """The fleet's hard-won lessons (docs/gotchas): symptom, cause, fix, where
    it was found. Empty query lists them all; otherwise the ones matching every word."""
    out = []
    words = [w.lower() for w in query.split()]
    for p in sorted(q for q in (DOCS / "gotchas").glob("*.md") if q.name != "README.md"):
        t = p.read_text()
        if all(w in t.lower() for w in words):
            title = t.splitlines()[0].lstrip("# ").strip()
            out.append({"id": p.stem, "title": title, "text": t if words else None})
    return out


@server.tool()
def search_docs(query: str, limit: int = 20) -> list[dict]:
    """Lines matching the query (a regex, case-insensitive) in every doc, with file and line."""
    rx = re.compile(query, re.I)
    hits = []
    for p in _md_files():
        for i, line in enumerate(p.read_text().splitlines(), 1):
            if rx.search(line):
                hits.append({"file": str(p.relative_to(SDK)), "line": i, "text": line.strip()[:300]})
                if len(hits) >= limit:
                    return hits
    return hits


@server.tool()
def read_doc(path: str) -> str:
    """A doc by its path under frank-sdk (e.g. docs/bench.md, docs/boards/nx.md)."""
    p = (SDK / path).resolve()
    if SDK not in p.parents or not p.is_file():
        return f"no doc {path}"
    return p.read_text()


@server.tool()
def api(name: str = "") -> dict:
    """frank-sdk's C headers: no name lists them; a name (platform, video, psram,
    clocks, ...) returns that header, which documents its API and rules."""
    heads = {p.stem: p for p in list(PLATFORM.rglob("include/frank/*.h")) + list(DRIVERS.rglob("include/frank/*.h"))}
    if not name:
        return {"headers": sorted(heads)}
    p = heads.get(name)
    return {"header": str(p.relative_to(SDK)), "text": p.read_text()} if p else {"error": f"no header {name}; {sorted(heads)}"}


@server.tool()
def example(name: str = "") -> dict:
    """The examples: no name lists them; a name returns its files."""
    if not name:
        return {"examples": sorted(p.name for p in EXAMPLES.iterdir() if p.is_dir())}
    d = EXAMPLES / name
    if not d.is_dir():
        return {"error": f"no example {name}"}
    return {str(p.relative_to(SDK)): p.read_text() for p in sorted(d.rglob("*")) if p.is_file() and "build" not in p.parts}


@server.tool()
def console_commands() -> dict:
    """The console protocol every frank-sdk firmware speaks, and the built-in ~C commands."""
    src = (PLATFORM / "console.c").read_text()
    builtins = re.findall(r'frank_console_add\("(\w+)", \w+, "([^"]*)"\)', src)
    return {
        "magic": {"~BOOT~": "reboot to BOOTSEL (then picotool load -v -x)", "~RST~": "plain reboot",
                  "~VERB~": "toggle verbose", "~PROF~": "profiler_rearm() if linked",
                  "~C name args~": "a command; the firmware prints 'OK name' when it ran"},
        "builtins": {n: h for n, h in builtins},
        "note": "a firmware adds its own with frank_console_add(); '~C help~' lists them on the board",
    }


def main():
    server.run()


if __name__ == "__main__":
    main()

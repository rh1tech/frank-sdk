"""frank-sdk checks that need no board: the board generator, the docs server,
the scaffolder, the ELF lint. `pytest mcp/tests` (the ELF test builds hello for
M2 and needs the Pico SDK; it is skipped without one)."""
import copy
import json
import re
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
import yaml

SDK = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SDK / "tools"))
import gen_boards  # noqa: E402

from frank_mcp import docs_server, lint, scaffold  # noqa: E402


def load(bid):
    return yaml.safe_load((SDK / "boards" / f"{bid.lower()}.yaml").read_text())


def test_generated_files_are_current():
    r = subprocess.run([sys.executable, str(SDK / "tools/gen_boards.py"), "--check"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


@pytest.mark.parametrize("bid", ["M1", "M2", "NX", "C2", "SL", "FZ", "FN", "Z0", "PC", "GA", "GB", "XT"])
def test_every_board_checks(bid):
    b = load(bid)
    gen_boards.check(b, gen_boards.pin_claims(b))


def test_pin_conflict_is_refused():
    b = load("M2")
    b["led"] = {"plain": 14}           # an HDMI pin
    with pytest.raises(gen_boards.BoardError, match="GPIO14 claimed"):
        gen_boards.check(b, gen_boards.pin_claims(b))


def test_shared_pins_are_allowed():
    b = load("M2")                     # UART and PS/2 mouse share GPIO0/1
    gen_boards.check(b, gen_boards.pin_claims(b))


def test_pin_beyond_package_is_refused():
    b = load("SL")                     # an RP2350A has GPIO0-29
    b["led"] = {"plain": 40}
    with pytest.raises(gen_boards.BoardError, match="does not exist"):
        gen_boards.check(b, gen_boards.pin_claims(b))


def test_pio_window_is_checked():
    b = load("NX")
    b["pio"]["sd"] = 2                 # SD on GPIO0-7 with the link up to GPIO42
    with pytest.raises(gen_boards.BoardError, match="32-pin window"):
        gen_boards.check(b, gen_boards.pin_claims(b))


def test_docs_tools():
    assert {b["id"] for b in docs_server.list_boards()} >= {"M1", "M2", "NX", "C2", "SL", "FZ", "Z0", "PC"}
    assert "link" in docs_server.pin("NX", 43)
    assert "free" in docs_server.pin("NX", 45) or "console" in docs_server.pin("NX", 45)
    assert docs_server.gotchas("QPI")
    assert all(g["id"] != "README" for g in docs_server.gotchas())
    assert "help" in docs_server.console_commands()["builtins"]
    assert "video" in docs_server.api()["headers"]
    assert "hello" in docs_server.example()["examples"]


def test_scaffold(tmp_path):
    r = scaffold.new_firmware(str(tmp_path / "fw"), "frank-test-fw", ["NX", "z0"], "a test",
                              features=["video"], sdk="local")
    d = Path(r["path"])
    cm = (d / "CMakeLists.txt").read_text()
    assert "frank_video" in cm and "{{" not in cm
    assert "frank_init(FW_NAME, FW_VERSION)" in (d / "src/main.c").read_text()
    assert (d / "frank-sdk").resolve() == SDK
    assert json.loads((d / ".mcp.json").read_text())["mcpServers"]["frank-dev"]
    for p in d.rglob("*"):
        if p.is_file() and "frank-sdk" not in p.parts and ".git" not in p.parts:
            assert not re.search(r"\{\{[A-Z0-9_]+\}\}", p.read_text(errors="ignore")), p


def test_scaffold_refuses_video_without_hdmi(tmp_path):
    with pytest.raises(Exception, match="no HDMI"):
        scaffold.new_firmware(str(tmp_path / "fw"), "fw", ["SL"], features=["video"], sdk="local")


@pytest.mark.skipif(not shutil.which("arm-none-eabi-gcc") or not Path(os.environ.get(
    "PICO_SDK_PATH", Path.home() / "pico/pico-sdk")).is_dir(), reason="no toolchain / Pico SDK")
def test_lint_on_hello(tmp_path):
    b = tmp_path / "b"
    sdk = os.environ.get("PICO_SDK_PATH", str(Path.home() / "pico/pico-sdk"))
    subprocess.run(["cmake", "-S", str(SDK / "examples/hello"), "-B", str(b), "-G", "Ninja",
                    "-DFRANK_BOARD=M2", f"-DPICO_SDK_PATH={sdk}"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(b)], check=True, capture_output=True)
    r = lint.check(str(b / "m2_hello.elf"), ["frank_stall_isr_c"])
    assert r["ok"], r["findings"]


def _fake_elf(tmp_path, monkeypatch, syms):
    elf = tmp_path / "fw.elf"
    elf.write_bytes(b"")
    monkeypatch.setattr(lint, "_nm", lambda e: [(a, 0, "T", n) for n, a in syms.items()])
    return str(elf)


RECOVERY = {"frank_reboot": 0x10000100, "frank_safe_mode_if_hung": 0x10000200}


def test_lint_stack_rules(tmp_path, monkeypatch):
    # frank-sdk's layout (top at 0x20080000): 8 KB from frank_linker_script is fine
    s = dict(RECOVERY, frank_boot_guard=0x10000300, __StackLimit=0x2007E000, __StackTop=0x20080000)
    assert lint.check(_fake_elf(tmp_path, monkeypatch, s))["ok"]
    s.update(__StackLimit=0x2007F800)   # 2 KB: too small anywhere
    assert [f["rule"] for f in lint.check(_fake_elf(tmp_path, monkeypatch, s))["findings"]] == ["stack-small"]
    # the SDK's layout (SCRATCH_Y): under 32 KB runs into core 1's stack
    s.update(__StackLimit=0x20081800, __StackTop=0x20082000)
    assert [f["rule"] for f in lint.check(_fake_elf(tmp_path, monkeypatch, s))["findings"]] == ["stack-small"]


def test_lint_boot_guard_optional_at_run_time(tmp_path, monkeypatch):
    s = dict(RECOVERY)
    assert any("frank_boot_guard" in f["detail"] for f in lint.check(_fake_elf(tmp_path, monkeypatch, s))["findings"])
    s["frank_runtime_board"] = 0x10000400
    assert lint.check(_fake_elf(tmp_path, monkeypatch, s))["ok"]


def test_console_falls_back_to_the_probe_uart(monkeypatch):
    from frank_mcp import bench
    ports = [{"device": "/dev/cdc", "serial": "BOARD", "probe": False},
             {"device": "/dev/probe-uart", "serial": "PROBE", "probe": True}]
    monkeypatch.setattr(bench, "serial_ports", lambda: ports)
    cfg = {"serial": "BOARD", "probe": "PROBE"}
    assert bench.uart_port(cfg) == "/dev/probe-uart"
    assert bench.uart_port({"serial": "BOARD"}) is None
    assert bench.console_port("BOARD") == "/dev/cdc"
    ports.pop(0)   # the board's CDC gone: only the probe's UART is left
    assert bench.console_port("BOARD") is None and bench.uart_port(cfg) == "/dev/probe-uart"


def test_scaffold_full_features(tmp_path):
    r = scaffold.new_firmware(str(tmp_path / "fw"), "fw-full", ["M2", "NX"], features=["video", "audio", "storage", "input"], sdk="local")
    d = Path(r["path"])
    cm = (d / "CMakeLists.txt").read_text()
    for lib in ("frank_video", "frank_audio", "frank_storage", "frank_input"):
        assert lib in cm
    assert "frank_sd_mount" in (d / "src/main.c").read_text()


def test_link_only_on_pairs(tmp_path):
    with pytest.raises(Exception, match="no link"):
        scaffold.new_firmware(str(tmp_path / "fw"), "fw", ["M2"], features=["link"], sdk="local")


# ---- the fleet (frank-test's run-time table) ----
sys.path.insert(0, str(SDK / "tools" / "fleet"))
import fleetlib  # noqa: E402
import gen_fleet  # noqa: E402


def _targets():
    return {b["id"]: b for b in gen_boards.load_all()}


def test_fleet_validates_and_cross_checks():
    files, errs = gen_fleet.render(_targets())
    assert not errs, errs
    assert "frank_board_table[]" in files["boards/gen/frank_fleet.c"]


def test_fleet_cross_check_catches_drift(tmp_path, monkeypatch):
    boards, fb = gen_fleet.load()
    nx = next(d for d in boards if d["slug"] == "next")
    bad = copy.deepcopy(nx)
    bad["pins"]["sd_clk"] = 9
    errs = gen_fleet.cross_check(bad, _targets())
    assert any("sd_clk" in e for e in errs), errs


@pytest.mark.skipif(not shutil.which("cc"), reason="no host C compiler")
def test_fleet_c_round_trip(tmp_path):
    """the generated C, compiled and dumped, says what the YAML says"""
    boards, fb = gen_fleet.load()
    unit = tmp_path / "both.c"
    unit.write_text(f'#include "{SDK / "boards/gen/frank_fleet.c"}"\n#include "{SDK / "fleet/fleet.c"}"\n')
    dumped = fleetlib.dump(unit)
    assert len(dumped["table"]) == len(boards)
    fields = fleetlib.pin_fields()
    for d, e in zip(boards, dumped["table"]):
        assert e["slug"] == d["slug"] and e["name"] == d["name"]
        for typ, n in fields:
            assert e["pins"][n] == d.get("pins", {}).get(n, -1 if typ == "int8_t" else 0), (d["slug"], n)
        assert [p for p, _ in e["sig"]] == [int(p) for p in (d.get("signature") or {})]
        assert e["manual_note"] == d.get("manual_note")

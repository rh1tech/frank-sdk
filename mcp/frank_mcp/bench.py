"""frank-sdk bench: find, flash, talk to and watch FRANK boards.

The same functions serve the frank-dev MCP server and the `frank` CLI.
Every hardware call has a timeout. Boards are addressed by USB serial number
(tty names move), from the machine-local ~/.config/frank/bench.yaml:

    boards:
      NX: {serial: 0123456789ABCDEF, probe: FEDCBA9876543210}
      M2: {serial: ...}
    capture: {video: "USB Video", audio: "USB Digital Audio", size: 640x480, fps: 30}

The procedures are the fleet's proven ones (frank-dos flash.sh, frank-ecat's
self-recovery, the grab/cap_audio scripts):
  - flash: "~BOOT~" over the board's own USB CDC console, resent every 5 s for
    40 s (a firmware still booting reads its console late), then
    `picotool load -v -x --ser`; a load that does not print OK fails.
    Only when the console never answers: the boot ROM's reboot to BOOTSEL over
    the debug probe (nothing is written over SWD).
  - never the debug probe's UART as a console unless bench.yaml says so: on
    the M2 and Next benches it is not wired to the board.
"""

from __future__ import annotations

import math
import os
import re
import struct
import subprocess
import tempfile
import time
import wave
from dataclasses import dataclass
from pathlib import Path

import yaml

CONFIG = Path(os.environ.get("FRANK_BENCH", Path.home() / ".config/frank/bench.yaml"))
RP_VID = 0x2E8A
PROBE_PIDS = {0x000C}          # debugprobe CMSIS-DAP
BOOTSEL_PID = 0x000F


class BenchError(Exception):
    pass


def config() -> dict:
    if not CONFIG.exists():
        raise BenchError(f"no bench config at {CONFIG}: copy frank-sdk/mcp/bench.example.yaml there")
    with open(CONFIG) as f:
        return yaml.safe_load(f) or {}


def board_cfg(board: str) -> dict:
    boards = config().get("boards", {})
    b = boards.get(board.upper())
    if not b or "serial" not in b:
        raise BenchError(f"board {board} is not in {CONFIG} (boards: {', '.join(boards) or 'none'})")
    return b


def run(cmd: list[str], timeout: float, **kw) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, **kw)
    except subprocess.TimeoutExpired as e:
        raise BenchError(f"timed out after {timeout:.0f} s: {' '.join(cmd)}") from e
    except FileNotFoundError as e:
        raise BenchError(f"not installed: {cmd[0]}") from e


# ---------------------------------------------------------------- devices

def serial_ports() -> list[dict]:
    from serial.tools import list_ports
    out = []
    for p in list_ports.comports():
        if p.vid is None:
            continue
        out.append({"device": p.device, "vid": p.vid, "pid": p.pid, "serial": p.serial_number,
                    "product": p.product, "probe": p.vid == RP_VID and p.pid in PROBE_PIDS})
    return out


def console_port(serial: str, wait: float = 0) -> str | None:
    end = time.time() + wait
    while True:
        for p in serial_ports():
            if p["serial"] == serial and not p["probe"]:
                return p["device"]
        if time.time() >= end:
            return None
        time.sleep(0.5)


def in_bootsel(serial: str) -> bool:
    try:
        r = run(["picotool", "info", "--ser", serial], timeout=8)
    except BenchError:
        return False
    return r.returncode == 0 and "No accessible" not in r.stdout + r.stderr


def picotool_info(serial: str) -> str:
    """what is on the board, from BOOTSEL or (-f) from the running firmware"""
    if in_bootsel(serial):
        r = run(["picotool", "info", "-a", "--ser", serial], timeout=15)
    else:
        r = run(["picotool", "info", "-a", "-f", "--ser", serial], timeout=30)
    return (r.stdout + r.stderr).strip()


def capture_devices() -> dict:
    r = run(["ffmpeg", "-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""], timeout=15)
    video, audio, cur = [], [], None
    for line in r.stderr.splitlines():
        if "video devices" in line:
            cur = video
        elif "audio devices" in line:
            cur = audio
        else:
            m = re.search(r"\] \[(\d+)\] (.+)$", line)
            if m and cur is not None:
                cur.append(m.group(2))
    return {"video": video, "audio": audio}


def status(wait: float = 0) -> dict:
    """everything bench.yaml names, each found or not (re-checked for `wait` s)"""
    cfg = config()
    end = time.time() + wait
    while True:
        ports = serial_ports()
        found = {}
        for name, b in cfg.get("boards", {}).items():
            ser = b.get("serial")
            con = next((p["device"] for p in ports if ser and p["serial"] == ser and not p["probe"]), None)
            found[name] = {"serial": ser, "console": con,
                           "bootsel": bool(ser) and not con and in_bootsel(ser),
                           "probe": b.get("probe"),
                           "probe_present": any(p["serial"] == b.get("probe") for p in ports) if b.get("probe") else None}
        missing = [n for n, f in found.items()
                   if (f["serial"] and not f["console"] and not f["bootsel"]) or f["probe_present"] is False]
        if not missing or time.time() >= end:
            break
        time.sleep(2)
    cap = capture_devices()
    want = cfg.get("capture", {})
    return {"boards": found,
            "capture": {"video": want.get("video") in cap["video"] if want.get("video") else None,
                        "audio": want.get("audio") in cap["audio"] if want.get("audio") else None,
                        "available": cap},
            "missing": missing}


# ---------------------------------------------------------------- console

def console(board: str, send: list[str] | None = None, seconds: float = 3.0,
            until: str | None = None) -> str:
    """Send each string (e.g. "~C help~") and read the console for `seconds`,
    or until `until` appears. Reading and sending share one open port: the
    console cannot be captured by one process and written by another."""
    import serial as pyserial
    ser = board_cfg(board)["serial"]
    port = console_port(ser, wait=10)
    if not port:
        raise BenchError(f"{board}: no console port for serial {ser} (in BOOTSEL? a HID build?)")
    out = b""
    try:
        with pyserial.Serial(port, 115200, timeout=0.1) as s:
            s.dtr = True
            for m in send or []:
                s.write(m.encode())
                # read while waiting: an answer followed by a reboot (~C x~~RST~)
                # was lost with the port, still in the OS buffer
                gap = time.time() + 0.2
                while time.time() < gap:
                    out += s.read(4096)
            end = time.time() + seconds
            while time.time() < end:
                out += s.read(4096)
                if until and until.encode() in out:
                    break
    except (pyserial.SerialException, OSError):
        out += b"\n[console port closed: the board rebooted or left]\n"
    return out.decode("utf-8", "replace")


def command(board: str, cmd: str, seconds: float = 5.0) -> str:
    """one "~C cmd~", up to its "OK name" line"""
    name = cmd.split()[0]
    return console(board, [f"~C {cmd}~"], seconds, until=f"OK {name}")


# ---------------------------------------------------------------- flash

def swd_bootsel(probe: str) -> str:
    """the boot ROM's reboot to BOOTSEL over SWD; writes nothing"""
    r = run(["openocd", "-f", "interface/cmsis-dap.cfg", "-c", f"adapter serial {probe}",
             "-c", "adapter speed 1000", "-f", "target/rp2350.cfg", "-c", "init", "-c", "halt",
             "-c", "rp2xxx rom_api_call RB 0x0002 100 0 0", "-c", "exit"], timeout=40)
    return r.stdout + r.stderr


def to_bootsel(board: str, allow_swd: bool = True) -> list[str]:
    """~BOOT~ on the console every 5 s for 40 s; the SWD boot ROM reboot only
    if the console never answers. Returns the log; raises if BOOTSEL is not reached."""
    import serial as pyserial
    b = board_cfg(board)
    ser = b["serial"]
    log = []
    end, last = time.time() + 40, 0.0
    while time.time() < end and not in_bootsel(ser):
        if time.time() - last >= 5:
            last = time.time()
            port = console_port(ser)
            if port:
                try:
                    with pyserial.Serial(port, 115200, write_timeout=2) as s:
                        s.write(b"~BOOT~")
                        time.sleep(0.5)
                    log.append(f"sent ~BOOT~ on {port}")
                except Exception as e:  # the port vanishing is the reboot
                    log.append(f"{port}: {e}")
        time.sleep(1)
    if not in_bootsel(ser):
        if not (allow_swd and b.get("probe")):
            raise BenchError(f"{board} did not reach BOOTSEL and no probe is configured\n" + "\n".join(log))
        log.append("no answer on the console: boot ROM reboot to BOOTSEL over SWD")
        log.append(swd_bootsel(b["probe"]).strip()[-300:])
        for _ in range(20):
            if in_bootsel(ser):
                break
            time.sleep(1)
        else:
            raise BenchError(f"{board} did not reach BOOTSEL\n" + "\n".join(log))
    return log


def flash(board: str, uf2: str, allow_swd: bool = True) -> str:
    uf2p = Path(uf2)
    if not uf2p.is_file():
        raise BenchError(f"no image {uf2}")
    ser = board_cfg(board)["serial"]
    log = to_bootsel(board, allow_swd)
    r = run(["picotool", "load", "-v", "-x", "--ser", ser, str(uf2p)], timeout=300)
    out = (r.stdout + r.stderr).replace("\r", "\n")
    if not re.search(r"^\s*OK", out, re.M) or re.search(r"FAILED|ERROR", out):
        raise BenchError("the flash does not hold the image (if the readback is noisy, "
                         "run psram_unstick first):\n" + out[-800:])
    log.append(f"flashed and verified {uf2p.name}")
    port = console_port(ser, wait=30)
    log.append(f"console back on {port}" if port else "no console port after 30 s (HID build, or it hangs)")
    return "\n".join(log)


def psram_unstick(board: str) -> str:
    """When flash loads fail verify with noisy reads: the PSRAM is stuck driving
    the QSPI bus. Loads tools/psram_unstick into RAM (writes no flash): it drives
    the PSRAM CS high and returns to BOOTSEL. Then flash again."""
    from .paths import SDK
    ser = board_cfg(board)["serial"]
    log = to_bootsel(board)
    bdir = Path.home() / ".cache" / "frank" / f"unstick-{board.lower()}"
    r = build(str(SDK / "tools" / "psram_unstick"), board, str(bdir))
    if not r.get("uf2"):
        raise BenchError("psram_unstick did not build: " + "\n".join(r.get("errors", [])))
    out = run(["picotool", "load", "-x", "--ser", ser, r["uf2"]], timeout=60)
    log.append((out.stdout + out.stderr).strip()[-200:])
    for _ in range(20):
        time.sleep(1)
        if in_bootsel(ser):
            log.append("PSRAM released; back in BOOTSEL: flash now")
            return "\n".join(log)
    raise BenchError("\n".join(log + ["the board did not come back to BOOTSEL"]))


# ---------------------------------------------------------------- capture

def _cap() -> dict:
    return {"video": "USB Video", "audio": "USB Digital Audio", "size": "640x480", "fps": 30,
            **config().get("capture", {})}


def grab_frame(out: str, skip: int = 15) -> str:
    """one frame from the HDMI capture (the first ~15 are stale)"""
    c = _cap()
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    r = run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "avfoundation",
             "-video_size", c["size"], "-framerate", str(c["fps"]), "-pixel_format", "uyvy422",
             "-i", f"{c['video']}:none", "-vf", f"select=gte(n\\,{skip})", "-frames:v", "1", out], timeout=30)
    if r.returncode or not Path(out).exists():
        raise BenchError("capture failed: " + r.stderr[-400:])
    return out


def _thumb(path: str) -> bytes:
    r = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-vf",
                        "scale=80:60,format=gray", "-f", "rawvideo", "-"], capture_output=True, timeout=30)
    return r.stdout


@dataclass
class FrameVerdict:
    first: str
    second: str
    changed: float      # mean absolute difference of 80x60 grey thumbnails, 0..255
    mean: float         # mean brightness of the second frame
    verdict: str


def watch(out_dir: str, gap: float = 10.0) -> FrameVerdict:
    """Two frames `gap` s apart. Identical = frozen (a hang; the fleet's rule:
    the same frame 10 s apart is a frozen board). Near-black = no picture.
    A changing picture says the firmware runs. Still look at the frames: the
    capture stick shows its own screen when it has no signal."""
    a = grab_frame(str(Path(out_dir) / "frame_a.png"))
    time.sleep(gap)
    b = grab_frame(str(Path(out_dir) / "frame_b.png"))
    ta, tb = _thumb(a), _thumb(b)
    n = min(len(ta), len(tb)) or 1
    diff = sum(abs(x - y) for x, y in zip(ta, tb)) / n
    mean = sum(tb) / max(len(tb), 1)
    if mean < 8:
        v = "black: no picture (or a black screen)"
    elif diff < 0.5:
        v = "frozen: identical frames (a hang, or a static screen)"
    else:
        v = "changing: the picture runs"
    return FrameVerdict(a, b, round(diff, 2), round(mean, 1), v)


def grab_audio(seconds: float = 3.0, expect_hz: float | None = None) -> dict:
    """Record the capture's audio; RMS and the dominant frequency. The MS2109
    stick's pitch is off by several percent and differs per frequency: check
    the firmware's own sample counter for the real rate."""
    c = _cap()
    with tempfile.TemporaryDirectory() as d:
        wav = os.path.join(d, "a.wav")
        r = run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "avfoundation", "-i",
                 f":{c['audio']}", "-t", str(seconds), "-ac", "1", "-ar", "48000", wav], timeout=seconds + 20)
        if r.returncode or not os.path.exists(wav):
            raise BenchError("audio capture failed: " + r.stderr[-400:])
        w = wave.open(wav)
        n = w.getnframes()
        s = struct.unpack(f"<{n}h", w.readframes(n))[24000:]
    if not s:
        return {"rms": 0, "peak_hz": None}
    rms = math.sqrt(sum(x * x for x in s) / len(s))
    lo, hi = (expect_hz * 0.8, expect_hz * 1.2) if expect_hz else (50.0, 8000.0)
    dec = s[::4][:24000]                       # 12 kHz, 2 s

    def power(f):
        w_ = 2 * math.pi * f * 4 / 48000
        cr = ci = 0.0
        for i, x in enumerate(dec):
            cr += x * math.cos(w_ * i)
            ci += x * math.sin(w_ * i)
        return cr * cr + ci * ci
    steps = 120
    best = max((power(lo + k * (hi - lo) / steps), lo + k * (hi - lo) / steps) for k in range(steps + 1))
    return {"rms": round(rms), "peak_hz": round(best[1], 1), "silent": rms < 50}


# ---------------------------------------------------------------- build

def build(source_dir: str, board: str, build_dir: str | None = None, defines: dict | None = None) -> dict:
    """cmake configure + build for one board; errors and memory use, compactly"""
    src = Path(source_dir).resolve()
    bdir = Path(build_dir) if build_dir else src / f"build-{board.lower()}"
    args = ["cmake", "-S", str(src), "-B", str(bdir), "-G", "Ninja", f"-DFRANK_BOARD={board.upper()}"]
    sdk = os.environ.get("PICO_SDK_PATH", str(Path.home() / "pico/pico-sdk"))
    args.append(f"-DPICO_SDK_PATH={sdk}")
    for k, v in (defines or {}).items():
        args.append(f"-D{k}={v}")
    r = run(args, timeout=300)
    if r.returncode:
        return {"ok": False, "stage": "configure", "errors": _errors(r.stdout + r.stderr)}
    r = run(["cmake", "--build", str(bdir)], timeout=900)
    text = r.stdout + r.stderr
    mem = {m.group(1): {"used": m.group(2).strip(), "size": m.group(3).strip(), "pct": m.group(4)}
           for m in re.finditer(r"^\s+(\w+):\s+([\d.]+ \w+)\s+([\d.]+ \w+)\s+([\d.]+)%", text, re.M)}
    uf2 = sorted(bdir.glob("*.uf2"), key=lambda p: p.stat().st_mtime)
    return {"ok": r.returncode == 0, "stage": "build", "errors": _errors(text),
            "warnings": len(re.findall(r"warning:", text)), "memory": mem,
            "uf2": str(uf2[-1]) if uf2 and r.returncode == 0 else None, "build_dir": str(bdir)}


def _errors(text: str, limit: int = 25) -> list[str]:
    lines = [l for l in text.splitlines() if re.search(r"\berror\b|Error|undefined reference|FATAL", l)]
    return lines[:limit]
